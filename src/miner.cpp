// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "blockhash.h"
#include "vote.h"
#include "miner.h"

#include "amount.h"
#include "chain.h"
#include "chainparams.h"
#include "coins.h"
#include "consensus/consensus.h"
#include "consensus/merkle.h"
#include "consensus/validation.h"
#include "hash.h"
#include "validation.h"
#include "net.h"
#include "policy/policy.h"
#include "pow.h"
#include "primitives/transaction.h"
#include "script/standard.h"
#include "timedata.h"
#include "txmempool.h"
#include "util.h"
#include "utilmoneystr.h"
#include "validationinterface.h"
#include "pubkey.h"
#include "base58.h"
#include "compat.h"
#include "policy/policy.h"

#include <stdlib.h>
#include <algorithm>
#include <boost/thread.hpp>
#include <boost/tuple/tuple.hpp>
#include <queue>
#include <utility>

typedef boost::shared_lock<boost::shared_mutex> read_lock;
typedef boost::unique_lock<boost::shared_mutex> write_lock;

//////////////////////////////////////////////////////////////////////////////
//
// BitcoinMiner
//

//
// Unconfirmed transactions in the memory pool often depend on other
// transactions in the memory pool. When we select transactions from the
// pool, we select by highest priority or fee rate, so we might consider
// transactions that depend on transactions that aren't yet in the block.

uint64_t nLastBlockTx = 0;
uint64_t nLastBlockSize = 0;
uint64_t nLastBlockWeight = 0;

class ScoreCompare
{
public:
    ScoreCompare() {}

    bool operator()(const CTxMemPool::txiter a, const CTxMemPool::txiter b)
    {
        return CompareTxMemPoolEntryByScore()(*b,*a); // Convert to less than
    }
};

int64_t UpdateTime(CBlockHeader* pblock, const Consensus::Params& consensusParams, const CBlockIndex* pindexPrev)
{
    int64_t nOldTime = pblock->nTime;
    int64_t nNewTime = std::max(pindexPrev->GetMedianTimePast()+1, GetAdjustedTime());

    if (nOldTime < nNewTime)
        pblock->nTime = nNewTime;

    // Updating time can change work required on testnet:
    if (consensusParams.fPowAllowMinDifficultyBlocks)
        pblock->nBits = GetNextWorkRequired(pindexPrev, pblock, consensusParams);

    return nNewTime - nOldTime;
}

BlockAssembler::BlockAssembler(const CChainParams& _chainparams)
    : chainparams(_chainparams)
{
    // Block resource limits
    // If neither -blockmaxsize or -blockmaxweight is given, limit to DEFAULT_BLOCK_MAX_*
    // If only one is given, only restrict the specified resource.
    // If both are given, restrict both.
    nBlockMaxWeight = DEFAULT_BLOCK_MAX_WEIGHT;
    nBlockMaxSize = DEFAULT_BLOCK_MAX_SIZE;
    bool fWeightSet = false;
    if (IsArgSet("-blockmaxweight")) {
        nBlockMaxWeight = GetArg("-blockmaxweight", DEFAULT_BLOCK_MAX_WEIGHT);
        nBlockMaxSize = MAX_BLOCK_SERIALIZED_SIZE;
        fWeightSet = true;
    }
    if (IsArgSet("-blockmaxsize")) {
        nBlockMaxSize = GetArg("-blockmaxsize", DEFAULT_BLOCK_MAX_SIZE);
        if (!fWeightSet) {
            nBlockMaxWeight = nBlockMaxSize * WITNESS_SCALE_FACTOR;
        }
    }
    if (IsArgSet("-blockmintxfee")) {
        CAmount n = 0;
        ParseMoney(GetArg("-blockmintxfee", ""), n);
        blockMinFeeRate = CFeeRate(n);
    } else {
        blockMinFeeRate = CFeeRate(DEFAULT_BLOCK_MIN_TX_FEE);
    }

    // Limit weight to between 4K and MAX_BLOCK_WEIGHT-4K for sanity:
    nBlockMaxWeight = std::max((unsigned int)4000, std::min((unsigned int)(MAX_BLOCK_WEIGHT-4000), nBlockMaxWeight));
    // Limit size to between 1K and MAX_BLOCK_SERIALIZED_SIZE-1K for sanity:
    nBlockMaxSize = std::max((unsigned int)1000, std::min((unsigned int)(MAX_BLOCK_SERIALIZED_SIZE-1000), nBlockMaxSize));
    // Whether we need to account for byte usage (in addition to weight usage)
    fNeedSizeAccounting = (nBlockMaxSize < MAX_BLOCK_SERIALIZED_SIZE-1000);
}

void BlockAssembler::resetBlock()
{
    inBlock.clear();

    // Reserve space for coinbase tx
    nBlockSize = 1000;
    nBlockWeight = 4000;
    nBlockSigOpsCost = 400;
    fIncludeWitness = false;

    // These counters do not include coinbase tx
    nBlockTx = 0;
    nFees = 0;

    lastFewTxs = 0;
    blockFinished = false;
}

std::unique_ptr<CBlockTemplate> BlockAssembler::CreateNewBlock(const CScript& scriptPubKeyIn, bool fMineWitnessTx)
{
    int64_t nTimeStart = GetTimeMicros();

    resetBlock();

    pblocktemplate.reset(new CBlockTemplate());

    if(!pblocktemplate.get())
        return nullptr;
    pblock = &pblocktemplate->block; // pointer for convenience

    // Add dummy coinbase tx as first transaction
    pblock->vtx.emplace_back();
    pblocktemplate->vTxFees.push_back(-1); // updated at end
    pblocktemplate->vTxSigOpsCost.push_back(-1); // updated at end

    LOCK2(cs_main, mempool.cs);
    CBlockIndex* pindexPrev = chainActive.Tip();
    nHeight = pindexPrev->nHeight + 1;

    pblock->nVersion = ComputeBlockVersion(pindexPrev, chainparams.GetConsensus());
    // -regtest only: allow overriding block.nVersion with
    // -blockversion=N to test forking scenarios
    if (chainparams.MineBlocksOnDemand())
        pblock->nVersion = GetArg("-blockversion", pblock->nVersion);

    pblock->nTime = GetAdjustedTime();
    const int64_t nMedianTimePast = pindexPrev->GetMedianTimePast();

    nLockTimeCutoff = (STANDARD_LOCKTIME_VERIFY_FLAGS & LOCKTIME_MEDIAN_TIME_PAST)
                       ? nMedianTimePast
                       : pblock->GetBlockTime();

    // Decide whether to include witness transactions
    // This is only needed in case the witness softfork activation is reverted
    // (which would require a very deep reorganization) or when
    // -promiscuousmempoolflags is used.
    // TODO: replace this with a call to main to assess validity of a mempool
    // transaction (which in most cases can be a no-op).
    fIncludeWitness = IsWitnessEnabled(pindexPrev, chainparams.GetConsensus()) && fMineWitnessTx;

    addPriorityTxs();
    int nPackagesSelected = 0;
    int nDescendantsUpdated = 0;
    addPackageTxs(nPackagesSelected, nDescendantsUpdated);

    int64_t nTime1 = GetTimeMicros();

    nLastBlockTx = nBlockTx;
    nLastBlockSize = nBlockSize;
    nLastBlockWeight = nBlockWeight;

    // Create coinbase transaction.
    CMutableTransaction coinbaseTx;
    coinbaseTx.vin.resize(1);
    coinbaseTx.vin[0].prevout.SetNull();
    coinbaseTx.vout.resize(1);
    coinbaseTx.vout[0].scriptPubKey = scriptPubKeyIn;
    coinbaseTx.vout[0].nValue = nFees + GetBlockSubsidy(nHeight, chainparams.GetConsensus());
    coinbaseTx.vin[0].scriptSig = CScript() << nHeight << OP_0;
    pblock->vtx[0] = MakeTransactionRef(std::move(coinbaseTx));
    pblocktemplate->vchCoinbaseCommitment = GenerateCoinbaseCommitment(*pblock, pindexPrev, chainparams.GetConsensus());
    pblocktemplate->vTxFees[0] = -nFees;

    uint64_t nSerializeSize = GetSerializeSize(*pblock, SER_NETWORK, PROTOCOL_VERSION);
    LogPrintf("CreateNewBlock(): total size: %u block weight: %u txs: %u fees: %ld sigops %d\n", nSerializeSize, GetBlockWeight(*pblock), nBlockTx, nFees, nBlockSigOpsCost);

    // Fill in header
    pblock->hashPrevBlock  = pindexPrev->GetBlockHash();
    UpdateTime(pblock, chainparams.GetConsensus(), pindexPrev);
    pblock->nBits          = GetNextWorkRequired(pindexPrev, pblock, chainparams.GetConsensus());
    pblock->nNonce         = 0;
    pblocktemplate->vTxSigOpsCost[0] = WITNESS_SCALE_FACTOR * GetLegacySigOpCount(*pblock->vtx[0]);

    CValidationState state;
    if (!TestBlockValidity(state, chainparams, *pblock, pindexPrev, false, false)) {
        throw std::runtime_error(strprintf("%s: TestBlockValidity failed: %s", __func__, FormatStateMessage(state)));
    }
    int64_t nTime2 = GetTimeMicros();

    LogPrint("bench", "CreateNewBlock() packages: %.2fms (%d packages, %d updated descendants), validity: %.2fms (total %.2fms)\n", 0.001 * (nTime1 - nTimeStart), nPackagesSelected, nDescendantsUpdated, 0.001 * (nTime2 - nTime1), 0.001 * (nTime2 - nTimeStart));

    return std::move(pblocktemplate);
}

std::unique_ptr<CBlockTemplate> BlockAssembler::CreateNewBlock(const CScript& scriptPubKeyIn, const CScript& opReturnScript, time_t t, bool fMineWitnessTx)
{
    int64_t nTimeStart = GetTimeMicros();

    resetBlock();

    pblocktemplate.reset(new CBlockTemplate());

    if(!pblocktemplate.get())
        return nullptr;
    pblock = &pblocktemplate->block; // pointer for convenience

    // Add dummy coinbase tx as first transaction
    pblock->vtx.emplace_back();
    pblocktemplate->vTxFees.push_back(-1); // updated at end
    pblocktemplate->vTxSigOpsCost.push_back(-1); // updated at end

    LOCK2(cs_main, mempool.cs);
    CBlockIndex* pindexPrev = chainActive.Tip();
    nHeight = pindexPrev->nHeight + 1;

    pblock->nVersion = ComputeBlockVersion(pindexPrev, chainparams.GetConsensus());
    // -regtest only: allow overriding block.nVersion with
    // -blockversion=N to test forking scenarios
    if (chainparams.MineBlocksOnDemand())
        pblock->nVersion = GetArg("-blockversion", pblock->nVersion);

    pblock->nTime = t;
    const int64_t nMedianTimePast = pindexPrev->GetMedianTimePast();

    nLockTimeCutoff = (STANDARD_LOCKTIME_VERIFY_FLAGS & LOCKTIME_MEDIAN_TIME_PAST)
                       ? nMedianTimePast
                       : pblock->GetBlockTime();

    // Decide whether to include witness transactions
    // This is only needed in case the witness softfork activation is reverted
    // (which would require a very deep reorganization) or when
    // -promiscuousmempoolflags is used.
    // TODO: replace this with a call to main to assess validity of a mempool
    // transaction (which in most cases can be a no-op).
    fIncludeWitness = IsWitnessEnabled(pindexPrev, chainparams.GetConsensus()) && fMineWitnessTx;

    addPriorityTxs();
    int nPackagesSelected = 0;
    int nDescendantsUpdated = 0;
    addPackageTxs(nPackagesSelected, nDescendantsUpdated);

    int64_t nTime1 = GetTimeMicros();

    nLastBlockTx = nBlockTx;
    nLastBlockSize = nBlockSize;
    nLastBlockWeight = nBlockWeight;

    // Create coinbase transaction.
    CMutableTransaction coinbaseTx;
    coinbaseTx.vin.resize(1);
    coinbaseTx.vin[0].prevout.SetNull();
    coinbaseTx.vout.resize(2);
    coinbaseTx.vout[0].scriptPubKey = scriptPubKeyIn;
    coinbaseTx.vout[0].nValue = nFees + GetBlockSubsidy(nHeight, chainparams.GetConsensus());
    coinbaseTx.vin[0].scriptSig = CScript() << nHeight << OP_0;

    coinbaseTx.vout[1].nValue = 0;
    coinbaseTx.vout[1].scriptPubKey = opReturnScript;

    pblock->vtx[0] = MakeTransactionRef(std::move(coinbaseTx));
    //pblocktemplate->vchCoinbaseCommitment = GenerateCoinbaseCommitment(*pblock, pindexPrev, chainparams.GetConsensus());
    pblocktemplate->vTxFees[0] = -nFees;

    uint64_t nSerializeSize = GetSerializeSize(*pblock, SER_NETWORK, PROTOCOL_VERSION);
    LogPrintf("CreateNewBlock(): total size: %u block weight: %u txs: %u fees: %ld sigops %d\n", nSerializeSize, GetBlockWeight(*pblock), nBlockTx, nFees, nBlockSigOpsCost);

    // Fill in header
    pblock->hashPrevBlock  = pindexPrev->GetBlockHash();
    //UpdateTime(pblock, chainparams.GetConsensus(), pindexPrev);
    pblock->nTime = t;
    pblock->nBits          = GetNextWorkRequired(pindexPrev, pblock, chainparams.GetConsensus());
    pblock->nNonce         = 0;
    //pblocktemplate->vTxSigOpsCost[0] = WITNESS_SCALE_FACTOR * GetLegacySigOpCount(*pblock->vtx[0]);

    CValidationState state;
    if (!TestBlockValidity(state, chainparams, *pblock, pindexPrev, false, false)) {
        return nullptr;
        throw std::runtime_error(strprintf("%s: TestBlockValidity failed: %s", __func__, FormatStateMessage(state)));
    }
    int64_t nTime2 = GetTimeMicros();

    LogPrint("bench", "CreateNewBlock() packages: %.2fms (%d packages, %d updated descendants), validity: %.2fms (total %.2fms)\n", 0.001 * (nTime1 - nTimeStart), nPackagesSelected, nDescendantsUpdated, 0.001 * (nTime2 - nTime1), 0.001 * (nTime2 - nTimeStart));

    return std::move(pblocktemplate);
}

bool BlockAssembler::isStillDependent(CTxMemPool::txiter iter)
{
    BOOST_FOREACH(CTxMemPool::txiter parent, mempool.GetMemPoolParents(iter))
    {
        if (!inBlock.count(parent)) {
            return true;
        }
    }
    return false;
}

void BlockAssembler::onlyUnconfirmed(CTxMemPool::setEntries& testSet)
{
    for (CTxMemPool::setEntries::iterator iit = testSet.begin(); iit != testSet.end(); ) {
        // Only test txs not already in the block
        if (inBlock.count(*iit)) {
            testSet.erase(iit++);
        }
        else {
            iit++;
        }
    }
}

bool BlockAssembler::TestPackage(uint64_t packageSize, int64_t packageSigOpsCost)
{
    // TODO: switch to weight-based accounting for packages instead of vsize-based accounting.
    if (nBlockWeight + WITNESS_SCALE_FACTOR * packageSize >= nBlockMaxWeight)
        return false;
    if (nBlockSigOpsCost + packageSigOpsCost >= MAX_BLOCK_SIGOPS_COST)
        return false;
    return true;
}

// Perform transaction-level checks before adding to block:
// - transaction finality (locktime)
// - premature witness (in case segwit transactions are added to mempool before
//   segwit activation)
// - serialized size (in case -blockmaxsize is in use)
bool BlockAssembler::TestPackageTransactions(const CTxMemPool::setEntries& package)
{
    uint64_t nPotentialBlockSize = nBlockSize; // only used with fNeedSizeAccounting
    BOOST_FOREACH (const CTxMemPool::txiter it, package) {
        if (!IsFinalTx(it->GetTx(), nHeight, nLockTimeCutoff))
            return false;
        if (!fIncludeWitness && it->GetTx().HasWitness())
            return false;
        if (fNeedSizeAccounting) {
            uint64_t nTxSize = ::GetSerializeSize(it->GetTx(), SER_NETWORK, PROTOCOL_VERSION);
            if (nPotentialBlockSize + nTxSize >= nBlockMaxSize) {
                return false;
            }
            nPotentialBlockSize += nTxSize;
        }
    }
    return true;
}

bool BlockAssembler::TestForBlock(CTxMemPool::txiter iter)
{
    if (nBlockWeight + iter->GetTxWeight() >= nBlockMaxWeight) {
        // If the block is so close to full that no more txs will fit
        // or if we've tried more than 50 times to fill remaining space
        // then flag that the block is finished
        if (nBlockWeight >  nBlockMaxWeight - 400 || lastFewTxs > 50) {
             blockFinished = true;
             return false;
        }
        // Once we're within 4000 weight of a full block, only look at 50 more txs
        // to try to fill the remaining space.
        if (nBlockWeight > nBlockMaxWeight - 4000) {
            lastFewTxs++;
        }
        return false;
    }

    if (fNeedSizeAccounting) {
        if (nBlockSize + ::GetSerializeSize(iter->GetTx(), SER_NETWORK, PROTOCOL_VERSION) >= nBlockMaxSize) {
            if (nBlockSize >  nBlockMaxSize - 100 || lastFewTxs > 50) {
                 blockFinished = true;
                 return false;
            }
            if (nBlockSize > nBlockMaxSize - 1000) {
                lastFewTxs++;
            }
            return false;
        }
    }

    if (nBlockSigOpsCost + iter->GetSigOpCost() >= MAX_BLOCK_SIGOPS_COST) {
        // If the block has room for no more sig ops then
        // flag that the block is finished
        if (nBlockSigOpsCost > MAX_BLOCK_SIGOPS_COST - 8) {
            blockFinished = true;
            return false;
        }
        // Otherwise attempt to find another tx with fewer sigops
        // to put in the block.
        return false;
    }

    // Must check that lock times are still valid
    // This can be removed once MTP is always enforced
    // as long as reorgs keep the mempool consistent.
    if (!IsFinalTx(iter->GetTx(), nHeight, nLockTimeCutoff))
        return false;

    return true;
}

void BlockAssembler::AddToBlock(CTxMemPool::txiter iter)
{
    pblock->vtx.emplace_back(iter->GetSharedTx());
    pblocktemplate->vTxFees.push_back(iter->GetFee());
    pblocktemplate->vTxSigOpsCost.push_back(iter->GetSigOpCost());
    if (fNeedSizeAccounting) {
        nBlockSize += ::GetSerializeSize(iter->GetTx(), SER_NETWORK, PROTOCOL_VERSION);
    }
    nBlockWeight += iter->GetTxWeight();
    ++nBlockTx;
    nBlockSigOpsCost += iter->GetSigOpCost();
    nFees += iter->GetFee();
    inBlock.insert(iter);

    bool fPrintPriority = GetBoolArg("-printpriority", DEFAULT_PRINTPRIORITY);
    if (fPrintPriority) {
        double dPriority = iter->GetPriority(nHeight);
        CAmount dummy;
        mempool.ApplyDeltas(iter->GetTx().GetHash(), dPriority, dummy);
        LogPrintf("priority %.1f fee %s txid %s\n",
                  dPriority,
                  CFeeRate(iter->GetModifiedFee(), iter->GetTxSize()).ToString(),
                  iter->GetTx().GetHash().ToString());
    }
}

int BlockAssembler::UpdatePackagesForAdded(const CTxMemPool::setEntries& alreadyAdded,
        indexed_modified_transaction_set &mapModifiedTx)
{
    int nDescendantsUpdated = 0;
    BOOST_FOREACH(const CTxMemPool::txiter it, alreadyAdded) {
        CTxMemPool::setEntries descendants;
        mempool.CalculateDescendants(it, descendants);
        // Insert all descendants (not yet in block) into the modified set
        BOOST_FOREACH(CTxMemPool::txiter desc, descendants) {
            if (alreadyAdded.count(desc))
                continue;
            ++nDescendantsUpdated;
            modtxiter mit = mapModifiedTx.find(desc);
            if (mit == mapModifiedTx.end()) {
                CTxMemPoolModifiedEntry modEntry(desc);
                modEntry.nSizeWithAncestors -= it->GetTxSize();
                modEntry.nModFeesWithAncestors -= it->GetModifiedFee();
                modEntry.nSigOpCostWithAncestors -= it->GetSigOpCost();
                mapModifiedTx.insert(modEntry);
            } else {
                mapModifiedTx.modify(mit, update_for_parent_inclusion(it));
            }
        }
    }
    return nDescendantsUpdated;
}

// Skip entries in mapTx that are already in a block or are present
// in mapModifiedTx (which implies that the mapTx ancestor state is
// stale due to ancestor inclusion in the block)
// Also skip transactions that we've already failed to add. This can happen if
// we consider a transaction in mapModifiedTx and it fails: we can then
// potentially consider it again while walking mapTx.  It's currently
// guaranteed to fail again, but as a belt-and-suspenders check we put it in
// failedTx and avoid re-evaluation, since the re-evaluation would be using
// cached size/sigops/fee values that are not actually correct.
bool BlockAssembler::SkipMapTxEntry(CTxMemPool::txiter it, indexed_modified_transaction_set &mapModifiedTx, CTxMemPool::setEntries &failedTx)
{
    assert (it != mempool.mapTx.end());
    if (mapModifiedTx.count(it) || inBlock.count(it) || failedTx.count(it))
        return true;
    return false;
}

void BlockAssembler::SortForBlock(const CTxMemPool::setEntries& package, CTxMemPool::txiter entry, std::vector<CTxMemPool::txiter>& sortedEntries)
{
    // Sort package by ancestor count
    // If a transaction A depends on transaction B, then A's ancestor count
    // must be greater than B's.  So this is sufficient to validly order the
    // transactions for block inclusion.
    sortedEntries.clear();
    sortedEntries.insert(sortedEntries.begin(), package.begin(), package.end());
    std::sort(sortedEntries.begin(), sortedEntries.end(), CompareTxIterByAncestorCount());
}

// This transaction selection algorithm orders the mempool based
// on feerate of a transaction including all unconfirmed ancestors.
// Since we don't remove transactions from the mempool as we select them
// for block inclusion, we need an alternate method of updating the feerate
// of a transaction with its not-yet-selected ancestors as we go.
// This is accomplished by walking the in-mempool descendants of selected
// transactions and storing a temporary modified state in mapModifiedTxs.
// Each time through the loop, we compare the best transaction in
// mapModifiedTxs with the next transaction in the mempool to decide what
// transaction package to work on next.
void BlockAssembler::addPackageTxs(int &nPackagesSelected, int &nDescendantsUpdated)
{
    // mapModifiedTx will store sorted packages after they are modified
    // because some of their txs are already in the block
    indexed_modified_transaction_set mapModifiedTx;
    // Keep track of entries that failed inclusion, to avoid duplicate work
    CTxMemPool::setEntries failedTx;

    // Start by adding all descendants of previously added txs to mapModifiedTx
    // and modifying them for their already included ancestors
    UpdatePackagesForAdded(inBlock, mapModifiedTx);

    CTxMemPool::indexed_transaction_set::index<ancestor_score>::type::iterator mi = mempool.mapTx.get<ancestor_score>().begin();
    CTxMemPool::txiter iter;

    // Limit the number of attempts to add transactions to the block when it is
    // close to full; this is just a simple heuristic to finish quickly if the
    // mempool has a lot of entries.
    const int64_t MAX_CONSECUTIVE_FAILURES = 1000;
    int64_t nConsecutiveFailed = 0;

    while (mi != mempool.mapTx.get<ancestor_score>().end() || !mapModifiedTx.empty())
    {
        // First try to find a new transaction in mapTx to evaluate.
        if (mi != mempool.mapTx.get<ancestor_score>().end() &&
                SkipMapTxEntry(mempool.mapTx.project<0>(mi), mapModifiedTx, failedTx)) {
            ++mi;
            continue;
        }

        // Now that mi is not stale, determine which transaction to evaluate:
        // the next entry from mapTx, or the best from mapModifiedTx?
        bool fUsingModified = false;

        modtxscoreiter modit = mapModifiedTx.get<ancestor_score>().begin();
        if (mi == mempool.mapTx.get<ancestor_score>().end()) {
            // We're out of entries in mapTx; use the entry from mapModifiedTx
            iter = modit->iter;
            fUsingModified = true;
        } else {
            // Try to compare the mapTx entry to the mapModifiedTx entry
            iter = mempool.mapTx.project<0>(mi);
            if (modit != mapModifiedTx.get<ancestor_score>().end() &&
                    CompareModifiedEntry()(*modit, CTxMemPoolModifiedEntry(iter))) {
                // The best entry in mapModifiedTx has higher score
                // than the one from mapTx.
                // Switch which transaction (package) to consider
                iter = modit->iter;
                fUsingModified = true;
            } else {
                // Either no entry in mapModifiedTx, or it's worse than mapTx.
                // Increment mi for the next loop iteration.
                ++mi;
            }
        }

        // We skip mapTx entries that are inBlock, and mapModifiedTx shouldn't
        // contain anything that is inBlock.
        assert(!inBlock.count(iter));

        uint64_t packageSize = iter->GetSizeWithAncestors();
        CAmount packageFees = iter->GetModFeesWithAncestors();
        int64_t packageSigOpsCost = iter->GetSigOpCostWithAncestors();
        if (fUsingModified) {
            packageSize = modit->nSizeWithAncestors;
            packageFees = modit->nModFeesWithAncestors;
            packageSigOpsCost = modit->nSigOpCostWithAncestors;
        }

        if (packageFees < blockMinFeeRate.GetFee(packageSize)) {
            // Everything else we might consider has a lower fee rate
            return;
        }

        if (!TestPackage(packageSize, packageSigOpsCost)) {
            if (fUsingModified) {
                // Since we always look at the best entry in mapModifiedTx,
                // we must erase failed entries so that we can consider the
                // next best entry on the next loop iteration
                mapModifiedTx.get<ancestor_score>().erase(modit);
                failedTx.insert(iter);
            }

            ++nConsecutiveFailed;

            if (nConsecutiveFailed > MAX_CONSECUTIVE_FAILURES && nBlockWeight >
                    nBlockMaxWeight - 4000) {
                // Give up if we're close to full and haven't succeeded in a while
                break;
            }
            continue;
        }

        CTxMemPool::setEntries ancestors;
        uint64_t nNoLimit = std::numeric_limits<uint64_t>::max();
        std::string dummy;
        mempool.CalculateMemPoolAncestors(*iter, ancestors, nNoLimit, nNoLimit, nNoLimit, nNoLimit, dummy, false);

        onlyUnconfirmed(ancestors);
        ancestors.insert(iter);

        // Test if all tx's are Final
        if (!TestPackageTransactions(ancestors)) {
            if (fUsingModified) {
                mapModifiedTx.get<ancestor_score>().erase(modit);
                failedTx.insert(iter);
            }
            continue;
        }

        // This transaction will make it in; reset the failed counter.
        nConsecutiveFailed = 0;

        // Package can be added. Sort the entries in a valid order.
        std::vector<CTxMemPool::txiter> sortedEntries;
        SortForBlock(ancestors, iter, sortedEntries);

        for (size_t i=0; i<sortedEntries.size(); ++i) {
            AddToBlock(sortedEntries[i]);
            // Erase from the modified set, if present
            mapModifiedTx.erase(sortedEntries[i]);
        }

        ++nPackagesSelected;

        // Update transactions that depend on each of these
        nDescendantsUpdated += UpdatePackagesForAdded(ancestors, mapModifiedTx);
    }
}

void BlockAssembler::addPriorityTxs()
{
    // How much of the block should be dedicated to high-priority transactions,
    // included regardless of the fees they pay
    unsigned int nBlockPrioritySize = GetArg("-blockprioritysize", DEFAULT_BLOCK_PRIORITY_SIZE);
    nBlockPrioritySize = std::min(nBlockMaxSize, nBlockPrioritySize);

    if (nBlockPrioritySize == 0) {
        return;
    }

    bool fSizeAccounting = fNeedSizeAccounting;
    fNeedSizeAccounting = true;

    // This vector will be sorted into a priority queue:
    std::vector<TxCoinAgePriority> vecPriority;
    TxCoinAgePriorityCompare pricomparer;
    std::map<CTxMemPool::txiter, double, CTxMemPool::CompareIteratorByHash> waitPriMap;
    typedef std::map<CTxMemPool::txiter, double, CTxMemPool::CompareIteratorByHash>::iterator waitPriIter;
    double actualPriority = -1;

    vecPriority.reserve(mempool.mapTx.size());
    for (CTxMemPool::indexed_transaction_set::iterator mi = mempool.mapTx.begin();
         mi != mempool.mapTx.end(); ++mi)
    {
        double dPriority = mi->GetPriority(nHeight);
        CAmount dummy;
        mempool.ApplyDeltas(mi->GetTx().GetHash(), dPriority, dummy);
        vecPriority.push_back(TxCoinAgePriority(dPriority, mi));
    }
    std::make_heap(vecPriority.begin(), vecPriority.end(), pricomparer);

    CTxMemPool::txiter iter;
    while (!vecPriority.empty() && !blockFinished) { // add a tx from priority queue to fill the blockprioritysize
        iter = vecPriority.front().second;
        actualPriority = vecPriority.front().first;
        std::pop_heap(vecPriority.begin(), vecPriority.end(), pricomparer);
        vecPriority.pop_back();

        // If tx already in block, skip
        if (inBlock.count(iter)) {
            assert(false); // shouldn't happen for priority txs
            continue;
        }

        // cannot accept witness transactions into a non-witness block
        if (!fIncludeWitness && iter->GetTx().HasWitness())
            continue;

        // If tx is dependent on other mempool txs which haven't yet been included
        // then put it in the waitSet
        if (isStillDependent(iter)) {
            waitPriMap.insert(std::make_pair(iter, actualPriority));
            continue;
        }

        // If this tx fits in the block add it, otherwise keep looping
        if (TestForBlock(iter)) {
            AddToBlock(iter);

            // If now that this txs is added we've surpassed our desired priority size
            // or have dropped below the AllowFreeThreshold, then we're done adding priority txs
            if (nBlockSize >= nBlockPrioritySize || !AllowFree(actualPriority)) {
                break;
            }

            // This tx was successfully added, so
            // add transactions that depend on this one to the priority queue to try again
            BOOST_FOREACH(CTxMemPool::txiter child, mempool.GetMemPoolChildren(iter))
            {
                waitPriIter wpiter = waitPriMap.find(child);
                if (wpiter != waitPriMap.end()) {
                    vecPriority.push_back(TxCoinAgePriority(wpiter->second,child));
                    std::push_heap(vecPriority.begin(), vecPriority.end(), pricomparer);
                    waitPriMap.erase(wpiter);
                }
            }
        }
    }
    fNeedSizeAccounting = fSizeAccounting;
}

void IncrementExtraNonce(CBlock* pblock, const CBlockIndex* pindexPrev, unsigned int& nExtraNonce)
{
    // Update nExtraNonce
    static uint256 hashPrevBlock;
    if (hashPrevBlock != pblock->hashPrevBlock)
    {
        nExtraNonce = 0;
        hashPrevBlock = pblock->hashPrevBlock;
    }
    ++nExtraNonce;
    int nHeight = pindexPrev->nHeight+1; // Height first in coinbase required for block.version=2
    CMutableTransaction txCoinbase(*pblock->vtx[0]);
    txCoinbase.vin[0].scriptSig = (CScript() << nHeight << CScriptNum(nExtraNonce)) + COINBASE_FLAGS;
    assert(txCoinbase.vin[0].scriptSig.size() <= 100);

    pblock->vtx[0] = MakeTransactionRef(std::move(txCoinbase));
    pblock->hashMerkleRoot = BlockMerkleRoot(*pblock);
}

static DPoS gDPoS;
static DPoS *gpDPoS = NULL;

DPoS::~DPoS()
{
	WriteIrreversibleBlockInfo(cIrreversibleBlockInfo);
}

void DPoS::Init()
{
    nMaxMemory = GetArg("-maxmemory", DEFAULT_MAX_MEMORY_SIZE);
    if(Params().NetworkIDString() == "main") {
        cSuperForgerAddress = CBitcoinAddress("166D9UoFdPcDEGFngswE226zigS8uBnm3C");
        gDPoS.nDposStartTime = 1539181795;

        nMaxDelegateNumber = 101;
        nBlockIntervalTime = 3;
        nDposStartHeight = 7000;
    } else {
        cSuperForgerAddress = CBitcoinAddress("my5ioJEbbhMjRzgyQpcnq6fmbfUMQgTqMZ");
        gDPoS.nDposStartTime = 0;

        nMaxDelegateNumber = 10;
        nBlockIntervalTime = 3;
        nDposStartHeight = 7000;
    }

	strIrreversibleBlockFileName = (GetDataDir() / "dpos" / "irreversible_block.dat").string();
	ReadIrreversibleBlockInfo(cIrreversibleBlockInfo);
   
    if(chainActive.Height() >= nDposStartHeight - 1) {
        SetStartTime(chainActive[nDposStartHeight -1]->nTime);
    }
}

DPoS& DPoS::GetInstance()
{
    if(gpDPoS == NULL) {
        gDPoS.Init();
        gpDPoS = &gDPoS;
    }

    return gDPoS;
}

bool DPoS::IsMining(DelegateInfo& cDelegateInfo, const CBitcoinAddress& cAddress, time_t t)
{
    CBlockIndex* pBlockIndex = chainActive.Tip();
    if(pBlockIndex->nHeight < nDposStartHeight - 1) {
        if(cAddress == cSuperForgerAddress) {
            static time_t tLast = 0;
            if(t < tLast + nBlockIntervalTime) {
                return false;
            } else {
                tLast = t;
                return true;
            }
        } else {
            return false;
        }
    }

    uint64_t nCurrentLoopIndex = GetLoopIndex(t);
    uint32_t nCurrentDelegateIndex = GetDelegateIndex(t);
    uint64_t nPrevLoopIndex = GetLoopIndex(pBlockIndex->nTime);
    uint32_t nPrevDelegateIndex = GetDelegateIndex(pBlockIndex->nTime);

    CKeyID keyid;
    if(cAddress.GetKeyID(keyid) == false) {
        LogPrintf("IsMining: GetKeyID failed");
        return false;
    }

    if(pBlockIndex->nHeight == nDposStartHeight - 1) {
        cDelegateInfo = DPoS::GetNextDelegates(t);
        if(cDelegateInfo.delegates[nCurrentDelegateIndex].keyid == keyid) {
            return true;
        } else {
            return false;
        }
    }

    if(nCurrentLoopIndex > nPrevLoopIndex) {
        cDelegateInfo = DPoS::GetNextDelegates(t);
        if(cDelegateInfo.delegates[nCurrentDelegateIndex].keyid == keyid) {
            return true;
        } else {
            return false;
        }
    } else if(nCurrentLoopIndex == nPrevLoopIndex && nCurrentDelegateIndex > nPrevDelegateIndex) {
        DelegateInfo cCurrentDelegateInfo;
        if(GetBlockDelegates(cCurrentDelegateInfo, pBlockIndex)) {
            if(nCurrentDelegateIndex + 1 > cCurrentDelegateInfo.delegates.size()) {
                return false;
            } else if(cCurrentDelegateInfo.delegates[nCurrentDelegateIndex].keyid == keyid) {
                //cDelegateInfo.delegates.clear();
                return true;
            } else {
                return false;
            }
        } else {
            return false;
        }
    } else {
        return false;
    }

    return false;
}

DelegateInfo DPoS::GetNextDelegates(int64_t t)
{
    uint64_t nMinHoldBalance = 500000000000;

    std::vector<Delegate> delegates = Vote::GetInstance().GetTopDelegateInfo(nMinHoldBalance, nMaxDelegateNumber - 1);

    LogPrint("DPoS", "GetNextDelegates start\n");
    for(auto i : delegates)
        LogPrint("DPoS", "delegate %s %lu\n", CBitcoinAddress(i.keyid).ToString().c_str(), i.votes);
    LogPrint("DPoS", "GetNextDelegates end\n");

    Delegate delegate;
    cSuperForgerAddress.GetKeyID(delegate.keyid);
    delegate.votes = 7;
    delegates.insert(delegates.begin(), delegate);

    delegates.resize(nMaxDelegateNumber);

    DelegateInfo cDelegateInfo;
    cDelegateInfo.delegates = SortDelegate(delegates, t);

    return cDelegateInfo;
}

std::vector<char> GetRand(unsigned num, unsigned int seed)
{
    std::vector<char> r;
    std::vector<char> s(num, -1);

    while(r.size() < num) {
        uint64_t v;
        v = rand_r(&seed);
        v %= num;
        if(s[v] < 0) {
            s[v] = 1;
            r.push_back(v);
        }
    }

    return r;
}

std::vector<Delegate> DPoS::SortDelegate(const std::vector<Delegate>& delegates, uint64_t t)
{
    std::vector<Delegate> result;
    unsigned int seed = (unsigned int)t;
    std::vector<char>&& r = GetRand(delegates.size(), seed);
    for(auto& i : r) {
        result.push_back(delegates[i]);
    }
    return result;
}

bool DPoS::GetBlockDelegates(DelegateInfo& cDelegateInfo, CBlockIndex* pBlockIndex)
{
    bool ret = false;
    uint64_t nLoopIndex = GetLoopIndex(pBlockIndex->nTime);
    while(pBlockIndex) {
        if(pBlockIndex->nHeight == nDposStartHeight || GetLoopIndex(pBlockIndex->pprev->nTime) < nLoopIndex) {
            CBlock block;
            if(ReadBlockFromDisk(block, pBlockIndex, Params().GetConsensus())) {
                ret = GetBlockDelegate(cDelegateInfo, block);
            }
            break;
        }

        pBlockIndex = pBlockIndex->pprev;
    }
    
    return ret;
}

bool DPoS::GetBlockDelegates(DelegateInfo& cDelegateInfo, const CBlock& block)
{
    CBlockIndex blockindex;
    blockindex.nTime = block.nTime;
    BlockMap::iterator miSelf = mapBlockIndex.find(block.hashPrevBlock);
    if(miSelf == mapBlockIndex.end()) {
        LogPrintf("GetBlockDelegates find blockindex(%s) error\n", block.hashPrevBlock.ToString().c_str());
        return false;
    }
    blockindex.pprev = miSelf->second;
    return GetBlockDelegates(cDelegateInfo, &blockindex);
}

uint64_t DPoS::GetLoopIndex(uint64_t time)
{
    if(time < nDposStartTime) {
        return 0;
    } else {
        return (time - nDposStartTime) / (nMaxDelegateNumber * nBlockIntervalTime);
    }
}

uint32_t DPoS::GetDelegateIndex(uint64_t time)
{
    if(time < nDposStartTime) {
        return 0;
    } else {
        return (time - nDposStartTime) % (nMaxDelegateNumber * nBlockIntervalTime) / nBlockIntervalTime;
    }
}

bool DPoS::CheckCoinbase(const CTransaction& tx, time_t t, int64_t height)
{
    bool ret = false;
    if(tx.vout.size() == 2) {
        CTxDestination dest;
        if (ExtractDestination(tx.vout[0].scriptPubKey, dest) ) {
            DelegateInfo cDelegateInfo;
            ret = ScriptToDelegateInfo(cDelegateInfo, t, tx.vout[1].scriptPubKey, &dest, true);
        }
    }

    if(ret == false) {
        LogPrintf("CheckCoinbase txhash:%s failed!", tx.GetHash().ToString());
    }
    return ret;
}

//OP_RETURN VECTOR<UNSIGNED CHAR>
//OP_RETURN PUBKEY SIG(t) DELEGATE_IDS
CScript DPoS::DelegateInfoToScript(const DelegateInfo& cDelegateInfo, const CKey& delegatekey, uint64_t t)
{
    const std::vector<Delegate>& delegates = cDelegateInfo.delegates;

    int nDataLen = 1 + delegates.size() * 20;
    std::vector<unsigned char> data;

    if(cDelegateInfo.delegates.empty() == false) {
        data.resize(nDataLen);
        data[0] = 0x7;

        unsigned char* pData = &data[1];
        for(unsigned int i =0; i < delegates.size(); ++i) {
            memcpy(pData, delegates[i].keyid.begin(), 20);
            pData += 20;
        }
    }

    std::vector<unsigned char> vchSig;
    std::string ts = std::to_string(t);
    delegatekey.Sign(Hash(ts.begin(), ts.end()), vchSig);

    CScript script;
    if(cDelegateInfo.delegates.empty() == false) {
        script << OP_RETURN << ToByteVector(delegatekey.GetPubKey()) << vchSig << data;
    } else {
        script << OP_RETURN << ToByteVector(delegatekey.GetPubKey()) << vchSig;
    }
    return script;
}

//OP_RETURN VECTOR<UNSIGNED CHAR>
//OP_RETURN PUBKEY SIG(t) DELEGATE_IDS
bool DPoS::ScriptToDelegateInfo(DelegateInfo& cDelegateInfo, uint64_t t, const CScript& script, const CTxDestination* paddress, bool fCheck)
{
    opcodetype op;
    std::vector<unsigned char> data;
    CScript::const_iterator it = script.begin();
    script.GetOp(it, op);
    if(op == OP_RETURN) {
        std::vector<unsigned char> vctPublicKey;
        if(script.GetOp2(it, op, &vctPublicKey) == false) {
            return false;
        }

        CPubKey pubkey(vctPublicKey);

        std::vector<unsigned char> vctSig;
        if(script.GetOp2(it, op, &vctSig) == false) {
            return false;
        }

        std::string sh = std::to_string(t);
        auto hash = Hash(sh.begin(), sh.end());

        if(fCheck) {
            if(pubkey.Verify(hash, vctSig) == false) {
                return false;
            }
        }

        if(paddress) {
            CBitcoinAddress address;
            address.Set(pubkey.GetID());
            if(*paddress != address.Get()) {
                return false;
            }
        }

        if(script.GetOp2(it, op, &data)) {
        if((data.size() - (1)) % (20) == 0) {
            unsigned char* pData = &data[1];
            uint32_t nDelegateNum = (data.size() - (1)) / (20);
            for(unsigned int i =0; i < nDelegateNum; ++i) {
                std::vector<unsigned char> vct(pData, pData + 20);
                cDelegateInfo.delegates.push_back(Delegate(CKeyID(base_blob<160>(vct)), 0));
                pData += 20;
            }

            return true;
        }
        }
    }
    return true;
}

CBitcoinAddress DPoS::GetBlockForgerAddress(const CBlock& block)
{
    auto& tx = block.vtx[0];

    CBitcoinAddress ret;
    if(tx->IsCoinBase() && tx->vout.size() == 2) {
        CTxDestination dest;
        if(ExtractDestination(tx->vout[0].scriptPubKey, dest)) {
            ret.Set(dest);
        }
    }

    return ret;
}

bool DPoS::GetBlockForgerKeyID(CKeyID& keyid, const CBlock& block)
{
    bool ret = false;
    CBitcoinAddress address = GetBlockForgerAddress(block);
    ret = address.GetKeyID(keyid);
    return ret;
}

bool DPoS::GetBlockDelegate(DelegateInfo& cDelegateInfo, const CBlock& block)
{
    bool ret = false;

    auto tx = block.vtx[0];
    if(tx->IsCoinBase() && tx->vout.size() == 2) {
        opcodetype op;
        std::vector<unsigned char> vctData;
        {
        CScript::const_iterator it = tx->vout[0].scriptPubKey.begin();
        auto& script = tx->vout[0].scriptPubKey;
        script.GetOp2(it, op, &vctData);
        }

        auto script = tx->vout[1].scriptPubKey;
        ret = ScriptToDelegateInfo(cDelegateInfo, block.nTime, script, NULL, false);
    }

    return ret;
}

bool DPoS::CheckBlockDelegate(const CBlock& block)
{
    DelegateInfo cDelegateInfo;
    if(DPoS::GetBlockDelegate(cDelegateInfo, block) == false) {
        LogPrint("CheckBlockDelegate GetBlockDelegate hash:%s error\n", block.GetHash().ToString().c_str());
        return false;
    }

    bool ret = true;
    DelegateInfo cNextDelegateInfo = GetNextDelegates(block.nTime);
    if(cDelegateInfo.delegates.size() == cNextDelegateInfo.delegates.size()) {
        for(unsigned int i =0; i < cDelegateInfo.delegates.size(); ++i) {
            if(cDelegateInfo.delegates[i].keyid != cNextDelegateInfo.delegates[i].keyid) {
                ret = false;
                break;
            }
        }
    }

    if(ret == false) {
        for(unsigned int i =0; i < cDelegateInfo.delegates.size(); ++i) {
            LogPrintf("CheckBlockDelegate BlockDelegate[%u]: %s\n", i, CBitcoinAddress(cDelegateInfo.delegates[i].keyid).ToString().c_str());
        }

        for(unsigned int i =0; i < cNextDelegateInfo.delegates.size(); ++i) {
            LogPrintf("CheckBlockDelegate NextBlockDelegate[%u]: %s %llu\n", i, CBitcoinAddress(cNextDelegateInfo.delegates[i].keyid).ToString().c_str(), cNextDelegateInfo.delegates[i].votes);
        }
    }

      return ret;
}

bool DPoS::CheckBlockHeader(const CBlockHeader& block)
{
    auto t = time(NULL) + 3;
    if(block.nTime > t) {
        LogPrintf("Block:%u time:%s error\n", block.nTime, t - 3);
        return false;
    }

    if(block.hashPrevBlock.IsNull()) {
        return true;
    }

    return true;

    BlockMap::iterator miSelf = mapBlockIndex.find(block.hashPrevBlock);
    if(miSelf == mapBlockIndex.end()) {
        LogPrintf("CheckBlockHeader find blockindex(%s) error\n", block.hashPrevBlock.ToString().c_str());
        return false;
    }

    CBlockIndex* pPrevBlockIndex = miSelf->second;

    if(pPrevBlockIndex->nHeight == nDposStartHeight - 1) {
        //SetStartTime(chainActive[nDposStartHeight -1]->nTime);
        SetStartTime(pPrevBlockIndex->nTime);
    }

    if(pPrevBlockIndex->nHeight < nDposStartHeight) {
        return true;
    }

    bool ret = false;
    uint64_t nCurrentLoopIndex = GetLoopIndex(block.nTime);
    uint32_t nCurrentDelegateIndex = GetDelegateIndex(block.nTime);
    uint64_t nPrevLoopIndex = GetLoopIndex(pPrevBlockIndex->nTime);
    uint32_t nPrevDelegateIndex = GetDelegateIndex(pPrevBlockIndex->nTime);
    
    if(nCurrentLoopIndex > nPrevLoopIndex
        || (nCurrentLoopIndex == nPrevLoopIndex && nCurrentDelegateIndex > nPrevDelegateIndex)) {
        ret = true;
    } else {
        ret = false;
    }

    if(ret == false) {
        LogPrintf("DPoS CheckBlockHeader hash(%s) error\n", block.GetHash().ToString().c_str());
    }
    return ret;
}

bool DPoS::CheckBlock(const CBlockIndex& blockindex, bool fIsCheckDelegateInfo)
{
    if(chainActive.Height() == nDposStartHeight - 1) {
        SetStartTime(chainActive[nDposStartHeight -1]->nTime);
    }

    CBlock block;
    if(ReadBlockFromDisk(block, &blockindex, Params().GetConsensus()) == false) {
        return false;
    }

    return CheckBlock(block, fIsCheckDelegateInfo);
}

bool DPoS::CheckBlock(const CBlock& block, bool fIsCheckDelegateInfo)
{
    auto t = time(NULL) + 3;
    if(block.nTime > t) {
        LogPrintf("Block:%u time:%s error\n", block.nTime, t - 3);
        return false;
    }

    if(block.hashPrevBlock.IsNull()) {
        return true;
    }

    BlockMap::iterator miSelf = mapBlockIndex.find(block.hashPrevBlock);
    if(miSelf == mapBlockIndex.end()) {
        LogPrintf("CheckBlock find blockindex(%s) error\n", block.hashPrevBlock.ToString().c_str());
        return false;
    }

    CBlockIndex* pPrevBlockIndex = miSelf->second;

    int64_t nBlockHeight = pPrevBlockIndex->nHeight + 1;

    if(CheckCoinbase(*block.vtx[0], block.nTime, nBlockHeight) == false) {
        LogPrintf("CheckBlock CheckCoinbase error\n");
        return false;
    }

    if(nDposStartTime == 0 && chainActive.Height() >= nDposStartHeight - 1) {
        SetStartTime(chainActive[nDposStartHeight -1]->nTime);
    }

    if(nBlockHeight < nDposStartHeight) {
        if(GetBlockForgerAddress(block) == cSuperForgerAddress) {
            return true;
        } else {
            LogPrintf("CheckBlock nBlockHeight < nDposStartHeight ForgerAddress error\n");
            return false;
        }
    }
    
    uint64_t nCurrentLoopIndex = GetLoopIndex(block.nTime);
    uint32_t nCurrentDelegateIndex = GetDelegateIndex(block.nTime);
    uint64_t nPrevLoopIndex = 0;
    uint32_t nPrevDelegateIndex = 0;

    nPrevLoopIndex = GetLoopIndex(pPrevBlockIndex->nTime);
    nPrevDelegateIndex = GetDelegateIndex(pPrevBlockIndex->nTime);

    bool ret = false;
    DelegateInfo cDelegateInfo;

    if(nBlockHeight == nDposStartHeight) {
        if(fIsCheckDelegateInfo) {
            if(CheckBlockDelegate(block) == false) {
                return false;
            }
        }
    
        GetBlockDelegate(cDelegateInfo, block);
    } else if(nCurrentLoopIndex < nPrevLoopIndex) {
        LogPrintf("CheckBlock nCurrentLoopIndex < nPrevLoopIndex error\n");
        return false;
    } else if(nCurrentLoopIndex > nPrevLoopIndex) {
        if(fIsCheckDelegateInfo) {
            if(CheckBlockDelegate(block) == false) {
                return false;
            }
            ProcessIrreversibleBlock(nBlockHeight, block.GetHash());
        }

        GetBlockDelegate(cDelegateInfo, block);
    //} else if(nCurrentLoopIndex == nPrevLoopIndex) {
    } else {
        if(nCurrentDelegateIndex <= nPrevDelegateIndex) {
            LogPrintf("CheckBlock nCurrentDelegateIndex <= nPrevDelegateIndex error pretime:%u\n", pPrevBlockIndex->nTime);
            return false;
        }

        GetBlockDelegates(cDelegateInfo, pPrevBlockIndex);
    }

    CKeyID delegate;
    GetBlockForgerKeyID(delegate, block);
    if(nCurrentDelegateIndex < cDelegateInfo.delegates.size()
        && cDelegateInfo.delegates[nCurrentDelegateIndex].keyid == delegate) {
        ret = true;
    } else {
        LogPrintf("CheckBlock GetDelegateID blockhash:%s error\n", block.ToString().c_str());
    }

    return ret;
}

int DPoS::FastCheckBlockHash(const uint256& hash, uint64_t height)
{
    int ret = 0;
    if(height < 622578 || height > 1334286) {
        return ret;
    }

    auto it = mapBlockHash.find(height);
    if(it != mapBlockHash.end()) {
        if(it->second == hash.ToString()) {
            ret = 1;
        } else {
            ret = -1;
        }
    }

    return ret;
}

bool DPoS::IsOnTheSameChain(const std::pair<int64_t, uint256>& first, const std::pair<int64_t, uint256>& second)
{
    bool ret = false;

    BlockMap::iterator it = mapBlockIndex.find(second.second);
    if(it != mapBlockIndex.end()) {
        CBlockIndex *pindex = it->second;
        while(pindex->nHeight != first.first) {
            pindex = pindex->pprev;
        }

        if(*pindex->phashBlock == first.second) {
            ret = true;
        }
    }

    return ret;
}

IrreversibleBlockInfo DPoS::GetIrreversibleBlockInfo()
{
    return cIrreversibleBlockInfo;
}

void DPoS::SetIrreversibleBlockInfo(const IrreversibleBlockInfo& info)
{
	cIrreversibleBlockInfo = info;
}

bool DPoS::ReadIrreversibleBlockInfo(IrreversibleBlockInfo& info)
{
	if(fUseIrreversibleBlock == false) {
		return true;
	}

	bool ret = false;
    FILE *file = fopen(strIrreversibleBlockFileName.c_str(), "r");
    if(file) {
        char buff[128];
		char line[256];
		int64_t height;
		uint256 hash;

       while(fgets(line, sizeof(line), file)) {
      		if(sscanf(line, "%ld;%s\n", &height, buff) > 0) {
				hash.SetHex(buff);
				AddIrreversibleBlock(height, hash);
			}
		}

        fclose(file);
		ret = true;
    }

	return ret;
}

bool DPoS::WriteIrreversibleBlockInfo(const IrreversibleBlockInfo& info)
{
	if(fUseIrreversibleBlock == false) {
		return true;
	}

    bool ret = false;
	if(cIrreversibleBlockInfo.mapHeightHash.empty()) {
		return true;	
	}

    FILE *file = fopen(strIrreversibleBlockFileName.c_str(), "w");
    if(file) {
		for(auto& it : cIrreversibleBlockInfo.mapHeightHash) {
        	fprintf(file, "%ld;%s\n", it.first, it.second.ToString().c_str());
		}
        fclose(file);
        ret = true;
    }

    return ret;
}

void DPoS::ProcessIrreversibleBlock(int64_t height, uint256 hash)
{
	if(fUseIrreversibleBlock == false) {
		return;
	}

    write_lock l(lockIrreversibleBlockInfo);

	int i = 0;
	for(i = nMaxConfirmBlockCount - 1; i >= 0; --i) {
		if(cIrreversibleBlockInfo.heights[i] < 0 || height <= cIrreversibleBlockInfo.heights[i]) {
			cIrreversibleBlockInfo.heights[i] = -1;
		} else {
			if(IsOnTheSameChain(std::make_pair(cIrreversibleBlockInfo.heights[i], cIrreversibleBlockInfo.hashs[i]), std::make_pair(height, hash))) {
				assert(height > cIrreversibleBlockInfo.heights[i]);
				if((height - cIrreversibleBlockInfo.heights[i]) * 100 >= nMaxDelegateNumber * nFirstIrreversibleThreshold) {
					AddIrreversibleBlock(cIrreversibleBlockInfo.heights[i], cIrreversibleBlockInfo.hashs[i]);
					LogPrintf("First NewIrreversibleBlock height:%ld hash:%s\n", cIrreversibleBlockInfo.heights[i], cIrreversibleBlockInfo.hashs[i].ToString().c_str());

					Vote::GetInstance().GetCommittee().NewIrreversibleBlock(cIrreversibleBlockInfo.heights[i]);
					Vote::GetInstance().GetBill().NewIrreversibleBlock(cIrreversibleBlockInfo.heights[i]);

					for(auto k = 0; k < nMaxConfirmBlockCount; ++k) {
						cIrreversibleBlockInfo.heights[k] = -1;	
					}
					cIrreversibleBlockInfo.heights[0] = height;
					cIrreversibleBlockInfo.hashs[0] = hash;
					return;
				} else if((height - cIrreversibleBlockInfo.heights[i]) * 100 >= nMaxDelegateNumber * nSecondIrreversibleThreshold) {
					if(i == nMaxConfirmBlockCount - 1) {
						AddIrreversibleBlock(cIrreversibleBlockInfo.heights[i], cIrreversibleBlockInfo.hashs[i]);
						LogPrintf("Second NewIrreversibleBlock height:%ld hash:%s\n", cIrreversibleBlockInfo.heights[i], cIrreversibleBlockInfo.hashs[i].ToString().c_str());
						Vote::GetInstance().GetCommittee().NewIrreversibleBlock(cIrreversibleBlockInfo.heights[i]);
						Vote::GetInstance().GetBill().NewIrreversibleBlock(cIrreversibleBlockInfo.heights[i]);

						for(int j = 0; j < nMaxConfirmBlockCount -1; ++j) {
							cIrreversibleBlockInfo.heights[j] = cIrreversibleBlockInfo.heights[j+1];
							cIrreversibleBlockInfo.hashs[j] = cIrreversibleBlockInfo.hashs[j+1];
						}

						cIrreversibleBlockInfo.heights[nMaxConfirmBlockCount - 1] = height;
						cIrreversibleBlockInfo.hashs[nMaxConfirmBlockCount - 1] = hash;
						return;
					} else {
						cIrreversibleBlockInfo.heights[i+1] = height;
						cIrreversibleBlockInfo.hashs[i+1] = hash;
						return;
					}
				} else {
					for(auto k = 0; k < nMaxConfirmBlockCount; ++k) {
						cIrreversibleBlockInfo.heights[k] = -1;	
					}
					cIrreversibleBlockInfo.heights[0] = height;
					cIrreversibleBlockInfo.hashs[0] = hash;
					return;
				}
			} else {
				cIrreversibleBlockInfo.heights[i] = -1;
			}
		}
	}

	if(i < 0) {
		cIrreversibleBlockInfo.heights[0] = height;
		cIrreversibleBlockInfo.hashs[0] = hash;
		return;
	}
}

bool DPoS::IsValidBlockCheckIrreversibleBlock(int64_t height, uint256 hash)
{
	if(fUseIrreversibleBlock == false) {
		return true;
	}

	bool ret = true;
    read_lock l(lockIrreversibleBlockInfo);

	auto it = cIrreversibleBlockInfo.mapHeightHash.find(height);
	if(it != cIrreversibleBlockInfo.mapHeightHash.end()) {
		if(hash != it->second) {
			LogPrintf("CheckIrreversibleBlock[%ld:%s] invalid block[%ld:%s]\n", it->first, it->second.ToString().c_str(), height, hash.ToString().c_str());
			ret = false;
		}
	}

	return ret;
}

void DPoS::AddIrreversibleBlock(int64_t height, uint256 hash)
{
	while((int64_t)cIrreversibleBlockInfo.mapHeightHash.size() >= nMaxIrreversibleCount) {
		cIrreversibleBlockInfo.mapHeightHash.erase(cIrreversibleBlockInfo.mapHeightHash.begin());
	}

	cIrreversibleBlockInfo.mapHeightHash.insert(std::make_pair(height, hash));

	Vote::GetInstance().DeleteInvalidVote(height);
}

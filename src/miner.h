// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_MINER_H
#define BITCOIN_MINER_H

#include "primitives/block.h"
#include "txmempool.h"
#include "key.h"
#include "script/standard.h"
#include "base58.h"

#include <stdint.h>
#include <memory>
#include "boost/multi_index_container.hpp"
#include "boost/multi_index/ordered_index.hpp"
#include <boost/thread/shared_mutex.hpp>

class CBlockIndex;
class CChainParams;
class CReserveKey;
class CScript;
class CWallet;

namespace Consensus { struct Params; };

static const bool DEFAULT_PRINTPRIORITY = false;

struct CBlockTemplate
{
    CBlock block;
    std::vector<CAmount> vTxFees;
    std::vector<int64_t> vTxSigOpsCost;
    std::vector<unsigned char> vchCoinbaseCommitment;
};

// Container for tracking updates to ancestor feerate as we include (parent)
// transactions in a block
struct CTxMemPoolModifiedEntry {
    CTxMemPoolModifiedEntry(CTxMemPool::txiter entry)
    {
        iter = entry;
        nSizeWithAncestors = entry->GetSizeWithAncestors();
        nModFeesWithAncestors = entry->GetModFeesWithAncestors();
        nSigOpCostWithAncestors = entry->GetSigOpCostWithAncestors();
    }

    CTxMemPool::txiter iter;
    uint64_t nSizeWithAncestors;
    CAmount nModFeesWithAncestors;
    int64_t nSigOpCostWithAncestors;
};

/** Comparator for CTxMemPool::txiter objects.
 *  It simply compares the internal memory address of the CTxMemPoolEntry object
 *  pointed to. This means it has no meaning, and is only useful for using them
 *  as key in other indexes.
 */
struct CompareCTxMemPoolIter {
    bool operator()(const CTxMemPool::txiter& a, const CTxMemPool::txiter& b) const
    {
        return &(*a) < &(*b);
    }
};

struct modifiedentry_iter {
    typedef CTxMemPool::txiter result_type;
    result_type operator() (const CTxMemPoolModifiedEntry &entry) const
    {
        return entry.iter;
    }
};

// This matches the calculation in CompareTxMemPoolEntryByAncestorFee,
// except operating on CTxMemPoolModifiedEntry.
// TODO: refactor to avoid duplication of this logic.
struct CompareModifiedEntry {
    bool operator()(const CTxMemPoolModifiedEntry &a, const CTxMemPoolModifiedEntry &b)
    {
        double f1 = (double)a.nModFeesWithAncestors * b.nSizeWithAncestors;
        double f2 = (double)b.nModFeesWithAncestors * a.nSizeWithAncestors;
        if (f1 == f2) {
            return CTxMemPool::CompareIteratorByHash()(a.iter, b.iter);
        }
        return f1 > f2;
    }
};

// A comparator that sorts transactions based on number of ancestors.
// This is sufficient to sort an ancestor package in an order that is valid
// to appear in a block.
struct CompareTxIterByAncestorCount {
    bool operator()(const CTxMemPool::txiter &a, const CTxMemPool::txiter &b)
    {
        if (a->GetCountWithAncestors() != b->GetCountWithAncestors())
            return a->GetCountWithAncestors() < b->GetCountWithAncestors();
        return CTxMemPool::CompareIteratorByHash()(a, b);
    }
};

typedef boost::multi_index_container<
    CTxMemPoolModifiedEntry,
    boost::multi_index::indexed_by<
        boost::multi_index::ordered_unique<
            modifiedentry_iter,
            CompareCTxMemPoolIter
        >,
        // sorted by modified ancestor fee rate
        boost::multi_index::ordered_non_unique<
            // Reuse same tag from CTxMemPool's similar index
            boost::multi_index::tag<ancestor_score>,
            boost::multi_index::identity<CTxMemPoolModifiedEntry>,
            CompareModifiedEntry
        >
    >
> indexed_modified_transaction_set;

typedef indexed_modified_transaction_set::nth_index<0>::type::iterator modtxiter;
typedef indexed_modified_transaction_set::index<ancestor_score>::type::iterator modtxscoreiter;

struct update_for_parent_inclusion
{
    update_for_parent_inclusion(CTxMemPool::txiter it) : iter(it) {}

    void operator() (CTxMemPoolModifiedEntry &e)
    {
        e.nModFeesWithAncestors -= iter->GetFee();
        e.nSizeWithAncestors -= iter->GetTxSize();
        e.nSigOpCostWithAncestors -= iter->GetSigOpCost();
    }

    CTxMemPool::txiter iter;
};

/** Generate a new block, without valid proof-of-work */
class BlockAssembler
{
private:
    // The constructed block template
    std::unique_ptr<CBlockTemplate> pblocktemplate;
    // A convenience pointer that always refers to the CBlock in pblocktemplate
    CBlock* pblock;

    // Configuration parameters for the block size
    bool fIncludeWitness;
    unsigned int nBlockMaxWeight, nBlockMaxSize;
    bool fNeedSizeAccounting;
    CFeeRate blockMinFeeRate;

    // Information on the current status of the block
    uint64_t nBlockWeight;
    uint64_t nBlockSize;
    uint64_t nBlockTx;
    uint64_t nBlockSigOpsCost;
    CAmount nFees;
    CTxMemPool::setEntries inBlock;

    // Chain context for the block
    int nHeight;
    int64_t nLockTimeCutoff;
    const CChainParams& chainparams;

    // Variables used for addPriorityTxs
    int lastFewTxs;
    bool blockFinished;

public:
    BlockAssembler(const CChainParams& chainparams);
    /** Construct a new block template with coinbase to scriptPubKeyIn */
    std::unique_ptr<CBlockTemplate> CreateNewBlock(const CScript& scriptPubKeyIn, bool fMineWitnessTx=true);
    std::unique_ptr<CBlockTemplate> CreateNewBlock(const CScript& scriptPubKeyIn, const CScript& opReturn, time_t, bool fMineWitnessTx = false);

private:
    // utility functions
    /** Clear the block's state and prepare for assembling a new block */
    void resetBlock();
    /** Add a tx to the block */
    void AddToBlock(CTxMemPool::txiter iter);

    // Methods for how to add transactions to a block.
    /** Add transactions based on tx "priority" */
    void addPriorityTxs();
    /** Add transactions based on feerate including unconfirmed ancestors
      * Increments nPackagesSelected / nDescendantsUpdated with corresponding
      * statistics from the package selection (for logging statistics). */
    void addPackageTxs(int &nPackagesSelected, int &nDescendantsUpdated);

    // helper function for addPriorityTxs
    /** Test if tx will still "fit" in the block */
    bool TestForBlock(CTxMemPool::txiter iter);
    /** Test if tx still has unconfirmed parents not yet in block */
    bool isStillDependent(CTxMemPool::txiter iter);

    // helper functions for addPackageTxs()
    /** Remove confirmed (inBlock) entries from given set */
    void onlyUnconfirmed(CTxMemPool::setEntries& testSet);
    /** Test if a new package would "fit" in the block */
    bool TestPackage(uint64_t packageSize, int64_t packageSigOpsCost);
    /** Perform checks on each transaction in a package:
      * locktime, premature-witness, serialized size (if necessary)
      * These checks should always succeed, and they're here
      * only as an extra check in case of suboptimal node configuration */
    bool TestPackageTransactions(const CTxMemPool::setEntries& package);
    /** Return true if given transaction from mapTx has already been evaluated,
      * or if the transaction's cached data in mapTx is incorrect. */
    bool SkipMapTxEntry(CTxMemPool::txiter it, indexed_modified_transaction_set &mapModifiedTx, CTxMemPool::setEntries &failedTx);
    /** Sort the package in an order that is valid to appear in a block */
    void SortForBlock(const CTxMemPool::setEntries& package, CTxMemPool::txiter entry, std::vector<CTxMemPool::txiter>& sortedEntries);
    /** Add descendants of given transactions to mapModifiedTx with ancestor
      * state updated assuming given transactions are inBlock. Returns number
      * of updated descendants. */
    int UpdatePackagesForAdded(const CTxMemPool::setEntries& alreadyAdded, indexed_modified_transaction_set &mapModifiedTx);
};

/** Modify the extranonce in a block */
void IncrementExtraNonce(CBlock* pblock, const CBlockIndex* pindexPrev, unsigned int& nExtraNonce);
int64_t UpdateTime(CBlockHeader* pblock, const Consensus::Params& consensusParams, const CBlockIndex* pindexPrev);

struct Delegate{
    CKeyID keyid;
    uint64_t votes;
    Delegate(){votes = 0;}
    Delegate(const CKeyID& keyid, uint64_t votes) {this->keyid = keyid; this->votes = votes;}
};

struct DelegateInfo{
    std::vector<Delegate> delegates;
};

const int nMaxConfirmBlockCount = 2;
struct IrreversibleBlockInfo{
    int64_t heights[nMaxConfirmBlockCount];
    uint256 hashs[nMaxConfirmBlockCount];
    std::map<int64_t, uint256> mapHeightHash;

    IrreversibleBlockInfo()
    {
        for(auto i = 0; i < nMaxConfirmBlockCount; ++i) {
            heights[i] = -1;
        }
    }
};

class DPoS{
public:
    DPoS() { nDposStartTime = 0;}
    ~DPoS();
    static DPoS& GetInstance();
    void Init();

    bool IsMining(DelegateInfo& cDelegateInfo, const CBitcoinAddress& address, time_t t);

    DelegateInfo GetNextDelegates(int64_t t);
    bool GetBlockDelegates(DelegateInfo& cDelegateInfo, CBlockIndex* pBlockIndex);
    bool GetBlockDelegates(DelegateInfo& cDelegateInfo, const CBlock& block);
    bool CheckBlockDelegate(const CBlock& block);
    bool CheckBlockHeader(const CBlockHeader& block);
    bool CheckBlock(const CBlockIndex& blockindex, bool fIsCheckDelegateInfo);
    bool CheckCoinbase(const CTransaction& tx, time_t t, int64_t height);

    uint64_t GetLoopIndex(uint64_t time);
    uint32_t GetDelegateIndex(uint64_t time);

    static bool DataToDelegate(DelegateInfo& cDelegateInfo, const std::string& data);
    static std::string DelegateToData(const DelegateInfo& cDelegateInfo);

    static bool ScriptToDelegateInfo(DelegateInfo& cDelegateInfo, uint64_t t, const CScript& script, const CTxDestination* paddress, bool fCheck);
    static CScript DelegateInfoToScript(const DelegateInfo& cDelegateInfo, const CKey& delegatekey, uint64_t t);

    static CBitcoinAddress GetBlockForgerAddress(const CBlock& block);
    static bool GetBlockForgerKeyID(CKeyID& keyid, const CBlock& block);
    static bool GetBlockDelegate(DelegateInfo& cDelegateInfo, const CBlock& block);

    int32_t GetStartDPoSHeight() {return nDposStartHeight;}
    CBitcoinAddress GetSuperForgerAddress() {return cSuperForgerAddress;}

    uint64_t GetStartTime() {return nDposStartTime;}
    void SetStartTime(uint64_t t) {nDposStartTime = t;}

    int GetMaxMemory() {return nMaxMemory;}

    IrreversibleBlockInfo GetIrreversibleBlockInfo();
    void SetIrreversibleBlockInfo(const IrreversibleBlockInfo& info);
    bool ReadIrreversibleBlockInfo(IrreversibleBlockInfo& info);
    bool WriteIrreversibleBlockInfo(const IrreversibleBlockInfo& info);
    void ProcessIrreversibleBlock(int64_t height, uint256 hash);
    bool IsValidBlockCheckIrreversibleBlock(int64_t height, uint256 hash);
    void AddIrreversibleBlock(int64_t height, uint256 hash);

    const int nFirstIrreversibleThreshold = 90;
    const int nSecondIrreversibleThreshold = 67;
    const int nMaxIrreversibleCount = 1000;

private:
    bool CheckBlock(const CBlock& block, bool fIsCheckDelegateInfo);
    static int FastCheckBlockHash(const uint256& hash, uint64_t height);
    std::vector<Delegate> SortDelegate(const std::vector<Delegate>& delegates, uint64_t t);

    bool IsOnTheSameChain(const std::pair<int64_t, uint256>& first, const std::pair<int64_t, uint256>& second);

private:
    int nMaxMemory;                    //GB
    int nMaxDelegateNumber;
    int nBlockIntervalTime;            //seconds
    int nDposStartHeight;
    uint64_t nDposStartTime;
    CBitcoinAddress cSuperForgerAddress;
    std::string strIrreversibleBlockFileName;
    IrreversibleBlockInfo cIrreversibleBlockInfo;
    boost::shared_mutex lockIrreversibleBlockInfo;
};

#endif // BITCOIN_MINER_H

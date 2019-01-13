
#include "validation.h"
#include "base58.h"
#include "module.h"

bool CheckStringFormat(const std::string& name, uint32_t nMinLen, uint32_t nMaxLen, bool bContainUnderline)
{
    bool ret = false;
    if(name.length() >= nMinLen && name.length() <= nMaxLen) {
        ret = true;
        for(auto& item : name) {
            if(bContainUnderline && (uint8_t)item == 95) {
                continue;
            }

            if(((item >= 48 && item <= 57)
                || (item >= 65 && item <= 90)
                || (item >= 97 && item <= 122)) == false) {
                ret = false;
                break;
            }
        }
    }

    return ret;
}

bool OpreturnModule::RegisteEvaluator(std::shared_ptr<BaseEvaluator> evaluator)
{
	bool ret = false;
	if(evaluator && mapEvaluator.find(evaluator->GetID()) == mapEvaluator.end()) {
		mapEvaluator[evaluator->GetID()] = evaluator;
		ret = true;
	}

	return ret;
}

bool OpreturnModule::AnalysisTx(uint32_t& id, std::string& data, const CTransaction& tx)
{
	bool ret = false;
	if(tx.vout.empty() == false
		&& tx.vout[0].nValue == 0
		&& tx.vout[0].scriptPubKey.empty() == false
		&& *tx.vout[0].scriptPubKey.begin() == OP_RETURN ) {
		auto& script = tx.vout[0].scriptPubKey;
		auto iter = script.begin() + 1;
		opcodetype opcode;
		std::vector<unsigned char> vchRet;
		if (script.GetOp2(iter, opcode, &vchRet)) {
			if(vchRet.size() > 4) {
				id = (vchRet[0] << 24) + (vchRet[1] << 16) + (vchRet[2] << 8) + vchRet[3];
				data.append(vchRet.begin() + 4, vchRet.end());
				ret = true;
			}
		}
	}

	return ret;
}

bool ExtractAddress(std::string& address, const CTxIn& vin)
{
	bool ret = false;

	uint256 hashBlock;
	CTransactionRef tx;
	if (vin.prevout.hash.IsNull() == false && GetTransaction(vin.prevout.hash, tx, Params().GetConsensus(), hashBlock, true)) {
		CTxDestination dest;
		if(ExtractDestination(tx->vout[vin.prevout.n].scriptPubKey, dest)) {
			address = CBitcoinAddress(dest).ToString();
			ret = true;
		}
	}

	return ret;
}

bool OpreturnModule::Do(const CBlock& block, uint32_t nHeight, std::map<uint256, uint64_t>& mapTxFee)
{
	uint32_t id;
	std::string data;
	std::set<std::shared_ptr<BaseEvaluator>> setEvaluate;
	for(auto& ptx : block.vtx) {
		std::string address;
		ExtractAddress(address, ptx->vin[0]);

		if(address.empty() == false && AnalysisTx(id, data, *ptx)) {
			auto it = mapEvaluator.find(id);
			if(it != mapEvaluator.end()) {
				setEvaluate.insert(it->second);
				auto hash = ptx->GetHash();
				it->second->Do(TxMsg(nHeight, mapTxFee[hash], hash, address), data);
			}
		}
	}

	for(auto& it : setEvaluate) {
		it->Done(nHeight);
	}

    return true;
}

bool OpreturnModule::TxToJson(UniValue& json, const CTransaction& tx)
{
	CScript script;
	CTxDestination dest;
	ExtractDestination(script, dest);

	bool ret = false;
	uint32_t id;
	std::string data;

	std::string address;
	ExtractAddress(address, tx.vin[0]);

	if(AnalysisTx(id, data, tx)) {
		auto it = mapEvaluator.find(id);
		if(it != mapEvaluator.end()) {
			ret = it->second->TxToJson(json, address, data);
		}
	}

	return ret;
}

bool OpreturnModule::Commit(uint256 hash, int64_t height)
{
	for(auto item : mapEvaluator) {
		item.second->Commit(height);
	}
	return true;
}

bool OpreturnModule::Rollback(uint256 hash, int64_t height)
{
	for(auto item : mapEvaluator) {
		item.second->Rollback(height);
	}
	return true;
}

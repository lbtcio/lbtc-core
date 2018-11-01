
#include "module.h"

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
			if(vchRet.size() == 4) {
				id = (vchRet[0] << 24) + (vchRet[1] << 16) + (vchRet[2] << 8) + vchRet[3];
				if (script.GetOp2(iter, opcode, &vchRet)) {
					data.append(vchRet.begin(), vchRet.end());
					ret = true;
				}
			}
		}
	}

	return ret;
}

bool OpreturnModule::Do(const CBlock& block, uint32_t nHeight, std::map<uint256, uint64_t>& mapTxFee)
{
	uint32_t id;
	std::string data;
	for(auto& ptx : block.vtx) {
		if(AnalysisTx(id, data, *ptx)) {
			auto it = mapEvaluator.find(id);
			if(it != mapEvaluator.end()) {
				auto hash = ptx->GetHash();
				it->second->Do(nHeight, hash, ptx->address, data, mapTxFee[hash]);
			}
		}
	}

    return true;
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

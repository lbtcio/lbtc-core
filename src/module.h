
#ifndef _MODULE_H_
#define _MODULE_H_

#include "uint256.h"
#include "primitives/block.h"
#include "primitives/transaction.h"
#include "script/standard.h"

struct TokenMsg {
	uint64_t nBlockHeight;
	uint64_t fee;
	uint256 txHash;
	std::string fromAddress;
	TokenMsg(uint64_t nBlockHeight, uint64_t fee, uint256 txHash, const std::string& fromAddress)
		: nBlockHeight(nBlockHeight), fee(fee), txHash(txHash), fromAddress(fromAddress) {}
	TokenMsg() {}
};

class BaseEvaluator {
public:
	virtual bool Do(const TokenMsg& tokenMsg, const std::string& data) = 0;
	virtual bool Commit(uint64_t height) = 0;
	virtual bool Rollback(uint64_t height) = 0;
	uint32_t GetID() {return nID;}
	void SetID(uint32_t id) {nID = id;}

private:
	uint32_t nID;
};

class OpreturnModule {
public:
	static OpreturnModule& GetInstance() {
		static OpreturnModule module;	
		return module;
	}

	OpreturnModule() {
	
	}

	bool Do(const CBlock& block, uint32_t nHeight, std::map<uint256, uint64_t>& mapTxFee);
	bool Commit(uint256 hash, int64_t height);
	bool Rollback(uint256 hash, int64_t height);
	bool RegisteEvaluator(std::shared_ptr<BaseEvaluator> evaluator);

private:
	bool AnalysisTx(uint32_t& id, std::string& data, const CTransaction& tx);

private:
	std::map<uint32_t, std::shared_ptr<BaseEvaluator>> mapEvaluator;
};

#endif


#ifndef _MODULE_H_
#define _MODULE_H_

#include "uint256.h"
#include "primitives/block.h"
#include "primitives/transaction.h"
#include "script/standard.h"
#include <univalue.h>

bool CheckStringFormat(const std::string& name, uint32_t nMinLen, uint32_t nMaxLen, bool bContainUnderline);

struct TxMsg {
	uint64_t nBlockHeight;
	uint64_t fee;
	uint256 txHash;
	std::string fromAddress;
	TxMsg(uint64_t nBlockHeight, uint64_t fee, uint256 txHash, const std::string& fromAddress)
		: nBlockHeight(nBlockHeight), fee(fee), txHash(txHash), fromAddress(fromAddress) {}
	TxMsg() {}
};

enum EvaluatorID {
	DPOS_EVALUTOR = 0,
	TOKEN_EVALUTOR = 1,
};

class BaseEvaluator {
public:
	virtual bool Do(const TxMsg& tokenMsg, const std::string& data) = 0;
	virtual bool Done(uint64_t height) = 0;
	virtual bool Commit(uint64_t height) = 0;
	virtual bool Rollback(uint64_t height) = 0;
	virtual uint32_t GetID() {return 0;}
	virtual bool TxToJson(UniValue& json, const std::string& address, const std::string& data) {return false;};
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
	bool TxToJson(UniValue& json, const CTransaction& tx);

private:
	bool AnalysisTx(uint32_t& id, std::string& data, const CTransaction& tx);

private:
	std::map<uint32_t, std::shared_ptr<BaseEvaluator>> mapEvaluator;
};

#endif

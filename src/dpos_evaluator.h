
#ifndef _DPOS_EVALUATOR_H_
#define _DPOS_EVALUATOR_H_

#include <stdint.h>
#include <string>

#include "module.h"

enum DposOpid {
	REGISTENAME = 1,
};

struct RegisteNameMsg : public TxMsg {
	std::string name;

	RegisteNameMsg() {}
	RegisteNameMsg(uint64_t nBlockHeight, uint64_t fee, uint256 txHash, const std::string& fromAddress, const std::string& name)
		: TxMsg(nBlockHeight, fee, txHash, fromAddress), name(name) {}
};

class DposEvaluator : public BaseEvaluator {
public:
	bool Do(const TxMsg& msg, const std::string& data);
	bool Done(uint64_t height) {return true;}
	bool Commit(uint64_t nBlockHeight);
	bool Rollback(uint64_t nBlockHeight);
	uint32_t GetID() {return EvaluatorID::DPOS_EVALUTOR;}

private:
	bool RegisteName(const TxMsg& msg);
};

#endif

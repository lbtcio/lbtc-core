
#ifndef _TOKEN_EVALUATOR_H_
#define _TOKEN_EVALUATOR_H_

#include <stdint.h>
#include <string>

#include "module.h"
#include "lbtc.pb.h"

enum TokenOpid {
	CREATETOKEN = 1,
	TRANSFRERTOKEN,
	LOCKTOKEN,
};

struct CreateTokenMsg : public TxMsg {
	std::string tokenAddress;
	std::string name;
	std::string symbol;
	uint64_t totalamount;
	uint8_t digits;

	CreateTokenMsg() {}
	CreateTokenMsg(uint64_t nBlockHeight, uint64_t fee, uint256 txHash, const std::string& fromAddress, const std::string& tokenAddress, const std::string& name, const std::string& symbol, uint64_t totalamount, uint8_t digits)
		: TxMsg(nBlockHeight, fee, txHash, fromAddress), tokenAddress(tokenAddress), name(name), symbol(symbol), totalamount(totalamount), digits(digits) {}
};

struct TransferTokenMsg : public TxMsg {
	std::string dstAddress;
	int32_t nTokenId;
	uint64_t nAmount;

	TransferTokenMsg() {}
	TransferTokenMsg(uint64_t nBlockHeight, uint64_t fee, uint256 txHash, const std::string& fromAddress, const std::string& dstAddress, int64_t nTokenId, uint64_t nAmount)
		: TxMsg(nBlockHeight, fee, txHash, fromAddress), dstAddress(dstAddress), nTokenId(nTokenId), nAmount(nAmount) {}
};

struct LockTokenMsg : public TxMsg {
	std::string dstAddress;
	int32_t nTokenId;
	uint64_t nAmount;
	uint64_t nExpiryHeight;

	LockTokenMsg() {}
	LockTokenMsg(uint64_t nBlockHeight, uint64_t fee, uint256 txHash, const std::string& fromAddress, const std::string& dstAddress, int64_t nTokenId, uint64_t nAmount, uint64_t nExpiryHeight)
		: TxMsg(nBlockHeight, fee, txHash, fromAddress), dstAddress(dstAddress), nTokenId(nTokenId), nAmount(nAmount), nExpiryHeight(nExpiryHeight) {}
};

class TokenEvaluator : public BaseEvaluator {
public:
	bool Do(const TxMsg& tokenMsg, const std::string& data);
	bool Done(uint64_t height);
	bool Commit(uint64_t nBlockHeight);
	bool Rollback(uint64_t nBlockHeight);
	uint32_t GetID() {return EvaluatorID::TOKEN_EVALUTOR;}
	bool TxToJson(UniValue& json, const std::string& address, const std::string& data);

private:
	bool TransferToken(const TxMsg& msg);
	bool LockToken(const TxMsg& msg);
	bool CreateToken(const TxMsg& msg);
};

std::string IsValid(LbtcPbMsg::CreateTokenMsg& pbTokenMsg);
std::string IsValid(LbtcPbMsg::TransferTokenMsg& msg);
std::string IsValid(LbtcPbMsg::LockTokenMsg& msg);

#endif

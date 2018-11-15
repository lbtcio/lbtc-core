
#ifndef _TOKEN_EVALUATOR_H_
#define _TOKEN_EVALUATOR_H_

#include <stdint.h>
#include <string>

#include "module.h"

enum TokenOpid {
	CREATETOKEN = 1,
	TRANSFRERTOKEN,
	LOCKTOEKN,
};

struct CreateTokenMsg : public TokenMsg {
	std::string tokenAddress;
	std::string name;
	std::string symbol;
	uint64_t totalamount;
	uint8_t digits;

	CreateTokenMsg() {}
	CreateTokenMsg(uint64_t nBlockHeight, uint64_t fee, uint256 txHash, const std::string& fromAddress, const std::string& tokenAddress, const std::string& name, const std::string& symbol, uint64_t totalamount, uint8_t digits)
		: TokenMsg(nBlockHeight, fee, txHash, fromAddress), tokenAddress(tokenAddress), name(name), symbol(symbol), totalamount(totalamount), digits(digits) {}
};

struct TransferTokenMsg : public TokenMsg {
	std::string dstAddress;
	int32_t nTokenId;
	uint64_t nAmount;

	TransferTokenMsg() {}
	TransferTokenMsg(uint64_t nBlockHeight, uint64_t fee, uint256 txHash, const std::string& fromAddress, const std::string& dstAddress, int64_t nTokenId, uint64_t nAmount)
		: TokenMsg(nBlockHeight, fee, txHash, fromAddress), dstAddress(dstAddress), nTokenId(nTokenId), nAmount(nAmount) {}
};

struct LockTokenMsg : public TokenMsg {
	std::string dstAddress;
	int32_t nTokenId;
	uint64_t nAmount;
	uint64_t nExpiryHeight;

	LockTokenMsg() {}
	LockTokenMsg(uint64_t nBlockHeight, uint64_t fee, uint256 txHash, const std::string& fromAddress, const std::string& dstAddress, int64_t nTokenId, uint64_t nAmount, uint64_t nExpiryHeight)
		: TokenMsg(nBlockHeight, fee, txHash, fromAddress), dstAddress(dstAddress), nTokenId(nTokenId), nAmount(nAmount), nExpiryHeight(nExpiryHeight) {}
};

class TokenEvaluator : public BaseEvaluator {
public:
	bool Do(const TokenMsg& tokenMsg, const std::string& data);
	bool Commit(uint64_t nBlockHeight);
	bool Rollback(uint64_t nBlockHeight);

private:
	bool TransferToken(const TokenMsg& msg);
	bool LockToken(const TokenMsg& msg);
	bool CreateToken(const TokenMsg& msg);
};

#endif

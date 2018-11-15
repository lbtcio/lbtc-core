
#include "token.pb.h"
#include "token_db.h"
#include "token_evaluator.h"

bool TokenEvaluator::Do(const TokenMsg& msg, const std::string& data)
{
	bool ret = false;
	PbTokenMsg::TokenMsg baseTokenMsg;
	if (baseTokenMsg.ParseFromString(data)) {
		switch(baseTokenMsg.opid()) {
		case TokenOpid::CREATETOKEN:
			{
			PbTokenMsg::CreateTokenMsg msgCreateToken;
			if (msgCreateToken.ParseFromString(data)) {
				ret = CreateToken(CreateTokenMsg(msg.nBlockHeight, msg.fee, msg.txHash, msg.fromAddress, msgCreateToken.tokenaddress(), msgCreateToken.name(), msgCreateToken.symbol(), msgCreateToken.totalamount(), msgCreateToken.digits()));
			}
			}
		break;

		case TokenOpid::TRANSFRERTOKEN:
			{
			PbTokenMsg::TransferTokenMsg msgTransferToken;
			if (msgTransferToken.ParseFromString(data)) {
				ret = TransferToken(TransferTokenMsg(msg.nBlockHeight, msg.fee, msg.txHash, msg.fromAddress, msgTransferToken.dstaddress(), msgTransferToken.tokenid(), msgTransferToken.amount()));
			}
			}
		break;

		case TokenOpid::LOCKTOEKN:
			{
			PbTokenMsg::LockTokenMsg msgLockToken;
			if (msgLockToken.ParseFromString(data)) {
				ret = LockToken(LockTokenMsg(msg.nBlockHeight, msg.fee, msg.txHash, msg.fromAddress, msgLockToken.dstaddress(), msgLockToken.tokenid(), msgLockToken.amount(), msgLockToken.expiryheight()));
			}
			}
		break;
		}
	}

	return ret;
}

bool TokenEvaluator::Commit(uint64_t nBlockHeight)
{
	auto& db = *TokenDB::GetInstance();
	db.Commit(nBlockHeight);
	return true;
}

bool TokenEvaluator::Rollback(uint64_t nBlockHeight)
{
	auto& db = *TokenDB::GetInstance();
	db.Rollback(nBlockHeight);
	return true;
}

bool TokenEvaluator::TransferToken(const TokenMsg& tokenMsg)
{
	TransferTokenMsg& msg = (TransferTokenMsg&)tokenMsg;
	auto& db = *TokenDB::GetInstance();

	int32_t fromAddressId = -1;
	int32_t dstAddressId = -1;
	uint64_t fromBalance = -1;
	uint64_t dstBalance = -1;
	if((fromAddressId = db.GetAddressId(msg.fromAddress)) > 0
		&& (fromBalance = db.GetBalance(msg.nTokenId, fromAddressId)) > 0
		&& fromBalance > msg.nAmount ) {
		dstAddressId = db.GetOrSetAddressId(msg.nBlockHeight, msg.dstAddress);
		dstBalance = db.GetBalance(msg.nTokenId, dstAddressId);

		db.SetBalance(msg.nBlockHeight, msg.nTokenId, fromAddressId, fromBalance - msg.nAmount);
		db.SetBalance(msg.nBlockHeight, msg.nTokenId, dstAddressId, dstBalance + msg.nAmount);
	}
	return true;
}

bool TokenEvaluator::LockToken(const TokenMsg& tokenMsg)
{
	LockTokenMsg& msg = (LockTokenMsg&)tokenMsg;
	auto& db = *TokenDB::GetInstance();

	int32_t fromAddressId = -1;
	int32_t dstAddressId = -1;
	uint64_t fromBalance = -1;
	if((fromAddressId = db.GetAddressId(msg.fromAddress)) > 0
		&& (fromBalance = db.GetBalance(msg.nTokenId, fromAddressId)) > 0
		&& fromBalance > msg.nAmount ) {
		dstAddressId = db.GetOrSetAddressId(msg.nBlockHeight, msg.dstAddress);

		auto pmapLockBalance = db.GetLockBalance(msg.nTokenId, dstAddressId);
		std::map<uint64_t, uint64_t> mapLockBalance;
		if(pmapLockBalance == NULL) {
			pmapLockBalance = &mapLockBalance;
		}
		(*pmapLockBalance)[msg.nExpiryHeight] += msg.nAmount;

		db.SetBalance(msg.nBlockHeight, msg.nTokenId, fromAddressId, fromBalance - msg.nAmount);
		db.SetLockBalance(msg.nBlockHeight, msg.nTokenId, dstAddressId, pmapLockBalance);
	}
	return true;
}

bool TokenEvaluator::CreateToken(const TokenMsg& tokenMsg)
{
	CreateTokenMsg& msg = (CreateTokenMsg&)tokenMsg;
	auto& db = *TokenDB::GetInstance();

	bool ret = false;
	int64_t nTokenId = -1;
	if((nTokenId = db.SetToken(msg.nBlockHeight, TokenInfo(msg.fromAddress, msg.tokenAddress, msg.name, msg.symbol, msg.totalamount, msg.digits))) > 0) {
		int64_t nAddressId = db.GetOrSetAddressId(msg.nBlockHeight, msg.fromAddress);
		db.SetBalance(msg.nBlockHeight, nTokenId, nAddressId, msg.totalamount);
		ret = true;
	}

	return ret;
}

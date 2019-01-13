
#include "token_db.h"
#include "dpos_db.h"
#include "token_evaluator.h"
#include "base58.h"

bool TokenEvaluator::Do(const TxMsg& msg, const std::string& data)
{
	bool ret = false;
	LbtcPbMsg::Msg baseTokenMsg;
	if (baseTokenMsg.ParseFromString(data)) {
		switch(baseTokenMsg.opid()) {
		case TokenOpid::CREATETOKEN:
			{
			LbtcPbMsg::CreateTokenMsg msgCreateToken;
			if (msgCreateToken.ParseFromString(data)) {
				ret = CreateToken(CreateTokenMsg(msg.nBlockHeight, msg.fee, msg.txHash, msg.fromAddress, msgCreateToken.tokenaddress(), msgCreateToken.name(), msgCreateToken.symbol(), msgCreateToken.totalamount(), msgCreateToken.digits()));
			}
			}
		break;

		case TokenOpid::TRANSFRERTOKEN:
			{
			LbtcPbMsg::TransferTokenMsg msgTransferToken;
			if (msgTransferToken.ParseFromString(data)) {
				ret = TransferToken(TransferTokenMsg(msg.nBlockHeight, msg.fee, msg.txHash, msg.fromAddress, msgTransferToken.dstaddress(), msgTransferToken.tokenid(), msgTransferToken.amount()));
			}
			}
		break;

		case TokenOpid::LOCKTOKEN:
			{
			LbtcPbMsg::LockTokenMsg msgLockToken;
			if (msgLockToken.ParseFromString(data)) {
				ret = LockToken(LockTokenMsg(msg.nBlockHeight, msg.fee, msg.txHash, msg.fromAddress, msgLockToken.dstaddress(), msgLockToken.tokenid(), msgLockToken.amount(), msgLockToken.expiryheight()));
			}
			}
		break;
		}
	}

	return ret;
}

bool TokenEvaluator::Done(uint64_t height)
{
	auto& db = *TokenDB::GetInstance();
	db.WriteLeveldb(height);
	return true;
}

std::string NumerToFloatString(uint64_t number, int digits)
{
	std::string result = std::to_string(number);
	if(digits > 0) {
		if((int)result.length() > digits) {
			result.insert(result.length() - digits, ".");
		} else if((int)result.length() == digits) {
			result.insert(0, "0.");
		} else {
			result = "0." + std::string(digits - result.length(), '0') + result;
		}
	}

	return result;
}

bool TokenEvaluator::TxToJson(UniValue& json, const std::string& address, const std::string& data)
{
	bool ret = false;
	LbtcPbMsg::Msg baseTokenMsg;
	UniValue entry(UniValue::VOBJ);
	auto& db = *TokenDB::GetInstance();

	if (baseTokenMsg.ParseFromString(data)) {
		switch(baseTokenMsg.opid()) {
		case TokenOpid::CREATETOKEN:
			{
			LbtcPbMsg::CreateTokenMsg msg;
			if (msg.ParseFromString(data)) {
				ret = true;
				entry.push_back(Pair("op",  "CreateToken"));
				entry.push_back(Pair("address",  msg.tokenaddress()));
				entry.push_back(Pair("name",  msg.name()));
				entry.push_back(Pair("symbol",  msg.symbol()));
				entry.push_back(Pair("totalamount",  NumerToFloatString(msg.totalamount(), msg.digits())));
				entry.push_back(Pair("digits",  (int32_t)msg.digits()));
				entry.push_back(Pair("creator",  address));
			}
			}
		break;

		case TokenOpid::TRANSFRERTOKEN:
			{
			LbtcPbMsg::TransferTokenMsg msg;
			if (msg.ParseFromString(data)) {
				TokenInfo* token = db.GetToken(msg.tokenid());
				if(token) {
					ret = true;
					entry.push_back(Pair("op",  "TransferToken"));
					entry.push_back(Pair("symbol",  token->symbol));
					entry.push_back(Pair("id",  (int64_t)msg.tokenid()));
					entry.push_back(Pair("fromaddress",  address));
					entry.push_back(Pair("toaddress",  msg.dstaddress()));
					entry.push_back(Pair("amount",  NumerToFloatString(msg.amount(), token->digits)));
				}
			}
			}
		break;

		case TokenOpid::LOCKTOKEN:
			{
			LbtcPbMsg::LockTokenMsg msg;
			if (msg.ParseFromString(data)) {
				TokenInfo* token = db.GetToken(msg.tokenid());
				if(token) {
					ret = true;
					entry.push_back(Pair("op",  "LockToken"));
					entry.push_back(Pair("symbol",  token->symbol));
					entry.push_back(Pair("id",  (int64_t)msg.tokenid()));
					entry.push_back(Pair("fromaddress",  address));
					entry.push_back(Pair("toaddress",  msg.dstaddress()));
					entry.push_back(Pair("amount",  NumerToFloatString(msg.amount(), token->digits)));
					entry.push_back(Pair("expiryheight",  msg.expiryheight()));
				}
			}
			}
		break;
		}
	}

	if(ret) {
		json.push_back(Pair("Token", entry));
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

bool TokenEvaluator::TransferToken(const TxMsg& tokenMsg)
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

		db.SetTokenHistory(msg.nBlockHeight, msg.txHash.GetHex(), TokenHistory(msg.nTokenId, msg.fromAddress, msg.dstAddress, msg.txHash.GetHex()));
	}

	return true;
}

bool TokenEvaluator::LockToken(const TxMsg& tokenMsg)
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

		db.SetTokenHistory(msg.nBlockHeight, msg.txHash.GetHex(), TokenHistory(msg.nTokenId, msg.fromAddress, msg.dstAddress, msg.txHash.GetHex()));
	}
	return true;
}

bool TokenEvaluator::CreateToken(const TxMsg& tokenMsg)
{
	CreateTokenMsg& msg = (CreateTokenMsg&)tokenMsg;
	auto& db = *TokenDB::GetInstance();

	bool ret = false;
	int64_t nTokenId = -1;
	TokenInfo tokeninfo(msg.fromAddress, msg.tokenAddress, msg.name, msg.symbol, msg.totalamount, msg.digits);
	if(DposDB::GetInstance()->GetAddressName(msg.fromAddress).empty()) {
		return false;
	}

	if(( nTokenId = db.SetToken(msg.nBlockHeight, tokeninfo)) > 0) {
		int64_t nFromAddressId = db.GetOrSetAddressId(msg.nBlockHeight, msg.fromAddress);
		int64_t nTokenAddressId = db.GetOrSetAddressId(msg.nBlockHeight, msg.tokenAddress);
		db.SetBalance(msg.nBlockHeight, nTokenId, nTokenAddressId, msg.totalamount);

		db.SetTokenHistory(msg.nBlockHeight, msg.txHash.GetHex(), TokenHistory(nTokenId, msg.fromAddress, msg.tokenAddress, msg.txHash.GetHex()));

		ret = true;
	}

	return ret;
}

static uint32_t coins[] = {1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000};
std::string IsValid(LbtcPbMsg::CreateTokenMsg& msg)
{
    if(!CBitcoinAddress(msg.tokenaddress()).IsValid())
        return "Invalid token address";
	if(CheckStringFormat(msg.symbol(), 2, 8, false) == false)
        return "Invalid format token symbol";
	if(CheckStringFormat(msg.name(), 2, 32, true) == false)
        return "Invalid format token name";
	if(msg.digits() > 8)
        return "Invalid token digits";
	if(msg.totalamount() <= 0 || msg.totalamount() > (100000000000 * (uint64_t)coins[msg.digits()]))
        return "Invalid token totalamount";
	return std::string();
}

std::string IsValid(LbtcPbMsg::TransferTokenMsg& msg)
{
    if(!CBitcoinAddress(msg.dstaddress()).IsValid())
        return "Invalid token address";
	if(msg.comment().length() > 128)
		return "Commnet length  is greater than 128";
	return std::string();
}

std::string IsValid(LbtcPbMsg::LockTokenMsg& msg)
{
    if(!CBitcoinAddress(msg.dstaddress()).IsValid())
        return "Invalid token address";
	if(msg.comment().length() > 128)
		return "Commnet length  is greater than 128";
	return std::string();
}

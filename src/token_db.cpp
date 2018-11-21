
#include "token_db.h"
#include "myserialize.h"

TokenDB::TokenDB()
	: dbToken(true), dbIdAddress(true)
{
}

TokenDB::~TokenDB()
{
	Save();
}

bool TokenDB::Load()
{
	MyUnserialize(strDataPath +"/TokenInfo.dat", dbToken);
	MyUnserialize(strDataPath +"/AddressID.dat", dbAddressId, dbIdAddress);
	MyUnserialize(strDataPath +"/TokenHolder.dat", mapdbBalance);
	MyUnserialize(strDataPath +"/LockTokenHolder.dat", mapdbLockBalance);
}

bool TokenDB::Save()
{
	if(strDataPath.empty() == false) {
		MySerialize(strDataPath +"/TokenInfo.dat", dbToken);
		MySerialize(strDataPath +"/AddressID.dat", dbAddressId, dbIdAddress);
		MySerialize(strDataPath +"/TokenHolder.dat", mapdbBalance);
		MySerialize(strDataPath +"/LockTokenHolder.dat", mapdbLockBalance);
	}
}

bool TokenDB::Commit(uint64_t nBlockHeight)
{
	UnlockBalance(nBlockHeight);

	dbToken.Commit(std::to_string(nBlockHeight));
	dbAddressId.Commit(std::to_string(nBlockHeight));
	dbIdAddress.Commit(std::to_string(nBlockHeight));
	for(auto& item : mapdbBalance)
		item.second.Commit(std::to_string(nBlockHeight));
	for(auto& item : mapdbLockBalance)
		item.second.Commit(std::to_string(nBlockHeight));

	return true;
}

bool TokenDB::Rollback(uint64_t nBlockHeight)
{
	dbToken.Rollback(std::to_string(nBlockHeight));
	dbAddressId.Rollback(std::to_string(nBlockHeight));
	dbIdAddress.Rollback(std::to_string(nBlockHeight));
	for(auto& item : mapdbBalance)
		item.second.Rollback(std::to_string(nBlockHeight));
	for(auto& item : mapdbLockBalance)
		item.second.Rollback(std::to_string(nBlockHeight));

	return true;
}

int64_t TokenDB::SetToken(uint64_t nBlockHeight, const TokenInfo& cTokenInfo)
{
	int64_t ret = -1;

	bool bIsInvalid = false;
    auto f = [&bIsInvalid, &cTokenInfo](const int64_t& key, const TokenInfo& value) -> void {
		if(bIsInvalid)
			return;

		if(cTokenInfo.tokenAddress == value.tokenAddress) {
			bIsInvalid = true;
		}
    };
	dbToken.Iterate(f);

	if(bIsInvalid == false) {
		ret = dbToken.Set(std::to_string(nBlockHeight), cTokenInfo);
	}

	return ret;
}

TokenInfo* TokenDB::GetToken(int64_t nTokenId)
{
	return dbToken.Get(nTokenId);
}

std::map<int64_t, TokenInfo> TokenDB::GetTokens()
{
	std::map<int64_t, TokenInfo> tokens;
    auto f = [&tokens](const int64_t& key, const TokenInfo& value) -> void {
		tokens[key] = value;
    };

	dbToken.Iterate(f);
	return tokens;
}

int64_t TokenDB::SetAddressId(uint64_t nBlockHeight, const std::string& strAddress)
{
	int64_t ret = dbIdAddress.Set(std::to_string(nBlockHeight), strAddress);
	if(ret > 0) {
		dbIdAddress.Set(std::to_string(nBlockHeight), ret, strAddress);
		dbAddressId.Set(std::to_string(nBlockHeight), strAddress, ret);
	}
	return ret;
}

int64_t TokenDB::GetOrSetAddressId(uint64_t nBlockHeight, const std::string& strAddress)
{
	int64_t ret = -1;
	if((ret = GetAddressId(strAddress)) < 0) {
		ret = SetAddressId(nBlockHeight, strAddress);
	}
	return ret;
}

int64_t TokenDB::GetAddressId(const std::string& strAddress)
{
	int64_t ret = -1;
	dbAddressId.Get(&ret, strAddress);
	return ret;
}

uint64_t TokenDB::SetBalance(uint64_t nBlockHeight, int64_t nTokenId, int64_t nAddressId, uint64_t nBalance)
{
	mapdbBalance[nTokenId].Set(std::to_string(nBlockHeight), nAddressId, nBalance);
	return nBalance;
}

uint64_t TokenDB::GetBalance(int64_t nTokenId, int64_t nAddressId)
{
	uint64_t ret = 0;
	auto itdb = mapdbBalance.find(nTokenId);
	if(itdb != mapdbBalance.end()) {
		 itdb->second.Get(&ret, nAddressId);
	}
	return ret;
}

//reuslt: tokenid - > amount
std::map<int64_t, uint64_t> TokenDB::GetBalances(int64_t nAddressId)
{
	std::map<int64_t, uint64_t> balances;

	std::map<int64_t, dbKV<int64_t, uint64_t>> mapdbBalance;
	uint64_t nAmount = 0;
	for(auto& item : mapdbBalance) {
		if((nAmount = GetBalance(item.first, nAddressId)) > 0) {
			balances[item.first] = nAmount;
		}
	}

	return balances;
}

std::map<uint64_t, uint64_t>* TokenDB::SetLockBalance(uint64_t nBlockHeight, int64_t nTokenId, int64_t nAddressId, std::map<uint64_t, uint64_t>* pLockBalance)
{
	std::map<uint64_t, uint64_t>* ret = NULL;
	auto& db = mapdbLockBalance[nTokenId];

	ret = db.Get(nAddressId);
	if(ret != pLockBalance) {
		db.Set(std::to_string(nBlockHeight), nAddressId, *pLockBalance);
	}

	return ret;
}

std::map<uint64_t, uint64_t>* TokenDB::GetLockBalance(int64_t nTokenId, int64_t nAddressId)
{
	std::map<uint64_t, uint64_t>* ret = NULL;
	auto itdb = mapdbLockBalance.find(nTokenId);
	if(itdb != mapdbLockBalance.end()) {
		ret = itdb->second.Get(nAddressId);
	}

	return ret;
}

//reuslt: tokenid - > [ expirtyheight -> amount]
std::map<uint64_t, std::map<uint64_t, uint64_t>> TokenDB::GetLockBalances(int64_t nAddressId)
{
	std::map<uint64_t, std::map<uint64_t, uint64_t>> lockbalances;
	std::map<uint64_t, uint64_t>* lockbalance = NULL;
	for(auto& item : mapdbLockBalance) {
		if((lockbalance = GetLockBalance(item.first, nAddressId))) {
			lockbalances[item.first] = *lockbalance;
		}
	}

	return lockbalances;
}

uint64_t TokenDB::UnlockBalance(uint64_t nBlockHeight, int64_t nTokenId, int64_t nAddressId)
{
	uint64_t ret = 0;

	std::map<uint64_t, uint64_t>* lockBalance = GetLockBalance(nTokenId, nAddressId);
	if(lockBalance) {
		for(auto it = lockBalance->begin(); it != lockBalance->end();) {
			if(it->first <= nBlockHeight) {
				ret += it->second;
				it = lockBalance->erase(it);
			} else {
				break;
			}
		}
	}

	if(ret > 0) {
		SetLockBalance(nBlockHeight, nTokenId, nAddressId, lockBalance);
	}

	return ret;
}

void TokenDB::UnlockBalance(uint64_t nBlockHeight)
{
	int64_t nTokenId = 0;
	std::vector<std::pair<int64_t, int64_t>> vctTokenidAddressid;
    auto f = [&vctTokenidAddressid, &nTokenId, nBlockHeight](const int64_t& key, const std::map<uint64_t, uint64_t>& value) -> void {
		for(auto& item : value) {
			if(item.first <= nBlockHeight) {
				vctTokenidAddressid.push_back(std::make_pair(nTokenId, key));
				break;
			}
		}
    };

	for(auto& item : mapdbLockBalance) {
		nTokenId = item.first;
		item.second.Iterate(f);
	}

	uint64_t nUnlockAmount = 0;
	uint64_t nAmount = 0;
	for(auto& item : vctTokenidAddressid) {
		nUnlockAmount = UnlockBalance(nBlockHeight, item.first, item.second);
		nAmount = GetBalance(item.first, item.second);
		SetBalance(nBlockHeight, item.first, item.second, nAmount + nUnlockAmount);
	}

	return;
}


#ifndef _TOKEN_DB_H_
#define _TOKEN_DB_H_

#include <map>
#include <string>
#include <stdint.h>

#include "db_kv.h"

struct TokenInfo {
	uint64_t id;
	std::string fromAddress;
	std::string tokenAddress;
	std::string name;
	std::string symbol;
	uint64_t totalamount;
	uint8_t digits;

	TokenInfo() {}
	TokenInfo(const std::string& fromAddress, const std::string& tokenAddress, const std::string& name, const std::string& symbol, uint64_t totalamount, uint8_t digits)
		: id(0), fromAddress(fromAddress), tokenAddress(tokenAddress), name(name), symbol(symbol), totalamount(totalamount), digits(digits) {}

    template<class Archive>
    void serialize(Archive& ar, const unsigned int version)
    {
		ar & id;
		ar & fromAddress;
		ar & tokenAddress;
		ar & name;
		ar & symbol;
		ar & totalamount;
		ar & digits;
    }
};

struct TokenHistory {
	int64_t tokenid;
	std::string fromaddress;
	std::string dstaddress;
	std::string txhash;

	TokenHistory() {}
	TokenHistory(int64_t tokenid, const std::string& fromaddress, const std::string& dstaddress, const std::string& txhash)
			: tokenid(tokenid), fromaddress(fromaddress), dstaddress(dstaddress), txhash(txhash) {}

    template<class Archive>
    void serialize(Archive& ar, const unsigned int version)
    {
		ar & tokenid;
		ar & fromaddress;
		ar & dstaddress;
		ar & txhash;
    }
};

class TokenDB {
public:
	static TokenDB* GetInstance() {
		static TokenDB db;
		return &db;
	}

	TokenDB();
	~TokenDB();
	void Init(const std::string& strPath) {
		strDataPath = strPath;
		Load();
	}

	bool Load();
	bool Save();

	bool Commit(uint64_t nBlockHeight);
	bool Rollback(uint64_t nBlockHeight);

	int64_t SetToken(uint64_t nBlockHeight, const TokenInfo& cTokenInfo);
	TokenInfo* GetToken(int64_t nTokenId);
	TokenInfo* GetToken(const std::string& strTokenAddress);
	std::map<int64_t, TokenInfo> GetTokens();

	int64_t SetAddressId(uint64_t nBlockHeight, const std::string& strAddress);
	int64_t GetOrSetAddressId(uint64_t nBlockHeight, const std::string& strAddress);
	int64_t GetAddressId(const std::string& strAddress);

	uint64_t GetBalance(int64_t nTokenId, int64_t nAddressId);
	uint64_t SetBalance(uint64_t nBlockHeight, int64_t nTokenId, int64_t nAddressId, uint64_t nBalance);
	std::map<int64_t, uint64_t> GetBalances(int64_t nAddressId);

	std::map<uint64_t, uint64_t>* SetLockBalance(uint64_t nBlockHeight, int64_t nTokenId, int64_t nAddressId, std::map<uint64_t, uint64_t>* pLockBalance);
	std::map<uint64_t, uint64_t>* GetLockBalance(int64_t nTokenId, int64_t nAddressId);
	std::map<uint64_t, std::map<uint64_t, uint64_t>> GetLockBalances(int64_t nAddressId);

	void UnlockBalance(uint64_t nBlockHeight);

	bool SetTokenHistory(uint64_t height, const std::string& hash, const TokenHistory& history);
	bool WriteLeveldb(uint64_t height);

private:
	uint64_t UnlockBalance(uint64_t nBlockHeight, int64_t nTokenId, int64_t nAddressId);
	bool EraseLeveldb(uint64_t height);

private:
	dbKV<int64_t, TokenInfo> dbToken;
	dbKV<int64_t, std::string> dbIdAddress;
	dbKV<std::string, int64_t> dbAddressId;
	std::map<int64_t, dbKV<int64_t, uint64_t>> mapdbBalance;
	std::map<int64_t, dbKV<int64_t, std::map<uint64_t, uint64_t>>> mapdbLockBalance;

	std::map<uint64_t, std::map<std::string, TokenHistory>> mapHeightTokenHistory;
	std::string strDataPath;
};

#endif

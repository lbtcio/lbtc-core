
#ifndef _DPOS_DB_H_
#define _DPOS_DB_H_

#include <map>
#include <string>
#include <stdint.h>

#include "db_kv.h"

class DposDB {
public:
	static DposDB* GetInstance() {
		static DposDB db;
		return &db;
	}

	DposDB();
	~DposDB();
	void Init(const std::string& strPath) {
		strDataPath = strPath;
		Load();
	}

	bool Load();
	bool Save();

	bool Commit(uint64_t nBlockHeight);
	bool Rollback(uint64_t nBlockHeight);

	bool SetAddressName(uint64_t nBlockHeight, const std::string& strAddress, const std::string& strName);
	std::string GetAddressName(const std::string& strAddress);
	std::string GetNameAddress(const std::string& strName);

private:
	dbKV<std::string, std::string> dbAddressName;
	dbKV<std::string, std::string> dbNameAddress;
	std::string strDataPath;
};

#endif


#include "dpos_db.h"


DposDB::DposDB()
{
}

DposDB::~DposDB()
{
	Save();
}

bool DposDB::Load()
{
	MyUnserialize(strDataPath +"/AddressName.dat", dbAddressName, dbNameAddress);
	return true;
}

bool DposDB::Save()
{
	if(strDataPath.empty() == false) {
		MySerialize(strDataPath +"/AddressName.dat", dbAddressName, dbNameAddress);
	}
	return true;
}

bool DposDB::Commit(uint64_t nBlockHeight)
{
	dbAddressName.Commit(std::to_string(nBlockHeight));
	dbNameAddress.Commit(std::to_string(nBlockHeight));
	return true;
}

bool DposDB::Rollback(uint64_t nBlockHeight)
{
	dbAddressName.Rollback(std::to_string(nBlockHeight));
	dbNameAddress.Rollback(std::to_string(nBlockHeight));
	return true;
}

bool DposDB::SetAddressName(uint64_t nBlockHeight, const std::string& strAddress, const std::string& strName)
{
	if(GetAddressName(strAddress).empty() == false || GetNameAddress(strName).empty() == false) {
		return false;
	}

	dbAddressName.Set(std::to_string(nBlockHeight), strAddress, strName);
	dbNameAddress.Set(std::to_string(nBlockHeight), strName, strAddress);
	return true;
}

std::string DposDB::GetAddressName(const std::string& strAddress)
{
	std::string ret;
	auto result = dbAddressName.Get(strAddress);
	if(result) {
		ret = *result;
	}
	return ret;
}

std::string DposDB::GetNameAddress(const std::string& strName)
{
	std::string ret;
	auto result = dbNameAddress.Get(strName);
	if(result) {
		ret = *result;
	}
	return ret;
}

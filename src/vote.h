    
#ifndef _LBTC_VOTE_H_
#define _LBTC_VOTE_H_

#include "miner.h"
#include "pubkey.h"
#include <unordered_map>
#include <map>
#include <set>
#include <boost/thread/shared_mutex.hpp>
#include <boost/filesystem.hpp>

#include "base58.h"
#include "script/script.h"
#include "votedb.h"

struct COpData{
    uint8_t opcode;
};
struct CMessageData : public COpData {
    std::string message;

    template<class Archive>
    void serialize(Archive& ar, const unsigned int version)
    {
        ar & message;
    }
};

struct CRegisterForgerData : public COpData {
    std::string name;

    template<class Archive>
    void serialize(Archive& ar, const unsigned int version)
    {
        ar & name;
    }
};

struct CVoteForgerData : public COpData {
	std::set<CKeyID> forgers;

    template<class Archive>
    void serialize(Archive& ar, const unsigned int version)
    {
        ar & forgers;
    }
};

struct CCancelVoteForgerData : public COpData {
	std::set<CKeyID> forgers;

    template<class Archive>
    void serialize(Archive& ar, const unsigned int version)
    {
        ar & forgers;
    }
};

struct CRegisterCommitteeData : public COpData {
    std::string name;
    std::string url;

    template<class Archive>
    void serialize(Archive& ar, const unsigned int version)
    {
        ar & name;
        ar & url;
    }
};

struct CVoteCommitteeData : public COpData {
    CKeyID committee;

    template<class Archive>
    void serialize(Archive& ar, const unsigned int version)
    {
        ar & committee;
    }
};

struct CCancelVoteCommitteeData : public COpData {
    CKeyID committee;

    template<class Archive>
    void serialize(Archive& ar, const unsigned int version)
    {
        ar & committee;
    }
};

struct CSubmitBillData : public COpData {
    CKeyID committee;
    std::string title;
    std::string detail;
    std::string url;
    std::vector<std::string> options;
    uint64_t starttime;
    uint64_t endtime;

    template<class Archive>
    void serialize(Archive& ar, const unsigned int version)
    {
        ar & committee;
        ar & title;
        ar & detail;
        ar & url;
        ar & starttime;
        ar & endtime;
        ar & options;
    }
};

struct CVoteBillData : public COpData {
    uint160 id;
    uint8_t index;

    template<class Archive>
    void serialize(Archive& ar, const unsigned int version)
    {
        ar & id;
        ar & index;
    }
};

std::vector<unsigned char> StructToData(const CRegisterForgerData& data);
std::vector<unsigned char> StructToData(const CVoteForgerData& data);
std::vector<unsigned char> StructToData(const CCancelVoteForgerData& data);
std::vector<unsigned char> StructToData(const CRegisterCommitteeData& data);
std::vector<unsigned char> StructToData(const CVoteCommitteeData& data);
std::vector<unsigned char> StructToData(const CCancelVoteCommitteeData& data);
std::vector<unsigned char> StructToData(const CSubmitBillData& data);
std::vector<unsigned char> StructToData(const CVoteBillData& data);
std::vector<unsigned char> StructToData(const CMessageData& data);
std::string CheckStruct(const CRegisterForgerData& data);
std::string CheckStruct(const CVoteForgerData& data);
std::string CheckStruct(const CCancelVoteForgerData& data);
std::string CheckStruct(const CRegisterCommitteeData& data);
std::string CheckStruct(const CVoteCommitteeData& data);
std::string CheckStruct(const CCancelVoteCommitteeData& data);
std::string CheckStruct(const CSubmitBillData& data);
std::string CheckStruct(const CVoteBillData& data);
std::string CheckStruct(const CMessageData& data);


bool DataToStruct(CMessageData& data, const CScript& script);
bool DataToStruct(CRegisterForgerData& data, const CScript& script);
bool DataToStruct(CVoteForgerData& data, const CScript& script);
bool DataToStruct(CCancelVoteForgerData& data, const CScript& script);

bool DataToStruct(CRegisterCommitteeData& data, const CScript& script);
bool DataToStruct(CVoteCommitteeData& data, const CScript& script);
bool DataToStruct(CCancelVoteCommitteeData& data, const CScript& script);
bool DataToStruct(CSubmitBillData& data, const CScript& script);
bool DataToStruct(CVoteBillData& data, const CScript& script);

typedef std::pair<uint160, uint8_t> CMyAddress;

struct key_hash
{
    std::size_t operator()(CMyAddress const& k) const {
        std::size_t hash = 0;
        boost::hash_range( hash, k.first.begin(), k.first.end() );
        return hash;
    }
};

class Vote{
public:
    Vote();
    ~Vote();
    bool Init(int64_t nBlockHeight, const std::string& strBlockHash);
    static Vote& GetInstance();
    std::vector<Delegate> GetTopDelegateInfo(uint64_t nMinHoldBalance, uint32_t nDelegateNum);

    bool ProcessVote(const CKeyID& voter, const std::set<CKeyID>& delegates, uint256 hash, uint64_t height, bool fUndo);
    bool ProcessCancelVote(const CKeyID& voter, const std::set<CKeyID>& delegates, uint256 hash, uint64_t height, bool fUndo);
    bool ProcessRegister(const CKeyID& delegate, const std::string& strDelegateName, uint256 hash, uint64_t height, bool fUndo);

    uint64_t GetDelegateVotes(const CKeyID& delegate);
    std::set<CKeyID> GetDelegateVoters(const CKeyID& delegate);
    std::set<CKeyID> GetVotedDelegates(const CKeyID& delegate);
    std::map<std::string, CKeyID> ListDelegates();

    bool Store(int64_t height, const std::string& strBlockHash);
    bool Load(int64_t height, const std::string& strBlockHash);

    static uint64_t GetBalance(const CKeyID& id) {return Vote::GetInstance().GetAddressBalance(CMyAddress(id, CChainParams::PUBKEY_ADDRESS));}

    uint64_t GetAddressBalance(const CMyAddress& id);
    void UpdateAddressBalance(const std::vector<std::pair<CMyAddress, int64_t>>& addressBalances);

    CKeyID GetDelegate(const std::string& name);
    std::string GetDelegate(const CKeyID& keyid);
    bool HaveDelegate(const std::string& name, const CKeyID& keyid);
    bool HaveDelegate_Unlock(const std::string& name, const CKeyID& keyid);
    bool HaveVote(const CKeyID& voter, const CKeyID& delegate);

    bool HaveDelegate(std::string name);
    bool HaveDelegate(const CKeyID& keyID);
    int64_t GetOldBlockHeight() {return nOldBlockHeight;}
    std::string GetOldBlockHash() {return strOldBlockHash;}

    void AddInvalidVote(uint256 hash, uint64_t height);
    void DeleteInvalidVote(uint64_t height);
    bool FindInvalidVote(uint256 hash);

    bool AddDelegateMultiaddress(const CMyAddress& delegate, const CMyAddress& multiAddress, const uint256& txid);
    bool DelDelegateMultiaddress(const CMyAddress& delegate, const CMyAddress& multiAddress, const uint256& txid);
    std::map<CMyAddress, uint256> GetDelegateMultiaddress(const CMyAddress& delegate);

    CVoteDBK1<CKeyID, CRegisterCommitteeData, CKeyID>& GetCommittee() {
        return *pcommittee;
    }

    CVoteDBK2<uint160, CSubmitBillData, CKeyID>& GetBill() {
        return *pbill;
    }

    static const int MaxNumberOfVotes = 51;
    uint64_t GetDelegateFunds(const CMyAddress& address);

    std::multimap<uint64_t, CMyAddress> GetCoinRank(int num);
    std::map<uint64_t, std::pair<uint64_t, uint64_t>> GetCoinDistribution(const std::set<uint64_t>&);

private:
    uint64_t _GetAddressBalance(const CMyAddress& address);
    uint64_t _UpdateAddressBalance(const CMyAddress& id, int64_t value);
    uint64_t _GetDelegateVotes(const CKeyID& delegate);

    bool RepairFile(int64_t nBlockHeight, const std::string& strBlockHash);
    bool ReadControlFile(int64_t& nBlockHeight, std::string& strBlockHash, const std::string& strFileName);
    bool WriteControlFile(int64_t nBlockHeight, const std::string& strBlockHash, const std::string& strFileName);

    bool Read();
    bool Write(const std::string& strBlockHash);
    void Delete(const std::string& strBlockHash);

    bool ProcessVote(const CKeyID& voter, const std::set<CKeyID>& delegates, uint256 hash, uint64_t height);
    bool ProcessCancelVote(const CKeyID& voter, const std::set<CKeyID>& delegates, uint256 hash, uint64_t height);
    bool ProcessRegister(const CKeyID& delegate, const std::string& strDelegateName, uint256 hash, uint64_t height);
    bool ProcessUnregister(const CKeyID& delegate, const std::string& strDelegateName, uint256 hash, uint64_t height);

    bool ProcessUndoVote(const CKeyID& voter, const std::set<CKeyID>& delegates, uint256 hash, uint64_t height);
    bool ProcessUndoCancelVote(const CKeyID& voter, const std::set<CKeyID>& delegates, uint256 hash, uint64_t height);
    bool ProcessUndoRegister(const CKeyID& delegate, const std::string& strDelegateName, uint256 hash, uint64_t height);

    bool ProcessVote(const CKeyID& voter, const std::set<CKeyID>& delegates);
    bool ProcessCancelVote(const CKeyID& voter, const std::set<CKeyID>& delegates);
    bool ProcessRegister(const CKeyID& delegate, const std::string& strDelegateName);
    bool ProcessUnregister(const CKeyID& delegate, const std::string& strDelegateName);

    std::map<CMyAddress, std::map<CMyAddress, uint256>>& GetDelegateMultiaddress() {return mapDelegateMultiaddress;}
    bool IsValidDelegate(const CMyAddress& address, uint64_t nMinHoldBalance);

private:
    int64_t nVersion;
    const int64_t nCurrentVersion = -1;
    boost::shared_mutex lockVote;

    std::map<CKeyID, std::set<CKeyID>> mapDelegateVoters;
    std::map<CKeyID, std::set<CKeyID>> mapVoterDelegates;
    std::map<CKeyID, std::string> mapDelegateName;
    std::map<std::string, CKeyID> mapNameDelegate;
    std::map<uint256, uint64_t>  mapHashHeightInvalidVote;
    boost::shared_mutex lockMapHashHeightInvalidVote;

    std::unordered_map<CMyAddress, uint64_t, key_hash> mapAddressBalance;

    std::map<CMyAddress, std::map<CMyAddress, uint256>> mapDelegateMultiaddress;

    std::string strFilePath;
    std::string strDelegateFileName;
    std::string strVoteFileName;
    std::string strBalanceFileName;
    std::string strControlFileName;
    std::string strInvalidVoteTxFileName;
    std::string strDelegateMultiaddressName;

    std::string strForgerFileName;
    std::string strCommitteeFileName;
    std::string strBillFileName;

    std::string strOldBlockHash;
    int64_t nOldBlockHeight;

    std::shared_ptr<CVoteDBK1<CKeyID, CRegisterCommitteeData, CKeyID>> pcommittee;
    std::shared_ptr<CVoteDBK2<uint160, CSubmitBillData, CKeyID>> pbill;
};

#endif

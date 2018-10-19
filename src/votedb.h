
#ifndef _VOTE_DB_H
#define _VOTE_DB_H

#include <map>
#include <vector>
#include <string>

#include <boost/thread/shared_mutex.hpp>
#include "uint256.h"
#include "myserialize.h"
#include "chainparams.h"

struct CState{
    bool bFinished;
    bool bPassed;
    uint8_t nOptionIndex;
    uint64_t nTotalVote;
    uint64_t nEndtime;
    uint64_t nFinishedHeight;

    CState() {
    }
    CState(uint64_t time) {
        nEndtime = time;
        bPassed = false;
        bFinished = false;
        nOptionIndex = 0;
        nTotalVote = 0;
        nFinishedHeight = 0;
    }

    template<class Archive>
    void serialize(Archive& ar, const unsigned int version)
    {
        ar & bFinished;
        ar & bPassed;
        ar & nOptionIndex;
        ar & nTotalVote;
        ar & nEndtime;
        ar & nFinishedHeight;
    }
};

template<typename K, typename V, typename Voter>
class CVoteDBK1{
public:
    typedef boost::shared_lock<boost::shared_mutex> read_lock;
    typedef boost::unique_lock<boost::shared_mutex> write_lock;

    CVoteDBK1(uint64_t height) : nVersion(1), nStartHeight(height) {}

    void Save(const std::string& filename)
    {
        MySerialize(filename, nVersion, mapKV, mapK1Voter, mapInvalid, setVoter);
    }

    void Load(const std::string& filename)
    {
        MyUnserialize(filename, nVersion, mapKV, mapK1Voter, mapInvalid, setVoter);
    }

    bool Register(const K& k, const V& v, const uint256& hash, uint64_t height, bool fUndo)
    {
        bool ret = false;

        write_lock w(lock);

        if(height < nStartHeight) {
            AddInvalid(hash, height);
            return false;
        }

        if(fUndo) {
            if(IsInvalid(hash)) {
                DelInvalid(hash);
                return false;
            }

            auto it = mapKV.find(k);
            if(it != mapKV.end()) {
                mapKV.erase(it);
                mapK1Voter.erase(k);
                ret = true;
            }
        } else {
            if(mapKV.find(k) == mapKV.end()) {
                ret = true;
                for(auto& i : mapKV) {
                    if(i.second.name == v.name) {
                        ret = false;
                        break;
                    }
                }
            }

            if(ret) {
                mapKV[k] = v;
                mapK1Voter[k] = std::map<Voter, uint64_t>();
            } else {
                AddInvalid(hash, height);
            }
        }

        return ret;
    }

    bool FindRegiste(std::function<bool(const K&, const V&)> f)
    {
        bool ret = false;
        read_lock r(lock);
        for(auto& it : mapKV) {
            if(f(it.first, it.second)){
                ret = true;
                break;
            }
        }
        return ret;
    }

    bool FindVote(std::function<bool(const K&, std::map<Voter, uint64_t>&)> f)
    {
        bool ret = false;
        read_lock r(lock);
        for(auto& it : mapK1Voter) {
            if(f(it.first, it.second)) {
                ret = true;
                break;
            }
        }
        return ret;
    }

    bool GetRegiste(V* pv, const K& k)
    {
        bool ret = false;
        read_lock r(lock);
        for(auto& it : mapKV) {
            if(it.first == k) {
                if(pv) {
                    *pv = it.second;
                }
                ret = true;
                break;
            }
        }
        return ret;
    }

    std::map<Voter, uint64_t> GetVote(const K& k)
    {
        read_lock r(lock);
        auto it = mapK1Voter.find(k);
        if(it != mapK1Voter.end()) {
            return it->second;
        } else {
            return std::map<Voter, uint64_t>();
        }
    }

    bool FindVoter(const Voter& voter)
    {
        read_lock r(lock);
        return setVoter.find(voter) != setVoter.end();
    }

    bool Vote(const Voter& vote, const K& k, const uint256& hash, uint64_t height, bool fUndo)
    {
        bool ret = false;

        write_lock w(lock);
        if(height < nStartHeight) {
            AddInvalid(hash, height);
            return false;
        }

        if(fUndo) {
            if(IsInvalid(hash)) {
                DelInvalid(hash);
                return false;
            }

            auto it = mapK1Voter.find(k);
            if(it != mapK1Voter.end()
                && it->second.find(vote) != it->second.end()) {
                it->second.erase(vote);
                setVoter.erase(vote);
                ret = true;
            }
        } else {
            auto it = mapK1Voter.find(k);
            if(
                setVoter.find(vote) == setVoter.end()
                && it != mapK1Voter.end()
                && it->second.find(vote) == it->second.end()) {
                it->second[vote] = 0;
                setVoter.insert(vote);
                ret = true;
            } else {
                AddInvalid(hash, height);
            }
        }

        return ret;
    }

    bool CancelVote(const Voter& vote, const K& k, const uint256& hash, uint64_t height, bool fUndo)
    {
        bool ret = false;

        write_lock w(lock);
        if(height < nStartHeight) {
            AddInvalid(hash, height);
            return false;
        }

        if(fUndo) {
            if(IsInvalid(hash)) {
                DelInvalid(hash);
                return false;
            }

            auto it = mapK1Voter.find(k);
            if(it != mapK1Voter.end()
                && it->second.find(vote) != it->second.end()) {
                it->second[vote] = 0;
                setVoter.insert(vote);
                ret = true;
            }
        } else {
            auto it = mapK1Voter.find(k);
            if(
                setVoter.find(vote) != setVoter.end()
                && it != mapK1Voter.end()
                && it->second.find(vote) != it->second.end()) {
                it->second.erase(vote);
                setVoter.erase(vote);
                ret = true;
            } else {
                AddInvalid(hash, height);
            }
        }

        return ret;
    }

    void NewIrreversibleBlock(uint64_t height)
    {
        write_lock w(lock);
        for(auto it = mapInvalid.begin(); it != mapInvalid.end(); ) {
            if(it->second < height) {
                it = mapInvalid.erase(it);
            } else {
                ++it;
            }
        }
    }

private:
    bool IsInvalid(const uint256& hash)
    {
        return mapInvalid.find(hash) != mapInvalid.end();
    }

    void AddInvalid(const uint256& hash, uint64_t height)
    {
        mapInvalid[hash] = height;
    }

    void DelInvalid(const uint256& hash)
    {
        mapInvalid.erase(hash);
    }


private:
    uint64_t nVersion;
    uint64_t nStartHeight;
    boost::shared_mutex lock;
    std::map<K, V> mapKV;
    std::map<K, std::map<Voter, uint64_t>> mapK1Voter;
    std::set<Voter> setVoter;
    std::map<uint256, uint64_t>  mapInvalid;
};

template<typename K, typename V, typename Voter>
class CVoteDBK2{
public:
    typedef boost::shared_lock<boost::shared_mutex> read_lock;
    typedef boost::unique_lock<boost::shared_mutex> write_lock;

    CVoteDBK2(uint64_t height, uint64_t votenum, std::function<uint64_t(const CKeyID&)> getAddressBalance)
        : nVersion(1), nStartHeight(height), nMinVoteNum(votenum)
    {
        this->funcGetAddressBalance = getAddressBalance;
    }

    void Save(const std::string& filename)
    {
        MySerialize(filename, nVersion, mapKV, mapK2Voter, mapInvalid, mapKState);
    }

    void Load(const std::string& filename)
    {
        MyUnserialize(filename, nVersion, mapKV, mapK2Voter, mapInvalid, mapKState);
    }

    bool Register(const K& k, const V& v, const uint256& hash, uint64_t height, bool fUndo)
    {
        bool ret = false;

        write_lock w(lock);
        if(height < nStartHeight) {
            AddInvalid(hash, height);
            return false;
        }

        if(fUndo) {
            if(IsInvalid(hash)) {
                DelInvalid(hash);
                return false;
            }

            auto it = mapKV.find(k);
            if(it != mapKV.end()) {
                mapKV.erase(it);
                mapK2Voter.erase(k);
                mapKState.erase(k);
                ret = true;
            }
        } else {
            auto it = mapKV.find(k);
            if(it == mapKV.end()) {
                mapKV[k] = v;

                auto& item = mapK2Voter[k];
                for(uint8_t i=0; i < v.options.size(); ++i) {
                    item.push_back(std::map<Voter, uint64_t>());
                }

                mapKState[k] = CState(v.endtime);
                ret = true;
            } else {
                AddInvalid(hash, height);
            }
        }

        return ret;
    }

    bool FindRegiste(std::function<bool(const K&, const V&)> f)
    {
        int ret = false;
        read_lock r(lock);
        for(auto& it : mapKV) {
            if(f(it.first, it.second)) {
                ret = true;
            }
        }
        return ret;
    }

    bool FindVote(std::function<bool(const K&, std::vector<std::map<Voter, uint64_t>>&)> f)
    {
        bool ret = false;
        read_lock r(lock);
        for(auto& it : mapK2Voter) {
            if(f(it.first, it.second)) {
                ret = true;
            }
        }

        return ret;
    }

    bool GetRegiste(V* pv, const K& k)
    {
        bool ret = false;
        read_lock r(lock);
        for(auto& it : mapKV) {
            if(it.first == k) {
                if(pv) {
                    *pv = it.second;
                }
                ret = true;
                break;
            }
        }
        return ret;
    }

    std::vector<std::map<Voter, uint64_t>> GetVote(const K& k)
    {
        read_lock r(lock);
        auto it = mapK2Voter.find(k);
        if(it != mapK2Voter.end()) {
            return it->second;
        } else {
            return std::vector<std::map<Voter, uint64_t>>();
        }
    }

    CState GetState(const K& k)
    {
        read_lock r(lock);
        auto it = mapKState.find(k);
        if(it != mapKState.end()) {
            return it->second;
        } else {
            return CState(0);
        }
    }

    bool Vote(const Voter& vote, const K& k, uint8_t k2, const uint256& hash, uint64_t height, bool fUndo)
    {
        bool ret = false;

        write_lock w(lock);
        if(height < nStartHeight) {
            AddInvalid(hash, height);
            return false;
        }

        auto its = mapKState.find(k);
        if(its != mapKState.end() && its->second.bFinished) {
            AddInvalid(hash, height);
            return false;
        }

        if(fUndo) {
            if(IsInvalid(hash)) {
                DelInvalid(hash);
                return false;
            }

            auto it = mapK2Voter.find(k);
            if(it != mapK2Voter.end()
                && k2 < it->second.size()
                && it->second[k2].find(vote) != it->second[k2].end()) {
                it->second[k2].erase(vote);
                ret = true;
            }
        } else {
            auto it = mapK2Voter.find(k);
            if(it != mapK2Voter.end()) {
                ret = true;
                for(auto& i : it->second) {
                    if(i.find(vote) != i.end()) {
                        ret = false;
                        break;
                    }
                }
            }

            if(ret) {
                if(k2 < it->second.size()) {
                    it->second[k2][vote] = 0;
                } else {
                    ret = false;
                }
            }

           if (ret == false){
                AddInvalid(hash, height);
            }
        }

        return ret;
    }

    void NewIrreversibleBlock(uint64_t height)
    {
        write_lock w(lock);
        for(auto it = mapInvalid.begin(); it != mapInvalid.end(); ) {
            if(it->second < height) {
                it = mapInvalid.erase(it);
            } else {
                ++it;
            }
        }
    }

    void FinishVote(const K& key)
    {
        uint64_t nTotalVote = 0;
        uint64_t nVoteOld = 0;
        uint8_t index = 0;

        auto& votes = mapK2Voter[key];
        for(uint32_t i=0; i < votes.size(); ++i) {
            uint64_t nVote = 0;
            for(auto& j : votes[i])     {
                j.second = funcGetAddressBalance(j.first);
                nVote += j.second;
            }

            nTotalVote += nVote;
            if(nVote > nVoteOld) {
                index = (uint8_t)i;
                nVoteOld = nVote;
            }
        }

        auto& state = mapKState[key];
        state.bFinished = true;
        state.nTotalVote = nTotalVote;
        state.nOptionIndex = index;

        if(nTotalVote > nMinVoteNum) {
            if(votes.size() == 2) {
                if(nVoteOld * 100 > nTotalVote * 60) {
                    state.bPassed = true;
                }
            } else if(votes.size() == 3) {
                if(nVoteOld * 100 > nTotalVote * 40) {
                    state.bPassed = true;
                }
            } else {
                if(nVoteOld * 100 > nTotalVote * 30) {
                    state.bPassed = true;
                }
            }
        }
    }

    void NewBlockHeight(uint64_t height, uint64_t time, bool fUndo)
    {
        write_lock w(lock);
        if(fUndo) {
            for(auto& i : mapKState) {
                auto& state = i.second;

                if(state.bFinished && state.nFinishedHeight == height) {
                    state = CState(state.nEndtime);
                }
            }
        } else {
            for(auto& i : mapKState) {
                auto& state = i.second;
                if(state.bFinished == false && time > state.nEndtime) {
                    state.bFinished = true;
                    state.nFinishedHeight = height;
                    FinishVote(i.first);
                }
            }
        }
    }

private:
    bool IsInvalid(const uint256& hash)
    {
        return mapInvalid.find(hash) != mapInvalid.end();
    }

    void AddInvalid(const uint256& hash, uint64_t height)
    {
        mapInvalid[hash] = height;
    }

    void DelInvalid(const uint256& hash)
    {
        mapInvalid.erase(hash);
    }

private:
    uint64_t nVersion;
    uint64_t nStartHeight;
    uint64_t nMinVoteNum;
    std::function<uint64_t(const CKeyID&)> funcGetAddressBalance;

    boost::shared_mutex lock;
    std::map<K, V> mapKV;
    std::map<K, std::vector<std::map<Voter, uint64_t>>> mapK2Voter;
    std::map<K, CState> mapKState;

    std::map<uint256, uint64_t>  mapInvalid;
};

#endif

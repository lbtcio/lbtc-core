
#ifndef _DB_KV_H_
#define _DB_KV_H_

#include "myserialize.h"
#include <boost/thread/shared_mutex.hpp>
#include <string>
#include <vector>
#include <map>
#include <list>
#include <memory>
#include <stdint.h>

template<class Key, class Value>
class dbKV {
public:
	typedef boost::shared_lock<boost::shared_mutex> read_lock;
	typedef boost::unique_lock<boost::shared_mutex> write_lock;

	enum db_undo_type{
		insert,
		del,
		update
	};

	struct db_undo{
		db_undo(db_undo_type t, const Key& k, const Value& v)
	   		: type(t), key(k), value(v) {}
		db_undo(){}
		template<class Archive>
		void serialize(Archive & ar, const unsigned int version) {
			ar & type;
			ar & key;
	   		ar & value;
		}

		db_undo_type type;
		Key key;
	   	Value value;
	};

	dbKV() : plock(std::make_shared<boost::shared_mutex>()) {}

	dbKV(bool bAutoIncrementKey)
		: plock(std::make_shared<boost::shared_mutex>())
	{
		this->bAutoIncrementKey = bAutoIncrementKey;
		nKey = 1;
	}

	bool Set(const std::string& transactionID, const Key& k, const Value& v)
	{
		write_lock w(*plock);
		bool ret = false;
		auto it = mapKV.find(k);
		if(it == mapKV.end()) {
			if(mapUndo.find(transactionID) == mapUndo.end()) {
				listUndo.push_back(transactionID);	
			}
			mapUndo[transactionID].push_back(db_undo(insert, k, v));
		} else {
			if(mapUndo.find(transactionID) == mapUndo.end()) {
				listUndo.push_back(transactionID);	
			}
			mapUndo[transactionID].push_back(db_undo(update, it->first, it->second));
		}

		mapKV[k] = v;
		ret = true;

		return ret;
	}

	//if success return autoincrease key, otherwise return negative value
	int64_t Set(const std::string& transactionID, const Value& v)
	{
		write_lock w(*plock);
		int64_t ret = -1;
		if(bAutoIncrementKey) {
			ret = nKey;
			if(mapUndo.find(transactionID) == mapUndo.end()) {
				listUndo.push_back(transactionID);	
			}
			mapUndo[transactionID].push_back(db_undo(insert, nKey, v));

			mapKV[nKey++] = v;
		}

		return ret;
	}

	bool Delete(const std::string& transactionID, const Key& k)
	{
		write_lock w(*plock);
		bool ret = false;
		auto it = mapKV.find(k);
		if(it != mapKV.end()) {
			if(mapUndo.find(transactionID) == mapUndo.end()) {
				listUndo.push_back(transactionID);	
			}
			mapUndo[transactionID].push_back(db_undo(insert, it->first, it->second));
			mapKV.erase(it);
			ret = true;
		} 

		return ret;
	}

	bool Get(Value* pv, const Key& k)
	{
		read_lock r(*plock);
		bool ret = false;
		auto it = mapKV.find(k);	
		if(it != mapKV.end()) {
			if(pv) {
				*pv = it->second;
			}
			ret = true;	
		}

		return ret;
	}

	Value* Get(const Key& k)
	{
		read_lock r(*plock);
		Value* ret = NULL;
		auto it = mapKV.find(k);
		if(it != mapKV.end()) {
			ret = &it->second;
		}

		return ret;
	}

	void Iterate(std::function<void(const Key&, const Value&)> f)
	{
		read_lock r(*plock);
		for(auto& item : mapKV)
			f(item.first, item.second);
	}

	bool Commit(const std::string& transactionID)
	{
		write_lock w(*plock);
		bool ret = false;		
		auto it = mapUndo.find(transactionID);
		if(it != mapUndo.end()) {
			mapUndo.erase(it);	
			ret = true;
			for(auto it = listUndo.begin(); it != listUndo.end(); ++it) {
				if(*it == transactionID) {
					listUndo.erase(listUndo.begin(), ++it);
				}
			}
		}

		return ret;
	}

	//it must call in reverse order with commit
	bool Rollback(const std::string& transactionID)
	{
		write_lock w(*plock);
		bool ret = false;
		if(listUndo.back() != transactionID) {
			return false;	
		}

		auto itUndo = mapUndo.find(transactionID);
		if(itUndo != mapUndo.end()) {
			auto& vUndo = itUndo->second;
			for(auto it = vUndo.rbegin(); it != vUndo.rend(); ++it) {
				if(it->type == insert) {
					mapKV.erase(it->key);
				} else if(it->type == update){
					mapKV[it->key] = it->value;
				} else if(it->type == del) {
					mapKV[it->key] = it->value;
				}
			}
			listUndo.pop_back();
			ret = true;
		}

		return ret;
	}

	template<class Archive>
	void serialize(Archive & ar, const unsigned int version) {
		ar & bAutoIncrementKey;
		ar &  nKey;
		ar &  mapKV;
		ar &  mapUndo;
		ar & listUndo;
	}

private:
	std::shared_ptr<boost::shared_mutex> plock;
	bool bAutoIncrementKey;
	int64_t nKey;
	std::map<Key, Value> mapKV;
	std::map<std::string, std::vector<db_undo> > mapUndo;
	std::list<std::string> listUndo;
};

#endif


#ifndef _DB_KV_H_
#define _DB_KV_H_

#include "myserialize.h"
#include <string>
#include <vector>
#include <map>
#include <list>
#include <memory>

template<class Key, class Value>
class dbKV {
public:

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

	dbKV() {}
	dbKV(bool bAutoIncrementKey) {
		this->bAutoIncrementKey = bAutoIncrementKey;
		nKey = 1;
	}

	bool Set(const std::string& transactionID, const Key& k, const Value& v)
	{
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
	long Set(const std::string& transactionID, const Value& v)
	{
		long ret = -1;
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

	bool Commit(const std::string& transactionID)
	{
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
	bool bAutoIncrementKey;
	long nKey;
	std::map<Key, Value> mapKV;
	std::map<std::string, std::vector<db_undo> > mapUndo;
	std::list<std::string> listUndo;
};

#endif

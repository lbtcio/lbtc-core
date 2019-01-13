
#ifndef _ADDRESS_INDEX_H_
#define _ADDRESS_INDEX_H_

#include "amount.h"

#include <algorithm>
#include <exception>
#include <map>
#include <set>
#include <stdint.h>
#include <string>
#include <utility>
#include <vector>

#include <boost/unordered_map.hpp>

struct CTokenHistoryAddressIndexKey {
    unsigned int type;
    uint160 hashBytes;
    int blockHeight;
    int64_t tokenID;

    size_t GetSerializeSize() const {
        return 36;
    }
    template<typename Stream>
    //void Serialize(Stream& s, int nType, int nVersion) const {
    void Serialize(Stream& s) const {
        ser_writedata8(s, type);
        //hashBytes.Serialize(s, nType, nVersion);
        hashBytes.Serialize(s);
        ser_writedata32be(s, blockHeight);
        ser_writedata64(s, tokenID);
    }

    template<typename Stream>
    //void Unserialize(Stream& s, int nType, int nVersion) {
    void Unserialize(Stream& s) {
        type = ser_readdata8(s);
        hashBytes.Unserialize(s);
        blockHeight = ser_readdata32be(s);
        tokenID = ser_readdata64(s);
    }

    CTokenHistoryAddressIndexKey(unsigned int type, uint160 hashBytes, int blockHeight, int64_t tokenID)
        : type(type), hashBytes(hashBytes), blockHeight(blockHeight), tokenID(tokenID) {}

    CTokenHistoryAddressIndexKey() {
        SetNull();
    }

    void SetNull() {
        type = 0;
        hashBytes.SetNull();
        blockHeight = 0;
        tokenID = 0;
    }

};

struct CAddressIndexKey {
    unsigned int type;
    uint160 hashBytes;
    int blockHeight;
    unsigned int txindex;
    uint256 txhash;
    size_t index;
    bool spending;

    //size_t GetSerializeSize(int nType, int nVersion) const {
    size_t GetSerializeSize() const {
        return 66;
    }
    template<typename Stream>
    //void Serialize(Stream& s, int nType, int nVersion) const {
    void Serialize(Stream& s) const {
        ser_writedata8(s, type);
        //hashBytes.Serialize(s, nType, nVersion);
        hashBytes.Serialize(s);
        // Heights are stored big-endian for key sorting in LevelDB
        ser_writedata32be(s, blockHeight);
        ser_writedata32be(s, txindex);
        //txhash.Serialize(s, nType, nVersion);
        txhash.Serialize(s);
        ser_writedata32be(s, index);
        char f = spending;
        ser_writedata8(s, f);
    }
    template<typename Stream>
    //void Unserialize(Stream& s, int nType, int nVersion) {
    void Unserialize(Stream& s) {
        type = ser_readdata8(s);
        //hashBytes.Unserialize(s, nType, nVersion);
        hashBytes.Unserialize(s);
        blockHeight = ser_readdata32be(s);
        txindex = ser_readdata32be(s);
        //txhash.Unserialize(s, nType, nVersion);
        txhash.Unserialize(s);
        index = ser_readdata32be(s);
        char f = ser_readdata8(s);
        spending = f;
    }

    CAddressIndexKey(unsigned int addressType, uint160 addressHash, int height, int blockindex,
                     uint256 txid, size_t indexValue, bool isSpending) {
        type = addressType;
        hashBytes = addressHash;
        blockHeight = height;
        txindex = blockindex;
        txhash = txid;
        index = indexValue;
        spending = isSpending;
    }

    CAddressIndexKey() {
        SetNull();
    }

    void SetNull() {
        type = 0;
        hashBytes.SetNull();
        blockHeight = 0;
        txindex = 0;
        txhash.SetNull();
        index = 0;
        spending = false;
    }

};

struct CAddressIndexIteratorKey {
    unsigned int type;
    uint160 hashBytes;

    //size_t GetSerializeSize(int nType, int nVersion) const {
    size_t GetSerializeSize() const {
        return 21;
    }
    template<typename Stream>
    //void Serialize(Stream& s, int nType, int nVersion) const {
    void Serialize(Stream& s) const {
        ser_writedata8(s, type);
        //hashBytes.Serialize(s, nType, nVersion);
        hashBytes.Serialize(s);
    }
    template<typename Stream>
    //void Unserialize(Stream& s, int nType, int nVersion) {
    void Unserialize(Stream& s) {
        type = ser_readdata8(s);
        //hashBytes.Unserialize(s, nType, nVersion);
        hashBytes.Unserialize(s);
    }

    CAddressIndexIteratorKey(unsigned int addressType, uint160 addressHash) {
        type = addressType;
        hashBytes = addressHash;
    }

    CAddressIndexIteratorKey() {
        SetNull();
    }

    void SetNull() {
        type = 0;
        hashBytes.SetNull();
    }
};

struct CAddressIndexIteratorHeightKey {
    unsigned int type;
    uint160 hashBytes;
    int blockHeight;

    //size_t GetSerializeSize(int nType, int nVersion) const {
    size_t GetSerializeSize() const {
        return 25;
    }
    template<typename Stream>
    //void Serialize(Stream& s, int nType, int nVersion) const {
    void Serialize(Stream& s) const {
        ser_writedata8(s, type);
        //hashBytes.Serialize(s, nType, nVersion);
        hashBytes.Serialize(s);
        ser_writedata32be(s, blockHeight);
    }
    template<typename Stream>
    //void Unserialize(Stream& s, int nType, int nVersion) {
    void Unserialize(Stream& s) {
        type = ser_readdata8(s);
        //hashBytes.Unserialize(s, nType, nVersion);
        hashBytes.Unserialize(s);
        blockHeight = ser_readdata32be(s);
    }

    CAddressIndexIteratorHeightKey(unsigned int addressType, uint160 addressHash, int height) {
        type = addressType;
        hashBytes = addressHash;
        blockHeight = height;
    }

    CAddressIndexIteratorHeightKey() {
        SetNull();
    }

    void SetNull() {
        type = 0;
        hashBytes.SetNull();
        blockHeight = 0;
    }
};

#endif

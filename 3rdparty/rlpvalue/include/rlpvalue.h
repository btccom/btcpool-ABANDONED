// Copyright 2014 BitPay Inc.
// Copyright 2015 Bitcoin Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/licenses/mit-license.php.

#ifndef __RLPVALUE_H__
#define __RLPVALUE_H__

#include <stdint.h>
#include <string.h>

#include <string>
#include <vector>
#include <map>
#include <cassert>

#include <utility>        // std::pair

enum RLP_constants {
	RLP_maxUintLen		= 8,
	RLP_bufferLenStart	= 0x80,
	RLP_listStart		= 0xc0,
};

// really, a generic buffer, but we are in the global namespace, hence a prefix
class RLPBuffer {
public:
	std::vector<unsigned char>	data;

	RLPBuffer() {}
	RLPBuffer(const RLPBuffer& other) : data(other.data) {}

	void clear() { data.clear(); }
	void reserve(size_t n) { data.reserve(n); }
	void push_back(unsigned char ch) { data.push_back(ch); }

	std::vector<unsigned char>::iterator begin() { return data.begin(); }
	std::vector<unsigned char>::iterator end() { return data.end(); }

	const unsigned char *get() const { return &data[0]; }
	size_t size() const { return data.size(); }
	std::string toStr() const {
		std::string rs((const char *) &data[0], data.size());
		return rs;
	}
};

// a single RLP value... which could be a nested list of RLP values
class RLPValue {
public:
    enum VType { VARR, VBUF, };

    RLPValue() { typ = VBUF; }
    RLPValue(RLPValue::VType initialType) {
        typ = initialType;
    }
    RLPValue(const std::string& val_) {
        assign(val_);
    }
    RLPValue(const char *val_) {
        std::string s(val_);
        assign(s);
    }
    ~RLPValue() {}

    void clear();

    void assign(const std::vector<unsigned char>& val);
    void assign(const std::string& val);

    bool setArray();

    enum VType getType() const { return typ; }
    std::string getValStr() const { return val.toStr(); }
    bool empty() const { return (values.size() == 0); }

    size_t size() const { return values.size(); }

    const RLPValue& operator[](size_t index) const;

    bool isBuffer() const { return (typ == VBUF); }
    bool isArray() const { return (typ == VARR); }

    bool push_back(const RLPValue& val);
    bool push_back(const std::string& val_) {
        RLPValue tmpVal;
	tmpVal.assign(val_);
        return push_back(tmpVal);
    }
    bool push_back(const char *val_) {
        std::string s(val_);
        return push_back(s);
    }
    bool push_backV(const std::vector<RLPValue>& vec);

    std::string write() const;

    bool read(const unsigned char *raw, size_t len,
	      size_t& consumed, size_t& wanted);

private:
    bool readArray(const unsigned char *raw, size_t len,
	      size_t uintlen, size_t payloadlen,
	      size_t& consumed, size_t& wanted);
    RLPValue::VType typ;
    RLPBuffer val;
    std::vector<RLPValue> values;

    void writeBuffer(std::string& s) const;
    void writeArray(std::string& s) const;

public:
    // Strict type-specific getters, these throw std::runtime_error if the
    // value is of unexpected type
    const std::vector<RLPValue>& getValues() const;
    std::string get_str() const;
    const RLPValue& get_array() const;

    enum VType type() const { return getType(); }
};

extern const char *uvTypeName(RLPValue::VType t);

extern const RLPValue NullRLPValue;

#endif // __RLPVALUE_H__

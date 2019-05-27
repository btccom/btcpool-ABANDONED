// Copyright 2014 BitPay Inc.
// Copyright 2015 Bitcoin Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/licenses/mit-license.php.

#include <stdint.h>
#include <iomanip>
#include <sstream>
#include <stdlib.h>

#include "rlpvalue.h"

const RLPValue NullRLPValue;

void RLPValue::clear()
{
    typ = VBUF;
    val.clear();
    values.clear();
}

void RLPValue::assign(const std::string& s)
{
    clear();

    val.reserve(s.size());

    for (auto it = s.begin(); it != s.end(); it++) {
        val.push_back((unsigned char) *it);
    }
}

void RLPValue::assign(const std::vector<unsigned char>& buf)
{
    val.data.assign(buf.begin(), buf.end());
}

bool RLPValue::setArray()
{
    clear();
    typ = VARR;
    return true;
}

bool RLPValue::push_back(const RLPValue& val_)
{
    if (typ != VARR)
        return false;

    values.push_back(val_);
    return true;
}

bool RLPValue::push_backV(const std::vector<RLPValue>& vec)
{
    if (typ != VARR)
        return false;

    values.insert(values.end(), vec.begin(), vec.end());

    return true;
}

const RLPValue& RLPValue::operator[](size_t index) const
{
    if (typ != VARR)
        return NullRLPValue;
    if (index >= values.size())
        return NullRLPValue;

    return values.at(index);
}

const char *uvTypeName(RLPValue::VType t)
{
    switch (t) {
    case RLPValue::VARR: return "array";
    case RLPValue::VBUF: return "buffer";
    }

    // not reached
    return NULL;
}


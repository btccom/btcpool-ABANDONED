// Copyright 2014 BitPay Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/licenses/mit-license.php.

#include <iomanip>
#include <stdio.h>
#include "rlpvalue.h"

std::string RLPValue::write() const
{
    std::string s;

    switch (typ) {
    case VARR:
        writeArray(s);
        break;
    case VBUF:
        writeBuffer(s);
        break;
    }

    return s;
}

std::string encodeBinary(uint64_t n)
{
	std::string rs;

	if (n == 0) {
		// do nothing; return empty string
	} else {
		rs.assign(encodeBinary(n / 256));

		unsigned char ch = n % 256;
		rs.append((const char *) &ch, 1);
	}

	return rs;
}

static std::string encodeLength(size_t n, unsigned char offset)
{
	std::string rs;

	if (n < 56) {
		unsigned char ch = n + offset;
		rs.assign((const char *) &ch, 1);
	}

	else {
		// assert(n too big);

		std::string binlen = encodeBinary(n);
		unsigned char ch = binlen.size() + offset + 55;
		rs.assign((const char *) &ch, 1);
        rs.append(binlen);
	}

	return rs;
}

void RLPValue::writeBuffer(std::string& s) const
{
	const unsigned char *p = val.size() ? val.get() : nullptr;
	size_t sz = val.size();

	if ((sz == 1) && (p[0] < 0x80))
		s.append((const char *) p, 1);
	else
		s += encodeLength(sz, 0x80) + val.toStr();
}

void RLPValue::writeArray(std::string& s) const
{
    std::string tmp;
    for (auto it = values.begin(); it != values.end(); it++) {
	const RLPValue& val = *it;
        tmp += val.write();
    }

    s += encodeLength(tmp.size(), 0xC0) + tmp;
}


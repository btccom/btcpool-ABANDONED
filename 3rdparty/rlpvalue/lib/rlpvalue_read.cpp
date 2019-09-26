// Copyright 2014 BitPay Inc.
// Copyright 2019 Bloq Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/licenses/mit-license.php.

#include <vector>
#include <cassert>
#include "rlpvalue.h"

uint64_t toInteger(const unsigned char *raw, size_t len)
{
    if (len == 0)
        return 0;
    else if (len == 1)
        return *raw;
    else
        return (raw[len - 1]) + (toInteger(raw, len - 1) * 256);
}

bool RLPValue::readArray(const unsigned char *raw, size_t len,
             size_t uintlen, size_t payloadlen,
             size_t& consumed, size_t& wanted)
{
    const size_t prefixlen = 1;

    // validate list length, including possible addition overflows.
    size_t expected = prefixlen + uintlen + payloadlen;
    if ((expected > len) || (payloadlen > len)) {
        wanted = expected > payloadlen ? expected : payloadlen;
        return false;
    }

    // we are type=array
    if (!setArray())
        return false;

    size_t child_len = payloadlen;
    size_t child_wanted = 0;
    size_t total_consumed = 0;

    const unsigned char *list_ent = raw + prefixlen + uintlen;

    // recursively read until payloadlen bytes parsed, or error
    while (child_len > 0) {
        RLPValue childVal;
        size_t child_consumed = 0;

        if (!childVal.read(list_ent, child_len,
                       child_consumed, child_wanted))
            return false;

        total_consumed += child_consumed;
        list_ent += child_consumed;
        child_len -= child_consumed;

        values.push_back(childVal);
    }

    consumed = total_consumed;
    return true;
}

bool RLPValue::read(const unsigned char *raw, size_t len,
                size_t& consumed, size_t& wanted)
{
    clear();
    consumed = 0;
    wanted = 0;

    std::vector<RLPValue*> stack;
    std::vector<unsigned char> buf;
    const unsigned char* end = raw + len;

    const size_t prefixlen = 1;

    unsigned char ch = *raw;

    if (len < 1) {
        wanted = 1;
        goto out_fail;
    }

    // Case 1: [prefix is 1-byte data buffer]
    if (ch <= 0x7f) {
        const unsigned char *tok_start = raw;
        const unsigned char *tok_end = tok_start + prefixlen;
        assert(tok_end <= end);

        // parsing done; assign data buffer value.
        buf.assign(tok_start, tok_end);
        assign(buf);

        consumed = buf.size();

    // Case 2: [prefix, including buffer length][data]
    } else if ((ch >= 0x80) && (ch <= 0xb7)) {
        size_t blen = ch - 0x80;
        size_t expected = prefixlen + blen;

        if (len < expected) {
            wanted = expected;
            goto out_fail;
        }

        const unsigned char *tok_start = raw + 1;
        const unsigned char *tok_end = tok_start + blen;
        assert(tok_end <= end);

	// require minimal encoding
	if ((blen == 1) && (tok_start[0] <= 0x7f))
		goto out_fail;

        // parsing done; assign data buffer value.
        buf.assign(tok_start, tok_end);
        assign(buf);

        consumed = expected;

    // Case 3: [prefix][buffer length][data]
    } else if ((ch >= 0xb8) && (ch <= 0xbf)) {
        size_t uintlen = ch - 0xb7;
        size_t expected = prefixlen + uintlen;

        if (len < expected) {
            wanted = expected;
            goto out_fail;
        }

	assert(uintlen > 0 && uintlen <= RLP_maxUintLen);

        const unsigned char *tok_start = raw + prefixlen;
	if ((uintlen > 1) && (tok_start[0] == 0))	// no leading zeroes
		goto out_fail;

        // read buffer length
        uint64_t slen = toInteger(tok_start, uintlen);

        // validate buffer length, including possible addition overflows.
        expected = prefixlen + uintlen + slen;
        if ((slen < (RLP_listStart - RLP_bufferLenStart - RLP_maxUintLen)) ||
	    (expected > len) || (slen > len)) {
	    wanted = slen > expected ? slen : expected;
            goto out_fail;
        }

        // parsing done; assign data buffer value.
        tok_start = raw + prefixlen + uintlen;
        const unsigned char *tok_end = tok_start + slen;
        buf.assign(tok_start, tok_end);
        assign(buf);

        consumed = expected;

    // Case 4: [prefix][list]
    } else if ((ch >= 0xc0) && (ch <= 0xf7)) {
        size_t payloadlen = ch - 0xc0;
        size_t expected = prefixlen + payloadlen;
        size_t list_consumed = 0;
        size_t list_wanted = 0;

        // read list payload
        if (!readArray(raw, len, 0, payloadlen, list_consumed, list_wanted)) {
            wanted = list_wanted;
            goto out_fail;
        }

        assert(list_consumed == payloadlen);

        consumed = expected;

    // Case 5: [prefix][list length][list]
    } else {
        assert((ch >= 0xf8) && (ch <= 0xff));

        size_t uintlen = ch - 0xf7;
        size_t expected = prefixlen + uintlen;

        if (len < expected) {
            wanted = expected;
            goto out_fail;
        }

	assert(uintlen > 0 && uintlen <= RLP_maxUintLen);

        const unsigned char *tok_start = raw + prefixlen;
	if ((uintlen > 1) && (tok_start[0] == 0))	// no leading zeroes
		goto out_fail;

        // read list length
        size_t payloadlen = toInteger(tok_start, uintlen);

	// special requirement for non-immediate length
	if (payloadlen < (0x100 - RLP_listStart - RLP_maxUintLen))
		goto out_fail;

        size_t list_consumed = 0;
        size_t list_wanted = 0;

        // read list payload
        if (!readArray(raw, len, uintlen, payloadlen, list_consumed, list_wanted)) {
            wanted = list_wanted;
            goto out_fail;
        }

        assert(list_consumed == payloadlen);

        consumed = prefixlen + uintlen + payloadlen;
    }

    return true;

out_fail:
    clear();
    return false;
}


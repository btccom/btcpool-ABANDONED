/*
 The MIT License (MIT)

 Copyright (c) [2016] [BTC.COM]

 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE.
 */
#pragma once
#include "uint256.h"
#include <boost/endian/buffers.hpp>
#include "Difficulty.h"

// https://github.com/nervosnetwork/ckb/blob/develop/util/types/src/utilities/difficulty.rs#L8
// https://github.com/nervosnetwork/ckb/blob/develop/util/types/src/utilities/difficulty.rs#L16
using CkbDifficulty = Difficulty<0x2100ffff>;
const std::string ckbdiffone =
    "ffffff0000000000000000000000000000000000000000000000000000000000";
namespace CKB {
arith_uint256 GetEaglesongHash2(uint256 pow_hash, uint64_t nonce);
arith_uint256 GetEaglesongHash128(uint256 pow_hash, std::string nonce);
} // namespace CKB

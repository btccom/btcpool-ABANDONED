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
#ifndef POOL_COMMON_ETH_H_
#define POOL_COMMON_ETH_H_

#include "Common.h"

#include <uint256.h>
#include "libethash/ethash.h"
#include "libblake2/blake2.h"

////////////////////////////// for Eth //////////////////////////////
string Eth_DifficultyToTarget(uint64_t diff);
uint64_t Eth_TargetToDifficulty(string target);
uint64_t Eth_TargetToDifficulty(const uint256 &target);
void Hex256ToEthash256(const string &strHex, ethash_h256_t &ethashHeader);
void Uint256ToEthash256(const uint256 hash, ethash_h256_t &ethashHeader);
uint256 Ethash256ToUint256(const ethash_h256_t &ethashHeader);

// NICEHASH_STRATUM uses a different difficulty value than the Ethereum network
// and BTCPool ETH. Conversion between difficulty and target is done the same
// way as with Bitcoin; difficulty of 1 is transformed to target being in HEX:
// 00000000ffff0000000000000000000000000000000000000000000000000000
// @see https://www.nicehash.com/sw/Ethereum_specification_R1.txt
inline double Eth_DiffToNicehashDiff(uint64_t diff) {
  // Ethereum difficulty is numerically equivalent to 2^32 times the difficulty
  // of Bitcoin/NICEHASH_STRATUM.
  return ((double)diff) / ((double)4294967296.0);
}

#endif

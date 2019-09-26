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
#include "CommonEth.h"
#include <arith_uint256.h>

static arith_uint256 kMaxUint256(
    "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
static uint64_t kMaxUint64 = 0xffffffffffffffffull;

string Eth_DifficultyToTarget(uint64_t diff) {
  if (0 == diff) {
    return kMaxUint256.GetHex();
  }

  arith_uint256 target = kMaxUint256 / diff;
  return target.GetHex();
}

uint64_t Eth_TargetToDifficulty(string targetHex) {
  arith_uint256 target(targetHex);

  if (target == 0) {
    return kMaxUint64;
  }

  arith_uint256 diff = kMaxUint256 / target;
  return diff.GetLow64();
}

uint64_t Eth_TargetToDifficulty(const uint256 &targetBin) {
  arith_uint256 target = UintToArith256(targetBin);

  if (target == 0) {
    return kMaxUint64;
  }

  arith_uint256 diff = kMaxUint256 / target;
  return diff.GetLow64();
}

void Hex256ToEthash256(const string &strHex, ethash_h256_t &ethashHeader) {
  if (strHex.size() != 64)
    return;

  for (size_t i = 0; i < 32; ++i) {
    size_t size;
    int val = stoi(strHex.substr(i * 2, 2), &size, 16);
    ethashHeader.b[i] = (uint8_t)val;
  }
}

void Uint256ToEthash256(const uint256 hash, ethash_h256_t &ethashHeader) {
  // uint256 store hash byte in reversed order
  for (int i = 0; i < 32; ++i)
    ethashHeader.b[i] = *(hash.begin() + 31 - i);
}

uint256 Ethash256ToUint256(const ethash_h256_t &ethashHeader) {
  vector<unsigned char> v;
  for (int i = 31; i >= 0; --i)
    v.push_back(ethashHeader.b[i]);
  return uint256(v);
}

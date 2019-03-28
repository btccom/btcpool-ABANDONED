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

#include "CommonBitcoin.h"
#include <arith_uint256.h>

uint64_t TargetToDiff(uint256 &target) {
  arith_uint256 t = UintToArith256(target);
  uint64_t difficulty;
  BitsToDifficulty(t.GetCompact(), &difficulty);
  return difficulty;
}

uint64_t TargetToDiff(const string &str) {
  uint256 t = uint256S(str);
  return TargetToDiff(t);
}

void BitsToTarget(uint32_t bits, uint256 &target) {
  target = ArithToUint256(arith_uint256().SetCompact(bits));
}

#ifdef CHAIN_TYPE_LTC
static const uint32_t BITS_DIFF1 = 0x1f00ffff;
#elif defined(CHAIN_TYPE_ZEC)
static const uint32_t BITS_DIFF1 = 0x1f07ffff;
#else
static const uint32_t BITS_DIFF1 = 0x1d00ffff;
#endif

static const uint32_t SHIFTS_DIFF1 = (BITS_DIFF1 >> 24) & 0xff;
static const auto TARGET_DIFF1 = arith_uint256().SetCompact(BITS_DIFF1);
static const auto MAX_TARGET = uint256S(
    "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");

uint32_t GetDiff1Bits() {
  return BITS_DIFF1;
}

static std::array<uint256, 64> GenerateDiff2TargetTable() {
  std::array<uint256, 64> table;
  uint32_t shifts = 0;
  for (auto &target : table) {
    target = ArithToUint256(TARGET_DIFF1 >> (shifts++));
  }
  return table;
}

static const auto kDiff2TargetTable = GenerateDiff2TargetTable();

void DiffToTarget(uint64_t diff, uint256 &target, bool useTable) {
  if (diff == 0) {
    target = MAX_TARGET;
    return;
  }

  if (useTable) {
    // try to find by table
    const uint64_t p = (uint64_t)log2(diff);
    if (p < kDiff2TargetTable.size() && diff == (1ull << p)) {
      target = kDiff2TargetTable[p];
      return;
    }
  }

  // If it is not found in the table, it will be calculated.
  target = ArithToUint256(TARGET_DIFF1 / diff);
}

void BitsToDifficulty(uint32_t bits, double *difficulty) {
  uint32_t nShift = (bits >> 24) & 0xff;
  double dDiff = (double)0x0000ffff / (double)(bits & 0x00ffffff);
  while (nShift < SHIFTS_DIFF1) {
    dDiff *= 256.0;
    nShift++;
  }
  while (nShift > SHIFTS_DIFF1) {
    dDiff /= 256.0;
    nShift--;
  }
  *difficulty = dDiff;
}

void BitsToDifficulty(uint32_t bits, uint64_t *difficulty) {
  double diff;
  BitsToDifficulty(bits, &diff);
  *difficulty = (uint64_t)diff;
}

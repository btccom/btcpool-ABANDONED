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

static uint32_t _DiffToBits(uint64_t diff) {
  uint64_t nbytes = (BITS_DIFF1 >> 24) & 0xff;
  uint64_t value = BITS_DIFF1 & 0xffffffULL;

  if (diff == 0) {
    return 1;
  }

  while (diff % 256 == 0) {
    nbytes -= 1;
    diff /= 256;
  }

  if (value % diff == 0) {
    value /= diff;
  } else if ((value << 8) % diff == 0) {
    nbytes -= 1;
    value <<= 8;
    value /= diff;
  } else {
    return 1;
  }

  if (value > 0x00ffffffULL) {
    return 1; // overflow... should not happen
  }
  return (uint32_t)(value | (nbytes << 24));
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
  if (useTable) {
    // try to find by table
    const uint64_t p = (uint64_t)log2(diff);
    if (p < (sizeof(kDiff2TargetTable) / sizeof(kDiff2TargetTable[0])) &&
        diff == (1ull << p)) {
      target = kDiff2TargetTable[p];
      return;
    }
  }

  // if we use the above table, it's big enough, we don't need to calc anymore
  BitsToTarget(_DiffToBits(diff), target);
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

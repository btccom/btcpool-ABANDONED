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
#ifndef POOL_COMMON_H_
#define POOL_COMMON_H_

#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <time.h>

#include <stdexcept>
#include <string>
#include <vector>
#include <deque>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <random>
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include "libethash/ethash.h"
#include <uint256.h>

using std::string;
using std::vector;
using std::deque;
using std::map;
using std::set;
using std::pair;
using std::make_pair;
using std::unique_ptr;
using std::shared_ptr;
using std::make_shared;
using std::atomic;
using std::thread;
using std::mutex;
using std::lock_guard;
using std::unique_lock;
using std::condition_variable;

typedef int8_t int8;
typedef uint8_t uint8;
typedef int16_t int16;
typedef uint16_t uint16;
typedef int32_t int32;
typedef uint32_t uint32;
typedef int64_t int64;
typedef uint64_t uint64;
typedef lock_guard<mutex> ScopeLock;
typedef unique_lock<mutex> UniqueLock;
typedef condition_variable Condition;


/**
 * byte order conversion utils
 */
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
inline uint16 HToBe(uint16 v) {
  return (v >> 8) | (v << 8);
}
inline uint32 HToBe(uint32 v) {
  return ((v & 0xff000000) >> 24) |
  ((v & 0x00ff0000) >> 8) |
  ((v & 0x0000ff00) << 8) |
  ((v & 0x000000ff) << 24);
}
inline uint64 HToBe(uint64 v) {
  return ((v & 0xff00000000000000ULL) >> 56) |
  ((v & 0x00ff000000000000ULL) >> 40) |
  ((v & 0x0000ff0000000000ULL) >> 24) |
  ((v & 0x000000ff00000000ULL) >>  8) |
  ((v & 0x00000000ff000000ULL) <<  8) |
  ((v & 0x0000000000ff0000ULL) << 24) |
  ((v & 0x000000000000ff00ULL) << 40) |
  ((v & 0x00000000000000ffULL) << 56);
}
#else
inline uint16 HToBe(uint16 v) {
  return v;
}
inline uint32 HToBe(uint32 v) {
  return v;
}
inline uint64 HToBe(uint64 v) {
  return v;
}
#endif
inline int16 HToBe(int16 v) {
  return (int16)HToBe((uint16)v);
}
inline int32 HToBe(int32 v) {
  return (int32)HToBe((uint32)v);
}
inline int64 HToBe(int64 v) {
  return (int64)HToBe((uint64)v);
}

uint64 TargetToDiff(uint256 &target);
uint64 TargetToDiff(const string &str);

void BitsToTarget(uint32 bits, uint256 & target);
void DiffToTarget(uint64 diff, uint256 & target, bool useTable=true);
string Eth_DifficultyToTarget(uint64 diff);
void Hex256ToEthash256(const string &strHex, ethash_h256_t &ethashHeader);
void Uint256ToEthash256(const uint256 hash, ethash_h256_t &ethashHeader);
uint256 Ethash256ToUint256(const ethash_h256_t &ethashHeader);

inline void BitsToDifficulty(uint32 bits, double *difficulty) {
  int nShift = (bits >> 24) & 0xff;
  double dDiff = (double)0x0000ffff / (double)(bits & 0x00ffffff);
  while (nShift < 29) {
    dDiff *= 256.0;
    nShift++;
  }
  while (nShift > 29) {
    dDiff /= 256.0;
    nShift--;
  }
  *difficulty = dDiff;
}

inline void BitsToDifficulty(uint32 bits, uint64 *difficulty) {
  double diff;
  BitsToDifficulty(bits, &diff);
  *difficulty = (uint64)diff;
}

// diff must be 2^N
inline uint64_t formatDifficulty(const uint64_t diff) {
  // set 2^63 as maximum difficulty, 2^63 = 9223372036854775808
  const uint64_t kMaxDiff = 9223372036854775808ull;
  if (diff >= kMaxDiff) {
    return kMaxDiff;
  }

  uint64_t newDiff = 1;
  int i = 0;
  while (newDiff < diff) {
    newDiff = newDiff << 1;
    i++;
  }
  assert(i <= 63);
  return 1ULL << i;
}

#endif

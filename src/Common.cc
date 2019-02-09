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
#include "Common.h"

uint32_t djb2(const char *s) {
  uint32_t hash = 5381;
  int c;
  uint8_t *str = (uint8_t *)s;
  while ((c = *str++))
    hash = ((hash << 5) + hash) + c; /* hash * 33 + c */

  return hash;
}

// diff must be 2^N
uint64_t formatDifficulty(const uint64_t diff) {
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

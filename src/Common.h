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
using std::function;

typedef lock_guard<mutex> ScopeLock;

using namespace std::chrono_literals;

/**
 * byte order conversion utils
 */
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
inline uint16_t HToBe(uint16_t v) {
  return (v >> 8) | (v << 8);
}
inline uint32_t HToBe(uint32_t v) {
  return ((v & 0xff000000) >> 24) | ((v & 0x00ff0000) >> 8) |
      ((v & 0x0000ff00) << 8) | ((v & 0x000000ff) << 24);
}
inline uint64_t HToBe(uint64_t v) {
  return ((v & 0xff00000000000000ULL) >> 56) |
      ((v & 0x00ff000000000000ULL) >> 40) |
      ((v & 0x0000ff0000000000ULL) >> 24) | ((v & 0x000000ff00000000ULL) >> 8) |
      ((v & 0x00000000ff000000ULL) << 8) | ((v & 0x0000000000ff0000ULL) << 24) |
      ((v & 0x000000000000ff00ULL) << 40) | ((v & 0x00000000000000ffULL) << 56);
}
#else
inline uint16_t HToBe(uint16_t v) {
  return v;
}
inline uint32_t HToBe(uint32_t v) {
  return v;
}
inline uint64_t HToBe(uint64_t v) {
  return v;
}
#endif
inline int16_t HToBe(int16_t v) {
  return (int16_t)HToBe((uint16_t)v);
}
inline int32_t HToBe(int32_t v) {
  return (int32_t)HToBe((uint32_t)v);
}
inline int64_t HToBe(int64_t v) {
  return (int64_t)HToBe((uint64_t)v);
}

uint32_t djb2(const char *str);
uint64_t formatDifficulty(const uint64_t diff);

#endif

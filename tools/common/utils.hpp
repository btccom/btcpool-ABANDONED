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

#include <string>
#include <stdint.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <inttypes.h>
#include <time.h>
#include <stdlib.h>

#define FMT_HEADER_ONLY
#include <fmt/format.h>
#include <fmt/printf.h>

static const char _hexchars[] = "0123456789abcdef";

static inline int _hex2bin_char(const char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return (c - 'a') + 10;
  if (c >= 'A' && c <= 'F')
    return (c - 'A') + 10;
  return -1;
}

void Bin2Hex(const uint8_t *in, size_t len, std::string &str) {
  str.clear();
  const uint8_t *p = in;
  while (len--) {
    str.push_back(_hexchars[p[0] >> 4]);
    str.push_back(_hexchars[p[0] & 0xf]);
    ++p;
  }
}

std::string date(const char *format, const time_t timestamp) {
  char buffer[80] = {0};
  struct tm tm;
  time_t ts = timestamp;
  gmtime_r(&ts, &tm);
  strftime(buffer, sizeof(buffer), format, &tm);
  return std::string(buffer);
}

template <typename... Args>
std::string StringFormat(const char *fmt, Args &&... args) {
  return fmt::sprintf(fmt, std::forward<Args>(args)...);
}

void BitsToDifficulty(uint32_t bits, double *difficulty) {
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

void BitsToDifficulty(uint32_t bits, uint64_t *difficulty) {
  double diff;
  BitsToDifficulty(bits, &diff);
  *difficulty = (uint64_t)diff;
}

// filter for woker name and miner agent
std::string filterWorkerName(const std::string &workerName) {
  std::string s;
  s.reserve(workerName.size());

  for (const auto &c : workerName) {
    if (('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') ||
        ('0' <= c && c <= '9') || c == '-' || c == '.' || c == '_' ||
        c == ':' || c == '|' || c == '^' || c == '/') {
      s += c;
    }
  }

  return s;
}

bool fileNonEmpty(const char *file) {
  struct stat buf;
  return (stat(file, &buf) == 0) && (buf.st_size > 0);
}

std::string getStatsFilePath(
    const std::string &chainType, const std::string &dataDir, time_t ts) {
  bool needSlash = false;
  if (dataDir.length() > 0 && *dataDir.rbegin() != '/') {
    needSlash = true;
  }
  // filename: sharelog-2016-07-12.bin
  return StringFormat(
      "%s%ssharelog%s-%s.bin",
      dataDir,
      needSlash ? "/" : "",
      chainType,
      date("%F", ts));
}

std::string getParquetFilePath(
    const std::string &chainType, const std::string &dataDir, time_t hour) {
  bool needSlash = false;
  if (dataDir.length() > 0 && *dataDir.rbegin() != '/') {
    needSlash = true;
  }
  // filename: sharelog-2016-07-12.bin
  return StringFormat(
      "%s%ssharelog%s-%s.parquet",
      dataDir,
      needSlash ? "/" : "",
      chainType,
      date("%F-%H", hour * 3600));
}

time_t str2time(const char *str, const char *format) {
  struct tm tm;
  strptime(str, format, &tm);
  return timegm(&tm);
}

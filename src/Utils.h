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
#ifndef POOL_UTILS_H_
#define POOL_UTILS_H_

#include <string>
#include <sstream>
#include <vector>
#include <queue>

#include <utilstrencodings.h>

#include <libconfig.h++>
#include <glog/logging.h>
#include <event2/buffer.h>

#include <fmt/format.h>
#include <fmt/printf.h>

#include "Common.h"
#include "zmq.hpp"

using libconfig::Setting;

bool Hex2BinReverse(const char *in, size_t size, vector<char> &out);
bool Hex2Bin(const char *in, size_t size, vector<char> &out);
bool Hex2Bin(const char *in, vector<char> &out);
void Bin2Hex(const uint8_t *in, size_t len, string &str);
void Bin2Hex(const vector<uint8_t> &in, string &str);
void Bin2Hex(const vector<char> &in, string &str);
void Bin2HexR(const uint8_t *in, size_t len, string &str);
void Bin2HexR(const vector<char> &in, string &str);

// remove prefix 0x of a hex string
inline string HexStripPrefix(const string &hex) {
  if (hex.size() >= 2 && hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X')) {
    return hex.substr(2);
  }
  return hex;
}

// add prefix 0x to a hex string
inline string HexAddPrefix(const string &hex) {
  if (hex.empty()) {
    return hex;
  }
  if (hex.size() >= 2 && hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X')) {
    return hex;
  }
  return string("0x") + hex;
}

// filter for mysql table name
inline string filterTableName(const string &tableName) {
  string s;
  s.reserve(tableName.size());

  for (const auto &c : tableName) {
    if (('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') ||
        ('0' <= c && c <= '9') || c == '_') {
      s += c;
    }
  }

  return s;
}

// bool DecodeBinTx(CTransaction& tx, const unsigned char *data, size_t len);
// bool DecodeBinBlk(CBlock& block, const unsigned char *data, size_t len);

std::string s_recv(zmq::socket_t &socket);
bool s_send(zmq::socket_t &socket, const std::string &string);
bool s_sendmore(zmq::socket_t &socket, const std::string &string);

void setSslVerifyPeer(bool verifyPeer);
bool httpGET(const char *url, string &response, long timeoutMs);
bool httpGET(
    const char *url, const char *userpwd, string &response, long timeoutMs);
bool httpPOST(
    const char *url,
    const char *userpwd,
    const char *postData,
    string &response,
    long timeoutMs,
    const char *contentType);
bool httpPOST(
    const char *url,
    const char *userpwd,
    const char *postData,
    string &response,
    long timeoutMs,
    const char *contentType,
    const char *agent);
bool blockchainNodeRpcCall(
    const char *url,
    const char *userpwd,
    const char *reqData,
    string &response);

bool rpcCall(
    const char *url,
    const char *userpwd,
    const char *reqData,
    int len,
    string &response,
    const char *agent);

string date(const char *format, const time_t timestamp);
inline string date(const char *format) {
  return date(format, time(nullptr));
}
time_t str2time(const char *str, const char *format);
inline time_t str2time(const char *str) {
  return str2time(str, "%F %T");
}

void writeTime2File(const char *filename, uint32_t t);

class Strings {
public:
  template <typename... Args>
  inline static string Format(const string &fmt, Args &&... args) {
    return fmt::sprintf(fmt, std::forward<Args>(args)...);
  }

  template <typename... Args>
  inline static void Append(string &dest, const string &fmt, Args &&... args) {
    dest += fmt::sprintf(fmt, std::forward<Args>(args)...);
  }

  template <typename... Args>
  inline static int
  EvBufferAdd(struct evbuffer *buf, const string &fmt, Args &&... args) {
    auto str = fmt::sprintf(fmt, std::forward<Args>(args)...);
    return evbuffer_add(buf, str.data(), str.size());
  }

  // If you got a "undefined reference" for a const static member of a class,
  // please wrap it with Strings::Value().
  template <typename T>
  inline static T Value(T t) {
    return t;
  }
};

string score2Str(double s);

// we use G, so never overflow
inline double share2HashrateG(uint64_t share, uint32_t timeDiff) {
  //    G: 1000000000.0
  // 2^32: 4294967296.0
  return share * (4294967296.0 / 1000000000.0 / timeDiff);
}
inline double share2HashrateT(uint64_t share, uint32_t timeDiff) {
  return share2HashrateG(share, timeDiff) / 1000.0;
}
inline double share2HashrateP(uint64_t share, uint32_t timeDiff) {
  return share2HashrateG(share, timeDiff) / 1000000.0;
}

bool fileExists(const char *file);
bool fileNonEmpty(const char *file);

template <typename V, class S>
void readFromSetting(
    const S &setting, const string &key, V &value, bool optional = false) {
  if (!setting.lookupValue(key, value) && !optional) {
    LOG(FATAL) << "config section missing key: " << key;
  }
}

template <typename V, class S>
V configLookup(const S &setting, const string &key, V defaultValue) {
  setting.lookupValue(key, defaultValue);
  return defaultValue;
}

string
getStatsFilePath(const char *chainType, const string &dataDir, time_t ts);

// redis sorted-set uses double as its rank.
// 37^9  = 1.299617398e+14 < 2^52 = 4.503599627e+15
// 37^10 = 4.808584372e+15 > 2^52 = 4.503599627e+15
// so max significand is 9 if you convert the rank as double.
uint64_t getAlphaNumRank(const string &str, size_t significand = 9);

// Check if a worker is a NiceHash client.
bool isNiceHashAgent(const string &clientAgent);

/////////////////// A map that can clean up expired items //////////////////////
template <typename K, typename V>
class SeqMap {
  std::unordered_map<K, V> map_;
  std::queue<K> queue_;

public:
  V &operator[](const K &key) {
    if (map_.find(key) == map_.end()) {
      queue_.push(key);
    }
    return map_[key];
  }

  bool contains(const K &key) { return map_.find(key) != map_.end(); }
  auto find(const K &key) { return map_.find(key); }
  auto begin() { return map_.begin(); }
  auto end() { return map_.end(); }
  size_t size() { return map_.size(); }
  bool empty() { return map_.empty(); }

  void clear(size_t maxSize) {
    while (queue_.size() > maxSize) {
      auto itr = map_.find(queue_.front());
      queue_.pop();
      if (itr != map_.end()) {
        map_.erase(itr);
      }
    }
  }

  void clear(size_t maxSize, std::function<void(V)> delFun) {
    while (queue_.size() > maxSize) {
      auto itr = map_.find(queue_.front());
      queue_.pop();
      if (itr != map_.end()) {
        if (delFun) {
          delFun(itr->second);
        }
        map_.erase(itr);
      }
    }
  }
};

class IdGenerator {
public:
  explicit IdGenerator(uint8_t serverId);
  uint64_t next();

private:
  uint32_t lastTimestamp_;
  uint32_t lastIdLow_;
};

#endif

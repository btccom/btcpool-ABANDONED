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

#include <stdint.h>
#include <string>

#include "common/utils.hpp"

using namespace std;

struct PoolInfo {
  string name_;
  string url_;
  string user_;
  string pwd_;
  string worker_;

  string toString() const {
    return StringFormat(
        "name: %s, url: %s, user: %s, pwd: %s, worker: %s",
        name_,
        url_,
        user_,
        pwd_,
        worker_);
  }

  bool enableTLS() const { return url_.find("tls://") == 0; }

  string host() const {
    string url = enableTLS() ? url_.substr(6) : url_;
    size_t pos = url.rfind(':');
    if (pos == url.npos) {
      return url;
    }
    return url.substr(0, pos);
  }

  uint16_t port() const {
    size_t pos = url_.rfind(':');
    if (pos == url_.npos) {
      return 0;
    }
    return (uint16_t)strtoul(url_.substr(pos + 1).c_str(), nullptr, 10);
  }
};
struct MatchRule {
  string field_;
  string value_;
  PoolInfo pool_;

  string toString() const {
    return StringFormat("(\"%s\" == '%s') -> %s", field_, value_, pool_.name_);
  }
};

struct StratumWorker {
  string fullName_;
  string wallet_;
  string userName_;
  string workerName_;
  string password_;

  string toString() const {
    return StringFormat(
        "wallet: %s, user: %s, worker: %s, pwd: %s",
        wallet_,
        userName_,
        workerName_,
        password_);
  }

  void setNames(string fullName, const string &password) {
    fullName_ = fullName;
    password_ = password;

    userName_ = getUserName(fullName);
    if (userName_.size() == 42 && userName_[0] == '0' &&
        (userName_[1] == 'x' || userName_[1] == 'X')) {
      wallet_ = userName_;
      fullName = fullName.substr(userName_.size() + 1);
      userName_ = getUserName(fullName);
    }
    workerName_ = getWorkerName(fullName);
  }

  static string getUserName(const string &fullName) {
    auto pos = fullName.find(".");
    if (pos == fullName.npos) {
      return fullName;
    }
    return fullName.substr(0, pos);
  }

  static string getWorkerName(const string &fullName) {
    auto pos = fullName.find(".");
    if (pos == fullName.npos) {
      return "";
    }
    return fullName.substr(pos + 1);
  }
};

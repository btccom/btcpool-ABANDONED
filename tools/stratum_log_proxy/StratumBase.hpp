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

using namespace std;

struct PoolInfo {
  bool enableTLS_ = false;
  string host_;
  uint16_t port_;
  string user_;
  string pwd_;
  string worker_;
};
struct PoolMatch {
  string field_;
  string value_;
  PoolInfo pool_;
};

struct StratumWorker {
  string fullName_;
  string wallet_;
  string userName_;
  string workerName_;
  string password_;

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

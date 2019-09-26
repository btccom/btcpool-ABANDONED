/*
 The MIT License (MIT)

 Copyright (C) 2017 RSK Labs Ltd.

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

#ifndef VCASH_WORK_H_
#define VCASH_WORK_H_

#include "utilities_js.hpp"

#include <glog/logging.h>
#include <string>
#include <uint256.h>

using std::string;

class VcashWork {
protected:
  u_int32_t created_at;
  string rpcAddress_;
  string rpcUserPwd_;

  string blockHash_;
  uint32_t bits_;
  uint256 target_;
  uint64_t baserewards_; // pub base_rewards: u64
  uint64_t height_; // pub height: u64
  uint64_t transactionsfee_; // pub transactions_fee: u64
  bool initialized_;

public:
  VcashWork();
  virtual ~VcashWork(){};

  bool initFromGw(const string &rawGetWork);
  bool isInitialized() const;
  string getRpcAddress() const;
  string getRpcUserPwd() const;
  uint32_t getCreatedAt() const;
  uint32_t getBits() const;

  string getBlockHash() const;
  string getTarget() const;
  uint64_t getBaseRewards() const;
  uint64_t getTransactionsfee() const;
  uint64_t getHeight() const;

private:
  virtual bool validate(JsonNode &work);
  virtual void initialize(JsonNode &work);
};

#endif // VCASH_WORK_H_

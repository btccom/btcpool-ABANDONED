/*
 The MIT License (MIT)

 Copyright (C) 2017 vcash Labs Ltd.

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

#include "VcashWork.h"

#include "Utils.h"
#include "Difficulty.h"

VcashWork::VcashWork()
  : initialized_(false) {
}

bool VcashWork::validate(JsonNode &work) {
  // check fields are valid
  if (work["created_at_ts"].type() != Utilities::JS::type::Int ||
      work["vcashdRpcAddress"].type() != Utilities::JS::type::Str ||
      work["vcashdRpcUserPwd"].type() != Utilities::JS::type::Str ||

      work["parentBlockHash"].type() != Utilities::JS::type::Str ||
      work["blockHashForMergedMining"].type() != Utilities::JS::type::Str ||
      work["bits"].type() != Utilities::JS::type::Int ||
      work["baserewards"].type() != Utilities::JS::type::Int ||
      work["transactionsfee"].type() != Utilities::JS::type::Int ||
      work["height"].type() != Utilities::JS::type::Int) {
    LOG(ERROR) << "vcash getwork fields failure";
    return false;
  }

  // check timestamp
  if (work["created_at_ts"].uint32() + 60u < time(nullptr)) {
    LOG(ERROR) << "too old vcash getwork: "
               << date("%F %T", work["created_at_ts"].uint32());
    return false;
  }

  return true;
}

void VcashWork::initialize(JsonNode &work) {
  created_at = work["created_at_ts"].uint32();
  rpcAddress_ = work["vcashdRpcAddress"].str();
  rpcUserPwd_ = work["vcashdRpcUserPwd"].str();
  blockHash_ = work["blockHashForMergedMining"].str();
  bits_ = work["bits"].uint32();
  BitsToTarget(bits_, target_);
  baserewards_ = work["baserewards"].uint64();
  transactionsfee_ = work["transactionsfee"].uint64();
  height_ = work["height"].uint64();

  initialized_ = true;
}

bool VcashWork::initFromGw(const string &rawGetWork) {
  JsonNode work;
  // DLOG(INFO) << "initFromGw: " << rawGetWork;
  // check is valid json
  if (!JsonNode::parse(
          rawGetWork.c_str(), rawGetWork.c_str() + rawGetWork.length(), work)) {
    LOG(ERROR) << "decode vcash getwork json fail: >" << rawGetWork << "<";
    return false;
  }

  if (!validate(work))
    return false;

  initialize(work);
  return true;
}

bool VcashWork::isInitialized() const {
  return initialized_;
}

u_int32_t VcashWork::getCreatedAt() const {
  return created_at;
}

uint32_t VcashWork::getBits() const {
  return bits_;
}

string VcashWork::getBlockHash() const {
  return blockHash_;
}

string VcashWork::getTarget() const {
  return target_.GetHex();
}

uint64_t VcashWork::getBaseRewards() const {
  return baserewards_;
}

uint64_t VcashWork::getTransactionsfee() const {
  return transactionsfee_;
}

uint64_t VcashWork::getHeight() const {
  return height_;
}

string VcashWork::getRpcAddress() const {
  return rpcAddress_;
}

string VcashWork::getRpcUserPwd() const {
  return rpcUserPwd_;
}
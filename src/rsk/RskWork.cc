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

/**
  File: RskWork.cc
  Purpose: Object that represents RSK data needed to generate a new job

  @author Martin Medina
  @copyright RSK Labs Ltd.
*/

#include "RskWork.h"

#include "Utils.h"

RskWork::RskWork() : initialized_(false) {}

bool RskWork::validate(JsonNode &work)
{
  // check fields are valid
  if (work["created_at_ts"].type()    != Utilities::JS::type::Int   ||
      work["rskdRpcAddress"].type()   != Utilities::JS::type::Str   ||
      work["rskdRpcUserPwd"].type()   != Utilities::JS::type::Str   ||
      work["parentBlockHash"].type()             != Utilities::JS::type::Str ||
      work["blockHashForMergedMining"].type()    != Utilities::JS::type::Str ||
      work["target"].type()                      != Utilities::JS::type::Str ||
      work["feesPaidToMiner"].type()             != Utilities::JS::type::Str ||
      work["notify"].type()                      != Utilities::JS::type::Str) {
    LOG(ERROR) << "rsk getwork fields failure";
    return false;
  }

  // check timestamp
  if (work["created_at_ts"].uint32() + 60u < time(nullptr)) {
    LOG(ERROR) << "too old rsk getwork: " << date("%F %T", work["created_at_ts"].uint32());
    return false;
  }

  return true;
}

void RskWork::initialize(JsonNode &work) {
  created_at = work["created_at_ts"].uint32();
  blockHash_ = work["blockHashForMergedMining"].str();
  target_ = work["target"].str();
  fees_ = work["feesPaidToMiner"].str();
  rpcAddress_ = work["rskdRpcAddress"].str(); 
  rpcUserPwd_ = work["rskdRpcUserPwd"].str();
  notifyFlag_ = work["notify"].boolean();

  initialized_ = true;
}

bool RskWork::initFromGw(const string &rawGetWork) {
  JsonNode work;
  //LOG(INFO) << "initFromGw: " << rawGetWork;
  // check is valid json
  if (!JsonNode::parse(rawGetWork.c_str(),
                       rawGetWork.c_str() + rawGetWork.length(),
                       work)) {
    LOG(ERROR) << "decode rsk getwork json fail: >" << rawGetWork << "<";
    return false;
  }

  if (!validate(work)) 
    return false;


  initialize(work);
  return true;
}

bool RskWork::isInitialized() const {
  return initialized_; 
}

u_int32_t RskWork::getCreatedAt() const {
  return created_at;
}

string RskWork::getBlockHash() const {
  return blockHash_;
}

string RskWork::getTarget() const {
  return target_;
}

string RskWork::getFees() const {
  return fees_;
}

string RskWork::getRpcAddress() const {
  return rpcAddress_;
}

string RskWork::getRpcUserPwd() const {
  return rpcUserPwd_;
}

bool RskWork::getNotifyFlag() const {
  return notifyFlag_;
}

bool RskWork::isCleanJob_;

void RskWork::setIsCleanJob(bool cleanJob) {
  isCleanJob_ = cleanJob;
}

bool RskWork::getIsCleanJob() const {
  return isCleanJob_;
}

bool RskWorkEth::validate(JsonNode &work)
{
  // check fields are valid
  if (work["created_at_ts"].type() != Utilities::JS::type::Int ||
      work["rskdRpcAddress"].type() != Utilities::JS::type::Str ||
      work["rskdRpcUserPwd"].type() != Utilities::JS::type::Str ||
      work["hHash"].type() != Utilities::JS::type::Str ||
      work["sHash"].type() != Utilities::JS::type::Str ||
      work["target"].type() != Utilities::JS::type::Str)
  {
    LOG(ERROR) << "getwork fields failure";
    return false;
  }

  // check timestamp
  if (work["created_at_ts"].uint32() + 60u < time(nullptr))
  {
    LOG(ERROR) << "too old getwork: " << date("%F %T", work["created_at_ts"].uint32());
    return false;
  }
  return true;
}

void RskWorkEth::initialize(JsonNode &work)
{
  //LOG(INFO) << "RskWorkEth:: initialize";
  created_at = work["created_at_ts"].uint32();
  rpcAddress_ = work["rskdRpcAddress"].str(); 
  rpcUserPwd_ = work["rskdRpcUserPwd"].str();
  blockHash_ = work["hHash"].str();
  seedHash_ = work["sHash"].str();
  target_ = work["target"].str();
  //LOG(INFO) << "eth work init seedHash; " << seedHash_ << " headerHash: " << blockHash_;
  initialized_ = true;
}
/**
  File: RskWork.cc
  Purpose: Object that represents RSK data needed to generate a new job

  @author Martin Medina
  @copyright RSK
*/

#include "RskWork.h"

#include "Utils.h"

RskWork::RskWork() : initialized_(false) {}

bool RskWork::initFromGw(const string &rawGetWork) {

  JsonNode work;

  // check is valid json
  if (!JsonNode::parse(rawGetWork.c_str(),
                       rawGetWork.c_str() + rawGetWork.length(),
                       work)) {
    LOG(ERROR) << "decode rsk getwork json fail: >" << rawGetWork << "<";
    return false;
  }

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

  blockHash_ = work["blockHashForMergedMining"].str();
  target_ = work["target"].str();
  fees_ = work["feesPaidToMiner"].str();
  rpcAddress_ = work["rskdRpcAddress"].str(); 
  rpcUserPwd_ = work["rskdRpcUserPwd"].str();
  notifyFlag_ = work["notify"].boolean();

  initialized_ = true;

  return true;
}

bool RskWork::isInitialized() const {
  return initialized_; 
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
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
#include "Stratum.h"

#include "Utils.h"

#include "utilities_js.hpp"

#include <glog/logging.h>

// filter for woker name and miner agent
string filterWorkerName(const string &workerName) {
  string s;
  s.reserve(workerName.size());

  for (const auto &c : workerName) {
    if (('a' <= c && c <= 'z') ||
        ('A' <= c && c <= 'Z') ||
        ('0' <= c && c <= '9') ||
        c == '-' || c == '.' || c == '_' || c == ':' ||
        c == '|' || c == '^' || c == '/') {
      s += c;
    }
  }

  return s;
}


//////////////////////////////// StratumStatus ////////////////////////////////
const char * StratumStatus::toString(int err) {
  switch (err) {
    case ACCEPT:
      return "Share accepted";
    case ACCEPT_STALE:
      return "Share accepted (stale)";
    case SOLVED:
      return "Share accepted and solved";
    case SOLVED_STALE:
      return "Share accepted and solved (stale)";
    case REJECT_NO_REASON:
      return "Share rejected";

    case JOB_NOT_FOUND:
      return "Job not found (=stale)";
    case DUPLICATE_SHARE:
      return "Duplicate share";
    case LOW_DIFFICULTY:
      return "Low difficulty";
    case UNAUTHORIZED:
      return "Unauthorized worker";
    case NOT_SUBSCRIBED:
      return "Not subscribed";

    case ILLEGAL_METHOD:
      return "Illegal method";
    case ILLEGAL_PARARMS:
      return "Illegal params";
    case IP_BANNED:
      return "Ip banned";
    case INVALID_USERNAME:
      return "Invalid username";
    case INTERNAL_ERROR:
      return "Internal error";
    case TIME_TOO_OLD:
      return "Time too old";
    case TIME_TOO_NEW:
      return "Time too new";

    case UNKNOWN: default:
      return "Unknown";
  }
}

//////////////////////////////// StratumWorker ////////////////////////////////
StratumWorker::StratumWorker(): userId_(0), workerHashId_(0) {}

void StratumWorker::reset() {
  userId_ = 0;
  workerHashId_ = 0;

  fullName_.clear();
  userName_.clear();
  workerName_.clear();
}

string StratumWorker::getUserName(const string &fullName) const {
  auto pos = fullName.find(".");
  if (pos == fullName.npos) {
    return fullName;
  }
  return fullName.substr(0, pos);
}

void StratumWorker::setUserIDAndNames(const int32_t userId, const string &fullName) {
  reset();
  userId_ = userId;

  auto pos = fullName.find(".");
  if (pos == fullName.npos) {
    userName_   = fullName;
  } else {
    userName_   = fullName.substr(0, pos);
    workerName_ = fullName.substr(pos+1);
  }

  // the worker name will insert to DB, so must be filter
  workerName_ = filterWorkerName(workerName_);

  // max length for worker name is 20
  if (workerName_.length() > 20) {
    workerName_.resize(20);
  }

  if (workerName_.empty()) {
    workerName_ = DEFAULT_WORKER_NAME;
  }

  workerHashId_ = calcWorkerId(workerName_);
  fullName_ = userName_ + "." + workerName_;
}

int64_t StratumWorker::calcWorkerId(const string &workerName) {
  int64_t workerHashId = 0;

  // calc worker hash id, 64bits
  // https://en.wikipedia.org/wiki/Birthday_attack
  const uint256 workerNameHash = Hash(workerName.begin(), workerName.end());

  // need to convert to uint64 first than copy memory
  const uint64_t tmpId = strtoull(workerNameHash.ToString().substr(0, 16).c_str(),
                                  nullptr, 16);
  memcpy((uint8_t *)&workerHashId, (uint8_t *)&tmpId, 8);

  if (workerHashId == 0) {  // zero is kept
    workerHashId++;
  }

  return workerHashId;
}

//////////////////////////////////  StratumJob  ////////////////////////////////
StratumJob::StratumJob()
  : jobId_(0), height_(0)
  , nVersion_(0), nBits_(0U)
  , nTime_(0U)
  , minTime_(0U)
  , isRskCleanJob_(false)
{
}

StratumJob::~StratumJob()
{
  
}

string StratumJob::serializeToJson() const
{
  return Strings::Format
          (
            "{"
              "\"jobId\":%" PRIu64
              ",\"height\":%d"
              ",\"nVersion\":%d"
              ",\"nBits\":%u"
              ",\"nTime\":%u"
              ",\"minTime\":%u"
              // rsk 
              ",\"rskBlockHashForMergedMining\":\"%s\",\"rskNetworkTarget\":\"0x%s\""
              ",\"rskFeesForMiner\":\"%s\""
              ",\"rskdRpcAddress\":\"%s\",\"rskdRpcUserPwd\":\"%s\""
              ",\"isRskCleanJob\":%s"
            "}"
            , jobId_
            , height_
            , nVersion_
            , nBits_
            , nTime_
            , minTime_
            // rsk
            , blockHashForMergedMining_.size() ? blockHashForMergedMining_.c_str() : ""
            , rskNetworkTarget_.GetHex().c_str()
            , feesForMiner_.size()             ? feesForMiner_.c_str()             : ""
            , rskdRpcAddress_.size()           ? rskdRpcAddress_.c_str()           : ""
            , rskdRpcUserPwd_.c_str()          ? rskdRpcUserPwd_.c_str()           : ""
            , isRskCleanJob_ ? "true" : "false"
          );
}


bool StratumJob::unserializeFromJson(const char *s, size_t len) {
  JsonNode j;
  if (!JsonNode::parse(s, s + len, j)) {
    return false;
  }
  if(
      j["jobId"].type()        != Utilities::JS::type::Int ||
      j["height"].type()       != Utilities::JS::type::Int ||
      j["nVersion"].type()     != Utilities::JS::type::Int ||
      j["nBits"].type()        != Utilities::JS::type::Int ||
      j["nTime"].type()        != Utilities::JS::type::Int ||
      j["minTime"].type()      != Utilities::JS::type::Int
    ) 
  {
    LOG(ERROR) << "parse stratum job failure: " << s;
    return false;
  }

  jobId_         = j["jobId"].uint64();
  height_        = j["height"].int32();
  nVersion_      = j["nVersion"].int32();
  nBits_         = j["nBits"].uint32();
  nTime_         = j["nTime"].uint32();
  minTime_       = j["minTime"].uint32();

  //
  // rsk, optional
  //
  if(
      j["rskBlockHashForMergedMining"].type()   == Utilities::JS::type::Str &&
      j["rskNetworkTarget"].type()              == Utilities::JS::type::Str &&
      j["rskFeesForMiner"].type()               == Utilities::JS::type::Str &&
      j["rskdRpcAddress"].type()                == Utilities::JS::type::Str &&
      j["rskdRpcUserPwd"].type()                == Utilities::JS::type::Str &&
      j["isRskCleanJob"].type()                 == Utilities::JS::type::Bool
    ) 
  {
    blockHashForMergedMining_ = j["rskBlockHashForMergedMining"].str();
    rskNetworkTarget_         = uint256S(j["rskNetworkTarget"].str());
    feesForMiner_             = j["rskFeesForMiner"].str();
    rskdRpcAddress_           = j["rskdRpcAddress"].str();
    rskdRpcUserPwd_           = j["rskdRpcUserPwd"].str();
    isRskCleanJob_            = j["isRskCleanJob"].boolean();
  }

  return true;
}



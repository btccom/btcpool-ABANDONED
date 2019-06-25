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
#include <uint256.h>
#include <hash.h>

// filter for woker name and miner agent
string filterWorkerName(const string &workerName) {
  string s;
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

//////////////////////////////// StratumStatus ////////////////////////////////
const char *StratumStatus::toString(int err) {
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
  case ILLEGAL_VERMASK:
    return "Invalid version mask";

  case INVALID_SOLUTION:
    return "Invalid Solution";
  case WRONG_NONCE_PREFIX:
    return "Wrong Nonce Prefix";

#ifdef WORK_WITH_STRATUM_SWITCHER
  case CLIENT_IS_NOT_SWITCHER:
    return "Client is not a stratum switcher";
#endif

  case UNKNOWN:
  default:
    return "Unknown";
  }
}

//////////////////////////////// StratumWorker ////////////////////////////////
StratumWorker::StratumWorker(const size_t chainSize)
  : chainId_(0)
  , workerHashId_(0) {
  userIds_.resize(chainSize, 0);
}

void StratumWorker::resetNames() {
  workerHashId_ = 0;

  fullName_.clear();
  userName_.clear();
  workerName_.clear();
}

string StratumWorker::getUserName(const string &fullName) {
  auto pos = fullName.find(".");
  if (pos == fullName.npos) {
    return fullName;
  }
  return fullName.substr(0, pos);
}

void StratumWorker::setChainIdAndUserId(
    const size_t chainId, const int32_t userId) {
  userIds_[chainId] = userId;
  chainId_ = chainId;
}

void StratumWorker::setNames(const string &fullName) {
  resetNames();

  auto pos = fullName.find(".");
  if (pos == fullName.npos) {
    userName_ = fullName;
  } else {
    userName_ = fullName.substr(0, pos);
    workerName_ = fullName.substr(pos + 1);
  }

  // the user name and worker name will insert to DB, so must be filter
  userName_ = filterWorkerName(userName_);
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

  // need to convert to uint64_t first than copy memory
  const uint64_t tmpId =
      strtoull(workerNameHash.ToString().substr(0, 16).c_str(), nullptr, 16);
  memcpy((uint8_t *)&workerHashId, (uint8_t *)&tmpId, 8);

  if (workerHashId == 0) { // zero is kept
    workerHashId++;
  }

  return workerHashId;
}

//////////////////////////////////  StratumJob  ////////////////////////////////
StratumJob::StratumJob()
  : jobId_(0) {
}

StratumJob::~StratumJob() {
}

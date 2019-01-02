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

#include "StratumSessionBeam.h"

#include "StratumMessageDispatcher.h"
#include "StratumMinerBeam.h"
#include "DiffController.h"

#include <boost/make_unique.hpp>

StratumSessionBeam::StratumSessionBeam(ServerBeam &server,
                                     struct bufferevent *bev,
                                     struct sockaddr *saddr,
                                     uint32_t sessionId)
    : StratumSessionBase(server, bev, saddr, sessionId)
    , currentJobDiff_(0){
}

void StratumSessionBeam::sendSetDifficulty(LocalJob &localJob, uint64_t difficulty) {
  // BEAM stratum have no set difficulty method, but change the job bits directly
  currentJobDiff_ = difficulty;
}

void StratumSessionBeam::sendMiningNotify(shared_ptr<StratumJobEx> exJobPtr, bool isFirstJob) {
  if (state_ < AUTHENTICATED || exJobPtr == nullptr) {
    LOG(ERROR) << "sendMiningNotify failed, state: " << state_;
    return;
  }

  StratumJobBeam *job = dynamic_cast<StratumJobBeam *>(exJobPtr->sjob_);
  if (nullptr == job) {
    return;
  }

  uint32_t inputHash = djb2(job->input_.c_str());
  auto ljob = findLocalJob(inputHash);
  // create a new LocalJobBeam if not exists
  if (ljob == nullptr) {
    ljob = &addLocalJob(exJobPtr->chainId_, job->jobId_, inputHash);
  } else {
    dispatcher_->addLocalJob(*ljob);
  }

  uint32_t shareBits = Beam_DiffToBits(currentJobDiff_);

  DLOG(INFO) << "new eth stratum job mining.notify: share difficulty=" << std::hex << currentJobDiff_
             << ", share target=" << Beam_DiffToTarget(currentJobDiff_).ToString();
  string strNotify = Strings::Format(
    "{"
      "\"id\":\"%08x\","
      "\"jsonrpc\":\"2.0\","
      "\"method\":\"job\","
      "\"difficulty\":%u,"
      "\"input\":\"%s\""
    "}\n",
    inputHash,
    shareBits,
    job->input_.c_str()
    );

  DLOG(INFO) << strNotify;
  sendData(strNotify); // send notify string

  // clear localBeamJobs_
  clearLocalJobs();
}

bool StratumSessionBeam::validate(const JsonNode &jmethod, const JsonNode &jparams, const JsonNode &jroot) {
  if (jmethod.type() == Utilities::JS::type::Str && jmethod.size() != 0) {
    return true;
  }
  return false;
}

void StratumSessionBeam::handleRequest(
  const std::string &idStr,
  const std::string &method,
  const JsonNode &jparams,
  const JsonNode &jroot
) {
  if (method == "login") {
    handleRequest_Authorize(idStr, jparams, jroot);
  }
  else if (dispatcher_) {
    dispatcher_->handleRequest(idStr, method, jparams, jroot);
  }
}

void StratumSessionBeam::handleRequest_Authorize(
  const string &idStr,
  const JsonNode &jparams,
  const JsonNode &jroot
) {
  // const type cannot access string indexed object member
  JsonNode &jsonRoot = const_cast<JsonNode &>(jroot);

  string fullName;
  if (jsonRoot["api_key"].type() == Utilities::JS::type::Str) {
    fullName = jsonRoot["api_key"].str();
  }
  
  checkUserAndPwd(idStr, fullName, "");
  return;
}

unique_ptr<StratumMiner> StratumSessionBeam::createMiner(
  const std::string &clientAgent,
  const std::string &workerName,
  int64_t workerId
) {
  return boost::make_unique<StratumMinerBeam>(
    *this,
    *getServer().defaultDifficultyController_,
    clientAgent,
    workerName,
    workerId
  );
}

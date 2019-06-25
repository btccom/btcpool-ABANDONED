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

StratumSessionBeam::StratumSessionBeam(
    ServerBeam &server,
    struct bufferevent *bev,
    struct sockaddr *saddr,
    uint32_t sessionId)
  : StratumSessionBase(server, bev, saddr, sessionId)
  , currentJobDiff_(0) {
}

void StratumSessionBeam::sendSetDifficulty(
    LocalJob &localJob, uint64_t difficulty) {
  // BEAM stratum have no set difficulty method, but change the job bits
  // directly
  currentJobDiff_ = difficulty;
}

void StratumSessionBeam::sendMiningNotify(
    shared_ptr<StratumJobEx> exJobPtr, bool isFirstJob) {
  if (state_ < AUTHENTICATED || exJobPtr == nullptr) {
    LOG(ERROR) << "sendMiningNotify failed, state: " << state_;
    return;
  }

  auto job = std::static_pointer_cast<StratumJobBeam>(exJobPtr->sjob_);
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
    // update the job id to the latest one
    ljob->jobId_ = job->jobId_;
  }

  uint32_t shareBits = Beam_DiffToBits(currentJobDiff_);

  DLOG(INFO) << "new stratum job mining.notify: share difficulty=" << std::hex
             << currentJobDiff_ << ", share target="
             << Beam_DiffToTarget(currentJobDiff_).ToString();
  string strNotify = Strings::Format(
      "{"
      "\"id\":\"%u\","
      "\"jsonrpc\":\"2.0\","
      "\"method\":\"job\","
      "\"difficulty\":%u,"
      "\"input\":\"%s\","
      "\"height\":%d"
      "}\n",
      inputHash,
      shareBits,
      job->input_,
      job->height_);

  DLOG(INFO) << strNotify;
  sendData(strNotify); // send notify string

  // clear localBeamJobs_
  clearLocalJobs();
}

bool StratumSessionBeam::validate(
    const JsonNode &jmethod, const JsonNode &jparams, const JsonNode &jroot) {
  if (jmethod.type() == Utilities::JS::type::Str && jmethod.size() != 0) {
    return true;
  }
  return false;
}

void StratumSessionBeam::handleRequest(
    const std::string &idStr,
    const std::string &method,
    const JsonNode &jparams,
    const JsonNode &jroot) {
  if (method == "login") {
    handleRequest_Authorize(idStr, jparams, jroot);
  } else if (dispatcher_) {
    dispatcher_->handleRequest(idStr, method, jparams, jroot);
  }
}

void StratumSessionBeam::handleRequest_Authorize(
    const string &idStr, const JsonNode &jparams, const JsonNode &jroot) {
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
    int64_t workerId) {
  return std::make_unique<StratumMinerBeam>(
      *this,
      *getServer().defaultDifficultyController_,
      clientAgent,
      workerName,
      workerId);
}

void StratumSessionBeam::responseAuthorizeSuccess(const string &idStr) {
  string response = Strings::Format(
      "{"
      "\"id\":%s,"
      "\"jsonrpc\":\"2.0\","
      "\"method\":\"result\","
      "\"nonceprefix\":\"%06x\","
      "\"code\":0,"
      "\"description\":\"Login successful\""
      "}\n",
      idStr,
      sessionId_);
  sendData(response.data(), response.size());
}

void StratumSessionBeam::responseError(const string &idStr, int code) {
  string response = Strings::Format(
      "{"
      "\"id\":%s,"
      "\"jsonrpc\":\"2.0\","
      "\"method\":\"result\","
      "\"code\":-%d,"
      "\"description\":\"%s\""
      "}\n",
      idStr,
      code,
      StratumStatus::toString(code));
  sendData(response.data(), response.size());
}

void StratumSessionBeam::responseTrue(const string &idStr) {
  string response = Strings::Format(
      "{"
      "\"id\":%s,"
      "\"jsonrpc\":\"2.0\","
      "\"method\":\"result\","
      "\"code\":1,"
      "\"description\":\"accepted\""
      "}\n",
      idStr);
  sendData(response.data(), response.size());
}

void StratumSessionBeam::responseFalse(const string &idStr, int errCode) {
  responseError(idStr, errCode);
}

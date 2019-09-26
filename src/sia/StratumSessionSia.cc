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

#include "StratumSessionSia.h"

#include "StratumMessageDispatcher.h"
#include "StratumMinerSia.h"
#include "DiffController.h"

#include "bitcoin/CommonBitcoin.h"

StratumSessionSia::StratumSessionSia(
    ServerSia &server,
    struct bufferevent *bev,
    struct sockaddr *saddr,
    uint32_t extraNonce1)
  : StratumSessionBase(server, bev, saddr, extraNonce1)
  , shortJobId_(0) {
}

void StratumSessionSia::sendSetDifficulty(
    LocalJob &localJob, uint64_t difficulty) {
  static_cast<StratumTraitsSia::LocalJobType &>(localJob).jobDifficulty_ =
      difficulty;
}

void StratumSessionSia::sendMiningNotify(
    shared_ptr<StratumJobEx> exJobPtr, bool isFirstJob) {

  if (state_ < AUTHENTICATED || nullptr == exJobPtr) {
    LOG(ERROR) << "sia sendMiningNotify failed, state: " << state_;
    return;
  }

  // {"id":6,"jsonrpc":"2.0","params":["49",
  // "0x0000000000000000c12d6c07fa3e7e182d563d67a961d418d8fa0141478310a500000000000000001d3eaa5a00000000240cc42aa2940c21c8f0ad76b5780d7869629ff66a579043bbdc2b150b8689a0",
  // "0x0000000007547ff5d321871ff4fb4f118b8d13a30a1ff7b317f3c5b20629578a"],
  // "method":"mining.notify"}

  auto siaJob = std::static_pointer_cast<StratumJobSia>(exJobPtr->sjob_);
  if (nullptr == siaJob) {
    return;
  }

  auto &ljob = addLocalJob(exJobPtr->chainId_, siaJob->jobId_, shortJobId_++);
  uint64_t jobDifficulty = ljob.jobDifficulty_;
  uint256 shareTarget;
  if (jobDifficulty == 0) {
    shareTarget = siaJob->networkTarget_;
    jobDifficulty = SiaDifficulty::TargetToDiff(shareTarget);
  } else {
    SiaDifficulty::DiffToTarget(jobDifficulty, shareTarget);
  }
  string strShareTarget = shareTarget.GetHex();
  LOG(INFO) << "new sia stratum job mining.notify: share difficulty="
            << jobDifficulty << ", share target=" << strShareTarget;
  const string strNotify = Strings::Format(
      "{\"id\":6,\"jsonrpc\":\"2.0\",\"method\":\"mining.notify\","
      "\"params\":[\"%u\",\"0x%s\",\"0x%s\"]}\n",
      ljob.shortJobId_,
      siaJob->blockHashForMergedMining_,
      strShareTarget);

  sendData(strNotify); // send notify string

  // clear localJobs_
  clearLocalJobs(exJobPtr->isClean_);
}

void StratumSessionSia::handleRequest(
    const std::string &idStr,
    const std::string &method,
    const JsonNode &jparams,
    const JsonNode &jroot) {
  if (method == "mining.subscribe") {
    handleRequest_Subscribe(idStr, jparams, jroot);
  } else if (method == "mining.authorize") {
    // TODO: implement this...
    // handleRequest_Authorize(idStr, jparams, jroot);
  } else {
    dispatcher_->handleRequest(idStr, method, jparams, jroot);
  }
}

void StratumSessionSia::handleRequest_Subscribe(
    const string &idStr, const JsonNode &jparams, const JsonNode &jroot) {
  if (state_ != CONNECTED) {
    responseError(idStr, StratumStatus::UNKNOWN);
    return;
  }

  state_ = SUBSCRIBED;

  const string s = Strings::Format(
      "{\"id\":%s,\"jsonrpc\":\"2.0\",\"result\":true}\n", idStr);
  sendData(s);
}

unique_ptr<StratumMiner> StratumSessionSia::createMiner(
    const std::string &clientAgent,
    const std::string &workerName,
    int64_t workerId) {
  return std::make_unique<StratumMinerSia>(
      *this,
      *getServer().defaultDifficultyController_,
      clientAgent,
      workerName,
      workerId);
}

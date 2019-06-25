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
#include "StratumServerBeam.h"

#include "StratumSessionBeam.h"
#include "DiffController.h"

#include <boost/thread.hpp>
#include <arith_uint256.h>

#include "CommonBeam.h"

using namespace std;

////////////////////////////////// JobRepositoryBeam
//////////////////////////////////
JobRepositoryBeam::JobRepositoryBeam(
    size_t chainId,
    ServerBeam *server,
    const char *kafkaBrokers,
    const char *consumerTopic,
    const string &fileLastNotifyTime)
  : JobRepositoryBase(
        chainId, server, kafkaBrokers, consumerTopic, fileLastNotifyTime)
  , lastHeight_(0) {
}

shared_ptr<StratumJobEx> JobRepositoryBeam::createStratumJobEx(
    shared_ptr<StratumJob> sjob, bool isClean) {
  return make_shared<StratumJobEx>(chainId_, sjob, isClean);
}

void JobRepositoryBeam::broadcastStratumJob(shared_ptr<StratumJob> sjob) {
  auto sjobBeam = static_pointer_cast<StratumJobBeam>(sjob);

  LOG(INFO) << "broadcast stratum job " << std::hex << sjobBeam->jobId_;

  bool isClean = false;
  if (sjobBeam->height_ > lastHeight_) {
    isClean = true;
    lastHeight_ = sjobBeam->height_;

    LOG(INFO) << "received new height stratum job, height: "
              << sjobBeam->height_ << ", input: " << sjobBeam->input_;
  }

  shared_ptr<StratumJobEx> exJob(createStratumJobEx(sjobBeam, isClean));

  if (isClean) {
    // mark all jobs as stale, should do this before insert new job
    // stale shares will not be rejected, they will be marked as ACCEPT_STALE
    // and have lower rewards.
    for (auto it : exJobs_) {
      it.second->markStale();
    }
  }

  // insert new job
  exJobs_[sjobBeam->jobId_] = exJob;

  // send job
  if (isClean) {
    //  we will send jobs:
    // - when there is a new height (here)
    // - when mining notify interval expires (in
    //   JobRepository::checkAndSendMiningNotify())
    sendMiningNotify(exJob);
  }
}

JobRepositoryBeam::~JobRepositoryBeam() {
}

////////////////////////////////// ServierBeam ///////////////////////////////
bool ServerBeam::setupInternal(const libconfig::Config &config) {
#ifndef WORK_WITH_STRATUM_SWITCHER
  // Use 16 bits index of Session ID.
  // The full Session ID (with server id as prefix) is 24 bits.
  // Session ID will be used as starting nonce, so the single
  // searching space of a miner will be 2^40 (= 2^64 - 2^24).
  delete sessionIDManager_;
  sessionIDManager_ = new SessionIDManagerT<16>(serverId_);
  sessionIDManager_->setAllocInterval(256);
#endif

  config.lookupValue("sserver.nonce_prefix_check", noncePrefixCheck_);

  return true;
}

void ServerBeam::checkAndUpdateShare(
    size_t chainId,
    ShareBeam &share,
    shared_ptr<StratumJobEx> exjob,
    const string &output,
    const std::set<uint64_t> &jobDiffs,
    const string &workFullName,
    uint256 &computedShareHash) {
  auto sjob = static_pointer_cast<StratumJobBeam>(exjob->sjob_);

  DLOG(INFO) << "checking share nonce: " << hex << share.nonce()
             << ", input: " << sjob->input_ << ", output: " << output;

  if (exjob->isStale()) {
    share.set_status(StratumStatus::JOB_NOT_FOUND);
    return;
  }

  if (noncePrefixCheck_ && (share.nonce() >> 40) != share.sessionid()) {
    share.set_status(StratumStatus::WRONG_NONCE_PREFIX);
    return;
  }

  beam::Difficulty::Raw shareHash;
  bool isValidSulution =
      Beam_ComputeHash(sjob->input_, share.nonce(), output, shareHash);
  if (!isValidSulution && !isEnableSimulator_) {
    share.set_status(StratumStatus::INVALID_SOLUTION);
    return;
  }
  computedShareHash = Beam_Uint256Conv(shareHash);

  beam::Difficulty networkDiff(share.blockbits());
  uint256 networkTarget = Beam_BitsToTarget(share.blockbits());

  DLOG(INFO) << "comapre share hash: " << computedShareHash.GetHex()
             << ", network target: " << networkTarget.GetHex();

  // print out high diff share
  // Invalid solution can easily reach the network target, so it should be
  // excluded to prevent too many high diff and solved shares when
  // isEnableSimulator_ enabled.
  beam::Difficulty highDiff;
  highDiff.Pack((uint64_t)(networkDiff.ToFloat() / 1024));
  if (isValidSulution && highDiff.IsTargetReached(shareHash)) {
    LOG(INFO) << "high diff share, share hash: " << computedShareHash.GetHex()
              << ", network target: " << networkTarget.GetHex()
              << ", worker: " << workFullName;
  }

  if (isSubmitInvalidBlock_ ||
      (isValidSulution && networkDiff.IsTargetReached(shareHash))) {
    LOG(INFO) << "solution found, share hash: " << computedShareHash.GetHex()
              << ", network target: " << networkTarget.GetHex()
              << ", worker: " << workFullName;

    share.set_status(StratumStatus::SOLVED);
    LOG(INFO) << "solved share: " << share.toString();
    return;
  }

  // higher difficulty is prior
  for (auto itr = jobDiffs.rbegin(); itr != jobDiffs.rend(); itr++) {
    beam::Difficulty jobDiff;
    jobDiff.Pack((uint64_t)*itr);

    uint256 jobTarget = Beam_DiffToTarget(*itr);
    DLOG(INFO) << "comapre share hash: " << computedShareHash.GetHex()
               << ", job target: " << jobTarget.GetHex();

    if (isEnableSimulator_ || jobDiff.IsTargetReached(shareHash)) {
      share.set_sharediff(*itr);
      share.set_status(StratumStatus::ACCEPT);
      return;
    }
  }

  share.set_status(StratumStatus::LOW_DIFFICULTY);
  return;
}

void ServerBeam::sendSolvedShare2Kafka(
    size_t chainId,
    const ShareBeam &share,
    const string &input,
    const string &output,
    const StratumWorker &worker,
    const uint256 &blockHash) {
  string msg = Strings::Format(
      "{\"nonce\":\"%016x"
      "\",\"input\":\"%s\",\"output\":\"%s\","
      "\"height\":%u,\"blockBits\":\"%08x\",\"userId\":%d,"
      "\"workerId\":%d"
      ",\"workerFullName\":\"%s\","
      "\"blockHash\":\"%s\",\"chain\":\"%s\"}",
      share.nonce(),
      input,
      output,
      share.height(),
      share.blockbits(),
      worker.userId(chainId),
      worker.workerHashId_,
      filterWorkerName(worker.fullName_),
      blockHash.ToString(),
      "BEAM");
  ServerBase::sendSolvedShare2Kafka(chainId, msg.data(), msg.size());
}

unique_ptr<StratumSession> ServerBeam::createConnection(
    struct bufferevent *bev, struct sockaddr *saddr, uint32_t sessionID) {
  return std::make_unique<StratumSessionBeam>(*this, bev, saddr, sessionID);
}

JobRepository *ServerBeam::createJobRepository(
    size_t chainId,
    const char *kafkaBrokers,
    const char *consumerTopic,
    const string &fileLastNotifyTime) {
  return new JobRepositoryBeam(
      chainId, this, kafkaBrokers, consumerTopic, fileLastNotifyTime);
}

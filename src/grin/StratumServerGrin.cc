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

#include "StratumServerGrin.h"

#include "StratumSessionGrin.h"
#include "CommonGrin.h"

#include <algorithm>
#include <arith_uint256.h>

unique_ptr<StratumSession> StratumServerGrin::createConnection(
    struct bufferevent *bev, struct sockaddr *saddr, uint32_t sessionID) {
  return std::make_unique<StratumSessionGrin>(*this, bev, saddr, sessionID);
}

void StratumServerGrin::checkAndUpdateShare(
    size_t chainId,
    ShareGrin &share,
    shared_ptr<StratumJobEx> exjob,
    const vector<uint64_t> &proofs,
    const string &workFullName,
    uint256 &blockHash) {
  auto sjob = std::static_pointer_cast<StratumJobGrin>(exjob->sjob_);

  DLOG(INFO) << "checking share nonce: " << std::hex << share.nonce()
             << ", pre_pow: " << sjob->prePowStr_
             << ", edge_bits: " << share.edgebits();

  if (exjob->isStale()) {
    share.set_status(StratumStatus::STALE_SHARE);
    return;
  }

  PreProofGrin preProof;
  preProof.prePow = sjob->prePow_;
  preProof.prePow.timestamp =
      preProof.prePow.timestamp.value() + DiffToShift(share.sharediff());
  preProof.nonce = share.nonce();
  bool isValidSolution = VerifyPowGrin(preProof, share.edgebits(), proofs);
  if (!isValidSolution && !isEnableSimulator_) {
    share.set_status(StratumStatus::INVALID_SOLUTION);
    return;
  }

  blockHash = PowHashGrin(share.edgebits(), proofs);
  share.set_hashprefix(blockHash.GetCheapHash());
  share.set_bitsreached(UintToArith256(blockHash).GetCompact());
  uint64_t scaledShareDiff = PowDifficultyGrin(
      share.height(),
      share.edgebits(),
      preProof.prePow.secondaryScaling.value(),
      proofs);
  DLOG(INFO) << "compare share difficulty: " << scaledShareDiff
             << ", network difficulty: " << sjob->difficulty_;

  // print out high diff share
  if (isValidSolution && scaledShareDiff / sjob->difficulty_ >= 1024) {
    LOG(INFO) << "high diff share, share difficulty: " << scaledShareDiff
              << ", network difficulty: " << sjob->difficulty_
              << ", worker: " << workFullName;
  }

  if (isSubmitInvalidBlock_ ||
      (isValidSolution && scaledShareDiff >= sjob->difficulty_)) {
    LOG(INFO) << "solution found, share difficulty: " << scaledShareDiff
              << ", network difficulty: " << sjob->difficulty_
              << ", worker: " << workFullName;

    share.set_status(StratumStatus::SOLVED);
    LOG(INFO) << "solved share: " << share.toString();
    return;
  }

  DLOG(INFO) << "compare share difficulty: " << scaledShareDiff
             << ", job difficulty: " << share.scaledShareDiff();

  if (isEnableSimulator_ || scaledShareDiff >= share.scaledShareDiff()) {
    share.set_status(StratumStatus::ACCEPT);
    return;
  }

  share.set_status(StratumStatus::LOW_DIFFICULTY);
  return;
}

void StratumServerGrin::sendSolvedShare2Kafka(
    size_t chainId,
    const ShareGrin &share,
    shared_ptr<StratumJobEx> exjob,
    const vector<uint64_t> &proofs,
    const StratumWorker &worker,
    const uint256 &blockHash) {
  string proofArray;
  if (!proofs.empty()) {
    proofArray = std::accumulate(
        std::next(proofs.begin()),
        proofs.end(),
        std::to_string(proofs.front()),
        [](string a, uint64_t b) {
          return std::move(a) + "," + std::to_string(b);
        });
  }

  auto sjob = std::static_pointer_cast<StratumJobGrin>(exjob->sjob_);
  string blockHashStr;
  Bin2Hex(blockHash.begin(), blockHash.size(), blockHashStr);
  string timestampStr;
  uint64_t shift = DiffToShift(share.sharediff());
  if (shift > 0) {
    timestampStr = Strings::Format(
        ",\"timestamp\":%" PRId64, sjob->prePow_.timestamp.value() + shift);
  }
  string msg = Strings::Format(
      "{\"prePow\":\"%s\""
      ",\"height\":%u,\"edgeBits\":%u,\"nonce\":%u"
      ",\"proofs\":[%s]"
      ",\"userId\":%d,\"workerId\":%d"
      ",\"workerFullName\":\"%s\""
      ",\"blockHash\":\"%s\""
      "%s}",
      sjob->prePowStr_,
      sjob->height_,
      share.edgebits(),
      share.nonce(),
      proofArray,
      worker.userId(chainId),
      worker.workerHashId_,
      filterWorkerName(worker.fullName_),
      blockHashStr,
      timestampStr);
  ServerBase::sendSolvedShare2Kafka(chainId, msg.c_str(), msg.length());
}

JobRepository *StratumServerGrin::createJobRepository(
    size_t chainId,
    const char *kafkaBrokers,
    const char *consumerTopic,
    const string &fileLastNotifyTime,
    bool niceHashForced,
    uint64_t niceHashMinDiff,
    const std::string &niceHashMinDiffZookeeperPath) {
  return new JobRepositoryGrin{chainId,
                               this,
                               kafkaBrokers,
                               consumerTopic,
                               fileLastNotifyTime,
                               niceHashForced,
                               niceHashMinDiff,
                               niceHashMinDiffZookeeperPath};
}

JobRepositoryGrin::JobRepositoryGrin(
    size_t chainId,
    StratumServerGrin *server,
    const char *kafkaBrokers,
    const char *consumerTopic,
    const string &fileLastNotifyTime,
    bool niceHashForced,
    uint64_t niceHashMinDiff,
    const std::string &niceHashMinDiffZookeeperPath)
  : JobRepositoryBase{chainId,
                      server,
                      kafkaBrokers,
                      consumerTopic,
                      fileLastNotifyTime,
                      niceHashForced,
                      niceHashMinDiff,
                      niceHashMinDiffZookeeperPath}
  , lastHeight_{0} {
}

shared_ptr<StratumJob> JobRepositoryGrin::createStratumJob() {
  return make_shared<StratumJobGrin>();
}

void JobRepositoryGrin::broadcastStratumJob(shared_ptr<StratumJob> sjob) {
  auto sjobGrin = std::static_pointer_cast<StratumJobGrin>(sjob);

  LOG(INFO) << "broadcast stratum job " << std::hex << sjobGrin->jobId_;

  bool isClean = false;
  if (sjobGrin->height_ > lastHeight_) {
    isClean = true;
    lastHeight_ = sjobGrin->height_;

    LOG(INFO) << "received new height stratum job, height: "
              << sjobGrin->height_ << ", prePow: " << sjobGrin->prePowStr_;
  }

  shared_ptr<StratumJobEx> exJob{createStratumJobEx(sjobGrin, isClean)};

  if (isClean) {
    // mark all jobs as stale, should do this before insert new job
    // stale shares will not be rejected, they will be marked as ACCEPT_STALE
    // and have lower rewards.
    for (auto it : exJobs_) {
      it.second->markStale();
    }
  }

  // insert new job
  exJobs_[sjobGrin->jobId_] = exJob;

  // sending data in lock scope may cause implicit race condition in libevent
  if (isClean) {
    // Grin miners do not like frequent job switching, hence we only send jobs:
    // - when there is a new height
    // - when miner calls getjobtemplate
    // - when mining notify interval expires
    sendMiningNotify(exJob);
  }
}

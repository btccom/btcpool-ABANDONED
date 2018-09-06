/*
 The MIT License (MIT)

 Copyright (c) [2018] [BTC.COM]

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

#include "StratumServerDecred.h"
#include "StratumDecred.h"
#include "StratumSessionDecred.h"
#include "CommonDecred.h"
#include "arith_uint256.h"
#include <iostream>

using std::ostream;
static ostream& operator<<(ostream& os, const StratumJobDecred& job)
{
  os << "jobId = " << job.jobId_ << ", prevHash = " << job.getPrevHash() << ", coinBase1 = " << job.getCoinBase1()
     << ", coinBase2 = " << job.header_.stakeVersion << ", vesion = " << job.header_.version << ", height = " << job.header_.height;
  return os;
}

JobRepositoryDecred::JobRepositoryDecred(const char *kafkaBrokers, const char *consumerTopic, const string &fileLastNotifyTime, ServerDecred *server)
  : JobRepositoryBase<ServerDecred>(kafkaBrokers, consumerTopic, fileLastNotifyTime, server)
  , lastHeight_(0)
{
}

StratumJob* JobRepositoryDecred::createStratumJob()
{
  return new StratumJobDecred();
}

void JobRepositoryDecred::broadcastStratumJob(StratumJob *sjob)
{
  auto jobDecred = dynamic_cast<StratumJobDecred*>(sjob);
  if (!jobDecred) {
    LOG(ERROR) << "wrong job type: jobId = " << sjob->jobId_;
    return;
  }

  LOG(INFO) << "broadcasting job: " << *jobDecred;
  bool isClean = false;
  auto height = jobDecred->header_.height.value();
  if (height != lastHeight_) {
    LOG(INFO) << "received job with new height, old height = " << lastHeight_;
    isClean = true;
    lastHeight_ = height;
  }

  shared_ptr<StratumJobEx> jobEx(createStratumJobEx(jobDecred, isClean));
  {
    ScopeLock sl(lock_);

    if (isClean) {
      // mark all jobs as stale, should do this before insert new job
      // stale shares will not be rejected, they will be marked as ACCEPT_STALE and have lower rewards.
      for (auto it : exJobs_) {
        it.second->markStale();
      }
    }

    // insert new job
    exJobs_[jobDecred->jobId_] = jobEx;
  }

  if (isClean) {
    sendMiningNotify(jobEx);
  }
}

ServerDecred::ServerDecred(int32_t shareAvgSeconds)
  : ServerBase<JobRepositoryDecred>(shareAvgSeconds)
{
}

StratumSession* ServerDecred::createSession(evutil_socket_t fd, bufferevent *bev, sockaddr *saddr, const uint32_t sessionID)
{
  return new StratumSessionDecred(fd, bev, this, saddr, kShareAvgSeconds_, sessionID);
}

JobRepository* ServerDecred::createJobRepository(const char *kafkaBrokers, const char *consumerTopic, const string &fileLastNotifyTime)
{
  return new JobRepositoryDecred(kafkaBrokers, consumerTopic, fileLastNotifyTime, this);
}

int ServerDecred::checkShare(ShareDecred &share, shared_ptr<StratumJobEx> exJobPtr, const vector<uint8_t> &extraNonce2,
                             uint32_t ntime, uint32_t nonce, const string &workerFullName)
{
  if (!exJobPtr || exJobPtr->isStale()) {
    return StratumStatus::JOB_NOT_FOUND;
  }

  auto sjob = dynamic_cast<StratumJobDecred*>(exJobPtr->sjob_);
  share.network_ = sjob->network_;
  share.voters_ = sjob->header_.voters.value();
  if (ntime > sjob->header_.timestamp.value() + 600) {
    return StratumStatus::TIME_TOO_NEW;
  }

  FoundBlockDecred foundBlock(share.jobId_, share.workerHashId_, share.userId_, workerFullName, sjob->header_, sjob->network_);
  foundBlock.header_.timestamp = ntime;
  foundBlock.header_.nonce = nonce;
  std::copy_n(extraNonce2.begin(), std::min(sizeof(foundBlock.header_.extraData), extraNonce2.size()), foundBlock.header_.extraData.begin());

  uint256 blkHash = foundBlock.header_.getHash();
  auto bnBlockHash = UintToArith256(blkHash);
  auto bnNetworkTarget = UintToArith256(sjob->target_);

  //
  // found new block
  //
  if (isSubmitInvalidBlock_ == true || bnBlockHash <= bnNetworkTarget) {
    // send
    kafkaProducerSolvedShare_->produce(&foundBlock, sizeof(FoundBlockDecred));

    // mark jobs as stale
    GetJobRepository()->markAllJobsAsStale();

    LOG(INFO) << ">>>> found a new block: " << blkHash.ToString()
    << ", jobId: " << share.jobId_ << ", userId: " << share.userId_
    << ", by: " << workerFullName << " <<<<";
  }

  // print out high diff share, 2^10 = 1024
  if ((bnBlockHash >> 10) <= bnNetworkTarget) {
    LOG(INFO) << "high diff share, blkhash: " << blkHash.ToString()
              << ", networkTarget: " << sjob->target_.ToString()
              << ", by: " << workerFullName;
  }

  // check share diff
  auto jobTarget = NetworkParamsDecred::get(sjob->network_).powLimit / share.shareDiff_;

  if (isEnableSimulator_ == false && bnBlockHash > jobTarget) {
    return StratumStatus::LOW_DIFFICULTY;
  }

  DLOG(INFO) << "blkHash: " << blkHash.ToString() << ", jobTarget: "
  << jobTarget.ToString() << ", networkTarget: " << sjob->target_.ToString();

  // reach here means an valid share
  return StratumStatus::ACCEPT;
}

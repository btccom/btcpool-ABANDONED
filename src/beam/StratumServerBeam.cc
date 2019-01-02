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
#include <boost/make_unique.hpp>
#include <arith_uint256.h>

#include "CommonBeam.h"

using namespace std;


////////////////////////////////// JobRepositoryBeam ///////////////////////////////
JobRepositoryBeam::JobRepositoryBeam(size_t chainId, ServerBeam *server, const char *kafkaBrokers, const char *consumerTopic, const string &fileLastNotifyTime)
  : JobRepositoryBase(chainId, server, kafkaBrokers, consumerTopic, fileLastNotifyTime)
  , lastHeight_(0)
{
}

StratumJobEx* JobRepositoryBeam::createStratumJobEx(StratumJob *sjob, bool isClean) {
  return new StratumJobEx(chainId_, sjob, isClean);
}

void JobRepositoryBeam::broadcastStratumJob(StratumJob *sjob) {
  StratumJobBeam* sjobBeam = dynamic_cast<StratumJobBeam*>(sjob);

  LOG(INFO) << "broadcast stratum job " << std::hex << sjobBeam->jobId_;

  bool isClean = false;
  if (sjobBeam->height_ != lastHeight_) {
    isClean = true;
    lastHeight_ = sjobBeam->height_;

    LOG(INFO) << "received new height stratum job, height: " << sjobBeam->height_
              << ", input: " << sjobBeam->input_;
  }
  
  shared_ptr<StratumJobEx> exJob(createStratumJobEx(sjobBeam, isClean));
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
    exJobs_[sjobBeam->jobId_] = exJob;
  }

  //send job
  sendMiningNotify(exJob);
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

  return true;
}

int ServerBeam::checkShareAndUpdateDiff(
  size_t chainId,
  ShareBeam &share,
  shared_ptr<StratumJobEx> exjob,
  const string &output,
  const std::set<uint64_t> &jobDiffs,
  const string &workFullName
) {
  StratumJobBeam *sjob = dynamic_cast<StratumJobBeam *>(exjob->sjob_);
  
  DLOG(INFO) << "checking share nonce: " << hex << share.nonce()
             << ", input: " << sjob->input_
             << ", output: " << output;

#ifndef NDEBUG
  // Calculate the time required of light verification.
  timeval start, end;
  long mtime, seconds, useconds;
  gettimeofday(&start, NULL);
#endif

  uint256 shareTarget;
  if (!Beam_ComputeHash(sjob->input_, share.nonce(), output, shareTarget)) {
    return StratumStatus::INVALID_SOLUTION;
  }

#ifndef NDEBUG
  gettimeofday(&end, NULL);
  seconds = end.tv_sec - start.tv_sec;
  useconds = end.tv_usec - start.tv_usec;
  mtime = ((seconds)*1000 + useconds / 1000.0) + 0.5;
  // Note: The performance difference between Debug and Release builds is very large.
  // The Release build may complete in 4 ms, while the Debug build takes 100 ms.
  DLOG(INFO) << "equihash compute takes " << mtime << " ms";
#endif

  uint256 networkTarget = Beam_BitsToTarget(share.blockbits());
  
  //can not compare two uint256 directly because uint256 is little endian and uses memcmp
  arith_uint256 bnShareTarget = UintToArith256(shareTarget);
  arith_uint256 bnNetworkTarget = UintToArith256(networkTarget);
  
  DLOG(INFO) << "comapre share target: " << shareTarget.GetHex()
             << ", network target: " << networkTarget.GetHex();
  
  // print out high diff share, 2^10 = 1024
  if ((bnShareTarget >> 10) <= bnNetworkTarget) {
    LOG(INFO) << "high diff share, share target: " << shareTarget.GetHex()
              << ", network target: " << networkTarget.GetHex()
              << ", worker: " << workFullName;
  }

  if (isSubmitInvalidBlock_ || bnShareTarget <= bnNetworkTarget) {
    LOG(INFO) << "solution found, share target: " << shareTarget.GetHex()
              << ", network target: " << networkTarget.GetHex()
              << ", worker: " << workFullName;

    if (exjob->isStale()) {
      LOG(INFO) << "stale solved share: " << share.toString();
      return StratumStatus::SOLVED_STALE;
    }
    else {
      LOG(INFO) << "solved share: " << share.toString();
      return StratumStatus::SOLVED;
    }
    
  }

  // higher difficulty is prior
  for (auto itr = jobDiffs.rbegin(); itr != jobDiffs.rend(); itr++) {
    uint256 jobTarget = Beam_DiffToTarget(*itr);
    DLOG(INFO) << "comapre share target: " << shareTarget.GetHex() << ", job target: " << jobTarget.GetHex();

    if (isEnableSimulator_ || bnShareTarget <= UintToArith256(jobTarget)) {
      share.set_sharediff(*itr);
      return exjob->isStale() ? StratumStatus::ACCEPT_STALE : StratumStatus::ACCEPT;
    }
  }

  return StratumStatus::LOW_DIFFICULTY;
}

void ServerBeam::sendSolvedShare2Kafka(
    size_t chainId,
    const ShareBeam &share,
    const string &input,
    const string& output,
    const StratumWorker &worker
) {
  string msg = Strings::Format(
    "{\"nonce\":\"%" PRIu64 "\",\"input\":\"%s\",\"output\":\"%s\","
    "\"height\":%u,\"blockBits\":%08x,\"userId\":%d,"
    "\"workerId\":%" PRId64 ",\"workerFullName\":\"%s\","
    "\"chain\":\"%s\"}",
    share.nonce(), input.c_str(), output.c_str(),
    share.height(), share.blockbits(), worker.userId_,
    worker.workerHashId_, filterWorkerName(worker.fullName_).c_str(),
    "BEAM"
  );
  ServerBase::sendSolvedShare2Kafka(chainId, msg.c_str(), msg.length());
}

unique_ptr<StratumSession> ServerBeam::createConnection(struct bufferevent *bev, struct sockaddr *saddr, uint32_t sessionID)
{
  return boost::make_unique<StratumSessionBeam>(*this, bev, saddr, sessionID);
}

JobRepository *ServerBeam::createJobRepository(
  size_t chainId,
  const char *kafkaBrokers,
  const char *consumerTopic,
  const string &fileLastNotifyTime
) {
  return new JobRepositoryBeam(chainId, this, kafkaBrokers, consumerTopic, fileLastNotifyTime);
}

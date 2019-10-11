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
#include "StratumServerCkb.h"

#include "StratumSessionCkb.h"
#include "CommonCkb.h"
#include "hextodec.h"
#include <algorithm>

unique_ptr<StratumSession> StratumServerCkb::createConnection(
    struct bufferevent *bev, struct sockaddr *saddr, uint32_t sessionID) {
  return std::make_unique<StratumSessionCkb>(*this, bev, saddr, sessionID);
}

void StratumServerCkb::checkAndUpdateShare(
    size_t chainId,
    ShareCkb &share,
    shared_ptr<StratumJobEx> exjob,
    const std::set<uint64_t> &jobDiffs,
    const string &workFullName,
    uint256 &blockHash) {
  auto sjob = std::static_pointer_cast<StratumJobCkb>(exjob->sjob_);

  DLOG(INFO) << "checking share nonce: " << std::hex << share.nonce()
             << ", pow_hash: " << sjob->pow_hash_;

  if (exjob->isStale()) {
    DLOG(INFO) << "the job is stale";
    share.set_status(StratumStatus::STALE_SHARE);
    return;
  }
  uint256 pow_hash = uint256S(sjob->pow_hash_.c_str());
  // auto bnblocktarget = CKB::GetEaglesongHash2(pow_hash, share.nonce());
  auto bnblocktarget = CKB::GetEaglesongHash128(pow_hash, share.ckbnonce());
  blockHash = ArithToUint256(bnblocktarget);

  uint256 target = uint256S(sjob->target_.c_str());
  auto bnNetworkTarget = UintToArith256(target);

  DLOG(INFO) << "compare share  hash: " << bnblocktarget.GetHex()
             << ", network target: " << bnNetworkTarget.GetHex();

  // print out high diff share
  if ((bnblocktarget >> 10) <= bnNetworkTarget) {
    LOG(INFO) << "high diff share, share hash: " << bnblocktarget.GetHex()
              << ", network target: " << bnNetworkTarget.GetHex()
              << ", worker: " << workFullName;
  }

  if (isSubmitInvalidBlock_ || bnblocktarget <= bnNetworkTarget) {
    LOG(INFO) << "solution found, share hash: " << bnblocktarget.GetHex()
              << ", network target: " << bnNetworkTarget.GetHex()
              << ", worker: " << workFullName;

    share.set_status(StratumStatus::SOLVED);
    LOG(INFO) << "solved share: " << share.toString();
    return;
  }

  for (auto itr = jobDiffs.rbegin(); itr != jobDiffs.rend(); itr++) {
    uint64_t jobDiff = *itr;

    uint256 target;
    CkbDifficulty::DiffToTarget(jobDiff, target);
    auto jobTarget = UintToArith256(target);

    if (isEnableSimulator_ || bnblocktarget <= jobTarget) {
      share.set_sharediff(jobDiff);
      share.set_status(StratumStatus::ACCEPT);
      return;
    }
  }

  DLOG(INFO) << "reject share : share_pow_hash " << sjob->pow_hash_
             << "\nnonce : " << std::hex << share.nonce()
             << "\nshare hash2 : " << bnblocktarget.GetHex();

  share.set_status(StratumStatus::LOW_DIFFICULTY);
  return;
}

void StratumServerCkb::sendSolvedShare2Kafka(
    size_t chainId,
    const ShareCkb &share,
    shared_ptr<StratumJobEx> exjob,
    const StratumWorker &worker,
    const uint256 &blockHash) {
  auto sjob = std::static_pointer_cast<StratumJobCkb>(exjob->sjob_);

  const BaseConverter &hex2dec = BaseConverter::HexToDecimalConverter();
  vector<char> bin;
  std::string decckbnonce = "";
  Hex2BinReverse(share.ckbnonce().c_str(), share.ckbnonce().size(), bin);
  Bin2Hex((const uint8_t *)bin.data(), bin.size(), decckbnonce);
  transform(
      decckbnonce.begin(), decckbnonce.end(), decckbnonce.begin(), ::toupper);
  decckbnonce = hex2dec.Convert(decckbnonce);

  string msg = Strings::Format(
      "{\"job_id\":%" PRIu64
      ",\"pow_hash\":\"%s\""
      ",\"work_id\":%" PRIu64
      ""
      ",\"height\":%" PRIu64
      ""
      ",\"target\":\"%s\""
      ""
      ",\"timestamp\":%" PRIu64
      ""
      ",\"nonce\":%s"
      ",\"userId\":%" PRId32 ",\"workerId\":%" PRId64
      ",\"workerFullName\":\"%s\""
      "}",
      share.jobid(),
      sjob->pow_hash_.c_str(),
      sjob->work_id_,
      sjob->height_,
      sjob->target_.c_str(),
      sjob->timestamp_,
      decckbnonce.c_str(),
      worker.userId(chainId),
      worker.workerHashId_,
      filterWorkerName(worker.fullName_).c_str());
  DLOG(INFO) << "send SolvedShare <<--" << msg << "-->>2Kafka ";

  ServerBase::sendSolvedShare2Kafka(chainId, msg.c_str(), msg.length());
}

JobRepository *StratumServerCkb::createJobRepository(
    size_t chainId,
    const char *kafkaBrokers,
    const char *consumerTopic,
    const string &fileLastNotifyTime,
    bool niceHashForced,
    uint64_t niceHashMinDiff,
    const std::string &niceHashMinDiffZookeeperPath) {
  return new JobRepositoryCkb{chainId,
                              this,
                              kafkaBrokers,
                              consumerTopic,
                              fileLastNotifyTime,
                              niceHashForced,
                              niceHashMinDiff,
                              niceHashMinDiffZookeeperPath};
}

JobRepositoryCkb::JobRepositoryCkb(
    size_t chainId,
    StratumServerCkb *server,
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

shared_ptr<StratumJob> JobRepositoryCkb::createStratumJob() {
  return make_shared<StratumJobCkb>();
}

void JobRepositoryCkb::broadcastStratumJob(shared_ptr<StratumJob> sjob) {
  auto sjobckb = std::static_pointer_cast<StratumJobCkb>(sjob);

  DLOG(INFO) << "broadcast stratum job " << std::hex << sjobckb->jobId_;

  bool isClean = false;
  if (sjobckb->height_ > lastHeight_) {
    isClean = true;
    lastHeight_ = sjobckb->height_;

    LOG(INFO) << "received new height stratum job, height: " << sjobckb->height_
              << ", hash: " << sjobckb->pow_hash_;
  }

  shared_ptr<StratumJobEx> exJob{createStratumJobEx(sjobckb, isClean)};
  {
    // ScopeLock sl(lock_);

    if (isClean) {
      // mark all jobs as stale, should do this before insert new job
      // stale shares will not be rejected, they will be marked as ACCEPT_STALE
      // and have lower rewards.
      for (auto it : exJobs_) {
        it.second->markStale();
      }
    }

    // insert new job
    exJobs_[sjobckb->jobId_] = exJob;
  }

  // sending data in lock scope may cause implicit race condition in libevent
  if (isClean) {
    // Grin miners do not like frequent job switching, hence we only send jobs:
    // - when there is a new height
    // - when miner calls getjobtemplate
    // - when mining notify interval expires
    sendMiningNotify(exJob);
  }
}

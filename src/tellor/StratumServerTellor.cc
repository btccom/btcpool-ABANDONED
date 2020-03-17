#include "StratumServerTellor.h"
#include "uint256.h"
#include "StratumSessionTellor.h"
#include "CommonTellor.h"
#include "hextodec.h"
#include "arith_uint256.h"
#include <algorithm>

unique_ptr<StratumSession> StratumServerTellor::createConnection(
    struct bufferevent *bev, struct sockaddr *saddr, uint32_t sessionID) {
  return std::make_unique<StratumSessionTellor>(*this, bev, saddr, sessionID);
}

void StratumServerTellor::checkAndUpdateShare(
    size_t chainId,
    ShareTellor &share,
    shared_ptr<StratumJobEx> exjob,
    const std::set<uint64_t> &jobDiffs,
    const string &workFullName,
    uint256 &blockHash) {
  auto sjob = std::static_pointer_cast<StratumJobTellor>(exjob->sjob_);

  if (exjob->isStale()) {
    DLOG(INFO) << "the job is stale";
    share.set_status(StratumStatus::STALE_SHARE);
    return;
  }
  std::string challenge = sjob->challenge_;
  std::string publicaddress = sjob->publicAddress_;

  blockHash =
      tellor::GetTellorPowHash(challenge, publicaddress, share.tellornonce());
  auto bnblocktarget = UintToArith256(blockHash);

  auto bnNetDifficulty = arith_uint256(sjob->difficulty_); //[TO DO]
  // auto bnNetDifficulty = UintToArith256(difficulty);

  DLOG(INFO) << "compare share  hash: " << bnblocktarget.GetHex();

  // a - (b * (a / b)
  auto mode = [](arith_uint256 a, arith_uint256 b) -> arith_uint256 {
    return a - (b * (a / b));
  };

  // print out high diff share[TO DO]
  // if ((mode(bnblocktarget, bnNetDifficulty) <= 1024)) {
  //   LOG(INFO) << "high diff share, share hash: " << bnblocktarget.GetHex()
  //             << ", network target: " << bnNetDifficulty.GetHex()
  //             << ", worker: " << workFullName;
  // }

  if (isSubmitInvalidBlock_ || (mode(bnblocktarget, bnNetDifficulty) == 0)) {
    LOG(INFO) << "solution found, share hash: " << bnblocktarget.GetHex()
              << ", network target: " << bnNetDifficulty.GetHex()
              << ", worker: " << workFullName;

    share.set_status(StratumStatus::SOLVED);
    LOG(INFO) << "solved share: " << share.toString();
    return;
  }

  for (auto itr = jobDiffs.rbegin(); itr != jobDiffs.rend(); itr++) {
    uint64_t jobDiff = *itr;

    DLOG(INFO) << ">>>> hash (" << bnblocktarget.GetHex() << ") % jobdiff("
               << jobDiff << ") = " << mode(bnblocktarget, jobDiff).GetHex();
    if (isEnableSimulator_ || (mode(bnblocktarget, jobDiff) == 0)) {
      DLOG(INFO) << ">>>> share accept <<<<";
      share.set_sharediff(jobDiff);
      share.set_status(StratumStatus::ACCEPT);
      return;
    }
  }

  DLOG(INFO) << "reject share >>>> challenge " << sjob->challenge_
             << "\npublic_address : " << sjob->publicAddress_
             << "\nnonce : " << std::hex << share.tellornonce()
             << "\nnet work difficulty : " << sjob->difficulty_
             << "\nshare hash : " << bnblocktarget.GetHex();

  share.set_status(StratumStatus::LOW_DIFFICULTY);
  return;
}

void StratumServerTellor::sendSolvedShare2Kafka(
    size_t chainId,
    const ShareTellor &share,
    shared_ptr<StratumJobEx> exjob,
    const StratumWorker &worker,
    const uint256 &blockHash) {
  auto sjob = std::static_pointer_cast<StratumJobTellor>(exjob->sjob_);

  string msg = Strings::Format(
      "{\"job_id\":%" PRIu64
      ""
      ",\"request_id\":%" PRIu64
      ""
      ",\"height\":%" PRIu64
      ""
      ",\"nonce\":\"%s\""
      ",\"userId\":%" PRId32 ",\"workerId\":%" PRId64
      ",\"workerFullName\":\"%s\""
      "}",
      share.jobid(),
      sjob->requestId_,
      sjob->height_,
      share.tellornonce().c_str(),
      worker.userId(chainId),
      worker.workerHashId_,
      filterWorkerName(worker.fullName_).c_str());
  DLOG(INFO) << "send SolvedShare <<--" << msg << "-->>2Kafka ";

  ServerBase::sendSolvedShare2Kafka(chainId, msg.c_str(), msg.length());
}

JobRepository *StratumServerTellor::createJobRepository(
    size_t chainId,
    const char *kafkaBrokers,
    const char *consumerTopic,
    const string &fileLastNotifyTime,
    bool niceHashForced,
    uint64_t niceHashMinDiff,
    const std::string &niceHashMinDiffZookeeperPath) {
  return new JobRepositoryTellor{chainId,
                                 this,
                                 kafkaBrokers,
                                 consumerTopic,
                                 fileLastNotifyTime,
                                 niceHashForced,
                                 niceHashMinDiff,
                                 niceHashMinDiffZookeeperPath};
}

JobRepositoryTellor::JobRepositoryTellor(
    size_t chainId,
    StratumServerTellor *server,
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

shared_ptr<StratumJob> JobRepositoryTellor::createStratumJob() {
  return make_shared<StratumJobTellor>();
}

void JobRepositoryTellor::broadcastStratumJob(shared_ptr<StratumJob> sjob) {
  auto sjobtellor = std::static_pointer_cast<StratumJobTellor>(sjob);

  DLOG(INFO) << "broadcast stratum job " << std::hex << sjobtellor->jobId_;

  bool isClean = false;
  if (sjobtellor->height_ > lastHeight_) {
    isClean = true;
    lastHeight_ = sjobtellor->height_;

    LOG(INFO) << "received new height stratum job, height: "
              << ", chllenge : " << sjobtellor->challenge_;
  }

  shared_ptr<StratumJobEx> exJob{createStratumJobEx(sjobtellor, isClean)};
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
    exJobs_[sjobtellor->jobId_] = exJob;
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

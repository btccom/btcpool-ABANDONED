#include "StratumMinerTellor.h"

#include "StratumSessionTellor.h"
#include "StratumServerTellor.h"

#include "DiffController.h"

#include <boost/functional/hash.hpp>

StratumMinerTellor::StratumMinerTellor(
    StratumSessionTellor &session,
    const DiffController &diffController,
    const std::string &clientAgent,
    const std::string &workerName,
    int64_t workerId)
  : StratumMinerBase(
        session, diffController, clientAgent, workerName, workerId) {
}

void StratumMinerTellor::handleRequest(
    const std::string &idStr,
    const std::string &method,
    const JsonNode &jparams,
    const JsonNode &jroot) {
  if (method == "mining.submit") {
    handleRequest_Submit(idStr, jparams);
  }
}

void StratumMinerTellor::handleRequest_Submit(
    const string &idStr, const JsonNode &jparams) {
  JsonNode &jsonParams = const_cast<JsonNode &>(jparams);

  auto &session = getSession();
  if (session.getState() != StratumSession::AUTHENTICATED) {
    handleShare(idStr, StratumStatus::UNAUTHORIZED, 0, session.getChainId());
    LOG(WARNING) << "session.getState() != StratumSession::AUTHENTICATED";
    return;
  }

  if (jsonParams.children()->size() < 3 ||
      jsonParams.children()->at(1).type() != Utilities::JS::type::Str ||
      jsonParams.children()->at(2).type() != Utilities::JS::type::Str) {
    LOG(WARNING) << "submit params count < 3";
    handleShare(idStr, StratumStatus::ILLEGAL_PARARMS, 0, session.getChainId());
    return;
  }
  //{"id":102,"method":"mining.submit","params":["Tellor1qyq2znu0gempdahctxsm49sa9jdzq9vnka7qt9ntff.worker1","17282f3f","eaf71970c0"]}
  // params: [username, jobId, nonce2]
  uint64_t extraNonce2 = 0;
  string extranonce = jsonParams.children()->at(2).str();
  uint64_t JobId = jsonParams.children()->at(1).uint64_hex();

  auto localJob = session.findLocalJob(JobId);
  // can't find local job
  if (localJob == nullptr) {
    DLOG(WARNING) << "can't find local job";
    handleShare(idStr, StratumStatus::JOB_NOT_FOUND, 0, session.getChainId());
    return;
  }

  auto &server = session.getServer();
  auto &worker = session.getWorker();
  uint32_t sessionId = session.getSessionId();

  shared_ptr<StratumJobEx> exjob = server.GetJobRepository(localJob->chainId_)
                                       ->getStratumJobEx(localJob->jobId_);
  // can't find stratum job
  if (exjob.get() == nullptr) {
    DLOG(WARNING) << "can't find stratum job";
    handleShare(idStr, StratumStatus::JOB_NOT_FOUND, 0, localJob->chainId_);
    return;
  }
  auto sjob = std::static_pointer_cast<StratumJobTellor>(exjob->sjob_);

  if (sjob->challenge_.compare(
          "0000000000000000000000000000000000000000000000000000000000000000") ==
      0) {
    handleShare(idStr, StratumStatus::JOB_NOT_FOUND, 0, localJob->chainId_);
    LOG(ERROR) << "current job is empty, miner cannot work on it "
               << worker.fullName_;
    return;
  }

  auto iter = jobDiffs_.find(localJob);
  if (iter == jobDiffs_.end()) {
    handleShare(idStr, StratumStatus::JOB_NOT_FOUND, 0, localJob->chainId_);
    LOG(ERROR) << "can't find session's diff, worker: " << worker.fullName_;
    return;
  }
  auto &jobDiff = iter->second;

  ShareTellor share;
  share.set_version(ShareTellor::CURRENT_VERSION);
  share.set_jobid(sjob->jobId_);
  share.set_workerhashid(workerId_);
  share.set_userid(worker.userId(localJob->chainId_));
  share.set_timestamp((uint64_t)time(nullptr));
  share.set_status(StratumStatus::REJECT_NO_REASON);
  share.set_sharediff(jobDiff.currentJobDiff_);
  // tellor difficulty is uint256,but no max value is uinnt64.[TO DO]
  share.set_blockdiff(sjob->difficulty_);
  share.set_height(sjob->height_);

  if (extranonce.length() >= 16) {
    extraNonce2 = strtoull(&extranonce[extranonce.length() - 16], nullptr, 16);
  } else {
    extraNonce2 = strtoull(extranonce.c_str(), nullptr, 16);
  }
  share.set_nonce(extraNonce2);

  string extranonce1_s;
  uint32_t sessionId_t = htobe32(sessionId);
  Bin2Hex((uint8_t *)&sessionId_t, 4, extranonce1_s);

  string tellornonce = extranonce1_s + extranonce;
  share.set_tellornonce(tellornonce);
  DLOG(INFO) << "sessionid : " << std::hex << sessionId
             << "\nextranonce : " << extraNonce2
             << "\nTellornonce : " << tellornonce;

  share.set_sessionid(sessionId); // TODO: fix it, set as real session id.
  share.set_username(worker.userName_);
  share.set_workername(workerName());

  IpAddress ip;
  ip.fromIpv4Int(session.getClientIp());
  share.set_ip(ip.toString());

  LocalShare localShare(extraNonce2, sessionId, JobId);
  // can't add local share
  if (!localJob->addLocalShare(localShare)) {
    handleShare(
        idStr,
        StratumStatus::DUPLICATE_SHARE,
        jobDiff.currentJobDiff_,
        localJob->chainId_);
    // add invalid share to counter
    invalidSharesCounter_.insert((int64_t)time(nullptr), 1);
    return;
  }
  DLOG(INFO) << " share received : " << share.toString();

  uint256 blockHash;
  server.checkAndUpdateShare(
      localJob->chainId_,
      share,
      exjob,
      jobDiff.jobDiffs_,
      worker.fullName_,
      blockHash);

  // we send share to kafka by default, but if there are lots of invalid
  // shares in a short time, we just drop them.
  if (handleShare(
          idStr, share.status(), share.sharediff(), localJob->chainId_)) {
    if (StratumStatus::isSolved(share.status())) {
      server.sendSolvedShare2Kafka(
          localJob->chainId_, share, exjob, worker, blockHash);
      // mark jobs as stale
      server.GetJobRepository(localJob->chainId_)
          ->markAllJobsAsStale(sjob->height());
    }
  } else {
    // check if there is invalid share spamming
    int64_t invalidSharesNum = invalidSharesCounter_.sum(
        time(nullptr), INVALID_SHARE_SLIDING_WINDOWS_SIZE);
    // too much invalid shares, don't send them to kafka
    if (invalidSharesNum >= INVALID_SHARE_SLIDING_WINDOWS_MAX_LIMIT) {
      LOG(WARNING) << "invalid share spamming, worker: " << worker.fullName_
                   << ", " << share.toString();
      return;
    }
  }

  std::string message;
  if (!share.SerializeToStringWithVersion(message)) {
    LOG(ERROR) << "share SerializeToStringWithVersion failed!"
               << share.toString();
    return;
  }
  server.sendShare2Kafka(localJob->chainId_, message.data(), message.size());
}

uint64_t StratumMinerTellor::calcCurDiff() {
  auto &session = getSession();
  auto &server = session.getServer();
  curDiff_ =
      tellor::calcCurDiff(session.getNetWorkDiff(), server.minDifficulty_);
  ;
  return curDiff_;
}

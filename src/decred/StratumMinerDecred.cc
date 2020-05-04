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

#include "StratumMinerDecred.h"

#include "StratumSessionDecred.h"
#include "StratumServerDecred.h"
#include "StratumDecred.h"
#include "StratumMessageDispatcher.h"
#include "DiffController.h"

#include <boost/endian/conversion.hpp>

StratumMinerDecred::StratumMinerDecred(
    StratumSessionDecred &session,
    const DiffController &diffController,
    const std::string &clientAgent,
    const std::string &workerName,
    int64_t workerId)
  : StratumMinerBase(
        session, diffController, clientAgent, workerName, workerId) {
}

void StratumMinerDecred::handleRequest(
    const std::string &idStr,
    const std::string &method,
    const JsonNode &jparams,
    const JsonNode &jroot) {
  if (method == "mining.submit") {
    handleRequest_Submit(idStr, jparams);
  }
}

void StratumMinerDecred::handleRequest_Submit(
    const string &idStr, const JsonNode &jparams) {
  auto &session = getSession();
  if (session.getState() != StratumSession::AUTHENTICATED) {
    handleShare(idStr, StratumStatus::UNAUTHORIZED, 0, session.getChainId());

    // there must be something wrong, send reconnect command
    string s = "{\"id\":null,\"method\":\"client.reconnect\",\"params\":[]}\n";
    session.sendData(s);
    return;
  }

  //  params[0] = Worker Name
  //  params[1] = Job ID
  //  params[2] = ExtraNonce 2
  //  params[3] = nTime
  //  params[4] = nonce
  if (jparams.children()->size() < 5 ||
      std::any_of(
          std::next(jparams.children()->begin()),
          jparams.children()->end(),
          [](const JsonNode &n) {
            return n.type() != Utilities::JS::type::Str || !IsHex(n.str());
          })) {
    handleShare(idStr, StratumStatus::ILLEGAL_PARARMS, 0, session.getChainId());
    return;
  }

  auto extraNonce2 = ParseHex(jparams.children()->at(2).str());
  if (extraNonce2.size() != kExtraNonce2Size_ &&
      extraNonce2.size() != 12) { // Extra nonce size
    handleShare(idStr, StratumStatus::ILLEGAL_PARARMS, 0, session.getChainId());
    return;
  }

  auto shortJobId =
      static_cast<uint8_t>(jparams.children()->at(1).uint32_hex());
  auto ntime = jparams.children()->at(3).uint32_hex();
  auto nonce = jparams.children()->at(4).uint32_hex();

  auto &server = session.getServer();
  auto &worker = session.getWorker();

  // a function to log rejected stale share
  auto rejectJobNotFound = [&]() {
    session.responseError(idStr, StratumStatus::JOB_NOT_FOUND);

    LOG(INFO) << "rejected share: "
              << StratumStatus::toString(StratumStatus::JOB_NOT_FOUND)
              << ", worker: " << worker.fullName_ << ", Share(id: " << idStr
              << ", shortJobId: " << static_cast<uint16_t>(shortJobId)
              << ", nTime: " << ntime << "/" << date("%F %T", ntime) << ")";
  };

  auto localJob = session.findLocalJob(shortJobId);
  if (!localJob) {
    // if can't find localJob, could do nothing
    rejectJobNotFound();
    return;
  }

  auto jobRepo = server.GetJobRepository(localJob->chainId_);
  auto exjob = jobRepo->getStratumJobEx(localJob->jobId_);
  if (exjob == nullptr) {
    // If can't find exjob, could do nothing too.
    //
    // This situation can occur after LocalJobs has been expanded to 256.
    // But in this case, the miner apparently submitted a very outdated job.
    // So we don't have to risk that the code might crash (subsequent code
    // forgets to check for null pointers) and send it to kafka.
    //
    // Tips: an exjob is only removed after max_job_lifetime seconds past.
    //
    rejectJobNotFound();
    return;
  }

  auto sjob = std::static_pointer_cast<StratumJobDecred>(exjob->sjob_);

  // 0 means miner use stratum job's default block time
  if (ntime == 0) {
    ntime = sjob->header_.timestamp.value();
  }

  auto iter = jobDiffs_.find(localJob);
  if (iter == jobDiffs_.end()) {
    handleShare(idStr, StratumStatus::JOB_NOT_FOUND, 0, localJob->chainId_);
    LOG(ERROR) << "can't find session's diff, worker: " << worker.fullName_;
    return;
  }

  ShareDecred share(
      workerId_,
      worker.userId(localJob->chainId_),
      session.getClientIp(),
      localJob->jobId_,
      iter->second,
      localJob->blkBits_,
      sjob->header_.height.value(),
      nonce,
      session.getSessionId());

  // we send share to kafka by default, but if there are lots of invalid
  // shares in a short time, we just drop them.
  bool isSendShareToKafka = true;

  LocalShareType localShare(
      reinterpret_cast<boost::endian::little_uint64_buf_t *>(extraNonce2.data())
          ->value(),
      nonce,
      ntime);

  // can't find local share
  if (!localJob->addLocalShare(localShare)) {
    share.set_status(StratumStatus::DUPLICATE_SHARE);
  } else {
    server.checkAndUpdateShare(
        share, exjob, extraNonce2, ntime, nonce, worker.fullName_);
  }

  if (!handleShare(
          idStr, share.status(), share.sharediff(), localJob->chainId_)) {
    // add invalid share to counter
    invalidSharesCounter_.insert(static_cast<int64_t>(time(nullptr)), 1);
  }

  DLOG(INFO) << share.toString();

  if (!StratumStatus::isAccepted(share.status())) {
    // log all rejected share to answer "Why the rejection rate of my miner
    // increased?"
    LOG(INFO) << "rejected share: " << StratumStatus::toString(share.status())
              << ", worker: " << worker.fullName_ << ", " << share.toString();

    // check if thers is invalid share spamming
    int64_t invalidSharesNum = invalidSharesCounter_.sum(
        time(nullptr), INVALID_SHARE_SLIDING_WINDOWS_SIZE);
    // too much invalid shares, don't send them to kafka
    if (invalidSharesNum >= INVALID_SHARE_SLIDING_WINDOWS_MAX_LIMIT) {
      isSendShareToKafka = false;
      LOG(WARNING) << "invalid share spamming, worker: " << worker.fullName_
                   << ", " << share.toString();
    }
  }

  if (isSendShareToKafka) {

    std::string message;
    if (!share.SerializeToStringWithVersion(message)) {
      LOG(ERROR) << "share SerializeToStringWithVersion failed!"
                 << share.toString();
      return;
    }

    server.sendShare2Kafka(localJob->chainId_, message.data(), message.size());
  }
  return;
}

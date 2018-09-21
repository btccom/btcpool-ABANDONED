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

StratumMinerDecred::StratumMinerDecred(StratumSessionDecred &session,
                                       const DiffController &diffController,
                                       const std::string &clientAgent,
                                       const std::string &workerName,
                                       int64_t workerId)
    : StratumMinerBase(session, diffController, clientAgent, workerName, workerId) {
}

void StratumMinerDecred::handleRequest(const std::string &idStr,
                                       const std::string &method,
                                       const JsonNode &jparams,
                                       const JsonNode &jroot) {
  if (method == "mining.submit") {
    handleRequest_Submit(idStr, jparams);
  }
}

void StratumMinerDecred::handleRequest_Submit(const string &idStr, const JsonNode &jparams) {
  auto &session = getSession();
  if (session.getState() != StratumSession::AUTHENTICATED) {
    session.responseError(idStr, StratumStatus::UNAUTHORIZED);

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
      std::any_of(std::next(jparams.children()->begin()), jparams.children()->end(),
                  [](const JsonNode &n) { return n.type() != Utilities::JS::type::Str || !IsHex(n.str()); })) {
    session.responseError(idStr, StratumStatus::ILLEGAL_PARARMS);
    return;
  }

  auto extraNonce2 = ParseHex(jparams.children()->at(2).str());
  if (extraNonce2.size() != kExtraNonce2Size_ &&
      extraNonce2.size() != 12) { // Extra nonce size
    session.responseError(idStr, StratumStatus::ILLEGAL_PARARMS);
    return;
  }

  auto shortJobId = static_cast<uint8_t>(jparams.children()->at(1).uint32_hex());
  auto ntime = jparams.children()->at(3).uint32_hex();
  auto nonce = jparams.children()->at(4).uint32_hex();

  auto &server = session.getServer();
  auto &worker = session.getWorker();
  auto jobRepo = server.GetJobRepository();

  auto localJob = session.findLocalJob(shortJobId);
  if (!localJob) {
    // if can't find localJob, could do nothing
    session.responseError(idStr, StratumStatus::JOB_NOT_FOUND);

    LOG(INFO) << "rejected share: " << StratumStatus::toString(StratumStatus::JOB_NOT_FOUND)
              << ", worker: " << worker.fullName_ << ", Share(id: " << idStr << ", shortJobId: "
              << static_cast<uint16_t>(shortJobId) << ", nTime: " << ntime << "/" << date("%F %T", ntime) << ")";
    return;
  }

  auto iter = jobDiffs_.find(localJob);
  if (iter == jobDiffs_.end()) {
    LOG(ERROR) << "can't find session's diff, worker: " << worker.fullName_;
    return;
  }
  auto clientIp = session.getClientIp();

  uint32_t height = 0;
  auto exjob = jobRepo->getStratumJobEx(localJob->jobId_);
  if (exjob) {
    // 0 means miner use stratum job's default block time
    auto sjob = static_cast<StratumJobDecred *>(exjob->sjob_);
    if (ntime == 0) {
      ntime = sjob->header_.timestamp.value();
    }

    height = sjob->header_.height.value();
  }

  ShareDecred share(workerId_,
                    worker.userId_,
                    clientIp,
                    localJob->jobId_,
                    iter->second,
                    localJob->blkBits_,
                    height,
                    nonce,
                    session.getSessionId());

  // we send share to kafka by default, but if there are lots of invalid
  // shares in a short time, we just drop them.
  bool isSendShareToKafka = true;

  LocalShare
      localShare(reinterpret_cast<boost::endian::little_uint64_buf_t *>(extraNonce2.data())->value(), nonce, ntime);

  // can't find local share
  if (!localJob->addLocalShare(localShare)) {
    share.status_ = StratumStatus::DUPLICATE_SHARE;
  } else {
    share.status_ = server.checkShare(share, exjob, extraNonce2, ntime, nonce, worker.fullName_);
  }

  if (!handleShare(idStr, share.status_, share.shareDiff_)) {
    // add invalid share to counter
    invalidSharesCounter_.insert(static_cast<int64_t>(time(nullptr)), 1);
  }

  DLOG(INFO) << share.toString();

  if (!StratumStatus::isAccepted(share.status_)) {
    // log all rejected share to answer "Why the rejection rate of my miner increased?"
    LOG(INFO) << "rejected share: " << StratumStatus::toString(share.status_)
              << ", worker: " << worker.fullName_ << ", " << share.toString();

    // check if thers is invalid share spamming
    int64_t invalidSharesNum = invalidSharesCounter_.sum(time(nullptr),
                                                         INVALID_SHARE_SLIDING_WINDOWS_SIZE);
    // too much invalid shares, don't send them to kafka
    if (invalidSharesNum >= INVALID_SHARE_SLIDING_WINDOWS_MAX_LIMIT) {
      isSendShareToKafka = false;

      LOG(INFO) << "invalid share spamming, diff: " << share.shareDiff_ << ", worker: "
                << worker.fullName_ << ", agent: " << clientAgent_ << ", ip: " << clientIp;
    }
  }

  if (isSendShareToKafka) {
    share.checkSum_ = share.checkSum();
    server.sendShare2Kafka(reinterpret_cast<const uint8_t *>(&share), sizeof(ShareDecred));
  }
  return;
}

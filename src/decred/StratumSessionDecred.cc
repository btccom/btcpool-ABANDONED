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

#include "StratumSessionDecred.h"
#include "StratumServerDecred.h"
#include "StratumDecred.h"
#include "DiffController.h"
#include <boost/endian/conversion.hpp>

StratumSessionDecred::StratumSessionDecred(evutil_socket_t fd, bufferevent *bev,
                                           ServerDecred *server, sockaddr *saddr,
                                           int32_t shareAvgSeconds, uint32_t extraNonce1)
  : StratumSessionBase<ServerDecred>(fd, bev, server, saddr, shareAvgSeconds, extraNonce1)
{
}

void StratumSessionDecred::sendMiningNotify(shared_ptr<StratumJobEx> exJobPtr, bool isFirstJob)
{
  if (state_ < AUTHENTICATED || exJobPtr == nullptr)
  {
    LOG(ERROR) << "decred sendMiningNotify failed, state = " << state_;
    return;
  }

  StratumJobDecred *jobDecred = dynamic_cast<StratumJobDecred *>(exJobPtr->sjob_);
  if (nullptr == jobDecred)
  {
    LOG(ERROR) << "Invalid job type, jobId = " << exJobPtr->sjob_->jobId_;
    return;
  }

  localJobs_.push_back(LocalJob());
  LocalJob &ljob = *(localJobs_.rbegin());
  ljob.blkBits_ = jobDecred->header_.nBits.value();
  ljob.jobId_ = jobDecred->jobId_;
  ljob.shortJobId_ = allocShortJobId();
  ljob.jobDifficulty_ = diffController_->calcCurDiff();

  // set difficulty
  if (currDiff_ != ljob.jobDifficulty_) {
    sendSetDifficulty(ljob.jobDifficulty_);
    currDiff_ = ljob.jobDifficulty_;
  }

  // PrevHash field is int32 reversed
  auto prevHash = reinterpret_cast<const boost::endian::little_uint32_buf_t *>(jobDecred->header_.prevBlock.begin());
  auto notifyStr = Strings::Format("{\"id\":null,\"jsonrpc\":\"2.0\",\"method\":\"mining.notify\","
                                   "\"params\":[\"%04" PRIx8 "\",\"%08x%08x%08x%08x%08x%08x%08x%08x\",\"%s00000000\",\"%s\",[],\"%s\",\"%" PRIx32 "\",\"%" PRIx32 "\",%s]}\n",
                                   ljob.shortJobId_,
                                   prevHash[0].value(),
                                   prevHash[1].value(),
                                   prevHash[2].value(),
                                   prevHash[3].value(),
                                   prevHash[4].value(),
                                   prevHash[5].value(),
                                   prevHash[6].value(),
                                   prevHash[7].value(),
                                   jobDecred->getCoinBase1().c_str(),
                                   HexStr(BEGIN(jobDecred->header_.stakeVersion), END(jobDecred->header_.stakeVersion)).c_str(),
                                   HexStr(BEGIN(jobDecred->header_.version), END(jobDecred->header_.version)).c_str(),
                                   jobDecred->header_.nBits.value(),
                                   jobDecred->header_.timestamp.value(),
                                   exJobPtr->isClean_ ? "true" : "false");
  sendData(notifyStr);

  // clear localJobs_
  clearLocalJobs();
}

void StratumSessionDecred::handleRequest_Subscribe(const string &idStr, const JsonNode &jparams)
{
  if (state_ != CONNECTED) {
    responseError(idStr, StratumStatus::UNKNOWN);
    return;
  }

#ifdef WORK_WITH_STRATUM_SWITCHER

  //
  // For working with StratumSwitcher, the ExtraNonce1 must be provided as param 2.
  //
  //  params[0] = client version           [require]
  //  params[1] = session id / ExtraNonce1 [require]
  //  params[2] = miner's real IP (unit32) [optional]
  //
  //  StratumSwitcher request eg.:
  //  {"id": 1, "method": "mining.subscribe", "params": ["StratumSwitcher/0.1", "01ad557d", 203569230]}
  //  203569230 -> 12.34.56.78
  //

  if (jparams.children()->size() < 2) {
    responseError(idStr, StratumStatus::ILLEGAL_PARARMS);
    return;
  }

  state_ = SUBSCRIBED;

  clientAgent_ = jparams.children()->at(0).str().substr(0, 30);  // 30 is max len
  clientAgent_ = filterWorkerName(clientAgent_);

  string extraNonce1Str = jparams.children()->at(1).str().substr(0, 8);  // 8 is max len
  sscanf(extraNonce1Str.c_str(), "%x", &extraNonce1_); // convert hex to int

  // receive miner's IP from stratumSwitcher
  if (jparams.children()->size() >= 3) {
    clientIpInt_ = htonl(jparams.children()->at(2).uint32());

    // ipv4
    clientIp_.resize(INET_ADDRSTRLEN);
    struct in_addr addr;
    addr.s_addr = clientIpInt_;
    clientIp_ = inet_ntop(AF_INET, &addr, (char *)clientIp_.data(), (socklen_t)clientIp_.size());
    LOG(INFO) << "client real IP: " << clientIp_;
  }

#else

  state_ = SUBSCRIBED;

  //
  //  params[0] = client version     [optional]
  //
  // client request eg.:
  //  {"id": 1, "method": "mining.subscribe", "params": ["gominer/0.2.0-decred"]}
  //
  if (jparams.children()->size() >= 1) {
    clientAgent_ = jparams.children()->at(0).str().substr(0, 30);  // 30 is max len
    clientAgent_ = filterWorkerName(clientAgent_);
  }

#endif // WORK_WITH_STRATUM_SWITCHER


  //  result[0] = 2-tuple with name of subscribed notification and subscription ID.
  //              Theoretically it may be used for unsubscribing, but obviously miners won't use it.
  //  result[1] = ExtraNonce1, used for building the coinbase. There are 2 variants of miners known
  //              to us: one will take first 4 bytes and another will take last four bytes so we put
  //              the value on both places.
  //  result[2] = ExtraNonce2_size, the number of bytes that the miner users for its ExtraNonce2 counter
  assert(kExtraNonce2Size_ == 8);
  auto extraNonce1Reversed = boost::endian::endian_reverse(extraNonce1_);
  const string s = Strings::Format("{\"id\":%s,\"result\":[[[\"mining.set_difficulty\",\"1\"]"
                                   ",[\"mining.notify\",\"%08x\"]],\"%08x00000000%08x\",%d],\"error\":null}\n",
                                   idStr.c_str(), extraNonce1_, extraNonce1Reversed, extraNonce1Reversed, kExtraNonce2Size_);
  sendData(s);
}

void StratumSessionDecred::handleRequest_Authorize(const string &idStr, const JsonNode &jparams, const JsonNode &jroot)
{
  if (state_ != SUBSCRIBED)
  {
    responseError(idStr, StratumStatus::NOT_SUBSCRIBED);
    return;
  }

  //
  //  params[0] = user[.worker]
  //  params[1] = password
  //  eg. {"params": ["slush.miner1", "password"], "id": 2, "method": "mining.authorize"}
  //  the password may be omitted.
  //  eg. {"params": ["slush.miner1"], "id": 2, "method": "mining.authorize"}
  //
  if (jparams.children()->size() < 1)
  {
    responseError(idStr, StratumStatus::INVALID_USERNAME);
    return;
  }

  string fullName = jparams.children()->at(0).str();
  string password;
  if (jparams.children()->size() > 1)
  {
    password = jparams.children()->at(1).str();
  }

  checkUserAndPwd(idStr, fullName, password);
}

void StratumSessionDecred::handleRequest_Submit(const string &idStr, const JsonNode &jparams)
{
  if (state_ != AUTHENTICATED) {
    responseError(idStr, StratumStatus::UNAUTHORIZED);

    // there must be something wrong, send reconnect command
    string s = "{\"id\":null,\"method\":\"client.reconnect\",\"params\":[]}\n";
    sendData(s);

    return;
  }

  //  params[0] = Worker Name
  //  params[1] = Job ID
  //  params[2] = ExtraNonce 2
  //  params[3] = nTime
  //  params[4] = nonce
  if (jparams.children()->size() < 5 ||
      std::any_of(std::next(jparams.children()->begin()), jparams.children()->end(),
                  [](const JsonNode& n){ return n.type() != Utilities::JS::type::Str || !IsHex(n.str()); })) {
    responseError(idStr, StratumStatus::ILLEGAL_PARARMS);
    return;
  }

  auto extraNonce2 = ParseHex(jparams.children()->at(2).str());
  if (extraNonce2.size() != kExtraNonce2Size_ &&
      extraNonce2.size() != 12) { // Extra nonce size
    responseError(idStr, StratumStatus::ILLEGAL_PARARMS);
    return;
  }

  auto shortJobId = static_cast<uint8_t>(jparams.children()->at(1).uint32_hex());
  auto ntime = jparams.children()->at(3).uint32_hex();
  auto nonce = jparams.children()->at(4).uint32_hex();

  auto server = GetServer();
  auto jobRepo = server->GetJobRepository();

  LocalJob *localJob = findLocalJob(shortJobId);
  if (!localJob) {
    // if can't find localJob, could do nothing
    responseError(idStr, StratumStatus::JOB_NOT_FOUND);

    LOG(INFO) << "rejected share: " << StratumStatus::toString(StratumStatus::JOB_NOT_FOUND)
              << ", worker: " << worker_.fullName_ << ", Share(id: " << idStr << ", shortJobId: "
              << static_cast<uint16_t>(shortJobId) << ", nTime: " << ntime << "/" << date("%F %T", ntime) << ")";
    return;
  }

  uint32_t height = 0;
  auto exjob = jobRepo->getStratumJobEx(localJob->jobId_);
  if (exjob) {
    // 0 means miner use stratum job's default block time
    auto sjob = static_cast<StratumJobDecred*>(exjob->sjob_);
    if (ntime == 0) {
        ntime = sjob->header_.timestamp.value();
    }

    height = sjob->header_.height.value();
  }

  ShareDecred share(worker_.workerHashId_, worker_.userId_, clientIpInt_, localJob->jobId_, localJob->jobDifficulty_, localJob->blkBits_, height, nonce, extraNonce1_);

  // we send share to kafka by default, but if there are lots of invalid
  // shares in a short time, we just drop them.
  bool isSendShareToKafka = true;

  LocalShare localShare(reinterpret_cast<boost::endian::little_uint64_buf_t *>(extraNonce2.data())->value(), nonce, ntime);

  // can't find local share
  if (!localJob->addLocalShare(localShare)) {
    share.status_ = StratumStatus::DUPLICATE_SHARE;
    responseError(idStr, share.status_);

    // add invalid share to counter
    invalidSharesCounter_.insert(static_cast<int64_t>(time(nullptr)), 1);

    goto finish;
  }

  share.status_ = server->checkShare(share, exjob, extraNonce2, ntime, nonce, worker_.fullName_);

  // accepted share
  if (StratumStatus::isAccepted(share.status_)) {
    diffController_->addAcceptedShare(share.shareDiff_);
    responseTrue(idStr);
  } else {
    // reject share
    responseError(idStr, share.status_);

    // add invalid share to counter
    invalidSharesCounter_.insert(static_cast<int64_t>(time(nullptr)), 1);
  }


finish:
  DLOG(INFO) << share.toString();

  if (!StratumStatus::isAccepted(share.status_)) {
    // log all rejected share to answer "Why the rejection rate of my miner increased?"
    LOG(INFO) << "rejected share: " << StratumStatus::toString(share.status_)
              << ", worker: " << worker_.fullName_ << ", " << share.toString();

    // check if thers is invalid share spamming
    int64_t invalidSharesNum = invalidSharesCounter_.sum(time(nullptr),
                                                         INVALID_SHARE_SLIDING_WINDOWS_SIZE);
    // too much invalid shares, don't send them to kafka
    if (invalidSharesNum >= INVALID_SHARE_SLIDING_WINDOWS_MAX_LIMIT) {
      isSendShareToKafka = false;

      LOG(INFO) << "invalid share spamming, diff: "<< share.shareDiff_ << ", worker: "
                << worker_.fullName_ << ", agent: " << clientAgent_ << ", ip: " << clientIp_;
    }
  }

  if (isSendShareToKafka) {
    share.checkSum_ = share.checkSum();
    GetServer()->sendShare2Kafka(reinterpret_cast<const uint8_t *>(&share), sizeof(ShareDecred));
  }
  return;
}

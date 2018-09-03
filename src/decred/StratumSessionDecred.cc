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
  //  result[1] = ExtraNonce1, used for building the coinbase.
  //  result[2] = ExtraNonce2_size, the number of bytes that the miner users for its ExtraNonce2 counter
  assert(kExtraNonce2Size_ == 8);
  const string s = Strings::Format("{\"id\":%s,\"result\":[[[\"mining.set_difficulty\",\"1\"]"
                                   ",[\"mining.notify\",\"%08x\"]],\"%08x\",%d],\"error\":null}\n",
                                   idStr.c_str(), extraNonce1_, extraNonce1_, kExtraNonce2Size_);
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
}

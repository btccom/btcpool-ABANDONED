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

#include "StratumSessionDecred.h"

#include "StratumMessageDispatcher.h"
#include "StratumMinerDecred.h"
#include "DiffController.h"

#include "bitcoin/CommonBitcoin.h"

StratumSessionDecred::StratumSessionDecred(
    ServerDecred &server,
    struct bufferevent *bev,
    struct sockaddr *saddr,
    uint32_t extraNonce1,
    const StratumProtocolDecred &protocol)
  : StratumSessionBase(server, bev, saddr, extraNonce1)
  , protocol_(protocol)
  , shortJobId_(0) {
}

void StratumSessionDecred::sendSetDifficulty(
    LocalJob &localJob, uint64_t difficulty) {
  string s;
  if (!server_.isDevModeEnable_) {
    s = Strings::Format(
        "{\"id\":null,\"method\":\"mining.set_difficulty\""
        ",\"params\":[%" PRIu64 "]}\n",
        difficulty);
  } else {
    s = Strings::Format(
        "{\"id\":null,\"method\":\"mining.set_difficulty\""
        ",\"params\":[%.3f]}\n",
        server_.devFixedDifficulty_);
  }

  sendData(s);
}

void StratumSessionDecred::sendMiningNotify(
    shared_ptr<StratumJobEx> exJobPtr, bool isFirstJob) {
  if (state_ < AUTHENTICATED || exJobPtr == nullptr) {
    LOG(ERROR) << "decred sendMiningNotify failed, state = " << state_;
    return;
  }

  auto jobDecred = std::static_pointer_cast<StratumJobDecred>(exJobPtr->sjob_);
  if (nullptr == jobDecred) {
    LOG(ERROR) << "Invalid job type, jobId = " << exJobPtr->sjob_->jobId_;
    return;
  }

  auto &ljob = addLocalJob(
      exJobPtr->chainId_,
      jobDecred->jobId_,
      shortJobId_++,
      jobDecred->header_.nBits.value());

  // PrevHash field is int32 reversed
  auto prevHash = reinterpret_cast<const boost::endian::little_uint32_buf_t *>(
      jobDecred->header_.prevBlock.begin());
  auto notifyStr = Strings::Format(
      "{\"id\":null,\"jsonrpc\":\"2.0\",\"method\":\"mining.notify\","
      "\"params\":[\"%04x"
      "\",\"%08x%08x%08x%08x%08x%08x%08x%08x\",\"%s00000000\",\"%s\",[],\"%s\","
      "\"%x\",\"%x\",%s]}\n",
      ljob.shortJobId_,
      prevHash[0].value(),
      prevHash[1].value(),
      prevHash[2].value(),
      prevHash[3].value(),
      prevHash[4].value(),
      prevHash[5].value(),
      prevHash[6].value(),
      prevHash[7].value(),
      jobDecred->getCoinBase1(),
      HexStr(
          BEGIN(jobDecred->header_.stakeVersion),
          END(jobDecred->header_.stakeVersion)),
      HexStr(
          BEGIN(jobDecred->header_.version), END(jobDecred->header_.version)),
      jobDecred->header_.nBits.value(),
      jobDecred->header_.timestamp.value(),
      exJobPtr->isClean_ ? "true" : "false");
  sendData(notifyStr);

  // clear localJobs_
  clearLocalJobs(exJobPtr->isClean_);
}

void StratumSessionDecred::handleRequest(
    const std::string &idStr,
    const std::string &method,
    const JsonNode &jparams,
    const JsonNode &jroot) {
  if (method == "mining.subscribe") {
    handleRequest_Subscribe(idStr, jparams, jroot);
  } else if (method == "mining.authorize") {
    handleRequest_Authorize(idStr, jparams, jroot);
  } else {
    dispatcher_->handleRequest(idStr, method, jparams, jroot);
  }
}

void StratumSessionDecred::handleRequest_Subscribe(
    const string &idStr, const JsonNode &jparams, const JsonNode &jroot) {
  if (state_ != CONNECTED) {
    responseError(idStr, StratumStatus::UNKNOWN);
    return;
  }

#ifdef WORK_WITH_STRATUM_SWITCHER

  //
  // For working with StratumSwitcher, the ExtraNonce1 must be provided as
  // param 2.
  //
  //  params[0] = client version           [require]
  //  params[1] = session id / ExtraNonce1 [require]
  //  params[2] = miner's real IP (unit32) [optional]
  //
  //  StratumSwitcher request eg.:
  //  {"id": 1, "method": "mining.subscribe", "params": ["StratumSwitcher/0.1",
  //  "01ad557d", 203569230]} 203569230 -> 12.34.56.78
  //

  if (jparams.children()->size() < 2) {
    responseError(idStr, StratumStatus::CLIENT_IS_NOT_SWITCHER);
    LOG(ERROR) << "A non-switcher subscribe request is detected and rejected.";
    LOG(ERROR) << "Cmake option POOL__WORK_WITH_STRATUM_SWITCHER enabled, you "
                  "can only connect to the sserver via a stratum switcher.";
    return;
  }

  state_ = SUBSCRIBED;

  setClientAgent(
      jparams.children()->at(0).str().substr(0, 30)); // 30 is max len

  string extNonce1Str =
      jparams.children()->at(1).str().substr(0, 8); // 8 is max len
  sscanf(extNonce1Str.c_str(), "%x", &sessionId_); // convert hex to int

  // receive miner's IP from stratumSwitcher
  if (jparams.children()->size() >= 3) {
    clientIpInt_ = htonl(jparams.children()->at(2).uint32());

    // ipv4
    clientIp_.resize(INET_ADDRSTRLEN);
    struct in_addr addr;
    addr.s_addr = clientIpInt_;
    clientIp_ = inet_ntop(
        AF_INET, &addr, (char *)clientIp_.data(), (socklen_t)clientIp_.size());
    LOG(INFO) << "client real IP: " << clientIp_;
  }

#else

  state_ = SUBSCRIBED;

  //
  //  params[0] = client version     [optional]
  //
  // client request eg.:
  //  {"id": 1, "method": "mining.subscribe", "params":
  //  ["gominer/0.2.0-decred"]}
  //
  if (jparams.children()->size() >= 1) {
    setClientAgent(
        jparams.children()->at(0).str().substr(0, 30)); // 30 is max len
  }

#endif // WORK_WITH_STRATUM_SWITCHER

  //  result[0] = 2-tuple with name of subscribed notification and subscription
  //  ID.
  //              Theoretically it may be used for unsubscribing, but obviously
  //              miners won't use it.
  //  result[1] = ExtraNonce1, used for building the coinbase. There are 2
  //  variants of miners known
  //              to us: one will take first 4 bytes and another will take last
  //              four bytes so we put the value on both places.
  //  result[2] = ExtraNonce2_size, the number of bytes that the miner users for
  //  its ExtraNonce2 counter
  auto extraNonce1Str = protocol_.getExtraNonce1String(sessionId_);
  const string s = Strings::Format(
      "{\"id\":%s,\"result\":[[[\"mining.set_difficulty\",\"%08x\"]"
      ",[\"mining.notify\",\"%08x\"]],\"%s\",%d],\"error\":null}\n",
      idStr,
      sessionId_,
      sessionId_,
      extraNonce1Str,
      Strings::Value(StratumMiner::kExtraNonce2Size_));
  sendData(s);
}

void StratumSessionDecred::handleRequest_Authorize(
    const string &idStr, const JsonNode &jparams, const JsonNode &jroot) {
  if (state_ != SUBSCRIBED) {
    responseError(idStr, StratumStatus::NOT_SUBSCRIBED);
    return;
  }

  //
  //  params[0] = user[.worker]
  //  params[1] = password
  //  eg. {"params": ["slush.miner1", "password"], "id": 2, "method":
  //  "mining.authorize"} the password may be omitted. eg. {"params":
  //  ["slush.miner1"], "id": 2, "method": "mining.authorize"}
  //
  if (jparams.children()->size() < 1) {
    responseError(idStr, StratumStatus::INVALID_USERNAME);
    return;
  }

  string fullName, password;

  fullName = jparams.children()->at(0).str();
  if (jparams.children()->size() > 1) {
    password = jparams.children()->at(1).str();
  }

  checkUserAndPwd(idStr, fullName, password);
  return;
}

unique_ptr<StratumMiner> StratumSessionDecred::createMiner(
    const std::string &clientAgent,
    const std::string &workerName,
    int64_t workerId) {
  return std::make_unique<StratumMinerDecred>(
      *this,
      *getServer().defaultDifficultyController_,
      clientAgent,
      workerName,
      workerId);
}

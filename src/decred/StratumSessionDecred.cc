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

#include <boost/make_unique.hpp>

StratumSessionDecred::StratumSessionDecred(ServerDecred &server,
                                           struct bufferevent *bev,
                                           struct sockaddr *saddr,
                                           uint32_t extraNonce1,
                                           const StratumProtocolDecred &protocol)
    : StratumSessionBase(server, bev, saddr, extraNonce1)
    , protocol_(protocol)
    , shortJobId_(0) {
}

void StratumSessionDecred::sendMiningNotify(shared_ptr<StratumJobEx> exJobPtr, bool isFirstJob) {
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

  auto &ljob = addLocalJob(jobDecred->jobId_, shortJobId_++, jobDecred->header_.nBits.value());

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

void StratumSessionDecred::handleRequest_Subscribe(const string &idStr,
                                                      const JsonNode &jparams,
                                                      const JsonNode &jroot) {
  if (state_ != CONNECTED) {
    responseError(idStr, StratumStatus::UNKNOWN);
    return;
  }

  state_ = SUBSCRIBED;

  //
  //  params[0] = client version     [optional]
  //
  // client request eg.:
  //  {"id": 1, "method": "mining.subscribe", "params": ["gominer/0.2.0-decred"]}
  //
  if (jparams.children()->size() >= 1) {
    setClientAgent(jparams.children()->at(0).str().substr(0, 30));  // 30 is max len
  }

  //  result[0] = 2-tuple with name of subscribed notification and subscription ID.
  //              Theoretically it may be used for unsubscribing, but obviously miners won't use it.
  //  result[1] = ExtraNonce1, used for building the coinbase. There are 2 variants of miners known
  //              to us: one will take first 4 bytes and another will take last four bytes so we put
  //              the value on both places.
  //  result[2] = ExtraNonce2_size, the number of bytes that the miner users for its ExtraNonce2 counter
  auto extraNonce1Str = protocol_.getExtraNonce1String(extraNonce1_);
  const string s = Strings::Format("{\"id\":%s,\"result\":[[[\"mining.set_difficulty\",\"1\"]"
                                   ",[\"mining.notify\",\"%08x\"]],\"%s\",%d],\"error\":null}\n",
                                   idStr.c_str(), extraNonce1_, extraNonce1Str.c_str(), StratumMiner::kExtraNonce2Size_);
  sendData(s);
}

bool StratumSessionDecred::handleRequest_Authorize(const string &idStr,
                                                      const JsonNode &jparams,
                                                      const JsonNode &jroot,
                                                      string &fullName,
                                                      string &password)
{
  if (state_ != SUBSCRIBED)
  {
    responseError(idStr, StratumStatus::NOT_SUBSCRIBED);
    return false;
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
    return false;
  }

  fullName = jparams.children()->at(0).str();
  if (jparams.children()->size() > 1)
  {
    password = jparams.children()->at(1).str();
  }
  return true;
}

unique_ptr<StratumMiner> StratumSessionDecred::createMiner(const std::string &clientAgent,
                                                              const std::string &workerName,
                                                              int64_t workerId) {
  return boost::make_unique<StratumMinerDecred>(*this,
                                                *getServer().defaultDifficultyController_,
                                                clientAgent,
                                                workerName,
                                                workerId);
}

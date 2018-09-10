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
#include "StratumSessionSia.h"
#include "DiffController.h"

#include "bitcoin/StratumBitcoin.h"
#include "eth/CommonEth.h"

#include <arith_uint256.h>

///////////////////////////////// StratumSessionSia ////////////////////////////////
StratumSessionSia::StratumSessionSia(evutil_socket_t fd,
                                     struct bufferevent *bev,
                                     ServerSia *server,
                                     struct sockaddr *saddr,
                                     const int32_t shareAvgSeconds,
                                     const uint32_t extraNonce1) 
  : StratumSessionBase(fd, bev, server, saddr, shareAvgSeconds, extraNonce1)
  , shortJobId_(0)
{
}

void StratumSessionSia::handleRequest_Subscribe(const string &idStr, const JsonNode &jparams)
{
  if (state_ != CONNECTED)
  {
    responseError(idStr, StratumStatus::UNKNOWN);
    return;
  }

  state_ = SUBSCRIBED;

  const string s = Strings::Format("{\"id\":%s,\"jsonrpc\":\"2.0\",\"result\":true}\n", idStr.c_str());
  sendData(s);
}

void StratumSessionSia::sendMiningNotify(shared_ptr<StratumJobEx> exJobPtr, bool isFirstJob)
{
  if (state_ < AUTHENTICATED || nullptr == exJobPtr)
  {
    LOG(ERROR) << "sia sendMiningNotify failed, state: " << state_;
    return;
  }

  // {"id":6,"jsonrpc":"2.0","params":["49",
  // "0x0000000000000000c12d6c07fa3e7e182d563d67a961d418d8fa0141478310a500000000000000001d3eaa5a00000000240cc42aa2940c21c8f0ad76b5780d7869629ff66a579043bbdc2b150b8689a0",
  // "0x0000000007547ff5d321871ff4fb4f118b8d13a30a1ff7b317f3c5b20629578a"],
  // "method":"mining.notify"}

  StratumJobSia *siaJob = dynamic_cast<StratumJobSia *>(exJobPtr->sjob_);
  if (nullptr == siaJob)
  {
    return;
  }

  localJobs_.push_back(LocalJob());
  LocalJob &ljob = *(localJobs_.rbegin());
  ljob.jobId_ = siaJob->jobId_;
  ljob.shortJobId_ = shortJobId_++;
  ljob.jobDifficulty_ = diffController_->calcCurDiff();
  uint256 shareTarget;
  DiffToTarget(ljob.jobDifficulty_, shareTarget);
  string strShareTarget = shareTarget.GetHex();
  LOG(INFO) << "new sia stratum job mining.notify: share difficulty=" << ljob.jobDifficulty_ << ", share target=" << strShareTarget;
  const string strNotify = Strings::Format("{\"id\":6,\"jsonrpc\":\"2.0\",\"method\":\"mining.notify\","
                                           "\"params\":[\"%u\",\"0x%s\",\"0x%s\"]}\n",
                                           ljob.shortJobId_,
                                           siaJob->blockHashForMergedMining_.c_str(),
                                           strShareTarget.c_str());

  sendData(strNotify); // send notify string

  // clear localJobs_
  clearLocalJobs();
}

void StratumSessionSia::handleRequest_Submit(const string &idStr, const JsonNode &jparams)
{
  ServerSia* server = GetServer();
  if (state_ != AUTHENTICATED)
  {
    responseError(idStr, StratumStatus::UNAUTHORIZED);
    // there must be something wrong, send reconnect command
    const string s = "{\"id\":null,\"method\":\"client.reconnect\",\"params\":[]}\n";
    sendData(s);
    return;
  }

  auto params = (const_cast<JsonNode &>(jparams)).array();
  if (params.size() != 3)
  {
    responseError(idStr, StratumStatus::ILLEGAL_PARARMS);
    LOG(ERROR) << "illegal header size: " << params.size();
    return;
  }

  string header = params[2].str();
  //string header = "00000000000000021f3e8ede65495c4311ef59e5b7a4338542e573819f5979e982719d0366014155e935aa5a00000000201929782a8fe3209b152520c51d2a82dc364e4a3eb6fb8131439835e278ff8b";
  if (162 == header.length())
    header = header.substr(2, 160);
  if (header.length() != 160)
  {
    responseError(idStr, StratumStatus::ILLEGAL_PARARMS);
    LOG(ERROR) << "illegal header" << params[2].str();
    return;
  }

  uint8 bHeader[80] = {0};
  for (int i = 0; i < 80; ++i)
    bHeader[i] = strtol(header.substr(i * 2, 2).c_str(), 0, 16);
  // uint64 nonce = strtoull(header.substr(64, 16).c_str(), nullptr, 16);
  // uint64 timestamp = strtoull(header.substr(80, 16).c_str(), nullptr, 16);
  // DLOG(INFO) << "nonce=" << std::hex << nonce << ", timestamp=" << std::hex << timestamp; 
  // //memcpy(bHeader + 32, &nonce, 8);
  // memcpy(bHeader + 40, &timestamp, 8);
  // for (int i = 48; i < 80; ++i)
  //   bHeader[i] = strtol(header.substr(i * 2, 2).c_str(), 0, 16);
  string str;
  for (int i = 0; i < 80; ++i)
    str += Strings::Format("%02x", bHeader[i]);
  DLOG(INFO) << str;

  uint8 out[32] = {0};
  int ret = blake2b(out, 32, bHeader, 80, nullptr, 0);
  DLOG(INFO) << "blake2b return=" << ret;
  //str = "";
  for (int i = 0; i < 32; ++i)
    str += Strings::Format("%02x", out[i]);
  DLOG(INFO) << str;

  uint8 shortJobId = (uint8)atoi(params[1].str());
  LocalJob *localJob = findLocalJob(shortJobId);
  if (nullptr == localJob) {
    responseError(idStr, StratumStatus::JOB_NOT_FOUND);
    LOG(ERROR) << "sia local job not found " << (int)shortJobId;
    return;
  }

  shared_ptr<StratumJobEx> exjob;
  exjob = server->GetJobRepository()->getStratumJobEx(localJob->jobId_);

  if (nullptr == exjob || nullptr == exjob->sjob_) {
    responseError(idStr, StratumStatus::JOB_NOT_FOUND);
    LOG(ERROR) << "sia local job not found " << std::hex << localJob->jobId_;
    return;
  }

  StratumJobSia *sjob = dynamic_cast<StratumJobSia*>(exjob->sjob_);
  if (nullptr == sjob) {
    responseError(idStr, StratumStatus::JOB_NOT_FOUND);
    LOG(ERROR) << "cast sia local job failed " << std::hex << localJob->jobId_;
    return;
  }

  uint64 nonce = *((uint64*) (bHeader + 32));
  LocalShare localShare(nonce, 0, 0);
  if (!server->isEnableSimulator_ && !localJob->addLocalShare(localShare))
  {
    responseError(idStr, StratumStatus::DUPLICATE_SHARE);
    LOG(ERROR) << "duplicated share nonce " << std::hex << nonce;
    // add invalid share to counter
    invalidSharesCounter_.insert((int64_t)time(nullptr), 1);
    return;
  }

  ShareBitcoin share;
  share.jobId_ = localJob->jobId_;
  share.workerHashId_ = worker_.workerHashId_;
  share.ip_ = clientIpInt_;
  share.userId_ = worker_.userId_;
  share.shareDiff_ = localJob->jobDifficulty_;
  share.timestamp_ = (uint32_t)time(nullptr);
  share.status_ = StratumStatus::REJECT_NO_REASON;

  arith_uint256 shareTarget(str);
  arith_uint256 networkTarget = UintToArith256(sjob->networkTarget_);
  
  if (shareTarget < networkTarget) {
    //valid share
    //submit share
    server->sendSolvedShare2Kafka(bHeader, 80);
    diffController_->addAcceptedShare(share.shareDiff_);
    LOG(INFO) << "sia solution found";
  }

  rpc2ResponseTrue(idStr);
  share.checkSum_ = share.checkSum();
  server->sendShare2Kafka((const uint8_t *)&share, sizeof(ShareBitcoin));
}

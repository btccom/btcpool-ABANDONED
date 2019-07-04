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
#include "StratumMinerSia.h"

#include "StratumSessionSia.h"
#include "StratumMessageDispatcher.h"
#include "DiffController.h"

#include "StratumSia.h"
#include "libblake2/blake2.h"

#include <arith_uint256.h>

///////////////////////////////// StratumSessionSia
///////////////////////////////////
StratumMinerSia::StratumMinerSia(
    StratumSessionSia &session,
    const DiffController &diffController,
    const std::string &clientAgent,
    const std::string &workerName,
    int64_t workerId)
  : StratumMinerBase(
        session, diffController, clientAgent, workerName, workerId) {
}

void StratumMinerSia::handleRequest(
    const std::string &idStr,
    const std::string &method,
    const JsonNode &jparams,
    const JsonNode &jroot) {
  if (method == "mining.submit") {
    handleRequest_Submit(idStr, jparams);
  }
}

void StratumMinerSia::handleRequest_Submit(
    const string &idStr, const JsonNode &jparams) {
  auto &session = getSession();
  auto &server = session.getServer();
  if (session.getState() != StratumSession::AUTHENTICATED) {
    session.responseError(idStr, StratumStatus::UNAUTHORIZED);
    // there must be something wrong, send reconnect command
    const string s =
        "{\"id\":null,\"method\":\"client.reconnect\",\"params\":[]}\n";
    session.sendData(s);
    return;
  }

  auto params = (const_cast<JsonNode &>(jparams)).array();
  if (params.size() != 3) {
    session.responseError(idStr, StratumStatus::ILLEGAL_PARARMS);
    LOG(ERROR) << "illegal header size: " << params.size();
    return;
  }

  string header = params[2].str();
  // string header =
  // "00000000000000021f3e8ede65495c4311ef59e5b7a4338542e573819f5979e982719d0366014155e935aa5a00000000201929782a8fe3209b152520c51d2a82dc364e4a3eb6fb8131439835e278ff8b";
  if (162 == header.length())
    header = header.substr(2, 160);
  if (header.length() != 160) {
    session.responseError(idStr, StratumStatus::ILLEGAL_PARARMS);
    LOG(ERROR) << "illegal header" << params[2].str();
    return;
  }

  uint8_t bHeader[80] = {0};
  for (int i = 0; i < 80; ++i)
    bHeader[i] = strtol(header.substr(i * 2, 2).c_str(), 0, 16);
  // uint64_t nonce = strtoull(header.substr(64, 16).c_str(), nullptr, 16);
  // uint64_t timestamp = strtoull(header.substr(80, 16).c_str(), nullptr, 16);
  // DLOG(INFO) << "nonce=" << std::hex << nonce << ", timestamp=" << std::hex
  // << timestamp;
  // //memcpy(bHeader + 32, &nonce, 8);
  // memcpy(bHeader + 40, &timestamp, 8);
  // for (int i = 48; i < 80; ++i)
  //   bHeader[i] = strtol(header.substr(i * 2, 2).c_str(), 0, 16);
  string str;
  for (int i = 0; i < 80; ++i)
    str += Strings::Format("%02x", bHeader[i]);
  DLOG(INFO) << str;

  uint8_t out[32] = {0};
  int ret = blake2b(out, 32, bHeader, 80, nullptr, 0);
  DLOG(INFO) << "blake2b return=" << ret;
  // str = "";
  for (int i = 0; i < 32; ++i)
    str += Strings::Format("%02x", out[i]);
  DLOG(INFO) << str;

  uint8_t shortJobId = (uint8_t)atoi(params[1].str());
  LocalJob *localJob = session.findLocalJob(shortJobId);
  if (nullptr == localJob) {
    session.responseError(idStr, StratumStatus::JOB_NOT_FOUND);
    LOG(ERROR) << "sia local job not found " << (int)shortJobId;
    return;
  }

  shared_ptr<StratumJobEx> exjob;
  exjob = server.GetJobRepository(localJob->chainId_)
              ->getStratumJobEx(localJob->jobId_);

  if (nullptr == exjob || nullptr == exjob->sjob_) {
    session.responseError(idStr, StratumStatus::JOB_NOT_FOUND);
    LOG(ERROR) << "sia local job not found " << std::hex << localJob->jobId_;
    return;
  }

  auto sjob = std::static_pointer_cast<StratumJobSia>(exjob->sjob_);
  if (nullptr == sjob) {
    session.responseError(idStr, StratumStatus::JOB_NOT_FOUND);
    LOG(ERROR) << "cast sia local job failed " << std::hex << localJob->jobId_;
    return;
  }

  uint64_t nonce = *((uint64_t *)(bHeader + 32));
  LocalShare localShare(nonce, 0, 0);
  if (!server.isEnableSimulator_ && !localJob->addLocalShare(localShare)) {
    session.responseError(idStr, StratumStatus::DUPLICATE_SHARE);
    LOG(ERROR) << "duplicated share nonce " << std::hex << nonce;
    // add invalid share to counter
    invalidSharesCounter_.insert((int64_t)time(nullptr), 1);
    return;
  }

  auto &worker = session.getWorker();
  auto iter = jobDiffs_.find(localJob);
  if (iter == jobDiffs_.end()) {
    LOG(ERROR) << "can't find session's diff, worker: " << worker.fullName_;
    return;
  }
  auto difficulty = iter->second;
  auto clientIp = session.getClientIp();

  ShareSia share;
  share.set_jobid(localJob->jobId_);
  share.set_workerhashid(workerId_);
  // share.ip_ = clientIp;
  IpAddress ip;
  ip.fromIpv4Int(clientIp);
  share.set_ip(ip.toString());

  share.set_userid(worker.userId(localJob->chainId_));
  share.set_sharediff(difficulty);
  share.set_timestamp((uint32_t)time(nullptr));
  share.set_status(StratumStatus::REJECT_NO_REASON);

  arith_uint256 shareTarget(str);
  arith_uint256 networkTarget = UintToArith256(sjob->networkTarget_);

  if (shareTarget < networkTarget) {
    // valid share
    // submit share
    server.sendSolvedShare2Kafka(localJob->chainId_, (const char *)bHeader, 80);
    diffController_->addShare(share.sharediff());
    // mark jobs as stale
    server.GetJobRepository(localJob->chainId_)->markAllJobsAsStale();

    LOG(INFO) << "sia solution found";
  }

  session.rpc2ResponseTrue(idStr);

  std::string message;
  uint32_t size = 0;
  if (!share.SerializeToArrayWithVersion(message, size)) {
    LOG(ERROR) << "share SerializeToArray failed!" << share.toString();
    return;
  }

  server.sendShare2Kafka(localJob->chainId_, message.data(), size);
}

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

#include "StratumSessionBytom.h"

#include "StratumMessageDispatcher.h"
#include "StratumMinerBytom.h"
#include "DiffController.h"

#include <boost/make_unique.hpp>

StratumSessionBytom::StratumSessionBytom(ServerBytom &server,
                                         struct bufferevent *bev,
                                         struct sockaddr *saddr,
                                         uint32_t extraNonce1)
    : StratumSessionBase(server, bev, saddr, extraNonce1), shortJobId_(1) {
}

void StratumSessionBytom::rpc2ResponseBoolean(const string &idStr, bool result, const string &failMessage) {
  if (result) {
    const string s = Strings::Format("{\"id\":%s,\"jsonrpc\":\"2.0\",\"result\":{\"status\":\"OK\"},\"error\":null}\n",
                                     idStr.c_str());
    sendData(s);
  } else {
    const string s = Strings::Format(
        "{\"id\":%s,\"jsonrpc\":\"2.0\",\"result\":null,\"error\":{\"code\":-1, \"message\":\"%s\"}}\n",
        idStr.c_str(),
        failMessage.c_str());
    sendData(s);
  }
}

void StratumSessionBytom::sendSetDifficulty(LocalJob &localJob, uint64_t difficulty) {
  // Bytom has no set difficulty method, but will change the target directly
  static_cast<StratumTraitsBytom::LocalJobType &>(localJob).jobDifficulty_ = difficulty;
}

void StratumSessionBytom::sendMiningNotify(shared_ptr<StratumJobEx> exJobPtr, bool isFirstJob) {
  auto &server = getServer();
  /*
    Bytom difficulty logic (based on B3-Mimic repo)
    - constants
      * Diff1: 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF
    Sending miningNotify
    - target
      Pool target is based from Diff1 and difficulty. target = Diff1 / difficulty
    Miner difficulty logic:
    - use target
    Pool check submit (see StratumMinerBytom::handleRequest_Submit)
  */
  if (state_ < AUTHENTICATED || nullptr == exJobPtr) {
    LOG(ERROR) << "bytom sendMiningNotify failed, state: " << state_;
    return;
  }

  StratumJobBytom *sJob = dynamic_cast<StratumJobBytom *>(exJobPtr->sjob_);
  if (nullptr == sJob)
    return;

  auto &ljob = addLocalJob(sJob->jobId_, shortJobId_++);
  uint64_t jobDifficulty = server.isDevModeEnable_ ? server.minerDifficulty_ : ljob.jobDifficulty_;
  if (jobDifficulty == 0)
    jobDifficulty = server.isDevModeEnable_ ? 1 : Bytom_TargetCompactToDifficulty(sJob->blockHeader_.bits);

  uint64 nonce = (((uint64) extraNonce1_) << 32);
  string notifyStr, nonceStr, versionStr, heightStr, timestampStr, bitsStr;
  Bin2HexR((uint8 *) &nonce, 8, nonceStr);
  Bin2Hex((uint8 *) &sJob->blockHeader_.version, 8, versionStr);
  Bin2Hex((uint8 *) &sJob->blockHeader_.height, 8, heightStr);
  Bin2Hex((uint8 *) &sJob->blockHeader_.timestamp, 8, timestampStr);
  Bin2Hex((uint8 *) &sJob->blockHeader_.bits, 8, bitsStr);

  string targetStr;
  {
    vector<uint8_t> targetBin;
    Bytom_DifficultyToTargetBinary(jobDifficulty, targetBin);
    //  trim the zeroes to reduce bandwidth
    unsigned int endIdx = targetBin.size() - 1;
    for (; endIdx > 0; --endIdx)  //  > 0 (not >=0) because need to print at least 1 byte
    {
      if (targetBin[endIdx] != 0)
        break;
    }
    //  reversed based on logic seen in B3-Mimic. Miner expect reversed hex
    Bin2HexR(targetBin.data(), endIdx + 1, targetStr);
  }

  string jobString = Strings::Format(
      "{\"version\": \"%s\","
      "\"height\": \"%s\","
      "\"previous_block_hash\": \"%s\","
      "\"timestamp\": \"%s\","
      "\"transactions_merkle_root\": \"%s\","
      "\"transaction_status_hash\": \"%s\","
      "\"nonce\": \"%s\","
      "\"bits\": \"%s\","
      "\"job_id\": \"%d\","
      "\"seed\": \"%s\","
      "\"target\": \"%s\"}",
      versionStr.c_str(),
      heightStr.c_str(),
      sJob->blockHeader_.previousBlockHash.c_str(),
      timestampStr.c_str(),
      sJob->blockHeader_.transactionsMerkleRoot.c_str(),
      sJob->blockHeader_.transactionStatusHash.c_str(),
      nonceStr.c_str(),
      bitsStr.c_str(),
      ljob.shortJobId_,
      sJob->seed_.c_str(),
      targetStr.c_str());

  if (isFirstJob) {
    notifyStr = Strings::Format(
        "{\"id\": 1, \"jsonrpc\": \"2.0\", \"result\": {\"id\": \"%s\", \"job\": %s, \"status\": \"OK\"}, \"error\": null}",
        server.isDevModeEnable_ ? "antminer_1" : worker_.fullName_.c_str(),
        jobString.c_str());
  } else {
    notifyStr = Strings::Format(
        "{\"jsonrpc\": \"2.0\", \"method\":\"job\", \"params\": %s}",
        jobString.c_str());
  }
  // LOG(INFO) << "Difficulty: " << ljob.jobDifficulty_ << "\nsendMiningNotify " << notifyStr.c_str();
  sendData(notifyStr);
}

bool StratumSessionBytom::validate(const JsonNode &jmethod, const JsonNode &jparams) {

  if (jmethod.type() == Utilities::JS::type::Str &&
      jmethod.size() != 0 &&
      jparams.type() == Utilities::JS::type::Obj) {
    return true;
  }

  return false;
}

bool StratumSessionBytom::handleRequest_Authorize(const string &idStr,
                                                  const JsonNode &jparams,
                                                  const JsonNode &jroot,
                                                  string &fullName,
                                                  string &password) {

  state_ = SUBSCRIBED;
  auto params = const_cast<JsonNode &> (jparams);
  fullName = params["login"].str();
  password = params["pass"].str();
  return true;
}

unique_ptr<StratumMiner> StratumSessionBytom::createMiner(const std::string &clientAgent,
                                                          const std::string &workerName,
                                                          int64_t workerId) {
  return boost::make_unique<StratumMinerBytom>(*this,
                                               *getServer().defaultDifficultyController_,
                                               clientAgent,
                                               workerName,
                                               workerId);
}

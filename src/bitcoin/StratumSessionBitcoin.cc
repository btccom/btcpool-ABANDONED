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

#include "StratumSessionBitcoin.h"

#include "StratumMessageDispatcher.h"
#include "StratumMinerBitcoin.h"
#include "DiffController.h"

struct StratumMessageExSubmit {
  boost::endian::little_uint8_buf_t magic;
  boost::endian::little_uint8_buf_t command;
  boost::endian::little_uint16_buf_t length;
  boost::endian::little_uint8_buf_t jobId;
  boost::endian::little_uint16_buf_t sessionId;
};

StratumSessionBitcoin::StratumSessionBitcoin(
    ServerBitcoin &server,
    struct bufferevent *bev,
    struct sockaddr *saddr,
    uint32_t sessionId)
  : StratumSessionBase(server, bev, saddr, sessionId)
  , shortJobIdIdx_(0)
  , versionMask_(0)
  , suggestedMinDiff_(0)
  , suggestedDiff_(0) {
}

uint16_t
StratumSessionBitcoin::decodeSessionId(const std::string &exMessage) const {
  if (exMessage.size() < (1 + 1 + 2 + 1 + 2))
    return StratumMessageEx::AGENT_MAX_SESSION_ID + 1;
  auto header =
      reinterpret_cast<const StratumMessageExSubmit *>(exMessage.data());
  return header->sessionId.value();
}

void StratumSessionBitcoin::sendSetDifficulty(
    LocalJob &localJob, uint64_t difficulty) {
  string s;

#ifdef CHAIN_TYPE_ZEC
  // {"id": null, "method": "mining.set_target", "params": ["TARGET"]}
  uint256 target;
  BitcoinDifficulty::DiffToTarget(difficulty, target);

  s = Strings::Format(
      "{\"id\":null,\"method\":\"mining.set_target\""
      ",\"params\":[\"%s\"]}\n",
      target.ToString().c_str());

#else
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
#endif

  sendData(s);
}

void StratumSessionBitcoin::sendMiningNotify(
    shared_ptr<StratumJobEx> exJobPtr, bool isFirstJob) {
  auto exJob = std::static_pointer_cast<StratumJobExBitcoin>(exJobPtr);
  if (state_ < AUTHENTICATED || exJob == nullptr) {
    return;
  }
  auto sjob = std::static_pointer_cast<StratumJobBitcoin>(exJob->sjob_);

  auto &ljob = addLocalJob(
      exJob->chainId_, sjob->jobId_, allocShortJobId(), sjob->nBits_);

#ifdef USER_DEFINED_COINBASE
  // add the User's coinbaseInfo to the coinbase1's tail
  string userCoinbaseInfo =
      GetServer()->userInfo_->getCoinbaseInfo(worker_.userId());
  ljob.userCoinbaseInfo_ = userCoinbaseInfo;
#endif

  string notifyStr;
  notifyStr.reserve(2048);

  // notify1
  notifyStr.append(exJob->miningNotify1_);

  // jobId
  if (isNiceHashClient_) {
    //
    // we need to send unique JobID to NiceHash Client, they have problems with
    // short Job ID
    //
    const uint64_t niceHashJobId =
        (uint64_t)time(nullptr) * kMaxNumLocalJobs_ + ljob.shortJobId_;
    notifyStr.append(Strings::Format("%u", niceHashJobId));
  } else {
    notifyStr.append(Strings::Format("%u", ljob.shortJobId_)); // short jobId
  }

  // notify2
  notifyStr.append(exJob->miningNotify2_);

  string coinbase1 = exJob->coinbase1_;

#ifdef USER_DEFINED_COINBASE
  string userCoinbaseHex;
  Bin2Hex(
      (const uint8_t *)ljob.userCoinbaseInfo_.c_str(),
      ljob.userCoinbaseInfo_.size(),
      userCoinbaseHex);
  // replace the last `userCoinbaseHex.size()` bytes to `userCoinbaseHex`
  coinbase1.replace(
      coinbase1.size() - userCoinbaseHex.size(),
      userCoinbaseHex.size(),
      userCoinbaseHex);
#endif

  // coinbase1
  notifyStr.append(coinbase1);

  // notify3
  if (isFirstJob)
    notifyStr.append(exJob->miningNotify3Clean_);
  else
    notifyStr.append(exJob->miningNotify3_);

  sendData(notifyStr); // send notify string

  // clear localJobs_
  clearLocalJobs(exJob->isClean_);
}

void StratumSessionBitcoin::handleRequest(
    const std::string &idStr,
    const std::string &method,
    const JsonNode &jparams,
    const JsonNode &jroot) {
  // Note: "mining.suggest_target" may be called before "mining.subscribe",
  // and most miners will call "mining.configure" in its first request.
  // So, don't assume that any future requests will appear after
  // subscription or authentication.

  if (method == "mining.subscribe") {
    handleRequest_Subscribe(idStr, jparams);
  } else if (method == "mining.authorize") {
    handleRequest_Authorize(idStr, jparams);
  } else if (method == "mining.configure") {
    handleRequest_MiningConfigure(idStr, jparams);
  } else if (method == "agent.get_capabilities") {
    handleRequest_AgentGetCapabilities(idStr, jparams);
  } else if (method == "mining.suggest_target") {
    handleRequest_SuggestTarget(idStr, jparams);
  } else {
    dispatcher_->handleRequest(idStr, method, jparams, jroot);
  }
}

void StratumSessionBitcoin::handleRequest_AgentGetCapabilities(
    const string &idStr, const JsonNode &jparams) {
  string s = Strings::Format(
      "{\"id\":%s,\"result\":{\"capabilities\":" BTCAGENT_PROTOCOL_CAPABILITIES
      "}}\n",
      idStr);
  sendData(s);
}

void StratumSessionBitcoin::handleRequest_MiningConfigure(
    const string &idStr, const JsonNode &jparams) {
  uint32_t allowedVersionMask = getServer().getVersionMask();

  if (jparams.children()->size() < 2 ||
      jparams.children()->at(0).type() != Utilities::JS::type::Array ||
      jparams.children()->at(1).type() != Utilities::JS::type::Obj) {
    responseError(idStr, StratumStatus::ILLEGAL_PARARMS);
    return;
  }

  //
  // {
  //   "method": "mining.configure",
  //   "id": 1,
  //   "params":
  //     [
  //       ["minimum-difficulty", "version-rolling"],
  //       {
  //         "minimum-difficulty.value": 2048,
  //         "version-rolling.mask": "1fffe000",
  //         "version-rolling.min-bit-count": 2
  //       }
  //     ]
  // }
  //
  // {
  //   "error": null,
  //   "id": 1,
  // 	 "result":
  //     {
  // 		  "version-rolling": true,
  // 		  "version-rolling.mask": "18000000",
  // 		  "minimum-difficulty": true
  // 	   }
  // }
  //
  auto extensions = jparams.children()->at(0).array();
  JsonNode options = jparams.children()->at(1);
  std::map<string, string> results;

  for (const auto &ext : extensions) {
    if (ext.type() != Utilities::JS::type::Str) {
      continue;
    }
    const string name = ext.str();

    //------------------------------------------------------------
    if (name == "minimum-difficulty") {
      auto diffNode = options["minimum-difficulty.value"];
      if (diffNode.type() != Utilities::JS::type::Int) {
        results["minimum-difficulty"] = "false";
        continue;
      }

      suggestedMinDiff_ = formatDifficulty(diffNode.uint64());
      results["minimum-difficulty"] = "true";

    } //----------------------------------------------------------
    else if (name == "version-rolling") {
      auto maskNode = options["version-rolling.mask"];
      if (maskNode.type() != Utilities::JS::type::Str) {
        results["version-rolling"] = "false";
        continue;
      }

      versionMask_ = maskNode.uint32_hex();
      results["version-rolling"] = "true";
      results["version-rolling.mask"] =
          Strings::Format("\"%08x\"", versionMask_ & allowedVersionMask);

    } //----------------------------------------------------------
    else {
      results[name] = "false";
    }
    //------------------------------------------------------------
  }

  string resultStr;

  // c++ map to json object
  if (!results.empty()) {
    auto itr = results.begin();
    resultStr += "\"" + itr->first + "\":" + itr->second;

    while (++itr != results.end()) {
      resultStr += ",\"" + itr->first + "\":" + itr->second;
    }
  }

  //
  // send result of mining.configure
  //
  string s = Strings::Format(
      "{\"id\":%s,\"result\":{%s},\"error\":null}\n", idStr, resultStr);
  sendData(s);

  //
  // mining.set_version_mask
  //
  if (versionMask_ != 0) {
    s = Strings::Format(
        "{\"id\":null,\"method\":\"mining.set_version_mask\",\"params\":[\"%"
        "08x\"]}\n",
        versionMask_ & allowedVersionMask);
    sendData(s);
  }
}

void StratumSessionBitcoin::handleRequest_Subscribe(
    const string &idStr, const JsonNode &jparams) {
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

  string sessionIdStr =
      jparams.children()->at(1).str().substr(0, 8); // 8 is max len
  sscanf(sessionIdStr.c_str(), "%x", &sessionId_); // convert hex to int

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
  //  params[1] = session id of pool [optional]
  //
  // client request eg.:
  //  {"id": 1, "method": "mining.subscribe", "params":
  //  ["bfgminer/4.4.0-32-gac4e9b3", "01ad557d"]}
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
  //  result[1] = ExtraNonce1, used for building the coinbase.
  //  result[2] = Extranonce2_size, the number of bytes that the miner users for
  //  its ExtraNonce2 counter

#ifdef CHAIN_TYPE_ZEC
  // {"id": 1, "result": ["SESSION_ID", "NONCE_1"], "error": null}
  auto s = Strings::Format(
      "{\"id\":%s,\"result\":[null,\"%08x\"],\"error\":null}\n",
      idStr.c_str(),
      sessionId_);
#else
  auto s = Strings::Format(
      "{\"id\":%s,\"result\":[[[\"mining.set_difficulty\",\"%08x\"]"
      ",[\"mining.notify\",\"%08x\"]],\"%08x\",%d],\"error\":null}\n",
      idStr.c_str(),
      sessionId_,
      sessionId_,
      sessionId_,
      Strings::Value(getServer().extraNonce2Size()));
#endif

  sendData(s);
}

void StratumSessionBitcoin::handleRequest_Authorize(
    const string &idStr, const JsonNode &jparams) {
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

void StratumSessionBitcoin::handleRequest_SuggestTarget(
    const string &idStr, const JsonNode &jparams) {
  if (state_ != CONNECTED) {
    responseError(idStr, StratumStatus::ILLEGAL_METHOD);
    return; // suggest should be call before subscribe
  }

  if (jparams.children()->size() == 0) {
    responseError(idStr, StratumStatus::ILLEGAL_PARARMS);
    return;
  }

  suggestedDiff_ = formatDifficulty(
      BitcoinDifficulty::TargetToDiff(jparams.children()->at(0).str()));
  responseTrue(idStr);
}

void StratumSessionBitcoin::logAuthorizeResult(
    bool success, const string &password) {
  if (success) {
    LOG(INFO) << "authorize success, userId: " << worker_.userId()
              << ", wokerHashId: " << worker_.workerHashId_
              << ", workerName: " << worker_.fullName_
              << ", password: " << password
              << ", versionMask: " << Strings::Format("%08x", versionMask_)
              << ", clientAgent: " << clientAgent_
              << ", clientIp: " << clientIp_
              << ", chain: " << getServer().chainName(worker_.chainId_);
  } else {
    LOG(WARNING) << "authorize failed, workerName:" << worker_.fullName_
                 << ", password: " << password
                 << ", versionMask: " << Strings::Format("%08x", versionMask_)
                 << ", clientAgent: " << clientAgent_
                 << ", clientIp: " << clientIp_;
  }
}

unique_ptr<StratumMessageDispatcher> StratumSessionBitcoin::createDispatcher() {
  if (isAgentClient_) {
    return std::make_unique<StratumMessageAgentDispatcher>(
        *this, *getServer().defaultDifficultyController_);
  } else {
    return std::make_unique<StratumMessageMinerDispatcher>(
        *this,
        createMiner(clientAgent_, worker_.workerName_, worker_.workerHashId_));
  }
}

uint8_t StratumSessionBitcoin::allocShortJobId() {
  // return range: [0, 9]
  if (shortJobIdIdx_ >= kMaxNumLocalJobs_) {
    shortJobIdIdx_ = 0;
  }
  return shortJobIdIdx_++;
}

unique_ptr<StratumMiner> StratumSessionBitcoin::createMiner(
    const std::string &clientAgent,
    const std::string &workerName,
    int64_t workerId) {
  auto miner = std::make_unique<StratumMinerBitcoin>(
      *this,
      *getServer().defaultDifficultyController_,
      clientAgent,
      workerName,
      workerId);

  if (suggestedMinDiff_ != 0) {
    miner->setMinDiff(suggestedMinDiff_);
  }

  if (suggestedDiff_ != 0) {
    miner->resetCurDiff(suggestedDiff_);
  }

  return miner;
}

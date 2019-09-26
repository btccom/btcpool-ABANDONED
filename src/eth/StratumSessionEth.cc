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

#include "StratumSessionEth.h"

#include "StratumMessageDispatcher.h"
#include "StratumMinerEth.h"
#include "DiffController.h"

#include <libethash/sha3.h>

// Remove the Ethereum address prefix from worker's full name
// 0x00d8c82Eb65124Ea3452CaC59B64aCC230AA3482.test.aaa -> test.aaa
static string stripEthAddrFromFullName(const string &fullNameStr) {
  const size_t pos = fullNameStr.find('.');
  // The Ethereum address is 42 bytes and starting with "0x" as normal
  // Example: 0x00d8c82Eb65124Ea3452CaC59B64aCC230AA3482
  if (pos == 42 && fullNameStr[0] == '0' &&
      (fullNameStr[1] == 'x' || fullNameStr[1] == 'X')) {
    return fullNameStr.substr(pos + 1);
  }
  return fullNameStr;
}

StratumSessionEth::StratumSessionEth(
    ServerEth &server,
    struct bufferevent *bev,
    struct sockaddr *saddr,
    uint32_t extraNonce1)
  : StratumSessionBase(server, bev, saddr, extraNonce1)
  , ethProtocol_(StratumProtocolEth::ETHPROXY)
  , nicehashLastSentDiff_(0)
  , currentJobDiff_(0)
  , extraNonce2_(false) {
}

void StratumSessionEth::sendSetDifficulty(
    LocalJob &localJob, uint64_t difficulty) {
  // Some ETH stratum variants have no set difficulty method, but change the
  // target directly
  currentJobDiff_ = difficulty;
}

void StratumSessionEth::sendMiningNotify(
    shared_ptr<StratumJobEx> exJobPtr, bool isFirstJob) {
  if (StratumProtocolEth::ETHPROXY == ethProtocol_) {
    // AntMiner E3 need id to be 0, otherwise it will not be able to mine.
    // It does not actively call `eth_getWork` like other ETHProxy miners.
    sendMiningNotifyWithId(exJobPtr, "0");
  } else {
    sendMiningNotifyWithId(exJobPtr, "null");
  }
}

void StratumSessionEth::sendMiningNotifyWithId(
    shared_ptr<StratumJobEx> exJobPtr, const string &idStr) {
  if (state_ < AUTHENTICATED || exJobPtr == nullptr) {
    LOG(ERROR) << "eth sendMiningNotify failed, state: " << state_;
    return;
  }

  auto ethJob = std::static_pointer_cast<StratumJobEth>(exJobPtr->sjob_);
  if (nullptr == ethJob) {
    return;
  }

  string header;
  string header2;
  if (ethJob->hasHeader()) {
    auto headerBin = ethJob->getHeaderWithExtraNonce(
        sessionId_, boost::make_optional<uint32_t>(extraNonce2_, 0));
    uint8_t hash[32];
    sha3_256(
        hash,
        32,
        reinterpret_cast<const uint8_t *>(headerBin.data()),
        headerBin.size());
    Bin2Hex(hash, 32, header);
    if (extraNonce2_) {
      // Cut 4 bytes from header - they will be filled by miner
      Bin2Hex(
          reinterpret_cast<const uint8_t *>(headerBin.data()),
          headerBin.size(),
          header2);
      header2 = ",\"header\":\"" +
          HexAddPrefix(header2.substr(0, header2.size() - 8)) + "\"";
    }
  } else {
    header = ethJob->headerHash_;
  }
  string seed = ethJob->seedHash_;

  // strip prefix "0x"
  if (66 == header.length()) {
    header = header.substr(2, 64);
  }
  if (66 == seed.length()) {
    seed = seed.substr(2, 64);
  }

  auto ljob = findLocalJob(header);
  // create a new LocalJobEth if not exists
  if (ljob == nullptr) {
    ljob = &addLocalJob(exJobPtr->chainId_, ethJob->jobId_, header);
  } else {
    dispatcher_->addLocalJob(*ljob);
    // update the job id to the latest one
    ljob->jobId_ = ethJob->jobId_;
  }

  string strShareTarget = Eth_DifficultyToTarget(currentJobDiff_);

  // Session ID, 24 bits.
  // Miners will fills 0 after the prefix to 64 bits.
  uint32_t startNoncePrefix = sessionId_;

  // Tips: NICEHASH_STRATUM use an extrNnonce, it is really an extraNonce (not
  // startNonce) and is sent at the subscribe of the session.

  DLOG(INFO) << "new eth stratum job mining.notify: share difficulty="
             << std::hex << currentJobDiff_
             << ", share target=" << strShareTarget
             << ", protocol=" << getProtocolString(ethProtocol_);
  string strNotify;

  switch (ethProtocol_) {
  case StratumProtocolEth::STRATUM: {
    // Etherminer mining.notify
    //{"id":6,"method":"mining.notify","params":
    //["dd159c7ec5b056ad9e95e7c997829f667bc8e34c6d43fcb9e0c440ed94a85d80",
    //"dd159c7ec5b056ad9e95e7c997829f667bc8e34c6d43fcb9e0c440ed94a85d80",
    //"a8784097a4d03c2d2ac6a3a2beebd0606aa30a8536a700446b40800841c0162c",
    //"0000000112e0be826d694b2e62d01511f12a6061fbaec8bc02357593e70e52ba",false]}
    strNotify = Strings::Format(
        "{\"id\":%s,\"method\":\"mining.notify\","
        "\"params\":[\"%s\",\"%s\",\"%s\",\"%s\",%s],"
        "\"height\":%lu%s}\n",
        idStr,
        header,
        header,
        seed,
        strShareTarget,
        exJobPtr->isClean_ ? "true" : "false",
        ethJob->height_,
        header2);
  } break;
  case StratumProtocolEth::ETHPROXY: {
    // Clymore eth_getWork
    //{"id":3,"jsonrpc":"2.0","result":
    //["0x599fffbc07777d4b6455c0e7ca479c9edbceef6c3fec956fecaaf4f2c727a492",
    //"0x1261dfe17d0bf58cb2861ae84734488b1463d282b7ee88ccfa18b7a92a7b77f7",
    //"0x0112e0be826d694b2e62d01511f12a6061fbaec8bc02357593e70e52ba","0x4ec6f5"]}
    strNotify = Strings::Format(
        "{\"id\":%s,\"jsonrpc\":\"2.0\","
        "\"result\":[\"0x%s\",\"0x%s\",\"0x%s\","
        // nonce cannot start with 0x because of
        // a compatibility issue with AntMiner E3.
        "\"%06x\"],"
        "\"height\":%lu%s}\n",
        idStr,
        header,
        seed,
        // Claymore use 58 bytes target
        strShareTarget.substr(6, 58),
        startNoncePrefix,
        ethJob->height_,
        header2);
  } break;
  case StratumProtocolEth::NICEHASH_STRATUM: {
    // send new difficulty
    if (currentJobDiff_ != nicehashLastSentDiff_) {
      // NICEHASH_STRATUM mining.set_difficulty
      // {"id": null,
      //  "method": "mining.set_difficulty",
      //  "params": [ 0.5 ]
      // }
      strNotify += Strings::Format(
          "{\"id\":%s,\"method\":\"mining.set_difficulty\","
          "\"params\":[%f]}\n",
          idStr,
          Eth_DiffToNicehashDiff(currentJobDiff_));
      nicehashLastSentDiff_ = currentJobDiff_;
    }

    // NICEHASH_STRATUM mining.notify
    // { "id": null,
    //   "method": "mining.notify",
    //   "params": [
    //     "bf0488aa",
    //     "abad8f99f3918bf903c6a909d9bbc0fdfa5a2f4b9cb1196175ec825c6610126c",
    //     "645cf20198c2f3861e947d4f67e3ab63b7b2e24dcc9095bd9123e7b33371f6cc",
    //     true
    //   ]}
    strNotify += Strings::Format(
        "{\"id\":%s,\"method\":\"mining.notify\","
        "\"params\":[\"%s\",\"%s\",\"%s\",%s],"
        "\"height\":%lu%s}\n",
        idStr,
        header,
        seed,
        header,
        exJobPtr->isClean_ ? "true" : "false",
        ethJob->height_,
        header2);
  } break;
  }

  DLOG(INFO) << strNotify;

  if (!strNotify.empty())
    sendData(strNotify); // send notify string
  else
    LOG(ERROR) << "Eth notify string is empty";

  // clear localEthJobs_
  clearLocalJobs(exJobPtr->isClean_);
}

void StratumSessionEth::handleRequest(
    const std::string &idStr,
    const std::string &method,
    const JsonNode &jparams,
    const JsonNode &jroot) {
  if (method == "mining.subscribe") {
    handleRequest_Subscribe(idStr, jparams, jroot);
  } else if (method == "mining.authorize" || method == "eth_submitLogin") {
    handleRequest_Authorize(idStr, jparams, jroot);
  } else {
    dispatcher_->handleRequest(idStr, method, jparams, jroot);
  }
}

void StratumSessionEth::handleRequest_Subscribe(
    const string &idStr, const JsonNode &jparams, const JsonNode &jroot) {

  if (state_ != CONNECTED) {
    responseError(idStr, StratumStatus::UNKNOWN);
    return;
  }

  auto params = jparams.children();

  if (params->size() >= 1) {
    setClientAgent(params->at(0).str().substr(0, 30)); // 30 is max len
  }

  string protocolStr;
  if (params->size() >= 2) {
    protocolStr = params->at(1).str();
    // tolower
    std::transform(
        protocolStr.begin(), protocolStr.end(), protocolStr.begin(), ::tolower);
  }

  // session id and miner ip need to pass within params if working with stratum
  // switcher
#ifdef WORK_WITH_STRATUM_SWITCHER
  //  params[0] = client version           [require]
  //  params[1] = protocol version         [require, can be empty]
  //  params[2] = session id / ExtraNonce1 [require]
  //  params[3] = miner's real IP (unit32) [optional]

  if (params->size() < 3) {
    responseError(idStr, StratumStatus::CLIENT_IS_NOT_SWITCHER);
    LOG(ERROR) << "A non-switcher subscribe request is detected and rejected.";
    LOG(ERROR) << "Cmake option POOL__WORK_WITH_STRATUM_SWITCHER enabled, you "
                  "can only connect to the sserver via a stratum switcher.";
    return;
  }

  string extraNonce1Str = params->at(2).str().substr(0, 8); // 8 is max len
  sscanf(extraNonce1Str.c_str(), "%x", &sessionId_); // convert hex to int

  // receive miner's IP from stratumSwitcher
  if (params->size() >= 4) {
    clientIpInt_ = htonl(params->at(3).uint32());

    // ipv4
    clientIp_.resize(INET_ADDRSTRLEN);
    struct in_addr addr;
    addr.s_addr = clientIpInt_;
    clientIp_ = inet_ntop(
        AF_INET, &addr, (char *)clientIp_.data(), (socklen_t)clientIp_.size());
    LOG(INFO) << "client real IP: " << clientIp_;
  }
#endif // WORK_WITH_STRATUM_SWITCHER

  if (protocolStr.substr(0, 16) == "ethereumstratum/") {
    ethProtocol_ = StratumProtocolEth::NICEHASH_STRATUM;

    string noncePrefix = Strings::Format("%06x", sessionId_);
    if (isNiceHashClient_) {
      // NiceHash only accepts 2 bytes or shorter of extraNonce.
      noncePrefix = noncePrefix.substr(0, 4);
    }

    // mining.notify of NICEHASH_STRATUM's subscribe
    // {
    //   "id": 1,
    //   "result": [
    //     [
    //       "mining.notify",
    //       "ae6812eb4cd7735a302a8a9dd95cf71f",
    //       "EthereumStratum/1.0.0"
    //     ],
    //     "080c"
    //   ],
    //   "error": null
    // }
    const string s = Strings::Format(
        "{\"id\":%s,\"result\":[["
        "\"mining.notify\","
        "\"%06x\","
        "\"EthereumStratum/1.0.0\""
        "],\"%s\"],\"error\":null}\n",
        idStr,
        sessionId_,
        noncePrefix);
    sendData(s);
  }
#ifdef WORK_WITH_STRATUM_SWITCHER
  else if (protocolStr.substr(0, 9) == "ethproxy/") {
    // required for stratum switcher
    // Because ethproxy has no subscribe phase, switcher has no chance to set
    // session id. So deliberately added a subscribe phase of ethproxy here.
    ethProtocol_ = StratumProtocolEth::ETHPROXY;
    responseTrue(idStr);
  }
#endif // WORK_WITH_STRATUM_SWITCHER
  else {
    ethProtocol_ = StratumProtocolEth::STRATUM;
    responseTrue(idStr);
  }

  checkExtraNonce2(jroot);
  state_ = SUBSCRIBED;
}

void StratumSessionEth::handleRequest_Authorize(
    const string &idStr, const JsonNode &jparams, const JsonNode &jroot) {
  // const type cannot access string indexed object member
  JsonNode &jsonRoot = const_cast<JsonNode &>(jroot);

#ifdef WORK_WITH_STRATUM_SWITCHER
  if (state_ != SUBSCRIBED) {
    responseError(idStr, StratumStatus::CLIENT_IS_NOT_SWITCHER);
    LOG(ERROR) << "A non-switcher authorize request is detected and rejected.";
    LOG(ERROR) << "Cmake option POOL__WORK_WITH_STRATUM_SWITCHER enabled, you "
                  "can only connect to the sserver via a stratum switcher.";
  }
#else
  if (StratumProtocolEth::ETHPROXY == ethProtocol_ &&
      jsonRoot["method"].str() == "eth_submitLogin") {
    // Subscribe is not required for ETHPROXY (without stratum switcher).
    // But if WORK_WITH_STRATUM_SWITCHER enabled, subscribe for ETHProxy is
    // required.
    checkExtraNonce2(jroot);
    state_ = SUBSCRIBED;
  }

  if (state_ != SUBSCRIBED) {
    responseError(idStr, StratumStatus::NOT_SUBSCRIBED);
    return;
  }
#endif

  // STRATUM / NICEHASH_STRATUM:        {"id":3, "method":"mining.authorize",
  // "params":["test.aaa", "x"]} ETH_PROXY (Claymore):              {"worker":
  // "eth1.0", "jsonrpc": "2.0", "params":
  // ["0x00d8c82Eb65124Ea3452CaC59B64aCC230AA3482.test.aaa", "x"], "id": 2,
  // "method": "eth_submitLogin"} ETH_PROXY (EthMiner, situation 1): {"id":1,
  // "method":"eth_submitLogin",
  // "params":["0x00d8c82Eb65124Ea3452CaC59B64aCC230AA3482"],
  // "worker":"test.aaa"} ETH_PROXY (EthMiner, situation 2): {"id":1,
  // "method":"eth_submitLogin", "params":["test"], "worker":"aaa"}

  if (jparams.children()->size() < 1) {
    responseError(idStr, StratumStatus::INVALID_USERNAME);
    return;
  }

  string fullName, password;

  fullName = jparams.children()->at(0).str();
  if (jsonRoot["worker"].type() == Utilities::JS::type::Str) {
    fullName += '.';
    fullName += jsonRoot["worker"].str();
  }
  fullName = stripEthAddrFromFullName(fullName);

  if (jparams.children()->size() > 1) {
    password = jparams.children()->at(1).str();
  }

  checkUserAndPwd(idStr, fullName, password);
  return;
}

unique_ptr<StratumMiner> StratumSessionEth::createMiner(
    const std::string &clientAgent,
    const std::string &workerName,
    int64_t workerId) {
  return std::make_unique<StratumMinerEth>(
      *this,
      *getServer().defaultDifficultyController_,
      clientAgent,
      workerName,
      workerId,
      ethProtocol_);
}

void StratumSessionEth::responseError(const string &idStr, int code) {
  if (StratumProtocolEth::ETHPROXY == ethProtocol_) {
    rpc2ResponseError(idStr, code);
  } else {
    rpc1ResponseError(idStr, code);
  }
}

void StratumSessionEth::responseTrue(const string &idStr) {
  if (StratumProtocolEth::ETHPROXY == ethProtocol_) {
    rpc2ResponseTrue(idStr);
  } else {
    rpc1ResponseTrue(idStr);
  }
}

void StratumSessionEth::responseTrueWithCode(const string &idStr, int code) {
  string s;
  if (StratumProtocolEth::ETHPROXY == ethProtocol_) {
    s = Strings::Format(
        "{\"id\":%s,\"jsonrpc\":\"2.0\",\"result\":true,\"message\":\"%s\"}\n",
        idStr.c_str(),
        StratumStatus::toString(code));
  } else {
    s = Strings::Format(
        "{\"id\":%s,\"result\":true,\"error\":null,\"message\":\"%s\"}\n",
        idStr.c_str(),
        StratumStatus::toString(code));
  }
  sendData(s);
}

void StratumSessionEth::responseFalse(const string &idStr, int code) {
  if (StratumProtocolEth::ETHPROXY == ethProtocol_) {
    rpc2ResponseFalse(idStr, code);
  } else {
    rpc1ResponseError(idStr, code);
  }
}

void StratumSessionEth::rpc2ResponseFalse(const string &idStr, int errCode) {
  auto data = Strings::Format(
      "{\"id\":%s,\"jsonrpc\":\"2.0\",\"result\":false,\"data\":{\"code\":%d,"
      "\"message\":\"%s\"}}\n",
      idStr.empty() ? "null" : idStr,
      errCode,
      StratumStatus::toString(errCode));
  sendData(data);
}

void StratumSessionEth::checkExtraNonce2(const JsonNode &jroot) {
  JsonNode &jsonRoot = const_cast<JsonNode &>(jroot);
  if (jsonRoot["extra_nonce"].type() == Utilities::JS::type::Bool) {
    extraNonce2_ = jsonRoot["extra_nonce"].boolean();
    if (extraNonce2_) {
      // User with extra nonce 2 is most likely a proxy
      isLongTimeout_ = true;
    }
  }
}

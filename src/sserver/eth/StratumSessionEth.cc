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
#include "StratumServerEth.h"
#include "sserver/common/DiffController.h"

#include "CommonEth.h"

///////////////////////////////// StratumSessionEth ////////////////////////////////
StratumSessionEth::StratumSessionEth(evutil_socket_t fd, struct bufferevent *bev,
                                     ServerEth *server, struct sockaddr *saddr,
                                     const int32_t shareAvgSeconds, const uint32_t extraNonce1)
  : StratumSessionBase(fd, bev, server, saddr, shareAvgSeconds, extraNonce1)
  , ethProtocol_(StratumProtocol::ETHPROXY)
  , nicehashLastSentDiff_(0)
{
}

StratumSessionEth::LocalJobEth* StratumSessionEth::findLocalJob(const string &headerHash) {
  for (auto rit = localEthJobs_.rbegin(); rit != localEthJobs_.rend(); ++rit) {
    if (rit->headerHash_ == headerHash) {
      return &(*rit);
    }
  }
  return nullptr;
}

void StratumSessionEth::clearLocalJobs()
{
  while (localEthJobs_.size() >= kMaxNumLocalJobs_)
  {
    localEthJobs_.pop_front();
  }
}

void StratumSessionEth::responseError(const string &idStr, int code) {
  return rpc2ResponseError(idStr, code);
}

void StratumSessionEth::responseTrue(const string &idStr) {
  return rpc2ResponseBoolean(idStr, true);
}

void StratumSessionEth::sendMiningNotify(shared_ptr<StratumJobEx> exJobPtr, bool isFirstJob) {
  sendMiningNotifyWithId(exJobPtr, "null");
}

void StratumSessionEth::sendMiningNotifyWithId(shared_ptr<StratumJobEx> exJobPtr, const string &idStr)
{
  if (state_ < AUTHENTICATED || exJobPtr == nullptr)
  {
    LOG(ERROR) << "eth sendMiningNotify failed, state: " << state_;
    return;
  }

  StratumJobEth *ethJob = dynamic_cast<StratumJobEth *>(exJobPtr->sjob_);
  if (nullptr == ethJob)
  {
    return;
  }
  
  string header = ethJob->blockHashForMergedMining_;
  string seed = ethJob->seedHash_;

  // strip prefix "0x"
  if (66 == header.length()) {
    header = header.substr(2, 64);
  }
  if (66 == seed.length()) {
    seed = seed.substr(2, 64);
  }

  LocalJobEth *ljob = findLocalJob(header);
  // create a new LocalJobEth if not exists
  if (ljob == nullptr) {
    localEthJobs_.push_back(LocalJobEth());
    ljob = &(*(localEthJobs_.rbegin()));
  }
  // update the job
  ljob->jobId_ = ethJob->jobId_;
  ljob->headerHash_ = header;
  ljob->addDiff(diffController_->calcCurDiff());

  string strShareTarget = Eth_DifficultyToTarget(ljob->currentJobDiff_);

  // extraNonce1_ == Session ID, 24 bits.
  // Miners will fills 0 after the prefix to 64 bits.
  uint32_t startNoncePrefix = extraNonce1_;
  
  // Tips: NICEHASH_STRATUM use an extrNnonce, it is really an extraNonce (not startNonce)
  // and is sent at the subscribe of the session.

  LOG(INFO) << "new eth stratum job mining.notify: share difficulty=" << std::hex << ljob->currentJobDiff_ << ", share target=" << strShareTarget << ", protocol=" << getProtocolString(ethProtocol_);
  string strNotify;

  switch (ethProtocol_)
  {
  case StratumProtocol::STRATUM:
  {
    //Etherminer mining.notify
    //{"id":6,"jsonrpc":"2.0","method":"mining.notify","params":
    //["dd159c7ec5b056ad9e95e7c997829f667bc8e34c6d43fcb9e0c440ed94a85d80",
    //"dd159c7ec5b056ad9e95e7c997829f667bc8e34c6d43fcb9e0c440ed94a85d80",
    //"a8784097a4d03c2d2ac6a3a2beebd0606aa30a8536a700446b40800841c0162c",
    //"0000000112e0be826d694b2e62d01511f12a6061fbaec8bc02357593e70e52ba",false]}
    strNotify = Strings::Format("{\"id\":%s,\"jsonrpc\":\"2.0\",\"method\":\"mining.notify\","
                                "\"params\":[\"%s\",\"%s\",\"%s\",\"%s\", %s]}\n",
                                idStr.c_str(),
                                header.c_str(),
                                header.c_str(),
                                seed.c_str(),
                                strShareTarget.c_str(),
                                exJobPtr->isClean_ ? "true" : "false");
  }
  break;
  case StratumProtocol::ETHPROXY:
  {
    //Clymore eth_getWork
    //{"id":3,"jsonrpc":"2.0","result":
    //["0x599fffbc07777d4b6455c0e7ca479c9edbceef6c3fec956fecaaf4f2c727a492",
    //"0x1261dfe17d0bf58cb2861ae84734488b1463d282b7ee88ccfa18b7a92a7b77f7",
    //"0x0112e0be826d694b2e62d01511f12a6061fbaec8bc02357593e70e52ba","0x4ec6f5"]}
    strNotify = Strings::Format("{\"id\":%s,\"jsonrpc\":\"2.0\","
                                "\"result\":[\"0x%s\",\"0x%s\",\"0x%s\",\"0x%06x\"]}\n",
                                idStr.c_str(),
                                header.c_str(),
                                seed.c_str(),
                                //Claymore use 58 bytes target
                                strShareTarget.substr(6, 58).c_str(),
                                startNoncePrefix);
  }
  break;
  case StratumProtocol::NICEHASH_STRATUM:
  {
    // send new difficulty
    if (ljob->currentJobDiff_ != nicehashLastSentDiff_) {
      // NICEHASH_STRATUM mining.set_difficulty
      // {"id": null, 
      //  "method": "mining.set_difficulty", 
      //  "params": [ 0.5 ]
      // }
      strNotify += Strings::Format("{\"id\":%s,\"jsonrpc\":\"2.0\",\"method\":\"mining.set_difficulty\","
                                   "\"params\":[%lf]}\n", idStr.c_str(), Eth_DiffToNicehashDiff(ljob->currentJobDiff_));
      nicehashLastSentDiff_ = ljob->currentJobDiff_;
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
    strNotify += Strings::Format("{\"id\":%s,\"jsonrpc\":\"2.0\",\"method\":\"mining.notify\","
                                 "\"params\":[\"%s\",\"%s\",\"%s\", %s]}\n",
                                 idStr.c_str(),
                                 header.c_str(),
                                 seed.c_str(),
                                 header.c_str(),
                                 exJobPtr->isClean_ ? "true" : "false");
  }
  break;
  }

  DLOG(INFO) << strNotify;

  if (!strNotify.empty())
    sendData(strNotify); // send notify string
  else
    LOG(ERROR) << "Eth notify string is empty";

  // clear localEthJobs_
  clearLocalJobs();
}

void StratumSessionEth::handleRequest_Subscribe(const string &idStr, const JsonNode &jparams)
{
  if (state_ != CONNECTED)
  {
    rpc2ResponseError(idStr, StratumStatus::UNKNOWN);
    return;
  }

  auto params = jparams.children();

  if (params->size() >= 1) {
    clientAgent_ = params->at(0).str().substr(0, 30);  // 30 is max len
    clientAgent_ = filterWorkerName(clientAgent_);
  }

  string protocolStr;
  if (params->size() >= 2) {
    protocolStr = params->at(1).str();
    // tolower
    std::transform(protocolStr.begin(), protocolStr.end(), protocolStr.begin(), ::tolower);
  }

  // session id and miner ip need to pass within params if working with stratum switcher
  #ifdef WORK_WITH_STRATUM_SWITCHER
    //  params[0] = client version           [require]
    //  params[1] = protocol version         [require, can be empty]
    //  params[2] = session id / ExtraNonce1 [require]
    //  params[3] = miner's real IP (unit32) [optional]

    if (params->size() < 3) {
      responseError(idStr, StratumStatus::ILLEGAL_PARARMS);
      return;
    }

    string extraNonce1Str = params->at(2).str().substr(0, 8);  // 8 is max len
    sscanf(extraNonce1Str.c_str(), "%x", &extraNonce1_); // convert hex to int

    // receive miner's IP from stratumSwitcher
    if (params->size() >= 4) {
      clientIpInt_ = htonl(params->at(3).uint32());

      // ipv4
      clientIp_.resize(INET_ADDRSTRLEN);
      struct in_addr addr;
      addr.s_addr = clientIpInt_;
      clientIp_ = inet_ntop(AF_INET, &addr, (char *)clientIp_.data(), (socklen_t)clientIp_.size());
      LOG(INFO) << "client real IP: " << clientIp_;
    }
  #endif // WORK_WITH_STRATUM_SWITCHER

  if (protocolStr.substr(0, 16) == "ethereumstratum/") {
    ethProtocol_ = StratumProtocol::NICEHASH_STRATUM;

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
    const string s = Strings::Format("{\"id\":%s,\"jsonrpc\":\"2.0\",\"result\":[["
                                        "\"mining.notify\","
                                        "\"%06x\","
                                        "\"EthereumStratum/1.0.0\""
                                     "],\"%06x\"],\"error\":null}\n",
                                     idStr.c_str(), extraNonce1_, extraNonce1_);
    sendData(s);
  }
#ifdef WORK_WITH_STRATUM_SWITCHER
  else if (protocolStr.substr(0, 9) == "ethproxy/") {
    // required for stratum switcher
    // Because ethproxy has no subscribe phase, switcher has no chance to set session id.
    // So deliberately added a subscribe phase of ethproxy here.
    ethProtocol_ = StratumProtocol::ETHPROXY;

    const string s = Strings::Format("{\"id\":%s,\"jsonrpc\":\"2.0\",\"result\":true}\n", idStr.c_str());
    sendData(s);
  }
#endif // WORK_WITH_STRATUM_SWITCHER
  else {
    ethProtocol_ = StratumProtocol::STRATUM;

    const string s = Strings::Format("{\"id\":%s,\"jsonrpc\":\"2.0\",\"result\":true}\n", idStr.c_str());
    sendData(s);
  }

  state_ = SUBSCRIBED;
}

string StratumSessionEth::stripEthAddrFromFullName(const string& fullNameStr) {
  const size_t pos = fullNameStr.find('.');
  // The Ethereum address is 42 bytes and starting with "0x" as normal
  // Example: 0x00d8c82Eb65124Ea3452CaC59B64aCC230AA3482
  if (pos == 42 && fullNameStr[0] == '0' && (fullNameStr[1] == 'x' || fullNameStr[1] == 'X')) {
    return fullNameStr.substr(pos + 1);
  }
  return fullNameStr;
}

void StratumSessionEth::handleRequest_Authorize(const string &idStr, const JsonNode &jparams, const JsonNode &jroot)
{
  // const type cannot access string indexed object member
  JsonNode &jsonRoot = const_cast<JsonNode &>(jroot);

#ifndef WORK_WITH_STRATUM_SWITCHER
  if (StratumProtocol::ETHPROXY == ethProtocol_ && jsonRoot["method"].str() == "eth_submitLogin") {
    // Subscribe is not required for ETHPROXY (without stratum switcher).
    // But if WORK_WITH_STRATUM_SWITCHER enabled, subscribe for ETHProxy is required.
    state_ = SUBSCRIBED;
  }
#endif

  if (state_ != SUBSCRIBED) {
    responseError(idStr, StratumStatus::NOT_SUBSCRIBED);
    return;
  }

  // STRATUM / NICEHASH_STRATUM:        {"id":3, "method":"mining.authorize", "params":["test.aaa", "x"]} 
  // ETH_PROXY (Claymore):              {"worker": "eth1.0", "jsonrpc": "2.0", "params": ["0x00d8c82Eb65124Ea3452CaC59B64aCC230AA3482.test.aaa", "x"], "id": 2, "method": "eth_submitLogin"}
  // ETH_PROXY (EthMiner, situation 1): {"id":1, "method":"eth_submitLogin", "params":["0x00d8c82Eb65124Ea3452CaC59B64aCC230AA3482"], "worker":"test.aaa"}
  // ETH_PROXY (EthMiner, situation 2): {"id":1, "method":"eth_submitLogin", "params":["test"], "worker":"aaa"}
  
  if (jparams.children()->size() < 1) {
    responseError(idStr, StratumStatus::INVALID_USERNAME);
    return;
  }

  string fullName = jparams.children()->at(0).str();
  if (jsonRoot["worker"].type() == Utilities::JS::type::Str) {
    fullName += '.';
    fullName += jsonRoot["worker"].str();
  }
  fullName = stripEthAddrFromFullName(fullName);

  string password;
  if (jparams.children()->size() > 1)
  {
    password = jparams.children()->at(1).str();
  }

  checkUserAndPwd(idStr, fullName, password);
}

void StratumSessionEth::handleRequest_GetWork(const string &idStr, const JsonNode &jparams)
{
  sendMiningNotifyWithId(GetServer()->jobRepository_->getLatestStratumJobEx(), idStr);
}

void StratumSessionEth::handleRequest_SubmitHashrate(const string &idStr, const JsonNode &jparams)
{
  rpc2ResponseBoolean(idStr, true);
}

void StratumSessionEth::handleRequest_Submit(const string &idStr, const JsonNode &jparams)
{
  if (state_ != AUTHENTICATED)
  {
    rpc2ResponseError(idStr, StratumStatus::UNAUTHORIZED);

    // there must be something wrong, send reconnect command
    const string s = "{\"id\":null,\"method\":\"client.reconnect\",\"params\":[]}\n";
    sendData(s);

    return;
  }

  //etherminer (STRATUM)
  // {"id": 4, "method": "mining.submit",
  // "params": ["0x7b9d694c26a210b9f0d35bb9bfdd70a413351111.fatrat1117",
  // "ae778d304393d441bf8e1c47237261675caa3827997f671d8e5ec3bd5d862503",
  // "0x4cc7c01bfbe51c67",
  // "0xae778d304393d441bf8e1c47237261675caa3827997f671d8e5ec3bd5d862503",
  // "0x52fdd9e9a796903c6b88af4192717e77d9a9c6fa6a1366540b65e6bcfa9069aa"]}

  //Claymore (ETHPROXY)
  //{"id":4,"method":"eth_submitWork",
  //"params":["0x17a0eae8082fb64c","0x94a789fba387d454312db3287f8440f841de762522da8ba620b7fcf34a80330c",
  //"0x2cc7dad9f2f92519891a2d5f67378e646571b89e5994fe9290d6d669e480fdff"]}

  //NICEHASH_STRATUM
  // {"id": 244,
  //  "method": "mining.submit", 
  //  "params": [ "username", "bf0488aa", "6a909d9bbc0f" ]
  // }
  //Note in above example that minernonce is 6 bytes, because provided extranonce was 2 bytes.
  //If pool provides 3 bytes extranonce, then minernonce must be 5 bytes.
  auto params = (const_cast<JsonNode &>(jparams)).array();

  if (StratumProtocol::STRATUM == ethProtocol_ && params.size() < 5)
  {
    rpc2ResponseError(idStr, StratumStatus::ILLEGAL_PARARMS);
    return;
  }
  else if (StratumProtocol::ETHPROXY == ethProtocol_ && params.size() < 3)
  {
    rpc2ResponseError(idStr, StratumStatus::ILLEGAL_PARARMS);
    return;
  }
  else if (StratumProtocol::NICEHASH_STRATUM == ethProtocol_ && params.size() < 3)
  {
    rpc2ResponseError(idStr, StratumStatus::ILLEGAL_PARARMS);
    return;
  }

  
  string jobId, sNonce, sHeader;
  switch (ethProtocol_)
  {
  case StratumProtocol::STRATUM:
  {
    jobId = params[1].str();
    sNonce = params[2].str();
    sHeader = params[3].str();
  }
  break;
  case StratumProtocol::ETHPROXY:
  {
    sNonce = params[0].str();
    sHeader = params[1].str();
    jobId = sHeader;
  }
  break;
  case StratumProtocol::NICEHASH_STRATUM:
  {
    jobId = params[1].str();
    sNonce = params[2].str();
    sHeader = jobId;
  }
  break;
  }

  // Claymore's jobId starting with "0x"
  // Remove it here to avoid compatibility issues with Claymore or other miners
  if (jobId.size() >= 66) {
    jobId = jobId.substr(2, 64);
  }

  DLOG(INFO) << "submit: " << jobId << ", " << sNonce << ", " << sHeader;

  LocalJobEth *localJob = findLocalJob(jobId);
  // can't find local job
  if (localJob == nullptr)
  {
    rpc2ResponseError(idStr, StratumStatus::JOB_NOT_FOUND);
    return;
  }

  // can't find stratum job
  shared_ptr<StratumJobEx> exjob = GetServer()->jobRepository_->getStratumJobEx(localJob->jobId_);
  if (exjob.get() == nullptr) {
    rpc2ResponseError(idStr, StratumStatus::JOB_NOT_FOUND);
    return;
  }
  StratumJobEth *sjob = dynamic_cast<StratumJobEth *>(exjob->sjob_);

  if (StratumProtocol::NICEHASH_STRATUM == ethProtocol_) {
    if (sNonce.size() != 16) {
      sNonce = Strings::Format("%06x", extraNonce1_) + sNonce;
    }
  }
  
  uint64_t nonce = stoull(sNonce, nullptr, 16);
  uint32_t height = exjob->sjob_->height_;
  uint64_t networkDiff = Eth_TargetToDifficulty(sjob->rskNetworkTarget_.GetHex());
  // Used to prevent duplicate shares. (sHeader has a prefix "0x")
  uint64_t headerPrefix = stoull(sHeader.substr(2, 16), nullptr, 16);
  EthConsensus::Chain chain = sjob->chain_;

  ShareEth share;
  share.version_      = ShareEth::getVersion(chain);
  share.headerHash_   = headerPrefix;
  share.workerHashId_ = worker_.workerHashId_;
  share.userId_       = worker_.userId_;
  share.shareDiff_    = localJob->currentJobDiff_;
  share.networkDiff_  = networkDiff;
  share.timestamp_    = (uint64_t)time(nullptr);
  share.status_       = StratumStatus::REJECT_NO_REASON;
  share.height_       = height;
  share.nonce_        = nonce;
  share.sessionId_    = extraNonce1_; // TODO: fix it, set as real session id.
  share.ip_.fromIpv4Int(clientIpInt_);

  ServerEth *s = GetServer();

  LocalShare localShare(nonce, 0, 0);
  // can't add local share
  if (!localJob->addLocalShare(localShare))
  {
    rpc2ResponseError(idStr, StratumStatus::DUPLICATE_SHARE);
    // add invalid share to counter
    invalidSharesCounter_.insert((int64_t)time(nullptr), 1);
    return;
  }


  // The mixHash is used to submit the work to the Ethereum node.
  // We don't need to pay attention to whether the mixHash submitted
  // by the miner is correct, because we recalculated it.
  // SolvedShare will be accepted correctly by the ETH node if
  // the difficulty is reached in our calculations.
  uint256 shareMixHash;
  share.status_ = s->checkShareAndUpdateDiff(share, localJob->jobId_, nonce, uint256S(sHeader),
                                             localJob->jobDiffs_, shareMixHash);
  
  if (StratumStatus::isAccepted(share.status_)) {
    DLOG(INFO) << "share reached the diff: " << share.shareDiff_;
  }
  else {
    DLOG(INFO) << "share not reached the diff: " << share.shareDiff_;
  }

  // we send share to kafka by default, but if there are lots of invalid
  // shares in a short time, we just drop them.

  if (StratumStatus::isAccepted(share.status_))
  {
    if (StratumStatus::isSolved(share.status_)) {
      s->sendSolvedShare2Kafka(sNonce, sHeader, shareMixHash.GetHex(), height, networkDiff, worker_, chain);
    }

    diffController_->addAcceptedShare(share.shareDiff_);
    rpc2ResponseBoolean(idStr, true);
  }
  else
  {
    // add invalid share to counter
    invalidSharesCounter_.insert((int64_t)time(nullptr), 1);
    rpc2ResponseError(idStr, share.status_);
  }

  bool isSendShareToKafka = true;
  DLOG(INFO) << share.toString();

  // check if thers is invalid share spamming
  if (!StratumStatus::isAccepted(share.status_))
  {
    int64_t invalidSharesNum = invalidSharesCounter_.sum(time(nullptr), INVALID_SHARE_SLIDING_WINDOWS_SIZE);
    // too much invalid shares, don't send them to kafka
    if (invalidSharesNum >= INVALID_SHARE_SLIDING_WINDOWS_MAX_LIMIT)
    {
      isSendShareToKafka = false;
      LOG(WARNING) << "invalid share spamming, diff: "
                   << share.shareDiff_ << ", uid: " << worker_.userId_
                   << ", uname: \"" << worker_.userName_ << "\", ip: " << clientIp_
                   << "checkshare result: " << share.status_;
    }
  }

  if (isSendShareToKafka)
  {
    share.checkSum_ = share.checkSum();
    GetServer()->sendShare2Kafka((const uint8_t *)&share, sizeof(ShareEth));
  }
}

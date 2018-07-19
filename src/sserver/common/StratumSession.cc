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
#include "StratumSession.h"
#include "DiffController.h"
#include "StratumServer.h"
#include "sserver/bitcoin/StratumServerBitcoin.h"
#include "Utils.h"
#include "utilities_js.hpp"
#include <arith_uint256.h>
#include <arpa/inet.h>
#include <boost/algorithm/string.hpp>


//////////////////////////////// StratumSession ////////////////////////////////
StratumSession::StratumSession(evutil_socket_t fd, struct bufferevent *bev,
                               Server *server, struct sockaddr *saddr,
                               const int32_t shareAvgSeconds,
                               const uint32_t extraNonce1) :
shareAvgSeconds_(shareAvgSeconds),
shortJobIdIdx_(0), agentSessions_(nullptr), isDead_(false),
invalidSharesCounter_(INVALID_SHARE_SLIDING_WINDOWS_SIZE),
bev_(bev), fd_(fd), server_(server)
{
  state_ = CONNECTED;
  currDiff_    = 0U;
  extraNonce1_ = extraNonce1;

  // usually stratum job interval is 30~60 seconds, 10 is enough for miners
  // should <= 10, we use short_job_id,  range: [0 ~ 9]. do NOT change it.
  kMaxNumLocalJobs_ = 10;
  assert(kMaxNumLocalJobs_ <= 10);

  inBuf_  = evbuffer_new();
  isLongTimeout_    = false;
  isNiceHashClient_ = false;

  clientAgent_ = "unknown";
  // ipv4
  clientIp_.resize(INET_ADDRSTRLEN);
  struct sockaddr_in *saddrin = (struct sockaddr_in *)saddr;
  clientIp_ = inet_ntop(AF_INET, &saddrin->sin_addr,
                        (char *)clientIp_.data(), (socklen_t)clientIp_.size());
  clientIpInt_ = saddrin->sin_addr.s_addr;

  setup();

  LOG(INFO) << "client connect, ip: " << clientIp_;
}

StratumSession::~StratumSession() {
  if (agentSessions_ != nullptr) {
    delete agentSessions_;
    agentSessions_ = nullptr;
  }

  LOG(INFO) << "close stratum session, ip: " << clientIp_
  << ", name: \"" << worker_.fullName_ << "\""
  << ", agent: \"" << clientAgent_ << "\"";

//  close(fd_);  // we don't need to close because we set 'BEV_OPT_CLOSE_ON_FREE'
  evbuffer_free(inBuf_);
  bufferevent_free(bev_);
}

bool StratumSession::initialize() {
  diffController_ = make_shared<DiffController>(server_->defaultDifficultyController_.get());
  return true;
}

void StratumSession::markAsDead() {
  // mark as dead
  isDead_ = true;

  // sent event to kafka: miner_dead
  if (worker_.userId_ > 0) {
    string eventJson;
    eventJson = Strings::Format("{\"created_at\":\"%s\","
                                "\"type\":\"miner_dead\","
                                "\"content\":{"
                                "\"user_id\":%d,\"user_name\":\"%s\","
                                "\"worker_name\":\"%s\","
                                "\"client_agent\":\"%s\",\"ip\":\"%s\""
                                "}}",
                                date("%F %T").c_str(),
                                worker_.userId_, worker_.userName_.c_str(),
                                worker_.workerName_.c_str(),
                                clientAgent_.c_str(), clientIp_.c_str());
    server_->sendCommonEvents2Kafka(eventJson);
  }
}

bool StratumSession::isDead() {
  return (isDead_ == true) ? true : false;
}

void StratumSession::setup() {
  // we set 15 seconds, will increase the timeout after sub & auth
  setReadTimeout(15);
}

void StratumSession::setReadTimeout(const int32_t timeout) {
  // clear it
  bufferevent_set_timeouts(bev_, NULL, NULL);

  // set a new one
  struct timeval rtv = {timeout, 0};
  struct timeval wtv = {120, 0};
  bufferevent_set_timeouts(bev_, &rtv, &wtv);
}

// if read success, will remove data from eventbuf
bool StratumSession::tryReadLine(string &line) {
  line.clear();

  // find eol
  struct evbuffer_ptr loc;
  loc = evbuffer_search_eol(inBuf_, nullptr, nullptr, EVBUFFER_EOL_LF);
  if (loc.pos == -1) {
    return false;  // not found
  }

  // copies and removes the first datlen bytes from the front of buf
  // into the memory at data
  line.resize(loc.pos + 1);  // containing "\n"
  evbuffer_remove(inBuf_, (void *)line.data(), line.size());
  return true;
}

bool StratumSession::validate(const JsonNode &jmethod, const JsonNode &jparams)
{
  if (jmethod.type() == Utilities::JS::type::Str &&
      jmethod.size() != 0 &&
      jparams.type() == Utilities::JS::type::Array)
  {
    return true;
  }

  return false;
}

void StratumSession::handleLine(const string &line) {
  DLOG(INFO) << "recv(" << line.size() << "): " << line;

  JsonNode jnode;
  if (!JsonNode::parse(line.data(), line.data() + line.size(), jnode)) {
    LOG(ERROR) << "decode line fail, not a json string. string value: \"" << line.c_str() << "\"";
    return;
  }
  JsonNode jid = jnode["id"];
  JsonNode jmethod = jnode["method"];
  JsonNode jparams = jnode["params"];

  string idStr = "null";
  if (jid.type() == Utilities::JS::type::Int) {
    idStr = jid.str();
  } else if (jid.type() == Utilities::JS::type::Str) {
    idStr = "\"" + jnode["id"].str() + "\"";
  }

  if (validate(jmethod, jparams)) {
    handleRequest(idStr, jmethod.str(), jparams, jnode);
    return;
  }

  // invalid params
  responseError(idStr, StratumStatus::ILLEGAL_PARARMS);
}

void StratumSession::responseError(const string &idStr, int errCode) {
  //
  // {"id": 10, "result": null, "error":[21, "Job not found", null]}
  //
  char buf[1024];
  int len = snprintf(buf, sizeof(buf),
                     "{\"id\":%s,\"result\":null,\"error\":[%d,\"%s\",null]}\n",
                     idStr.empty() ? "null" : idStr.c_str(),
                     errCode, StratumStatus::toString(errCode));                  
  sendData(buf, len);
}

void StratumSession::responseTrue(const string &idStr) {
  const string s = "{\"id\":" + idStr + ",\"result\":true,\"error\":null}\n";
  sendData(s);
}

void StratumSession::rpc2ResponseBoolean(const string &idStr, bool result) {
  const string s = Strings::Format("{\"id\":%s,\"jsonrpc\":\"2.0\",\"result\":%s}\n", idStr.c_str(), result ? "true" : "false");
  sendData(s);
}

void StratumSession::rpc2ResponseError(const string &idStr, int errCode) {
  //
  // {"id": 10, "result": null, "error":[21, "Job not found", null]}
  //
  char buf[1024];
  int len = snprintf(buf, sizeof(buf),
                     "{\"id\":%s,\"jsonrpc\":\"2.0\",\"result\":null,\"error\":[%d,\"%s\",null]}\n",
                     idStr.empty() ? "null" : idStr.c_str(),
                     errCode, StratumStatus::toString(errCode));                  
  sendData(buf, len);
}

void StratumSession::handleRequest(const string &idStr, const string &method,
                                   const JsonNode &jparams, const JsonNode &jroot)
{
  if (method == "mining.submit" ||
      "eth_submitWork" == method ||
      "submit" == method)
  { // most of requests are 'mining.submit'
    // "eth_submitWork": claymore eth
    // "submit": bytom
    handleRequest_Submit(idStr, jparams);
  }
  else if (method == "mining.subscribe")
  {
    handleRequest_Subscribe(idStr, jparams);
  }
  else if (method == "mining.authorize" ||
           "eth_submitLogin" == method ||
           "login" == method)
  {
    // "eth_submitLogin": claymore eth
    // "login": bytom
    handleRequest_Authorize(idStr, jparams, jroot);
  }
  else if (method == "mining.multi_version")
  {
    handleRequest_MultiVersion(idStr, jparams);
  }
  else if (method == "mining.suggest_target")
  {
    handleRequest_SuggestTarget(idStr, jparams);
  }
  else if (method == "mining.suggest_difficulty")
  {
    handleRequest_SuggestDifficulty(idStr, jparams);
  }
  else if (method == "mining.extranonce.subscribe")
  {
    //Claymore will send this for sia but no need response
    //Do nothing for now
  }
  else if ("eth_getWork" == method ||
           "getwork" == method)
  {
    handleRequest_GetWork(idStr, jparams);
  }
  else if ("eth_submitHashrate" == method)
  {
    handleRequest_SubmitHashrate(idStr, jparams);
  }
  else
  {
    if (!handleRequest_Specific(idStr, method, jparams, jroot))
    {
      // unrecognised method, just ignore it
      LOG(WARNING) << "unrecognised method: \"" << method << "\""
                   << ", client: " << clientIp_ << "/" << clientAgent_;
    }
  }
}

void StratumSession::handleRequest_MultiVersion(const string &idStr,
                                                const JsonNode &jparams) {
//  // we ignore right now, 2016-07-04
//  const string s = Strings::Format("{\"id\":%s,\"method\":\"mining.midstate_change\",\"params\":[4]}",
//                                   idStr.c_str());
//  sendData(s);
}

static
bool _isNiceHashAgent(const string &clientAgent) {
  if (clientAgent.length() < 9) {
    return false;
  }
  string agent = clientAgent;
  // tolower
  std::transform(agent.begin(), agent.end(), agent.begin(), ::tolower);
  if (agent.substr(0, 9) == "nicehash/") {
    return true;
  }
  return false;
}

void StratumSession::handleRequest_Subscribe(const string &idStr,
                                             const JsonNode &jparams) {
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
  //  params[1] = session id of pool [optional]
  //
  // client request eg.:
  //  {"id": 1, "method": "mining.subscribe", "params": ["bfgminer/4.4.0-32-gac4e9b3", "01ad557d"]}
  //
  if (jparams.children()->size() >= 1) {
    clientAgent_ = jparams.children()->at(0).str().substr(0, 30);  // 30 is max len
    clientAgent_ = filterWorkerName(clientAgent_);
  }

#endif // WORK_WITH_STRATUM_SWITCHER


  //  result[0] = 2-tuple with name of subscribed notification and subscription ID.
  //              Theoretically it may be used for unsubscribing, but obviously miners won't use it.
  //  result[1] = ExtraNonce1, used for building the coinbase.
  //  result[2] = Extranonce2_size, the number of bytes that the miner users for its ExtraNonce2 counter
  assert(kExtraNonce2Size_ == 8);
  const string s = Strings::Format("{\"id\":%s,\"result\":[[[\"mining.set_difficulty\",\"%08x\"]"
                                   ",[\"mining.notify\",\"%08x\"]],\"%08x\",%d],\"error\":null}\n",
                                   idStr.c_str(), extraNonce1_, extraNonce1_, extraNonce1_, kExtraNonce2Size_);
  sendData(s);

  if (clientAgent_ == "__PoolWatcher__") {
    isLongTimeout_ = true;
  }

  // check if it's NinceHash/x.x.x
  if (_isNiceHashAgent(clientAgent_))
    isNiceHashClient_ = true;

  //
  // check if it's BTCAgent
  //
  if (strncmp(clientAgent_.c_str(), BTCCOM_MINER_AGENT_PREFIX,
              std::min(clientAgent_.length(), strlen(BTCCOM_MINER_AGENT_PREFIX))) == 0) {
    LOG(INFO) << "agent model, client: " << clientAgent_;
    agentSessions_ = new AgentSessions(shareAvgSeconds_, this);

    isLongTimeout_ = true;  // will set long timeout
  }
}

void StratumSession::_handleRequest_AuthorizePassword(const string &password) {
  // testcase: TEST(StratumSession, SetDiff)
  using namespace boost::algorithm;

  uint64_t d = 0u, md = 0u;
  vector<string> arr;  // key=value,key=value
  split(arr, password, is_any_of(","));
  if (arr.size() == 0)
    return;

  for (auto it = arr.begin(); it != arr.end(); it++) {
    vector<string> arr2;  // key,value
    split(arr2, *it, is_any_of("="));
    if (arr2.size() != 2 || arr2[1].empty()) {
      continue;
    }

    if (arr2[0] == "d") {
      // 'd' : start difficulty
      d = strtoull(arr2[1].c_str(), nullptr, 10);
    }
    else if (arr2[0] == "md") {
      // 'md' : minimum difficulty
      md = strtoull(arr2[1].c_str(), nullptr, 10);
    }
  }

  d  = formatDifficulty(d);
  md = formatDifficulty(md);

  // set min diff first
  if (md >= server_->defaultDifficultyController_->kMinDiff_) {
    diffController_->setMinDiff(md);
  }

  // than set current diff
  if (d >= server_->defaultDifficultyController_->kMinDiff_) {
    diffController_->resetCurDiff(d);
  }
}

void StratumSession::checkUserAndPwd(const string &idStr, const string &fullName, const string &password)
{
  if (!password.empty())
  {
    _handleRequest_AuthorizePassword(password);
  }

  const string userName = worker_.getUserName(fullName);

  const int32_t userId = server_->userInfo_->getUserId(userName);
  if (userId <= 0)
  {
    LOG(ERROR) << "invalid username=" << userName << ", userId=" << userId;
    responseError(idStr, StratumStatus::INVALID_USERNAME);
    return;
  }

  // auth success
  // some protocols do not need response. eg. bytom
  if (needToSendLoginResponse())
    responseTrue(idStr);

  state_ = AUTHENTICATED;

  // set id & names, will filter workername in this func
  worker_.setUserIDAndNames(userId, fullName);
  server_->userInfo_->addWorker(worker_.userId_, worker_.workerHashId_,
                                worker_.workerName_, clientAgent_);
  DLOG(INFO) << "userId: " << worker_.userId_
             << ", wokerHashId: " << worker_.workerHashId_ << ", workerName:" << worker_.workerName_;

  // set read timeout to 10 mins, it's enought for most miners even usb miner.
  // if it's a pool watcher, set timeout to a week
  setReadTimeout(isLongTimeout_ ? 86400 * 7 : 60 * 10);

  // send latest stratum job
  sendMiningNotify(server_->jobRepository_->getLatestStratumJobEx(), true /* is first job */);

  // sent events to kafka: miner_connect
  {
    string eventJson;
    eventJson = Strings::Format("{\"created_at\":\"%s\","
                                "\"type\":\"miner_connect\","
                                "\"content\":{"
                                "\"user_id\":%d,\"user_name\":\"%s\","
                                "\"worker_name\":\"%s\","
                                "\"client_agent\":\"%s\",\"ip\":\"%s\""
                                "}}",
                                date("%F %T").c_str(),
                                worker_.userId_, worker_.userName_.c_str(),
                                worker_.workerName_.c_str(),
                                clientAgent_.c_str(), clientIp_.c_str());
    server_->sendCommonEvents2Kafka(eventJson);
  }
}

void StratumSession::handleRequest_Authorize(const string &idStr,
                                             const JsonNode &jparams, const JsonNode &/*jroot*/)
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

void StratumSession::handleExMessage_AuthorizeAgentWorker(const int64_t workerId,
                                                          const string &clientAgent,
                                                          const string &workerName) {
  if (state_ != AUTHENTICATED) {
    LOG(ERROR) << "curr stratum session has NOT auth yet";
    return;
  }
  server_->userInfo_->addWorker(worker_.userId_, workerId,
                                workerName, clientAgent);
}

void StratumSession::_handleRequest_SetDifficulty(uint64_t suggestDiff) {
  diffController_->resetCurDiff(formatDifficulty(suggestDiff));
}

void StratumSession::handleRequest_SuggestTarget(const string &idStr,
                                                 const JsonNode &jparams) {
  if (state_ != CONNECTED) {
    return;  // suggest should be call before subscribe
  }
  if (jparams.children()->size() == 0) {
    responseError(idStr, StratumStatus::ILLEGAL_PARARMS);
    return;
  }
  _handleRequest_SetDifficulty(TargetToDiff(jparams.children()->at(0).str()));
}

void StratumSession::handleRequest_SuggestDifficulty(const string &idStr,
                                                     const JsonNode &jparams) {
  if (state_ != CONNECTED) {
    return;  // suggest should be call before subscribe
  }
  if (jparams.children()->size() == 0) {
    responseError(idStr, StratumStatus::ILLEGAL_PARARMS);
    return;
  }
  _handleRequest_SetDifficulty(jparams.children()->at(0).uint64());
}

void StratumSession::handleRequest_Submit(const string &idStr,
                                          const JsonNode &jparams) {
  if (state_ != AUTHENTICATED) {
    responseError(idStr, StratumStatus::UNAUTHORIZED);

    // there must be something wrong, send reconnect command
    const string s = "{\"id\":null,\"method\":\"client.reconnect\",\"params\":[]}\n";
    sendData(s);

    return;
  }

  //  params[0] = Worker Name
  //  params[1] = Job ID
  //  params[2] = ExtraNonce 2
  //  params[3] = nTime
  //  params[4] = nonce
  if (jparams.children()->size() < 5) {
    responseError(idStr, StratumStatus::ILLEGAL_PARARMS);
    return;
  }

  uint8_t shortJobId;
  if (isNiceHashClient_) {
    shortJobId = (uint8_t)(jparams.children()->at(1).uint64() % 10);
  } else {
    shortJobId = (uint8_t)jparams.children()->at(1).uint32();
  }
  const uint64_t extraNonce2 = jparams.children()->at(2).uint64_hex();
  uint32_t nTime             = jparams.children()->at(3).uint32_hex();
  const uint32_t nonce       = jparams.children()->at(4).uint32_hex();

  handleRequest_Submit(idStr, shortJobId, extraNonce2, nonce, nTime,
                       false /* not agent session */, nullptr);
}

// TODO: remove goto.
void StratumSession::handleRequest_Submit(const string &idStr,
                                          const uint8_t shortJobId,
                                          const uint64_t extraNonce2,
                                          const uint32_t nonce,
                                          uint32_t nTime,
                                          bool isAgentSession,
                                          DiffController *sessionDiffController) {
  ServerBitcoin* serverBitcoin = dynamic_cast<ServerBitcoin*>(server_);
  if(!serverBitcoin)
  {
    LOG(FATAL) << "StratumSession::handleRequest_Submit. cast ServerBitcoin failed";
  }

  //
  // if share is from agent session, we don't need to send reply json
  //
  if (isAgentSession == true && agentSessions_ == nullptr) {
    LOG(ERROR) << "can't find agentSession, worker: " << worker_.fullName_;
    return;
  }

  const string extraNonce2Hex = Strings::Format("%016llx", extraNonce2);
  assert(extraNonce2Hex.length()/2 == kExtraNonce2Size_);

  LocalJob *localJob = findLocalJob(shortJobId);
  if (localJob == nullptr) {
    // if can't find localJob, could do nothing
    if (isAgentSession == false) {
    	responseError(idStr, StratumStatus::JOB_NOT_FOUND);
    }
    
    LOG(INFO) << "rejected share: " << StratumStatus::toString(StratumStatus::JOB_NOT_FOUND)
    << ", worker: " << worker_.fullName_ << ", Share(id: " << idStr << ", shortJobId: "
    << (int)shortJobId << ", nTime: " << nTime << "/" << date("%F %T", nTime) << ")";
    return;
  }

  uint32_t height = 0;

  shared_ptr<StratumJobEx> exjob;
  exjob = server_->jobRepository_->getStratumJobEx(localJob->jobId_);

  if (exjob.get() != NULL) {
    // 0 means miner use stratum job's default block time
    if (nTime == 0) {
        nTime = exjob->sjob_->nTime_;
    }

    height = exjob->sjob_->height_;
  }

  ShareBitcoin share;
  share.version_      = ShareBitcoin::CURRENT_VERSION;
  share.jobId_        = localJob->jobId_;
  share.workerHashId_ = worker_.workerHashId_;
  share.userId_       = worker_.userId_;
  share.shareDiff_    = localJob->jobDifficulty_;
  share.blkBits_      = localJob->blkBits_;
  share.timestamp_    = (uint64_t)time(nullptr);
  share.height_       = height;
  share.nonce_        = nonce;
  share.sessionId_    = extraNonce1_;
  share.status_       = StratumStatus::REJECT_NO_REASON;
  share.ip_.fromIpv4Int(clientIpInt_);

  if (isAgentSession == true) {
    const uint16_t sessionId = (uint16_t)(extraNonce2 >> 32);

    // reset to agent session's workerId
    share.workerHashId_ = agentSessions_->getWorkerId(sessionId);
    if (share.workerHashId_ == 0) {
      LOG(ERROR) << "invalid workerId 0, sessionId: " << sessionId << ", worker: " << worker_.fullName_;
      return;
    }

    // reset to agent session's diff
    if (localJob->agentSessionsDiff2Exp_.size() < (size_t)sessionId + 1) {
      LOG(ERROR) << "can't find agent session's diff, sessionId: " << sessionId << ", worker: " << worker_.fullName_;
      return;
    }
    share.shareDiff_ = (uint64_t)exp2(localJob->agentSessionsDiff2Exp_[sessionId]);
  }

  // calc jobTarget
  uint256 jobTarget;
  DiffToTarget(share.shareDiff_, jobTarget);

  // we send share to kafka by default, but if there are lots of invalid
  // shares in a short time, we just drop them.
  bool isSendShareToKafka = true;

  LocalShare localShare(extraNonce2, nonce, nTime);

  // can't find local share
  if (!localJob->addLocalShare(localShare)) {
    share.status_ = StratumStatus::DUPLICATE_SHARE;

    if (isAgentSession == false) {
      responseError(idStr, share.status_);
    }

    // add invalid share to counter
    invalidSharesCounter_.insert((int64_t)time(nullptr), 1);

    goto finish;
  }

#ifdef  USER_DEFINED_COINBASE
  // check block header
  share.status_ = serverBitcoin->checkShare(share, extraNonce1_, extraNonce2Hex,
                                     nTime, nonce, jobTarget,
                                     worker_.fullName_,
                                     &localJob->userCoinbaseInfo_);
#else
  // check block header
  share.status_ = serverBitcoin->checkShare(share, extraNonce1_, extraNonce2Hex,
                                     nTime, nonce, jobTarget,
                                     worker_.fullName_);
#endif

  // accepted share
  if (StratumStatus::isAccepted(share.status_)) {

    // agent miner's diff controller
    if (isAgentSession && sessionDiffController != nullptr) {
      sessionDiffController->addAcceptedShare(share.shareDiff_);
    }

    if (isAgentSession == false) {
    	diffController_->addAcceptedShare(share.shareDiff_);
      responseTrue(idStr);
    }
  } else {
    // reject share
    if (isAgentSession == false) {
    	responseError(idStr, share.status_);
    }

    // add invalid share to counter
    invalidSharesCounter_.insert((int64_t)time(nullptr), 1);
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

      LOG(INFO) << "invalid share spamming, diff: "
      << share.shareDiff_ << ", worker: " << worker_.fullName_ << ", agent: "
      << clientAgent_ << ", ip: " << clientIp_;
    }
  }

  if (isSendShareToKafka) {
    share.checkSum_ = share.checkSum();
  	server_->sendShare2Kafka((const uint8_t *)&share, sizeof(ShareBitcoin));
  }
  return;
}

StratumSession::LocalJob *StratumSession::findLocalJob(uint8_t shortJobId) {
  //DLOG(INFO) << "findLocalJob id=" << shortJobId;
  for (auto rit = localJobs_.rbegin(); rit != localJobs_.rend(); ++rit) {
    //DLOG(INFO) << "search id=" << (int)rit->shortJobId_;
    if ((int)rit->shortJobId_ == (int)shortJobId) {
      //DLOG(INFO) << "local job found";
      return &(*rit);
    }
  }
  return nullptr;
}

void StratumSession::sendSetDifficulty(const uint64_t difficulty) {
  string s;
  if (!server_->isDevModeEnable_) {
    s = Strings::Format("{\"id\":null,\"method\":\"mining.set_difficulty\""
                         ",\"params\":[%" PRIu64"]}\n",
                         difficulty);
  } else {
    s = Strings::Format("{\"id\":null,\"method\":\"mining.set_difficulty\""
                         ",\"params\":[%.3f]}\n",
                         server_->minerDifficulty_);
  }

  sendData(s);
}

uint8_t StratumSession::allocShortJobId() {
  // return range: [0, 9]
  if (shortJobIdIdx_ >= 10) {
    shortJobIdIdx_ = 0;
  }
  return shortJobIdIdx_++;
}

void StratumSession::sendMiningNotify(shared_ptr<StratumJobEx> exJobPtrShared, bool isFirstJob) {
  StratumJobExBitcoin* exJobPtr = static_cast<StratumJobExBitcoin*>(exJobPtrShared.get());
  if (state_ < AUTHENTICATED || exJobPtr == nullptr) {
    return;
  }
  StratumJobBitcoin *sjob = dynamic_cast<StratumJobBitcoin*>(exJobPtr->sjob_);

  localJobs_.push_back(LocalJob());
  LocalJob &ljob = *(localJobs_.rbegin());
  ljob.blkBits_       = sjob->nBits_;
  ljob.jobId_         = sjob->jobId_;
  ljob.shortJobId_    = allocShortJobId();
  ljob.jobDifficulty_ = diffController_->calcCurDiff();

#ifdef USER_DEFINED_COINBASE
  // add the User's coinbaseInfo to the coinbase1's tail
  string userCoinbaseInfo = server_->userInfo_->getCoinbaseInfo(worker_.userId_);
  ljob.userCoinbaseInfo_ = userCoinbaseInfo;
#endif

  if (agentSessions_ != nullptr)
  {
    // calc diff and save to ljob
    agentSessions_->calcSessionsJobDiff(ljob.agentSessionsDiff2Exp_);

    // get ex-message
    string exMessage;
    agentSessions_->getSessionsChangedDiff(ljob.agentSessionsDiff2Exp_, exMessage);
    if (exMessage.size())
    	sendData(exMessage);
  }

  // set difficulty
  if (currDiff_ != ljob.jobDifficulty_) {
    sendSetDifficulty(ljob.jobDifficulty_);
    currDiff_ = ljob.jobDifficulty_;
  }

  string notifyStr;
  notifyStr.reserve(2048);

  // notify1
  notifyStr.append(exJobPtr->miningNotify1_);

  // jobId
  if (isNiceHashClient_) {
    //
    // we need to send unique JobID to NiceHash Client, they have problems with
    // short Job ID
    //
    const uint64_t niceHashJobId = (uint64_t)time(nullptr) * 10 + ljob.shortJobId_;
    notifyStr.append(Strings::Format("% " PRIu64"", niceHashJobId));
  } else {
    notifyStr.append(Strings::Format("%u", ljob.shortJobId_));  // short jobId
  }

  // notify2
  notifyStr.append(exJobPtr->miningNotify2_);

  string coinbase1 = exJobPtr->coinbase1_;

#ifdef USER_DEFINED_COINBASE
  string userCoinbaseHex;
  Bin2Hex((const uint8_t *)ljob.userCoinbaseInfo_.c_str(), ljob.userCoinbaseInfo_.size(), userCoinbaseHex);
  // replace the last `userCoinbaseHex.size()` bytes to `userCoinbaseHex`
  coinbase1.replace(coinbase1.size()-userCoinbaseHex.size(), userCoinbaseHex.size(), userCoinbaseHex);
#endif

  // coinbase1
  notifyStr.append(coinbase1);

  // notify3
  if (isFirstJob)
  	notifyStr.append(exJobPtr->miningNotify3Clean_);
  else
    notifyStr.append(exJobPtr->miningNotify3_);

  sendData(notifyStr);  // send notify string

  // clear localJobs_
  clearLocalJobs();
}

void StratumSession::clearLocalJobs()
{
  while (localJobs_.size() >= kMaxNumLocalJobs_)
  {
    localJobs_.pop_front();
  }
}

void StratumSession::sendData(const char *data, size_t len) {
  // add data to a bufferevent’s output buffer
  // it is automatically locked so we don't need to lock
  bufferevent_write(bev_, data, len);
  DLOG(INFO) << "send(" << len << "): " << data;
}

// if read a message (ex-message or stratum) success should return true,
// otherwise return false.
bool StratumSession::handleMessage() {
  //
  // handle ex-message
  //
  const size_t evBufLen = evbuffer_get_length(inBuf_);

  // no matter what kind of messages, length should at least 4 bytes
  if (evBufLen < 4)
    return false;

  uint8_t buf[4];
  evbuffer_copyout(inBuf_, buf, 4);

  // handle ex-message
  if (buf[0] == CMD_MAGIC_NUMBER) {
    const uint16_t exMessageLen = *(uint16_t *)(buf + 2);

    //
    // It is not a valid message if exMessageLen < 4, because the length of
    // message header (1 byte magic_number + 1 byte type/cmd + 2 bytes length)
    // is 4. The header length is included in exMessageLen.
    //
    // Without the checking at below, send "\x0f\xff\x00\x00" to the sserver,
    // and it will fall into infinite loop with handleMessage() calling.
    //
    if (exMessageLen < 4) {
      LOG(ERROR) << "received invalid ex-message, type: " << std::hex << (int)buf[1]
        << ", len: " << exMessageLen;
      return false;
    }
    
    if (evBufLen < exMessageLen)  // didn't received the whole message yet
      return false;

    // copies and removes the first datlen bytes from the front of buf
    // into the memory at data
    string exMessage;
    exMessage.resize(exMessageLen);
    evbuffer_remove(inBuf_, (uint8_t *)exMessage.data(), exMessage.size());

    switch (buf[1]) {
      case CMD_SUBMIT_SHARE:
        handleExMessage_SubmitShare(&exMessage);
        break;
      case CMD_SUBMIT_SHARE_WITH_TIME:
        handleExMessage_SubmitShareWithTime(&exMessage);
        break;
      case CMD_REGISTER_WORKER:
        handleExMessage_RegisterWorker(&exMessage);
        break;
      case CMD_UNREGISTER_WORKER:
        handleExMessage_UnRegisterWorker(&exMessage);
        break;

      default:
        LOG(ERROR) << "received unknown ex-message, type: " << std::hex << (int)buf[1]
        << ", len: " << exMessageLen;
        break;
    }
    return true;  // read message success, return true
  }

  //
  // handle stratum message
  //
  string line;
  if (tryReadLine(line)) {
    handleLine(line);
    return true;
  }

  return false;  // read mesasge failure
}

void StratumSession::readBuf(struct evbuffer *buf) {
  // moves all data from src to the end of dst
  evbuffer_add_buffer(inBuf_, buf);

  while (handleMessage()) {
  }
}

void StratumSession::handleExMessage_RegisterWorker(const string *exMessage) {
  if (agentSessions_ == nullptr) {
    return;
  }
  agentSessions_->handleExMessage_RegisterWorker(exMessage);
}

void StratumSession::handleExMessage_SubmitShare(const string *exMessage) {
  if (agentSessions_ == nullptr) {
    return;
  }
  // without timestamp
  agentSessions_->handleExMessage_SubmitShare(exMessage, false);
}

void StratumSession::handleExMessage_SubmitShareWithTime(const string *exMessage) {
  if (agentSessions_ == nullptr) {
    return;
  }
  // with timestamp
  agentSessions_->handleExMessage_SubmitShare(exMessage, true);
}

void StratumSession::handleExMessage_UnRegisterWorker(const string *exMessage) {
  if (agentSessions_ == nullptr) {
    return;
  }
  agentSessions_->handleExMessage_UnRegisterWorker(exMessage);
}

uint32_t StratumSession::getSessionId() const {
  return extraNonce1_;
}


///////////////////////////////// StratumSessionSia ////////////////////////////////
StratumSessionSia::StratumSessionSia(evutil_socket_t fd,
                                     struct bufferevent *bev,
                                     Server *server,
                                     struct sockaddr *saddr,
                                     const int32_t shareAvgSeconds,
                                     const uint32_t extraNonce1) : StratumSession(fd,
                                                                                  bev,
                                                                                  server,
                                                                                  saddr,
                                                                                  shareAvgSeconds,
                                                                                  extraNonce1),
                                                                                  shortJobId_(0)
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
  exjob = server_->jobRepository_->getStratumJobEx(localJob->jobId_);
  if (nullptr == exjob || nullptr == exjob->sjob_) {
    responseError(idStr, StratumStatus::JOB_NOT_FOUND);
    LOG(ERROR) << "sia local job not found " << std::hex << localJob->jobId_;
    return;
  }

  uint64 nonce = *((uint64*) (bHeader + 32));
  LocalShare localShare(nonce, 0, 0);
  if (!server_->isEnableSimulator_ && !localJob->addLocalShare(localShare))
  {
    responseError(idStr, StratumStatus::DUPLICATE_SHARE);
    LOG(ERROR) << "duplicated share nonce " << std::hex << nonce;
    // add invalid share to counter
    invalidSharesCounter_.insert((int64_t)time(nullptr), 1);
    return;
  }

  ShareBitcoin share;
  share.version_ = ShareBitcoin::CURRENT_VERSION;
  share.jobId_ = localJob->jobId_;
  share.workerHashId_ = worker_.workerHashId_;
  share.ip_ = clientIpInt_;
  share.userId_ = worker_.userId_;
  share.shareDiff_ = localJob->jobDifficulty_;
  share.timestamp_ = (uint32_t)time(nullptr);
  share.status_ = StratumStatus::REJECT_NO_REASON;

  arith_uint256 shareTarget(str);
  arith_uint256 networkTarget = UintToArith256(exjob->sjob_->networkTarget_);
  
  if (shareTarget < networkTarget) {
    //valid share
    //submit share
    ServerSia *s = dynamic_cast<ServerSia*> (server_);
    s->sendSolvedShare2Kafka(bHeader, 80);
    diffController_->addAcceptedShare(share.shareDiff_);
    LOG(INFO) << "sia solution found";
  }

  rpc2ResponseBoolean(idStr, true);
  share.checkSum_ = share.checkSum();
  server_->sendShare2Kafka((const uint8_t *)&share, sizeof(ShareBitcoin));
}

///////////////////////////////// AgentSessions ////////////////////////////////
AgentSessions::AgentSessions(const int32_t shareAvgSeconds,
                             StratumSession *stratumSession)
:shareAvgSeconds_(shareAvgSeconds), stratumSession_(stratumSession)
{
  kDefaultDiff2Exp_ = (uint8_t)log2(DiffController::kDefaultDiff_);

  // we just pre-alloc all
  workerIds_.resize(UINT16_MAX, 0);
  diffControllers_.resize(UINT16_MAX, nullptr);
  curDiff2ExpVec_.resize(UINT16_MAX, kDefaultDiff2Exp_);
}

AgentSessions::~AgentSessions() {
  for (auto ptr : diffControllers_) {
    if (ptr != nullptr) {
      delete ptr;
    }
  }
}

int64_t AgentSessions::getWorkerId(const uint16_t sessionId) {
  return workerIds_[sessionId];
}

void AgentSessions::handleExMessage_RegisterWorker(const string *exMessage) {
  //
  // CMD_REGISTER_WORKER:
  // | magic_number(1) | cmd(1) | len (2) | session_id(2) | clientAgent | worker_name |
  //
  if (exMessage->size() < 8 || exMessage->size() > 100 /* 100 bytes is big enough */)
    return;

  const uint8_t *p = (uint8_t *)exMessage->data();
  const uint16_t sessionId = *(uint16_t *)(p + 4);
  if (sessionId > AGENT_MAX_SESSION_ID)
    return;

  // copy out string and make sure end with zero
  string clientStr;
  clientStr.append(exMessage->begin() + 6, exMessage->end());
  clientStr[clientStr.size() - 1] = '\0';

  // client agent
  const char *clientAgentPtr = clientStr.c_str();
  const string clientAgent = filterWorkerName(clientAgentPtr);

  // worker name
  string workerName;
  if (strlen(clientAgentPtr) < clientStr.size() - 2) {
    workerName = filterWorkerName(clientAgentPtr + strlen(clientAgentPtr) + 1);
  }
  if (workerName.empty())
    workerName = DEFAULT_WORKER_NAME;

  // worker Id
  const int64_t workerId = StratumWorker::calcWorkerId(workerName);

  DLOG(INFO) << "[agent] clientAgent: " << clientAgent
  << ", workerName: " << workerName << ", workerId: "
  << workerId << ", session id:" << sessionId;

  // ptr can't be nullptr, just make it easy for test
  if (stratumSession_ == nullptr)
    return;

  // set sessionId -> workerId
  workerIds_[sessionId] = workerId;

  // deletes managed object
  if (diffControllers_[sessionId] != nullptr) {
    delete diffControllers_[sessionId];
    diffControllers_[sessionId] = nullptr;
  }

  // acquires new pointer
  assert(diffControllers_[sessionId] == nullptr);
  diffControllers_[sessionId] = new DiffController(stratumSession_->server_->defaultDifficultyController_.get());

  // set curr diff to default Diff
  curDiff2ExpVec_[sessionId] = kDefaultDiff2Exp_;

  // submit worker info to stratum session
  stratumSession_->handleExMessage_AuthorizeAgentWorker(workerId, clientAgent,
                                                        workerName);
}

void AgentSessions::handleExMessage_SubmitShare(const string *exMessage,
                                                const bool isWithTime) {
  //
  // CMD_SUBMIT_SHARE / CMD_SUBMIT_SHARE_WITH_TIME:
  // | magic_number(1) | cmd(1) | len (2) | jobId (uint8_t) | session_id (uint16_t) |
  // | extra_nonce2 (uint32_t) | nNonce (uint32_t) | [nTime (uint32_t) |]
  //
  if (exMessage->size() != (isWithTime ? 19 : 15))
    return;

  const uint8_t *p = (uint8_t *)exMessage->data();
  const uint8_t shortJobId = *(uint8_t  *)(p +  4);
  const uint16_t sessionId = *(uint16_t *)(p +  5);
  if (sessionId > AGENT_MAX_SESSION_ID)
    return;

  const uint32_t  exNonce2 = *(uint32_t *)(p +  7);
  const uint32_t     nonce = *(uint32_t *)(p + 11);
  const uint32_t time = (isWithTime == false ? 0 : *(uint32_t *)(p + 15));

  const uint64_t fullExtraNonce2 = ((uint64_t)sessionId << 32) | (uint64_t)exNonce2;

  // debug
  string logLine = Strings::Format("[agent] shortJobId: %02x, sessionId: %08x"
                                   ", exNonce2: %016llx, nonce: %08x, time: %08x",
                                   shortJobId, (uint32_t)sessionId,
                                   fullExtraNonce2, nonce, time);
  DLOG(INFO) << logLine;

  if (stratumSession_ != nullptr)
    stratumSession_->handleRequest_Submit("null", shortJobId,
                                          fullExtraNonce2, nonce, time,
                                          true /* submit by agent's miner */,
                                          diffControllers_[sessionId]);
}

void AgentSessions::handleExMessage_UnRegisterWorker(const string *exMessage) {
  //
  // CMD_UNREGISTER_WORKER:
  // | magic_number(1) | cmd(1) | len (2) | session_id(2) |
  //
  if (exMessage->size() != 6)
    return;

  const uint8_t *p = (uint8_t *)exMessage->data();
  const uint16_t sessionId = *(uint16_t *)(p +  4);
  if (sessionId > AGENT_MAX_SESSION_ID)
    return;

  DLOG(INFO) << "[agent] sessionId: " << sessionId;

  // un-register worker
  workerIds_[sessionId] = 0;

  // set curr diff to default Diff
  curDiff2ExpVec_[sessionId] = kDefaultDiff2Exp_;

  // release diff controller
  if (diffControllers_[sessionId] != nullptr) {
    delete diffControllers_[sessionId];
    diffControllers_[sessionId] = nullptr;
  }
}

void AgentSessions::calcSessionsJobDiff(vector<uint8_t> &sessionsDiff2Exp) {
  sessionsDiff2Exp.clear();
  sessionsDiff2Exp.resize(UINT16_MAX, kDefaultDiff2Exp_);

  for (size_t i = 0; i < diffControllers_.size(); i++) {
    if (diffControllers_[i] == nullptr) {
      continue;
    }
    const uint64_t diff = diffControllers_[i]->calcCurDiff();
    sessionsDiff2Exp[i] = (uint8_t)log2(diff);
  }
}

void AgentSessions::getSessionsChangedDiff(const vector<uint8_t> &sessionsDiff2Exp,
                                           string &data) {
  vector<uint32_t> changedDiff2Exp;
  changedDiff2Exp.resize(UINT16_MAX, 0u);
  assert(curDiff2ExpVec_.size() == sessionsDiff2Exp.size());

  // get changed diff and set to new diff
  for (size_t i = 0; i < curDiff2ExpVec_.size(); i++) {
    if (curDiff2ExpVec_[i] == sessionsDiff2Exp[i]) {
      continue;
    }
    changedDiff2Exp[i] = sessionsDiff2Exp[i];
    curDiff2ExpVec_[i] = sessionsDiff2Exp[i];  // set new diff
  }

  // diff_2exp -> session_id | session_id | ... | session_id
  map<uint8_t, vector<uint16_t> > diffSessionIds;
  for (uint32_t i = 0; i < changedDiff2Exp.size(); i++) {
    if (changedDiff2Exp[i] == 0u) {
      continue;
    }
    diffSessionIds[changedDiff2Exp[i]].push_back((uint16_t)i);
  }

  getSetDiffCommand(diffSessionIds, data);
}

void AgentSessions::getSetDiffCommand(map<uint8_t, vector<uint16_t> > &diffSessionIds,
                                      string &data) {
  //
  // CMD_MINING_SET_DIFF:
  // | magic_number(1) | cmd(1) | len (2) | diff_2_exp(1) | count(2) | session_id (2) ... |
  //
  //
  // max session id count is 32,764, each message's max length is UINT16_MAX.
  //     65535 -1-1-2-1-2 = 65,528
  //     65,528 / 2 = 32,764
  //
  data.clear();
  const size_t kMaxCount = 32764;

  for (auto it = diffSessionIds.begin(); it != diffSessionIds.end(); it++) {

    while (it->second.size() > 0) {
      size_t count = std::min(kMaxCount, it->second.size());

      string buf;
      const uint16_t len = 1+1+2+1+2+ count * 2;
      buf.resize(len);
      uint8_t *p = (uint8_t *)buf.data();

      // cmd
      *p++ = CMD_MAGIC_NUMBER;
      *p++ = CMD_MINING_SET_DIFF;

      // len
      *(uint16_t *)p = len;
      p += 2;

      // diff, 2 exp
      *p++ = it->first;

      // count
      *(uint16_t *)p = (uint16_t)count;
      p += 2;

      // session ids
      for (size_t j = 0; j < count; j++) {
        *(uint16_t *)p = it->second[j];
        p += 2;
      }

      // remove the first `count` elements from vector
      it->second.erase(it->second.begin(), it->second.begin() + count);

      data.append(buf);
      
    } /* /while */
  } /* /for */
}

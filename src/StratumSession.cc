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
#include "StratumServer.h"
#include "DiffController.h"

#include <boost/algorithm/string.hpp>

#include <event2/buffer.h>

//////////////////////////////// StratumSession ////////////////////////////////
StratumSession::StratumSession(evutil_socket_t fd, struct bufferevent *bev,
                               Server *server, struct sockaddr *saddr,
                               const int32_t shareAvgSeconds,
                               const uint32_t extraNonce1) :
shareAvgSeconds_(shareAvgSeconds),
shortJobIdIdx_(0), isDead_(false),
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
  DLOG(WARNING) << "message for handleRequest_MultiVersion received but not handled";
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




void StratumSession::_handleRequest_SetDifficulty(uint64_t suggestDiff) {
  diffController_->resetCurDiff(formatDifficulty(suggestDiff));
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



uint32_t StratumSession::getSessionId() const {
  return extraNonce1_;
}


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

#include "StratumMessageDispatcher.h"
#include "StratumMiner.h"
#include "StratumServer.h"
#include "Stratum.h"
#include "DiffController.h"

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/make_unique.hpp>

#include <event2/buffer.h>
#include <glog/logging.h>

using namespace std;

static const uint32_t ReadTimeout = 15;
static const uint32_t WriteTimeout = 120;
static const string PoolWatcherAgent = "__PoolWatcher__";
static const string BtccomAgentPrefix = "btccom-agent/";

StratumSession::StratumSession(Server &server, struct bufferevent *bev, struct sockaddr *saddr, uint32_t extraNonce1)
    : server_(server), bev_(bev), extraNonce1_(extraNonce1), buffer_(evbuffer_new()), clientAgent_("unknown")
    , isAgentClient_(false), isNiceHashClient_(false), state_(CONNECTED), isDead_(false), isLongTimeout_(false) {
  assert(saddr->sa_family == AF_INET);
  auto ipv4 = reinterpret_cast<struct sockaddr_in *>(saddr);
  clientIpInt_ = ipv4->sin_addr.s_addr;
  clientIp_.resize(INET_ADDRSTRLEN);
  evutil_inet_ntop(AF_INET, &ipv4->sin_addr, &clientIp_.front(), INET_ADDRSTRLEN);

  setup();
  LOG(INFO) << "client connect, ip: " << clientIp_;
}

StratumSession::~StratumSession() {
  evbuffer_free(buffer_);
}

void StratumSession::setup() {
  setReadTimeout(ReadTimeout);
}

void StratumSession::setReadTimeout(int32_t readTimeout) {
  // clear it
  bufferevent_set_timeouts(bev_, nullptr, nullptr);

  // we set 15 seconds, will increase the timeout after sub & auth
  struct timeval rtv = {readTimeout, 0};
  struct timeval wtv = {WriteTimeout, 0};
  bufferevent_set_timeouts(bev_, &rtv, &wtv);
}

bool StratumSession::handleMessage() {
  //
  // handle ex-message
  //
  const size_t evBufLen = evbuffer_get_length(buffer_);

  // no matter what kind of messages, length should at least 4 bytes
  if (evBufLen < 4)
    return false;

  StratumMessageEx exMessageHeader;
  evbuffer_copyout(buffer_, &exMessageHeader, 4);

  // handle ex-message
  if (exMessageHeader.magic.value() == StratumMessageEx::CMD_MAGIC_NUMBER) {
    auto len = exMessageHeader.length.value();
    auto cmd = exMessageHeader.command.value();

    //
    // It is not a valid message if exMessageLen < 4, because the length of
    // message header (1 byte magic_number + 1 byte type/cmd + 2 bytes length)
    // is 4. The header length is included in exMessageLen.
    //
    // Without the checking at below, send "\x0f\xff\x00\x00" to the sserver,
    // and it will fall into infinite loop with handleMessage() calling.
    //
    if (len < 4) {
      LOG(ERROR) << "received invalid ex-message, type: " << std::hex << cmd << ", len: " << len;
      return false;
    }

    if (evBufLen < len)  // didn't received the whole message yet
      return false;

    // copies and removes the first datlen bytes from the front of buf
    // into the memory at data
    string exMessage;
    exMessage.resize(len);
    evbuffer_remove(buffer_, &exMessage.front(), exMessage.size());
    dispatcher_->handleExMessage(exMessage);
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

  return false;  // read message failure
}

bool StratumSession::tryReadLine(std::string &line) {
  line.clear();

  // find eol
  struct evbuffer_ptr loc;
  loc = evbuffer_search_eol(buffer_, nullptr, nullptr, EVBUFFER_EOL_LF);
  if (loc.pos == -1) {
    return false;  // not found
  }

  // copies and removes the first datlen bytes from the front of buf
  // into the memory at data
  line.resize(loc.pos + 1);  // containing "\n"
  evbuffer_remove(buffer_, &line.front(), line.size());
  return true;
}

void StratumSession::handleLine(const std::string &line) {
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
  } else {
    // invalid params
    responseError(idStr, StratumStatus::ILLEGAL_PARARMS);
  }
}

void StratumSession::handleRequest(const std::string &idStr,
                                      const std::string &method,
                                      const JsonNode &jparams,
                                      const JsonNode &jroot) {
  if (isSubscribe(method)) {
    handleRequest_Subscribe(idStr, jparams, jroot);
  } else if (isAuthorize(method)) {
    string fullName;
    string password;
    if (handleRequest_Authorize(idStr, jparams, jroot, fullName, password)) {
      checkUserAndPwd(idStr, fullName, password);
    }
  }

  if (dispatcher_) {
    dispatcher_->handleRequest(idStr, method, jparams, jroot);
  }
}

void StratumSession::checkUserAndPwd(const string &idStr, const string &fullName, const string &password)
{
  if (!password.empty())
  {
    _handleRequest_AuthorizePassword(password);
  }

  const string userName = worker_.getUserName(fullName);
  const int32_t userId = server_.userInfo_->getUserId(userName);
  if (userId <= 0)
  {
    LOG(ERROR) << "invalid username=" << userName << ", userId=" << userId;
    responseError(idStr, StratumStatus::INVALID_USERNAME);
    return;
  }

  responseAuthorized(idStr);

  state_ = AUTHENTICATED;

  // set id & names, will filter workername in this func
  worker_.setUserIDAndNames(userId, fullName);
  server_.userInfo_->addWorker(worker_.userId_, worker_.workerHashId_, worker_.workerName_, clientAgent_);
  dispatcher_ = createDispatcher();
  DLOG(INFO) << "userId: " << worker_.userId_
             << ", wokerHashId: " << worker_.workerHashId_ << ", workerName:" << worker_.workerName_;

  // set read timeout to 10 mins, it's enought for most miners even usb miner.
  // if it's a pool watcher, set timeout to a week
  setReadTimeout(isLongTimeout_ ? 86400 * 7 : 60 * 10);

  // send latest stratum job
  sendMiningNotify(server_.jobRepository_->getLatestStratumJobEx(), true /* is first job */);

  // sent events to kafka: miner_connect
  {
    string eventJson;
    eventJson = Strings::Format("{\"created_at\":\"%s\","
                                "\"type\":\"miner_connect\","
                                "\"content\":{"
                                "\"user_id\":%d,\"user_name\":\"%s\","
                                "\"worker_name\":\"%s\","
                                "\"client_agent\":\"%s\",\"ip\":\"%s\","
                                "\"session_id\":\"%08x\""
                                "}}",
                                date("%F %T").c_str(),
                                worker_.userId_, worker_.userName_.c_str(),
                                worker_.workerName_.c_str(),
                                clientAgent_.c_str(), clientIp_.c_str(),
                                extraNonce1_);
    server_.sendCommonEvents2Kafka(eventJson);
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
  if (md >= server_.defaultDifficultyController_->kMinDiff_) {
    dispatcher_->setMinDiff(md);
  }

  // than set current diff
  if (d >= server_.defaultDifficultyController_->kMinDiff_) {
    dispatcher_->resetCurDiff(d);
  }
}

void StratumSession::setClientAgent(const string &clientAgent) {
  clientAgent_ = filterWorkerName(clientAgent);
  isNiceHashClient_ = isNiceHashAgent(clientAgent_);
  isAgentClient_ = (0 == clientAgent_.compare(0, BtccomAgentPrefix.size(), BtccomAgentPrefix));
  isLongTimeout_ = (isAgentClient_ || clientAgent_ == PoolWatcherAgent);
}

bool StratumSession::validate(const JsonNode &jmethod, const JsonNode &jparams) {
  if (jmethod.type() == Utilities::JS::type::Str &&
      jmethod.size() != 0 &&
      jparams.type() == Utilities::JS::type::Array)
  {
    return true;
  }

  return false;
}

unique_ptr<StratumMessageDispatcher> StratumSession::createDispatcher() {
  // By default there is no agent support
  return boost::make_unique<StratumMessageMinerDispatcher>(*this,
                                                           createMiner(clientAgent_,
                                                                       worker_.workerName_,
                                                                       worker_.workerHashId_));
}

bool StratumSession::isDead() const {
  return isDead_.load();
}

void StratumSession::addWorker(const std::string &clientAgent, const std::string &workerName, int64_t workerId) {
  if (state_ != AUTHENTICATED) {
    LOG(ERROR) << "curr stratum session has NOT auth yet";
    return;
  }
  server_.userInfo_->addWorker(worker_.userId_, workerId, workerName, clientAgent);
}

void StratumSession::markAsDead() {
  // mark as dead
  isDead_.store(true);

  // sent event to kafka: miner_dead
  if (worker_.userId_ > 0) {
    string eventJson;
    eventJson = Strings::Format("{\"created_at\":\"%s\","
                                "\"type\":\"miner_dead\","
                                "\"content\":{"
                                "\"user_id\":%d,\"user_name\":\"%s\","
                                "\"worker_name\":\"%s\","
                                "\"client_agent\":\"%s\",\"ip\":\"%s\","
                                "\"session_id\":\"%08x\""
                                "}}",
                                date("%F %T").c_str(),
                                worker_.userId_, worker_.userName_.c_str(),
                                worker_.workerName_.c_str(),
                                clientAgent_.c_str(), clientIp_.c_str(),
                                extraNonce1_);
    server_.sendCommonEvents2Kafka(eventJson);
  }
}

void StratumSession::sendData(const char *data, size_t len) {
  // add data to a bufferevent’s output buffer
  // it is automatically locked so we don't need to lock
  bufferevent_write(bev_, data, len);
  DLOG(INFO) << "send(" << len << ") to " << worker_.fullName_ << " : " << data;
}

void StratumSession::readBuf(struct evbuffer *buf) {
  // moves all data from src to the end of dst
  evbuffer_add_buffer(buffer_, buf);

  while (handleMessage()) {
  }
}

/**
 * JSON-RPC 1.0 Specification
 * <https://www.jsonrpc.org/specification_v1>
 *
 * 1.2 Response
 * When the method invocation completes, the service must reply with a response.
 * The response is a single object serialized using JSON.
 *
 * It has three properties:
 *    result - The Object that was returned by the invoked method.
 *             This must be null in case there was an error invoking the method.
 *    error - An Error object if there was an error invoking the method.
 *            It must be null if there was no error.
 *    id - This must be the same id as the request it is responding to.
 */

void StratumSession::responseTrue(const string &idStr) {
  const string s = "{\"id\":" + idStr + ",\"result\":true,\"error\":null}\n";
  sendData(s);
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

/**
 * JSON-RPC 2.0 Specification
 * <https://www.jsonrpc.org/specification>
 *
 * 5 Response object
 * When a rpc call is made, the Server MUST reply with a Response, except for in the case of Notifications.
 * The Response is expressed as a single JSON Object, with the following members:
 * jsonrpc
 *   A String specifying the version of the JSON-RPC protocol. MUST be exactly "2.0".
 * result
 *   This member is REQUIRED on success.
 *   This member MUST NOT exist if there was an error invoking the method.
 *   The value of this member is determined by the method invoked on the Server.
 * error
 *   This member is REQUIRED on error.
 *   This member MUST NOT exist if there was no error triggered during invocation.
 *   The value for this member MUST be an Object as defined in section 5.1.
 * id
 *   This member is REQUIRED.
 *   It MUST be the same as the value of the id member in the Request Object.
 *   If there was an error in detecting the id in the Request object (e.g. Parse error/Invalid Request), it MUST be Null.
 *
 * Either the result member or error member MUST be included, but both members MUST NOT be included.
 */

void StratumSession::rpc2ResponseTrue(const string &idStr) {
  const string s = Strings::Format("{\"id\":%s,\"jsonrpc\":\"2.0\",\"result\":true}\n", idStr.c_str());
  sendData(s);
}

/**
 * JSON-RPC 2.0 Specification
 * <https://www.jsonrpc.org/specification>
 *
 * 5.1 Error object
 *
 * When a rpc call encounters an error, the Response Object MUST contain the error member
 * with a value that is a Object with the following members:
 *
 * code
 *     A Number that indicates the error type that occurred.
 *     This MUST be an integer.
 * message
 *     A String providing a short description of the error.
 *     The message SHOULD be limited to a concise single sentence.
 * data
 *     A Primitive or Structured value that contains additional information about the error.
 *     This may be omitted.
 *     The value of this member is defined by the Server (e.g. detailed error information, nested errors etc.).
 */

void StratumSession::rpc2ResponseError(const string &idStr, int errCode) {
  char buf[1024];
  int len = snprintf(buf, sizeof(buf),
                     "{\"id\":%s,\"jsonrpc\":\"2.0\",\"error\":{\"code\":%d,\"message\":\"%s\"}}\n",
                     idStr.empty() ? "null" : idStr.c_str(),
                     errCode, StratumStatus::toString(errCode));
  sendData(buf, len);
}

void StratumSession::responseAuthorized(const std::string &idStr) {
  responseTrue(idStr);
}

void StratumSession::sendSetDifficulty(LocalJob &localJob, uint64_t difficulty) {
  string s;
  if (!server_.isDevModeEnable_) {
    s = Strings::Format("{\"id\":null,\"method\":\"mining.set_difficulty\""
                        ",\"params\":[%" PRIu64"]}\n",
                        difficulty);
  } else {
    s = Strings::Format("{\"id\":null,\"method\":\"mining.set_difficulty\""
                        ",\"params\":[%.3f]}\n",
                        server_.minerDifficulty_);
  }

  sendData(s);
}

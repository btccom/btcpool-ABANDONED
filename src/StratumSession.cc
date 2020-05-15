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

#include <event2/buffer.h>
#include <glog/logging.h>

using namespace std;

static const uint32_t ReadTimeout = 15;
static const uint32_t WriteTimeout = 120;
static const string PoolWatcherAgent = "__PoolWatcher__";
static const string BtccomAgentPrefix = "btccom-agent/";
static const string PoolGrandPoolWatcher = "__PoolGrandPoolWatcher__";

class ProxyStrategyDecodeAddress : public ProxyStrategy {
public:
  explicit ProxyStrategyDecodeAddress(StratumSession &session)
    : session_{session} {}
  bool check(const std::string &line) override {
    std::vector<std::string> tokens;
    boost::algorithm::split(
        tokens,
        line,
        boost::algorithm::is_space(),
        boost::algorithm::token_compress_on);
    // TODO: add IPv6 support when the pool supports IPv6
    if (!tokens.empty() && tokens[0] == "PROXY") {
      if (tokens.size() >= 3 && tokens[1] == "TCP4") {
        struct in_addr address;
        if (1 == evutil_inet_pton(AF_INET, tokens[2].c_str(), &address)) {
          LOG(INFO) << "PROXY protocol detected, client ip: " << tokens[2];
          session_.setIpAddress(address);
        }
      }

      // Stop further processing even if it is a malformed PROXY line
      return true;
    }

    return false;
  }

private:
  StratumSession &session_;
};

StratumSession::StratumSession(
    StratumServer &server,
    struct bufferevent *bev,
    struct sockaddr *saddr,
    uint32_t sessionId)
  : server_(server)
  , bev_(bev)
  , sessionId_(sessionId)
  , buffer_(evbuffer_new())
  , clientAgent_("unknown")
  , isAgentClient_(false)
  , isNiceHashClient_(false)
  , state_(CONNECTED)
  , worker_(server.chains_.size())
  , isDead_(false)
  , isLongTimeout_(false)
  , savedAuthorizeInfo_(nullptr)
  , proxyStrategy_(std::make_unique<ProxyStrategy>()) {
  assert(saddr->sa_family == AF_INET);
  auto ipv4 = reinterpret_cast<struct sockaddr_in *>(saddr);
  setIpAddress(ipv4->sin_addr);

  // make a null dispatcher here to guard against invalid access
  dispatcher_ = std::make_unique<StratumMessageNullDispatcher>();

  setup();

  if (!server_.logHideIpPrefix(clientIp_)) {
    LOG(INFO) << "client connect, ip: " << clientIp_;
  }
}

StratumSession::~StratumSession() {
  LOG_IF(INFO, state_ != CONNECTED)
      << "close stratum session, ip: " << clientIp_ << ", name: \""
      << worker_.fullName_ << "\""
      << ", agent: \"" << clientAgent_ << "\"";

  // Release actual miner objects first as they depends on other members of
  // session
  dispatcher_.reset();
  evbuffer_free(buffer_);
  bufferevent_free(bev_);
}

void StratumSession::setup() {
  setReadTimeout(ReadTimeout);
  if (getServer().proxyProtocol()) {
    proxyStrategy_ = std::make_unique<ProxyStrategyDecodeAddress>(*this);
  }
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
      LOG(ERROR) << "received invalid ex-message, type: " << std::hex << cmd
                 << ", len: " << len;
      return false;
    }

    if (evBufLen < len) // didn't received the whole message yet
      return false;

    // copies and removes the first datlen bytes from the front of buf
    // into the memory at data
    string exMessage;
    exMessage.resize(len);
    evbuffer_remove(buffer_, &exMessage.front(), exMessage.size());
    if (dispatcher_) {
      dispatcher_->handleExMessage(exMessage);
    }
    return true; // read message success, return true
  }

  //
  // handle stratum message
  //
  string line;
  if (tryReadLine(line)) {
    handleLine(line);
    return true;
  }

  return false; // read message failure
}

bool StratumSession::tryReadLine(std::string &line) {
  line.clear();

  // find eol
  struct evbuffer_ptr loc;
  loc = evbuffer_search_eol(buffer_, nullptr, nullptr, EVBUFFER_EOL_LF);
  if (loc.pos < 0) {
    return false; // not found
  }

  // copies and removes the first datlen bytes from the front of buf
  // into the memory at data
  line.resize(loc.pos + 1); // containing "\n"
  evbuffer_remove(buffer_, &line.front(), line.size());
  return true;
}

void StratumSession::handleLine(const std::string &line) {
  DLOG(INFO) << "recv(" << line.size() << "): " << line;

  if (state_ == CONNECTED && proxyStrategy_->check(line)) {
    // PROXY header shall appear only once
    proxyStrategy_ = std::make_unique<ProxyStrategy>();
    return;
  }

  JsonNode jnode;
  if (!JsonNode::parse(line.data(), line.data() + line.size(), jnode)) {
    LOG(ERROR) << "decode line fail, not a json string. string value: \""
               << line << "\"";
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

  if (validate(jmethod, jparams, jnode)) {
    handleRequest(idStr, jmethod.str(), jparams, jnode);
  } else {
    // invalid params
    responseError(idStr, StratumStatus::ILLEGAL_PARARMS);
  }
}

void StratumSession::logAuthorizeResult(bool success, const string &password) {
  if (success) {
    LOG(INFO) << "authorize success, userId: " << worker_.userId()
              << ", wokerHashId: " << worker_.workerHashId_
              << ", workerName: " << worker_.fullName_
              << ", password: " << password << ", clientAgent: " << clientAgent_
              << ", clientIp: " << clientIp_
              << ", chain: " << getServer().chainName(worker_.chainId_);
  } else {
    LOG(WARNING) << "authorize failed, workerName:" << worker_.fullName_
                 << ", password: " << password
                 << ", clientAgent: " << clientAgent_
                 << ", clientIp: " << clientIp_;
  }
}

string StratumSession::getMinerInfoJson(
    const string &action,
    const int64_t workerId,
    const string &workerName,
    const string &minerAgent,
    const string &desc) {
  return Strings::Format(
      "{\"created_at\":\"%s\","
      "\"type\":\"worker_update\","
      "\"action\":\"%s\","
      "\"content\":{"
      "\"user_id\":%d,"
      "\"user_name\":\"%s\","
      "\"worker_id\":%d,"
      "\"worker_name\":\"%s\","
      "\"miner_agent\":\"%s\","
      "\"ip\":\"%s\","
      "\"session_id\":\"%08x\","
      "\"desc\":\"%s\""
      "%s" // Optional field for single user mode
      "}}",
      date("%F %T"),
      action,
      server_.singleUserMode() ? server_.singleUserId(getChainId())
                               : worker_.userId(),
      server_.singleUserMode() ? server_.singleUserName() : worker_.userName_,
      workerId,
      workerName,
      minerAgent,
      clientIp_,
      sessionId_,
      desc,
      server_.singleUserMode()
          ? Strings::Format(",\"ext_user_id\":%d", worker_.userId())
          : "");
}

bool StratumSession::autoRegCallback(const string &userName) {
  if (savedAuthorizeInfo_ && userName == savedAuthorizeInfo_->userName_) {
    checkUserAndPwd(
        savedAuthorizeInfo_->idStr_,
        savedAuthorizeInfo_->fullName_,
        savedAuthorizeInfo_->password_,
        true);

    savedAuthorizeInfo_ = nullptr;
    return true;
  }

  return false;
}

void StratumSession::checkUserAndPwd(
    const string &idStr,
    const string &fullName,
    const string &password,
    bool isAutoRegCallback) {
  if (isAutoRegCallback) {
    if (isDead_ || state_ != AUTO_REGISTING) {
      LOG(INFO) << "cannot authorized from auto registing, "
                << (isDead_ ? "session dead" : "state wrong")
                << ", worker: " << fullName;
      return;
    }
  }

  // set id & names, will filter workername in this func
  worker_.setNames(
      fullName,
      [this](string &userName) {
        server_.userInfo_->regularUserName(userName);
      },
      server_.singleUserMode(),
      server_.singleUserName());

  if (worker_.userName_.empty()) {
    DLOG(INFO) << "got an empty user name";

    logAuthorizeResult(false, password);
    responseError(idStr, StratumStatus::INVALID_USERNAME);
    return;
  }

  size_t chainId = 0;
  bool found = server_.userInfo_->getChainId(worker_.userName_, chainId);
  if (!found) {
    DLOG(INFO) << "cannot find user " << worker_.userName_ << " in any chain";

    if (!server_.userInfo_->autoRegEnabled()) {
      logAuthorizeResult(false, password);
      responseError(idStr, StratumStatus::INVALID_USERNAME);
      return;
    }
  }

  if (!found || !switchChain(chainId)) {
    if (!isAutoRegCallback && server_.userInfo_->autoRegEnabled()) {
      DLOG(INFO) << "try auto registing user " << worker_.userName_;

      savedAuthorizeInfo_ = shared_ptr<AuthorizeInfo>(
          new AuthorizeInfo({idStr, worker_.userName_, fullName, password}));
      // try auto registing
      if (server_.userInfo_->tryAutoReg(
              worker_.userName_, sessionId_, worker_.fullName_)) {
        // Request for auto registing success
        // Return and waiting for a callback
        state_ = AUTO_REGISTING;
        return;
      }
      // auto registing failed, remove saved info
      savedAuthorizeInfo_ = nullptr;
    }

    DLOG(INFO) << "cannot switch user chain";
    logAuthorizeResult(false, password);
    responseError(idStr, StratumStatus::INVALID_USERNAME);
    return;
  }

  logAuthorizeResult(true, password);
  responseAuthorizeSuccess(idStr);

  state_ = AUTHENTICATED;
  dispatcher_ = createDispatcher();

  if (!password.empty()) {
    setDefaultDifficultyFromPassword(password);
  }

  // set read timeout to 10 mins, it's enought for most miners even usb miner.
  // if it's a pool watcher, set timeout to a week
  setReadTimeout(isLongTimeout_ ? 86400 * 7 : getServer().tcpReadTimeout());

  // send latest stratum job
  sendMiningNotify(
      server_.chains_[chainId].jobRepository_->getLatestStratumJobEx(),
      true /* is first job */);
}

void StratumSession::AnonymousAuthorize(
    const string &idStr, const string &fullName, const string &password) {

  // set id & names, will filter workername in this func
  worker_.setNames(
      fullName,
      [this](string &userName) {
        server_.userInfo_->regularUserName(userName);
      },
      false,
      "",
      true);

  if (worker_.userName_.empty()) {
    DLOG(INFO) << "got an empty user name";

    logAuthorizeResult(false, password);
    responseError(idStr, StratumStatus::INVALID_USERNAME);
    return;
  }

  uint32_t userid = 0u;
  if (server_.anonymousNameIds_.find(worker_.userName_) ==
      server_.anonymousNameIds_.end()) {
    // assign tenp
    if (server_.userIdManager_->allocSessionId(&userid) == false) {
      LOG(ERROR) << "alloc userid failed,";
      logAuthorizeResult(false, password);
      responseError(idStr, StratumStatus::INVALID_USERNAME);
      return;
    } else {
      server_.anonymousNameIds_.insert({worker_.userName_, userid});
    }
  }
  userid = server_.anonymousNameIds_[worker_.userName_];
  worker_.setChainIdAndUserId(0, userid); //所有币种通用一个userid
  logAuthorizeResult(true, password);
  responseAuthorizeSuccess(idStr);

  state_ = AUTHENTICATED;
  dispatcher_ = createDispatcher();

  if (!password.empty()) {
    setDefaultDifficultyFromPassword(password);
  }

  // set read timeout to 10 mins, it's enought for most miners even usb miner.
  // if it's a pool watcher, set timeout to a week
  setReadTimeout(isLongTimeout_ ? 86400 * 7 : getServer().tcpReadTimeout());

  // send latest stratum job
  sendMiningNotify(
      server_.chains_[0].jobRepository_->getLatestStratumJobEx(),
      true /* is first job */);
}

bool StratumSession::switchChain(size_t chainId) {
  const int32_t userId =
      server_.userInfo_->getUserId(chainId, worker_.userName_);
  if (userId <= 0) {
    LOG(ERROR) << "cannot find user id, chainId: " << chainId
               << ", userName: " << worker_.userName_;
    return false;
  }

  bool miningOnOldChain = worker_.chainId_ != chainId && worker_.userId() > 0;
  if (miningOnOldChain) {
    // sent events to old chain's kafka: worker_update
    server_.sendCommonEvents2Kafka(
        worker_.chainId_,
        getMinerInfoJson(
            "miner_dead",
            worker_.workerHashId_,
            worker_.workerName_,
            clientAgent_,
            "switch_chain"));

    dispatcher_->beforeSwitchChain();
  }

  worker_.setChainIdAndUserId(chainId, userId);

  // sent events to new chain's kafka: worker_update
  server_.sendCommonEvents2Kafka(
      chainId,
      getMinerInfoJson(
          "miner_connect",
          worker_.workerHashId_,
          worker_.workerName_,
          clientAgent_,
          miningOnOldChain ? "switch_chain" : "new_conn"));

  if (miningOnOldChain) {
    dispatcher_->afterSwitchChain();
  }

  return true;
}

void StratumSession::setDefaultDifficultyFromPassword(const string &password) {
  // testcase: TEST(StratumSession, SetDiff)
  using namespace boost::algorithm;

  uint64_t d = 0u, md = 0u;
  vector<string> arr; // key=value,key=value
  split(arr, password, is_any_of(","));
  if (arr.size() == 0)
    return;

  for (auto it = arr.begin(); it != arr.end(); it++) {
    vector<string> arr2; // key,value
    split(arr2, *it, is_any_of("="));
    if (arr2.size() != 2 || arr2[1].empty()) {
      continue;
    }

    if (arr2[0] == "d") {
      // 'd' : start difficulty
      d = strtoull(arr2[1].c_str(), nullptr, 10);
    } else if (arr2[0] == "md") {
      // 'md' : minimum difficulty
      md = strtoull(arr2[1].c_str(), nullptr, 10);
    }
  }

  // set min diff first
  if (md > 0) {
    // diff range correction is done in setMinDiff
    dispatcher_->setMinDiff(formatDifficulty(md));
  }

  // than set current diff
  if (d > 0) {
    // diff range correction is done in resetCurDiff
    dispatcher_->resetCurDiff(formatDifficulty(d));
  }
}

void StratumSession::setClientAgent(const string &clientAgent) {
  clientAgent_ = filterWorkerName(clientAgent);
  isNiceHashClient_ = isNiceHashAgent(clientAgent_);
  isAgentClient_ =
      (0 ==
       clientAgent_.compare(0, BtccomAgentPrefix.size(), BtccomAgentPrefix));

  isGrandPoolClient_ = this->getServer().grandPoolEnabled_ &&
      clientAgent_ == PoolGrandPoolWatcher;

  isLongTimeout_ =
      (isAgentClient_ || isNiceHashClient_ || isGrandPoolClient_ ||
       clientAgent_ == PoolWatcherAgent ||
       std::regex_search(clientAgent, getServer().longTimeoutPattern_));
}

bool StratumSession::validate(
    const JsonNode &jmethod, const JsonNode &jparams, const JsonNode &jroot) {
  if (jmethod.type() == Utilities::JS::type::Str && jmethod.size() != 0 &&
      jparams.type() == Utilities::JS::type::Array) {
    return true;
  }

  return false;
}

unique_ptr<StratumMessageDispatcher> StratumSession::createDispatcher() {
  // By default there is no agent support
  return std::make_unique<StratumMessageMinerDispatcher>(
      *this,
      createMiner(clientAgent_, worker_.workerName_, worker_.workerHashId_));
}

bool StratumSession::isDead() const {
  return isDead_.load();
}

void StratumSession::addWorker(
    const std::string &clientAgent,
    const std::string &workerName,
    int64_t workerId) {
  if (state_ != AUTHENTICATED) {
    LOG(ERROR) << "curr stratum session has NOT auth yet";
    return;
  }
  server_.sendCommonEvents2Kafka(
      worker_.chainId_,
      getMinerInfoJson(
          "miner_connect", workerId, workerName, clientAgent, "from_btcagent"));
}

void StratumSession::removeWorker(
    const std::string &clientAgent,
    const std::string &workerName,
    int64_t workerId) {
  if (state_ != AUTHENTICATED) {
    LOG(ERROR) << "curr stratum session has NOT auth yet";
    return;
  }
  server_.sendCommonEvents2Kafka(
      worker_.chainId_,
      getMinerInfoJson(
          "miner_dead", workerId, workerName, clientAgent, "from_btcagent"));
}

void StratumSession::markAsDead() {
  // mark as dead
  isDead_.store(true);

  // sent event to kafka: miner_dead
  if (worker_.userId() > 0) {
    server_.sendCommonEvents2Kafka(
        worker_.chainId_,
        getMinerInfoJson(
            "miner_dead",
            worker_.workerHashId_,
            worker_.workerName_,
            clientAgent_,
            "del_conn"));
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

void StratumSession::responseTrue(const string &idStr) {
  rpc1ResponseTrue(idStr);
}

void StratumSession::responseError(const string &idStr, int errCode) {
  rpc1ResponseError(idStr, errCode);
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

void StratumSession::rpc1ResponseTrue(const string &idStr) {
  const string s = "{\"id\":" + idStr + ",\"result\":true,\"error\":null}\n";
  sendData(s);
}

void StratumSession::rpc1ResponseError(const string &idStr, int errCode) {
  //
  // {"id": 10, "result": null, "error":[21, "Job not found", null]}
  //
  auto data = Strings::Format(
      "{\"id\":%s,\"result\":null,\"error\":[%d,\"%s\",null]}\n",
      idStr.empty() ? "null" : idStr,
      errCode,
      StratumStatus::toString(errCode));
  sendData(data);
}

/**
 * JSON-RPC 2.0 Specification
 * <https://www.jsonrpc.org/specification>
 *
 * 5 Response object
 * When a rpc call is made, the StratumServer MUST reply with a Response, except
 * for in the case of Notifications. The Response is expressed as a single JSON
 * Object, with the following members: jsonrpc A String specifying the version
 * of the JSON-RPC protocol. MUST be exactly "2.0". result This member is
 * REQUIRED on success. This member MUST NOT exist if there was an error
 * invoking the method. The value of this member is determined by the method
 * invoked on the StratumServer. error This member is REQUIRED on error. This
 * member MUST NOT exist if there was no error triggered during invocation. The
 * value for this member MUST be an Object as defined in section 5.1. id This
 * member is REQUIRED. It MUST be the same as the value of the id member in the
 * Request Object. If there was an error in detecting the id in the Request
 * object (e.g. Parse error/Invalid Request), it MUST be Null.
 *
 * Either the result member or error member MUST be included, but both members
 * MUST NOT be included.
 */

void StratumSession::rpc2ResponseTrue(const string &idStr) {
  const string s = Strings::Format(
      "{\"id\":%s,\"jsonrpc\":\"2.0\",\"result\":true}\n", idStr);
  sendData(s);
}

/**
 * JSON-RPC 2.0 Specification
 * <https://www.jsonrpc.org/specification>
 *
 * 5.1 Error object
 *
 * When a rpc call encounters an error, the Response Object MUST contain the
 * error member with a value that is a Object with the following members:
 *
 * code
 *     A Number that indicates the error type that occurred.
 *     This MUST be an integer.
 * message
 *     A String providing a short description of the error.
 *     The message SHOULD be limited to a concise single sentence.
 * data
 *     A Primitive or Structured value that contains additional information
 * about the error. This may be omitted. The value of this member is defined by
 * the StratumServer (e.g. detailed error information, nested errors etc.).
 */

void StratumSession::rpc2ResponseError(const string &idStr, int errCode) {
  auto data = Strings::Format(
      "{\"id\":%s,\"jsonrpc\":\"2.0\",\"error\":{\"code\":%d,\"message\":\"%"
      "s\"}}\n",
      idStr.empty() ? "null" : idStr,
      errCode,
      StratumStatus::toString(errCode));
  sendData(data);
}

void StratumSession::responseAuthorizeSuccess(const std::string &idStr) {
  responseTrue(idStr);
}

void StratumSession::reportShare(
    size_t chainId, int32_t status, uint64_t shareDiff) {
  ++server_.chains_[chainId].shareStats_[status];
}

bool StratumSession::acceptStale() const {
  return server_.acceptStale_;
}

bool StratumSession::niceHashForced() const {
  return server_.chains_[getChainId()].jobRepository_->niceHashForced();
}

uint64_t StratumSession::niceHashMinDiff() const {
  return server_.chains_[getChainId()].jobRepository_->niceHashMinDiff();
}

void StratumSession::setIpAddress(const struct in_addr &address) {
  clientIpInt_ = address.s_addr;
  clientIp_.resize(INET_ADDRSTRLEN);
  evutil_inet_ntop(AF_INET, &address, &clientIp_.front(), INET_ADDRSTRLEN);
  // remove the padding bytes
  clientIp_ = clientIp_.c_str();
}
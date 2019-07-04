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

#ifndef STRATUM_SESSION_H_
#define STRATUM_SESSION_H_

#include "StratumMessageDispatcher.h"
#include "Stratum.h"
#include "utilities_js.hpp"

#include <boost/endian/buffers.hpp>

#include <event2/bufferevent.h>

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>

class DiffController;
class StratumServer;
class StratumJobEx;

// Supported BTCAgent features / capabilities, a JSON array.
// Sent within the request / response of agent.get_capabilities for protocol
// negotiation. Known capabilities:
//     verrol: version rolling (shares with a version mask can be submitted
//     through a BTCAgent session).
#define BTCAGENT_PROTOCOL_CAPABILITIES "[\"verrol\"]"

enum class StratumCommandEx : uint8_t {
  REGISTER_WORKER = 0x01u, // Agent -> Pool
  SUBMIT_SHARE = 0x02u, // Agent -> Pool,  mining.submit(...)
  SUBMIT_SHARE_WITH_TIME = 0x03u, // Agent -> Pool,  mining.submit(..., nTime)
  UNREGISTER_WORKER = 0x04u, // Agent -> Pool
  MINING_SET_DIFF = 0x05u, // Pool  -> Agent, mining.set_difficulty(diff)
  SUBMIT_SHARE_WITH_VER =
      0x12u, // Agent -> Pool,  mining.submit(..., nVersionMask)
  SUBMIT_SHARE_WITH_TIME_VER =
      0x13u, // Agent -> Pool,  mining.submit(..., nTime, nVersionMask)
};

struct StratumMessageEx {
  // Hack to prevent linkage errors
  enum {
    CMD_MAGIC_NUMBER = 0x7F,
    AGENT_MAX_SESSION_ID = 0xFFFE,
  };

  boost::endian::little_uint8_buf_t magic;
  boost::endian::little_uint8_buf_t command;
  boost::endian::little_uint16_buf_t length;
};

class IStratumSession {
public:
  virtual ~IStratumSession() = default;
  virtual void addWorker(
      const std::string &clientAgent,
      const std::string &workerName,
      int64_t workerId) = 0;
  virtual void removeWorker(
      const std::string &clientAgent,
      const std::string &workerName,
      int64_t workerId) = 0;
  virtual std::unique_ptr<StratumMiner> createMiner(
      const std::string &clientAgent,
      const std::string &workerName,
      int64_t workerId) = 0;
  virtual uint16_t decodeSessionId(const std::string &exMessage) const = 0;
  virtual StratumMessageDispatcher &getDispatcher() = 0;
  virtual void responseTrue(const std::string &idStr) = 0;
  virtual void responseTrueWithCode(const std::string &idStr, int code) = 0;
  virtual void responseError(const std::string &idStr, int code) = 0;
  virtual void sendData(const char *data, size_t len) = 0;
  virtual void sendData(const std::string &str) = 0;
  virtual void sendSetDifficulty(LocalJob &localJob, uint64_t difficulty) = 0;
  virtual bool switchChain(size_t chainId) = 0;
  virtual void
  reportShare(size_t chainId, int32_t status, uint64_t shareDiff) = 0;
  virtual bool acceptStale() const = 0;
};

class StratumSession : public IStratumSession {
public:
  // Mining State
  //
  // Please put non-authenticated status in front of AUTHENTICATED.
  // Otherwise code like this will go wrong:
  // <code>if (state_ < AUTHENTICATED || exJobPtr == nullptr)</code>
  //
  enum State { CONNECTED, SUBSCRIBED, AUTO_REGISTING, AUTHENTICATED };

protected:
  StratumServer &server_;
  struct bufferevent *bev_;
  uint32_t sessionId_;
  struct evbuffer *buffer_;

  uint32_t clientIpInt_;
  std::string clientIp_;

  std::string clientAgent_; // eg. bfgminer/4.4.0-32-gac4e9b3
  bool isAgentClient_;
  bool isNiceHashClient_;
  std::unique_ptr<StratumMessageDispatcher> dispatcher_;

  State state_;
  StratumWorker worker_;
  std::atomic<bool> isDead_;
  bool isLongTimeout_;

  struct AuthorizeInfo {
    string idStr_;
    string userName_;
    string fullName_;
    string password_;
  };

  shared_ptr<AuthorizeInfo> savedAuthorizeInfo_;

  void setup();
  void setReadTimeout(int32_t readTimeout);

  bool handleMessage(); // handle all messages: ex-message and stratum message
  bool tryReadLine(std::string &line);
  void handleLine(const std::string &line);
  virtual void handleRequest(
      const std::string &idStr,
      const std::string &method,
      const JsonNode &jparams,
      const JsonNode &jroot) = 0;
  void checkUserAndPwd(
      const string &idStr,
      const string &fullName,
      const string &password,
      bool isAutoRegCallback = false);
  void setDefaultDifficultyFromPassword(const string &password);
  void setClientAgent(const string &clientAgent);

  virtual void logAuthorizeResult(bool success, const string &password);
  string getMinerInfoJson(
      const string &action,
      const int64_t workerId,
      const string &workerName,
      const string &minerAgent,
      const string &desc);

  virtual bool validate(
      const JsonNode &jmethod, const JsonNode &jparams, const JsonNode &jroot);
  virtual std::unique_ptr<StratumMessageDispatcher> createDispatcher();

  StratumSession(
      StratumServer &server,
      struct bufferevent *bev,
      struct sockaddr *saddr,
      uint32_t sessionId);

public:
  virtual ~StratumSession();
  virtual bool initialize() { return true; }
  bool switchChain(size_t chainId) override;
  bool autoRegCallback(const string &userName);
  uint16_t decodeSessionId(const std::string &exMessage) const override {
    return StratumMessageEx::AGENT_MAX_SESSION_ID;
  };
  bool acceptStale() const override;

  StratumServer &getServer() { return server_; }
  StratumWorker &getWorker() { return worker_; }
  StratumMessageDispatcher &getDispatcher() override { return *dispatcher_; }
  uint32_t getClientIp() const { return clientIpInt_; };
  uint32_t getSessionId() const { return sessionId_; }
  size_t getChainId() const { return worker_.chainId_; }
  State getState() const { return state_; }
  string getUserName() const { return worker_.userName_; }

  bool isDead() const;
  void markAsDead();
  void addWorker(
      const std::string &clientAgent,
      const std::string &workerName,
      int64_t workerId) override;
  void removeWorker(
      const std::string &clientAgent,
      const std::string &workerName,
      int64_t workerId) override;

  void sendData(const char *data, size_t len) override;
  void sendData(const std::string &str) override {
    sendData(str.data(), str.size());
  }
  void readBuf(struct evbuffer *buf);

  // Please keep them in here and be virtual or you have to refactor
  // checkUserAndPwd(). We need override them to respond JSON-RPC 2.0 responses
  // in ETHProxy and Beam authentication.
  virtual void responseTrue(const std::string &idStr) override;
  virtual void
  responseTrueWithCode(const std::string &idStr, int code) override {
    responseTrue(idStr);
  }
  virtual void responseError(const std::string &idStr, int code) override;
  virtual void responseAuthorizeSuccess(const std::string &idStr);

  void rpc1ResponseTrue(const string &idStr);
  void rpc1ResponseError(const string &idStr, int errCode);
  void rpc2ResponseTrue(const string &idStr);
  void rpc2ResponseError(const string &idStr, int errCode);

  virtual void sendMiningNotify(
      shared_ptr<StratumJobEx> exJobPtr, bool isFirstJob = false) = 0;

  void reportShare(size_t chainId, int32_t status, uint64_t shareDiff) override;
};

//  This base class is to help type safety of accessing server_ member variable.
//  Avoid manual casting. And by templating a minimum class declaration, we
//  avoid bloating the code too much.
template <typename StratumTraits>
class StratumSessionBase : public StratumSession {
protected:
  using ServerType = typename StratumTraits::ServerType;
  StratumSessionBase(
      ServerType &server,
      struct bufferevent *bev,
      struct sockaddr *saddr,
      uint32_t sessionId)
    : StratumSession(server, bev, saddr, sessionId)
    , kMaxNumLocalJobs_(256) {}

  using LocalJobType = typename StratumTraits::LocalJobType;
  static_assert(
      std::is_base_of<LocalJob, LocalJobType>::value,
      "Local job type is not derived from LocalJob");
  std::deque<LocalJobType> localJobs_;
  size_t kMaxNumLocalJobs_;
  static constexpr size_t kNumLocalJobsToKeep_ = 4;

public:
  size_t maxNumLocalJobs() const { return kMaxNumLocalJobs_; }

  template <typename Key>
  LocalJobType *findLocalJob(const Key &key) {
    auto iter = localJobs_.rbegin();
    auto iend = localJobs_.rend();
    for (; iter != iend; ++iter) {
      if (*iter == key) {
        return &(*iter);
      }
    }
    return nullptr;
  }

  template <typename... Args>
  LocalJobType &addLocalJob(size_t chainId, uint64_t jobId, Args &&... args) {
    localJobs_.emplace_back(chainId, jobId, std::forward<Args>(args)...);
    auto &localJob = localJobs_.back();
    dispatcher_->addLocalJob(localJob);
    return localJob;
  }

  void clearLocalJobs(bool isClean) {
    size_t numOfJobsToKeep = isClean ? kNumLocalJobsToKeep_ : kMaxNumLocalJobs_;
    while (localJobs_.size() > numOfJobsToKeep) {
      dispatcher_->removeLocalJob(localJobs_.front());
      localJobs_.pop_front();
    }
  }

  std::deque<LocalJobType> &getLocalJobs() { return localJobs_; }

  inline ServerType &getServer() const {
    return static_cast<ServerType &>(server_);
  }
};

#endif // #ifndef STRATUM_SESSION_H_

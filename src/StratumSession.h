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
class Server;
class StratumJobEx;

enum class StratumCommandEx : uint8_t {
  CMD_REGISTER_WORKER = 1,
  CMD_SUBMIT_SHARE = 2,
  CMD_SUBMIT_SHARE_WITH_TIME = 3,
  CMD_UNREGISTER_WORKER = 4,
  MINING_SET_DIFF = 5,
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
  virtual void addWorker(const std::string &clientAgent, const std::string &workerName, int64_t workerId) = 0;
  virtual std::unique_ptr<StratumMiner> createMiner(const std::string &clientAgent,
                                                    const std::string &workerName,
                                                    int64_t workerId) = 0;
  virtual uint16_t decodeSessionId(const std::string &exMessage) const = 0;
  virtual StratumMessageDispatcher &getDispatcher() = 0;
  virtual void responseTrue(const std::string &idStr) = 0;
  virtual void responseError(const std::string &idStr, int code) = 0;
  virtual void sendData(const char *data, size_t len) = 0;
  virtual void sendData(const std::string &str) = 0;
  virtual void sendSetDifficulty(LocalJob &localJob, uint64_t difficulty) = 0;
};

class StratumSession : public IStratumSession {
public:
  // mining state
  enum State {
    CONNECTED     = 0,
    SUBSCRIBED    = 1,
    AUTHENTICATED = 2
  };
protected:
  Server &server_;
  struct bufferevent *bev_;
  uint32_t extraNonce1_;
  struct evbuffer *buffer_;

  uint32_t clientIpInt_;
  std::string clientIp_;

  std::string clientAgent_;  // eg. bfgminer/4.4.0-32-gac4e9b3
  bool isAgentClient_;
  bool isNiceHashClient_;
  std::unique_ptr<StratumMessageDispatcher> dispatcher_;

  State state_;
  StratumWorker worker_;
  std::atomic<bool> isDead_;
  bool isLongTimeout_;

  void setup();
  void setReadTimeout(int32_t readTimeout);

  bool handleMessage();  // handle all messages: ex-message and stratum message
  bool tryReadLine(std::string &line);
  void handleLine(const std::string &line);
  void handleRequest(const std::string &idStr, const std::string &method, const JsonNode &jparams, const JsonNode &jroot);
  void checkUserAndPwd(const string &idStr, const string &fullName, const string &password);
  void _handleRequest_AuthorizePassword(const string &password);
  void setClientAgent(const string &clientAgent);

  virtual bool validate(const JsonNode &jmethod, const JsonNode &jparams);
  virtual bool isSubscribe(const std::string &method) const = 0;
  virtual bool isAuthorize(const std::string &method) const = 0;
  virtual void handleRequest_Subscribe(const std::string &idStr, const JsonNode &jparams, const JsonNode &jroot) = 0;
  virtual bool handleRequest_Authorize(const std::string &idStr, const JsonNode &jparams, const JsonNode &jroot, std::string &fullName, std::string &password) = 0;
  virtual std::unique_ptr<StratumMessageDispatcher> createDispatcher();

  StratumSession(Server &server, struct bufferevent *bev, struct sockaddr *saddr, uint32_t extraNonce1);

public:
  virtual ~StratumSession();
  virtual bool initialize() { return true; }
  uint16_t decodeSessionId(const std::string &exMessage) const override { return StratumMessageEx::AGENT_MAX_SESSION_ID; };

  Server &getServer() { return server_; }
  StratumWorker &getWorker() { return worker_; }
  StratumMessageDispatcher &getDispatcher() override { return *dispatcher_; }
  uint32_t getClientIp() const { return clientIpInt_; };
  uint32_t getSessionId() const { return extraNonce1_; }
  State getState() const { return state_; }
  bool isDead() const;
  void markAsDead();
  void addWorker(const std::string &clientAgent, const std::string &workerName, int64_t workerId) override;

  void sendData(const char *data, size_t len) override;
  void sendData(const std::string &str) override { sendData(str.data(), str.size()); }
  void readBuf(struct evbuffer *buf);

  void responseTrue(const std::string &idStr) override;
  void responseError(const std::string &idStr, int code) override;
  virtual void responseAuthorized(const std::string &idStr);
  void rpc2ResponseTrue(const string &idStr);
  void rpc2ResponseError(const string &idStr, int errCode);
  void sendSetDifficulty(LocalJob &localJob, uint64_t difficulty) override;
  virtual void sendMiningNotify(shared_ptr<StratumJobEx> exJobPtr, bool isFirstJob=false) = 0;
};

//  This base class is to help type safety of accessing server_ member variable. Avoid manual casting.
//  And by templating a minimum class declaration, we avoid bloating the code too much.
template<typename StratumTraits>
class StratumSessionBase : public StratumSession
{
protected:
  using ServerType = typename StratumTraits::ServerType;
  StratumSessionBase(ServerType &server, struct bufferevent *bev, struct sockaddr *saddr, uint32_t extraNonce1)
      : StratumSession(server, bev, saddr, extraNonce1)
      , kMaxNumLocalJobs_(10)
  {
    // usually stratum job interval is 30~60 seconds, 10 is enough for miners
    // should <= 10, we use short_job_id,  range: [0 ~ 9]. do NOT change it.
    assert(kMaxNumLocalJobs_ <= 10);
  }

  using LocalJobType = typename StratumTraits::LocalJobType;
  static_assert(std::is_base_of<LocalJob, LocalJobType>::value, "Local job type is not derived from LocalJob");
  std::deque<LocalJobType> localJobs_;
  size_t kMaxNumLocalJobs_;

public:
  template<typename Key>
  LocalJobType *findLocalJob(const Key& key)
  {
    for (auto &localJob : localJobs_) {
      if (localJob == key) return &localJob;
    }
    return nullptr;
  }

  template<typename ... Args>
  LocalJobType &addLocalJob(uint64_t jobId, Args&&... args) {
    localJobs_.emplace_back(jobId, std::forward<Args>(args)...);
    auto &localJob = localJobs_.back();
    dispatcher_->addLocalJob(localJob);
    return localJob;
  }

  void clearLocalJobs() {
    while (localJobs_.size() >= kMaxNumLocalJobs_) {
      dispatcher_->removeLocalJob(localJobs_.front());
      localJobs_.pop_front();
    }
  }
  inline ServerType &getServer() const
  {
    return static_cast<ServerType &>(server_);
  }
};

#endif // #ifndef STRATUM_SESSION_H_

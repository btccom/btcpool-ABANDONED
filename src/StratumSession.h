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

#include "Common.h"

#include "utilities_js.hpp"
#include "Stratum.h"
#include "Statistics.h"

#include <boost/endian/buffers.hpp>
#include <deque>

#include <event2/bufferevent.h>

#include <glog/logging.h>


#define CMD_MAGIC_NUMBER      0x7Fu
// types
#define CMD_REGISTER_WORKER   0x01u             // Agent -> Pool
#define CMD_SUBMIT_SHARE      0x02u             // Agent -> Pool, without block time
#define CMD_SUBMIT_SHARE_WITH_TIME  0x03u       // Agent -> Pool
#define CMD_UNREGISTER_WORKER 0x04u             // Agent -> Pool
#define CMD_MINING_SET_DIFF   0x05u             // Pool  -> Agent

// agent
#define AGENT_MAX_SESSION_ID   0xFFFEu  // 0xFFFEu = 65534

#define BTCCOM_MINER_AGENT_PREFIX "btccom-agent/"

// invalid share sliding window size
#define INVALID_SHARE_SLIDING_WINDOWS_SIZE       60  // unit: seconds
#define INVALID_SHARE_SLIDING_WINDOWS_MAX_LIMIT  20  // max number

class Server;
class StratumJobEx;
class DiffController;
class StratumSession;

struct StratumMessageEx {
  boost::endian::little_uint8_buf_t magic;
  boost::endian::little_uint8_buf_t command;
  boost::endian::little_uint16_buf_t length;
};

//////////////////////////////// StratumSession ////////////////////////////////
class StratumSession {
public:
  // mining state
  enum State {
    CONNECTED     = 0,
    SUBSCRIBED    = 1,
    AUTHENTICATED = 2
  };

  // shares submitted by this session, for duplicate share check
  struct LocalShare {
    uint64_t exNonce2_;  // extra nonce2 fixed 8 bytes
    uint32_t nonce_;     // nonce in block header
    uint32_t time_;      // nTime in block header

    LocalShare(uint64_t exNonce2, uint32_t nonce, uint32_t time):
    exNonce2_(exNonce2), nonce_(nonce), time_(time) {}

    LocalShare & operator=(const LocalShare &other) {
      exNonce2_ = other.exNonce2_;
      nonce_    = other.nonce_;
      time_     = other.time_;
      return *this;
    }

    bool operator<(const LocalShare &r) const {
      if (exNonce2_ < r.exNonce2_ ||
          (exNonce2_ == r.exNonce2_ && nonce_ < r.nonce_) ||
          (exNonce2_ == r.exNonce2_ && nonce_ == r.nonce_ && time_ < r.time_)) {
        return true;
      }
      return false;
    }
  };

  // latest stratum jobs of this session
  struct LocalJob {
    uint64_t jobId_;
    uint64_t jobDifficulty_;     // difficulty of this job
    uint32_t blkBits_;
    uint8_t  shortJobId_;
#ifdef USER_DEFINED_COINBASE
    string   userCoinbaseInfo_;
#endif
    std::set<LocalShare> submitShares_;
    std::vector<uint8_t> agentSessionsDiff2Exp_;

    LocalJob(): jobId_(0), jobDifficulty_(0), blkBits_(0), shortJobId_(0) {}

    bool addLocalShare(const LocalShare &localShare) {
      auto itr = submitShares_.find(localShare);
      if (itr != submitShares_.end()) {
        return false;  // already exist
      }
      submitShares_.insert(localShare);
      return true;
    }
  };

  //----------------------
protected:
  int32_t shareAvgSeconds_;
  shared_ptr<DiffController> diffController_;
  State state_;
  StratumWorker worker_;
  string   clientAgent_;  // eg. bfgminer/4.4.0-32-gac4e9b3
  string   clientIp_;
  uint32_t clientIpInt_;

  uint32_t extraNonce1_;   // MUST be unique across all servers. TODO: rename it to "sessionId_"
  static const int kExtraNonce2Size_ = 8;  // extraNonce2 size is always 8 bytes

  uint64_t currDiff_;
  std::deque<LocalJob> localJobs_;
  size_t kMaxNumLocalJobs_;

  struct evbuffer *inBuf_;
  bool   isLongTimeout_;
  uint8_t shortJobIdIdx_;

  // nicehash has can't use short JobID
  bool isNiceHashClient_;

  atomic<bool> isDead_;

  // invalid share counter
  StatsWindow<int64_t> invalidSharesCounter_;

  uint8_t allocShortJobId();

  void setup();
  void setReadTimeout(const int32_t timeout);

  virtual bool handleMessage();  // handle all messages: ex-message and stratum message

  virtual void responseError(const string &idStr, int code);
  virtual void responseTrue(const string &idStr);
  void rpc2ResponseTrue(const string &idStr);
  void rpc2ResponseError(const string &idStr, int errCode);

  bool tryReadLine(string &line);
  void handleLine(const string &line);
  void handleRequest(const string &idStr, const string &method, const JsonNode &jparams, const JsonNode &jroot);

  void handleRequest_SuggestDifficulty(const string &idStr, const JsonNode &jparams);
  void handleRequest_MultiVersion     (const string &idStr, const JsonNode &jparams);
  void _handleRequest_SetDifficulty(uint64_t suggestDiff);
  void _handleRequest_AuthorizePassword(const string &password);

  LocalJob *findLocalJob(uint8_t shortJobId);
  void clearLocalJobs();

  void checkUserAndPwd(const string &idStr, const string &fullName, const string &password);

  virtual void handleRequest_Subscribe        (const string &idStr, const JsonNode &jparams) = 0;
  virtual void handleRequest_Authorize        (const string &idStr, const JsonNode &jparams, const JsonNode &jroot) = 0;
  virtual void handleRequest_Submit           (const string &idStr, const JsonNode &jparams) = 0;
  virtual void handleRequest_GetWork(const string &idStr, const JsonNode &jparams) {};          //  Gani#TODO: non standard? move to derived class?
  virtual void handleRequest_SubmitHashrate(const string &idStr, const JsonNode &jparams) {};   //  Gani#TODO: non standard? move to derived class?
  //return true if request is handled
  virtual bool handleRequest_Specific(const string &idStr, const string &method,
                                      const JsonNode &jparams, const JsonNode &jroot) { return false; }
  virtual bool needToSendLoginResponse() const {return true;}

  virtual void handleExMessage_RegisterWorker     (const string *exMessage) {}
  virtual void handleExMessage_UnRegisterWorker   (const string *exMessage) {}
  virtual void handleExMessage_SubmitShare        (const string *exMessage) {}
  virtual void handleExMessage_SubmitShareWithTime(const string *exMessage) {}

public:
  struct bufferevent* bev_;
  evutil_socket_t fd_;
  Server *server_;
protected:
  StratumSession(evutil_socket_t fd, struct bufferevent *bev,
                 Server *server, struct sockaddr *saddr,
                 const int32_t shareAvgSeconds, const uint32_t extraNonce1);
public:
  virtual ~StratumSession();
  virtual bool initialize();
  virtual void sendMiningNotify(shared_ptr<StratumJobEx> exJobPtr, bool isFirstJob=false) = 0;
  virtual bool validate(const JsonNode &jmethod, const JsonNode &jparams);

  void markAsDead();
  bool isDead();

  void sendSetDifficulty(const uint64_t difficulty);
  void sendData(const char *data, size_t len);
  inline void sendData(const string &str) {
    sendData(str.data(), str.size());
  }
  void readBuf(struct evbuffer *buf);

  uint32_t getSessionId() const;
};

//  This base class is to help type safety of accessing server_ member variable. Avoid manual casting.
//  And by templating a minimum class declaration, we avoid bloating the code too much.
template<typename ServerType>
class StratumSessionBase : public StratumSession
{
protected:
  StratumSessionBase(evutil_socket_t fd, struct bufferevent *bev,
                 ServerType *server, struct sockaddr *saddr,
                 const int32_t shareAvgSeconds, const uint32_t extraNonce1)
    : StratumSession(fd, bev, server, saddr, shareAvgSeconds, extraNonce1)
  {

  }
public:
  inline ServerType* GetServer() const
  {
    return static_cast<ServerType*>(server_);
  }
private:
  using StratumSession::server_; //  hide the server_ member variable
};

#endif

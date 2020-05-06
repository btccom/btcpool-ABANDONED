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
#ifndef STRATUM_SERVER_H_
#define STRATUM_SERVER_H_

#include "Common.h"

#include "Kafka.h"
#include "Stratum.h"
#include "Zookeeper.h"
#include "UserInfo.h"

#include "prometheus/Exporter.h"
#include "prometheus/Collector.h"
#include "prometheus/Metric.h"

#include "WorkerPool.h"

#include <bitset>
#include <regex>

#include <openssl/ssl.h>
#include <event2/bufferevent.h>
#include <event2/listener.h>
#include <event2/event.h>
#include <event2/bufferevent_ssl.h>

#include <glog/logging.h>

namespace libconfig {
class Config;
}

class StratumServer;
class StratumJobEx;
class StratumServerWrapper;
class StratumSession;
class DiffController;
class Management;

//////////////////////////////// SessionIDManager //////////////////////////////

enum StratumServerType { BTC = 1, ETH };

class SessionIDManager {
public:
  virtual ~SessionIDManager() {}
  virtual bool ifFull() = 0;
  // The default value is 0: no interval, the session id will be allocated
  // continuously. If the value is N, then id2 = id1 + N. Skipped ids are not
  // assigned to other sessions unless the allocator reaches the maximum and
  // rolls back to the beginning. This setting can be used to reserve more
  // mining space for workers and there is no DoS risk.
  virtual void setAllocInterval(uint32_t interval) = 0;
  virtual bool allocSessionId(uint32_t *sessionID) = 0;
  virtual void freeSessionId(uint32_t sessionId) = 0;
};

// thread-safe
// template IBITS: index bits
template <uint8_t IBITS>
class SessionIDManagerT : public SessionIDManager {
  //
  //  SESSION ID: UINT32_T
  //
  //  0 bit or longer       8bit            24 bit or shorter
  // -----------------    ---------    ----------------------------
  // leading zero bits    server ID             session id
  //     [000...]          [1, 255]    range: [0, kSessionIdMask]
  //

  const static uint32_t kSessionIdMask =
      (1 << IBITS) - 1; // example: 0x00FFFFFF;

  uint8_t serverId_;
  std::bitset<kSessionIdMask + 1> sessionIds_;

  uint32_t count_; // how many ids are used now
  uint32_t allocIdx_;
  uint32_t allocInterval_;
  mutex lock_;

  bool _ifFull();

public:
  SessionIDManagerT(const uint8_t serverId);

  bool ifFull() override;
  void setAllocInterval(uint32_t interval) override;
  bool allocSessionId(uint32_t *sessionID) override;
  void freeSessionId(uint32_t sessionId) override;
};

////////////////////////////////// JobRepository ///////////////////////////////
class JobRepository {
protected:
  atomic<bool> running_;
  size_t chainId_;
  std::map<uint64_t /* jobId */, shared_ptr<StratumJobEx>> exJobs_;

  KafkaSimpleConsumer kafkaConsumer_; // consume topic: 'StratumJob'
  StratumServer *server_; // call server to send new job

  string fileLastNotifyTime_;

  time_t kMaxJobsLifeTime_;
  time_t kMiningNotifyInterval_;

  time_t lastJobSendTime_;
  uint64_t lastJobId_;
  uint64_t lastJobHeight_;

  thread threadConsume_;
  friend class StratumServerStats;
  friend class Management;

  bool niceHashForced_;
  std::atomic<uint64_t> niceHashMinDiff_;
  std::unique_ptr<ZookeeperValueWatcher> niceHashMinDiffWatcher_;

private:
  void runThreadConsume();
  void consumeStratumJob(rd_kafka_message_t *rkmessage);
  void tryCleanExpiredJobs();
  void checkAndSendMiningNotify();

public:
  JobRepository(
      size_t chainId,
      StratumServer *server,
      const char *kafkaBrokers,
      const char *consumerTopic,
      const string &fileLastNotifyTime,
      bool niceHashForced,
      uint64_t niceHashMinDiff,
      const std::string &niceHashMinDiffZookeeperPath);
  virtual ~JobRepository();

  void stop();
  bool setupThreadConsume();
  void markAllJobsAsStale(uint64_t height);

  void setMaxJobLifeTime(const time_t maxJobLifeTime);
  void setMiningNotifyInterval(time_t miningNotifyInterval);
  void sendMiningNotify(shared_ptr<StratumJobEx> exJob);
  void sendLatestMiningNotify();
  shared_ptr<StratumJobEx> getStratumJobEx(const uint64_t jobId);
  shared_ptr<StratumJobEx> getLatestStratumJobEx();

  virtual shared_ptr<StratumJob> createStratumJob() = 0;
  virtual shared_ptr<StratumJobEx>
  createStratumJobEx(shared_ptr<StratumJob> sjob, bool isClean);
  virtual void broadcastStratumJob(shared_ptr<StratumJob> sjob) = 0;

  bool niceHashForced() const { return niceHashForced_; }
  uint64_t niceHashMinDiff() const { return niceHashMinDiff_; }
};

//  This base class is to help type safety of accessing server_ member variable.
//  Avoid manual casting. And by templating a minimum class declaration, we
//  avoid bloating the code too much.
template <typename ServerType>
class JobRepositoryBase : public JobRepository {
public:
  using JobRepository::JobRepository;

protected:
  inline ServerType *GetServer() const {
    return static_cast<ServerType *>(server_);
  }

private:
  using JobRepository::server_; //  hide the server_ member variable
};

////////////////////////////////// StratumJobEx ////////////////////////////////
//
// StratumJobEx is use to wrap StratumJob
//
class StratumJobEx {
  // 0: MINING, 1: STALE
  atomic<int32_t> state_;

public:
  size_t chainId_;
  bool isClean_;
  shared_ptr<StratumJob> sjob_;

public:
  StratumJobEx(size_t chainId, shared_ptr<StratumJob> sjob, bool isClean);
  virtual ~StratumJobEx();

  void markStale();
  bool isStale();
};

class StratumServerMiningModel {
public:
  enum { SUBACCOUNT = 1, ANONYMOUS = 2 };
};

///////////////////////////////////// StratumServer
//////////////////////////////////////
class StratumServer {
  // NetIO
  bool enableTLS_;
  SSL_CTX *sslCTX_;
  struct sockaddr_in sin_;
  struct event_base *base_;
  struct evconnlistener *listener_;
  std::set<unique_ptr<StratumSession>> connections_;
  uint32_t tcpReadTimeout_; // seconds
  uint32_t shutdownGracePeriod_;
  struct event *disconnectTimer_;

  unique_ptr<Management> management_;

public:
  struct ChainVars {
    string name_;

    // kafka producers
    KafkaProducer *kafkaProducerShareLog_;
    KafkaProducer *kafkaProducerSolvedShare_;
    KafkaProducer *kafkaProducerCommonEvents_;

    JobRepository *jobRepository_;
    std::map<int32_t, size_t> shareStats_;

    int32_t singleUserId_;
  };

  bool acceptStale_;

  // ------------------- Development Options: -------------------
  // WARNING: if enable simulator, all share will be accepted. only for test.
  bool isEnableSimulator_;
  // WARNING: if enable it, will make block and submit it even it's not a
  //          solved share. use to test submit block.
  bool isSubmitInvalidBlock_;
  // WARNING: if enable, difficulty sent to miners is always
  // devFixedDifficulty_.
  bool isDevModeEnable_;
  // WARNING: difficulty to send to miners.
  float devFixedDifficulty_;

  bool grandPoolEnabled_;

#ifndef WORK_WITH_STRATUM_SWITCHER
  SessionIDManager *sessionIDManager_;
#endif

  // hide "client connect" log with the prefix
  string logHideIpPrefix_;

  // in anonymous-mode, we need produce userid only used in this proccess
  SessionIDManager *userIdManager_;
  std::unordered_map<string, int32_t> anonymousNameIds_;
  uint32_t miningModel_;

  UserInfo *userInfo_;
  vector<ChainVars> chains_;
  shared_ptr<DiffController> defaultDifficultyController_;
  uint8_t serverId_;

  bool singleUserChain_ = false;
  bool singleUserMode_ = false;
  string singleUserName_;

  bool proxyProtocol_ = false;

  shared_ptr<Zookeeper> zk_;

  friend class StratumServerStats;
  friend class Management;
  shared_ptr<prometheus::Collector> statsCollector_;
  unique_ptr<prometheus::IExporter> statsExporter_;

  std::regex longTimeoutPattern_;

  unique_ptr<WorkerPool> shareWorker_;

protected:
  SSL_CTX *getSSLCTX(const libconfig::Config &config);

  // This class cannot be instantiated.
  // Only subclasses of this class can be instantiated.
  StratumServer();
  virtual bool setupInternal(const libconfig::Config &config) { return true; };
  void initZookeeper(const libconfig::Config &config);

public:
  virtual ~StratumServer();

  bool setup(const libconfig::Config &config);
  void run();
  void stop();
  void stopGracefully();

  // Dispatch the task to the libevent loop
  void dispatch(std::function<void()> task);
  // Dispatch the task with alive check
  void dispatchSafely(std::function<void()> task, std::weak_ptr<bool> alive);
  // Dispatch the work to the share worker
  void dispatchToShareWorker(std::function<void()> work);

  shared_ptr<Zookeeper> getZookeeper(const libconfig::Config &config) {
    initZookeeper(config);
    return zk_;
  }

  const uint32_t tcpReadTimeout() { return tcpReadTimeout_; }
  const string &chainName(size_t chainId) { return chains_[chainId].name_; }
  size_t /* online sessions */
  switchChain(string userName, size_t newChainId);
  size_t /* switched sessions */
  autoSwitchChain(size_t newChainId);
  size_t /* auto reg sessions */
  autoRegCallback(const string &userName);

  void sendMiningNotifyToAll(shared_ptr<StratumJobEx> exJobPtr);

  void addConnection(unique_ptr<StratumSession> connection);
  void removeConnection(StratumSession &connection);

  static void listenerCallback(
      struct evconnlistener *listener,
      evutil_socket_t socket,
      struct sockaddr *saddr,
      int socklen,
      void *server);
  static void disconnectCallback(evutil_socket_t, short, void *context);
  static void readCallback(struct bufferevent *, void *connection);
  static void eventCallback(struct bufferevent *, short, void *connection);

  void sendShare2Kafka(size_t chainId, const char *data, size_t len);
  void sendSolvedShare2Kafka(size_t chainId, const char *data, size_t len);
  void sendCommonEvents2Kafka(size_t chainId, const string &message);

  virtual unique_ptr<StratumSession> createConnection(
      struct bufferevent *bev, struct sockaddr *saddr, uint32_t sessionID) = 0;

  bool singleUserChain() const { return singleUserChain_; }
  bool singleUserMode() const { return singleUserMode_; }
  string singleUserName() const { return singleUserName_; }
  int32_t singleUserId(size_t chainId) {
    return chains_[chainId].singleUserId_;
  }

  Management &management() { return *management_; }

  bool proxyProtocol() const { return proxyProtocol_; }

  bool logHideIpPrefix(const string &ip);

protected:
  virtual JobRepository *createJobRepository(
      size_t chainId,
      const char *kafkaBrokers,
      const char *consumerTopic,
      const string &fileLastNotifyTime,
      bool niceHashForced,
      uint64_t niceHashMinDiff,
      const std::string &niceHashMinDiffZookeeperPath) = 0;
};

template <typename TJobRepository>
class ServerBase : public StratumServer {
public:
  TJobRepository *GetJobRepository(size_t chainId) {
    return static_cast<TJobRepository *>(chains_[chainId].jobRepository_);
  }
};

#endif

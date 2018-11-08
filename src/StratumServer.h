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

#include <bitset>

#include <event2/bufferevent.h>
#include <event2/listener.h>
#include <event2/event.h>

#include <glog/logging.h>

namespace libconfig {
class Config;
}

class Server;
class StratumJobEx;
class StratumServer;
class StratumSession;
class DiffController;

#ifndef WORK_WITH_STRATUM_SWITCHER

//////////////////////////////// SessionIDManager //////////////////////////////

enum StratumServerType
{
  BTC = 1,
  ETH
};

class SessionIDManager {
public:
  virtual ~SessionIDManager() {}
  virtual bool ifFull() = 0;
  // The default value is 0: no interval, the session id will be allocated continuously.
  // If the value is N, then id2 = id1 + N.
  // Skipped ids are not assigned to other sessions unless the allocator reaches
  // the maximum and rolls back to the beginning.
  // This setting can be used to reserve more mining space for workers and there is no DoS risk.
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

  const static uint32_t kSessionIdMask = (1 << IBITS) - 1;      // example: 0x00FFFFFF;

  uint8_t serverId_;
  std::bitset<kSessionIdMask + 1> sessionIds_;

  uint32_t count_;  // how many ids are used now
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

#endif // #ifndef WORK_WITH_STRATUM_SWITCHER


////////////////////////////////// JobRepository ///////////////////////////////
class JobRepository
{
protected:
  atomic<bool> running_;
  mutex lock_;
  std::map<uint64_t /* jobId */, shared_ptr<StratumJobEx>> exJobs_;

  KafkaConsumer kafkaConsumer_; // consume topic: 'StratumJob'
  Server *server_;              // call server to send new job

  string fileLastNotifyTime_;

  time_t kMaxJobsLifeTime_;
  const time_t kMiningNotifyInterval_;

  time_t lastJobSendTime_;

  thread threadConsume_;

private:
  void runThreadConsume();
  void consumeStratumJob(rd_kafka_message_t *rkmessage);
  void tryCleanExpiredJobs();
  void checkAndSendMiningNotify();

protected:
  JobRepository(const char *kafkaBrokers, const char *consumerTopic, const string &fileLastNotifyTime, Server *server);
public:
  virtual ~JobRepository();

  void stop();
  bool setupThreadConsume();
  void markAllJobsAsStale();
  
  void setMaxJobDelay (const time_t maxJobDelay);
  void sendMiningNotify(shared_ptr<StratumJobEx> exJob);
  shared_ptr<StratumJobEx> getStratumJobEx(const uint64_t jobId);
  shared_ptr<StratumJobEx> getLatestStratumJobEx();

  virtual StratumJob* createStratumJob() = 0;
  virtual StratumJobEx* createStratumJobEx(StratumJob *sjob, bool isClean);
  virtual void broadcastStratumJob(StratumJob *sjob) = 0;
};

//  This base class is to help type safety of accessing server_ member variable. Avoid manual casting.
//  And by templating a minimum class declaration, we avoid bloating the code too much.
template<typename ServerType>
class JobRepositoryBase : public JobRepository
{
protected:
  JobRepositoryBase(const char *kafkaBrokers, const char *consumerTopic, const string &fileLastNotifyTime, ServerType *server)
    : JobRepository(kafkaBrokers, consumerTopic, fileLastNotifyTime, server)
  {

  }
protected:
  inline ServerType* GetServer() const
  {
    return static_cast<ServerType*>(server_);
  }
private:
  using JobRepository::server_; //  hide the server_ member variable
};



///////////////////////////////////// UserInfo /////////////////////////////////
// 1. update userName->userId by interval
// 2. insert worker name to db
class UserInfo {
  struct WorkerName {
    int32_t userId_;
    int64_t workerId_;
    char    workerName_[21];
    char    minerAgent_[31];

    WorkerName(): userId_(0), workerId_(0) {
      memset(workerName_, 0, sizeof(workerName_));
      memset(minerAgent_, 0, sizeof(minerAgent_));
    }
  };

  //--------------------
  pthread_rwlock_t rwlock_;
  atomic<bool> running_;
  string apiUrl_;

  // username -> userId
  std::unordered_map<string, int32_t> nameIds_;
  int32_t lastMaxUserId_;
  
#ifdef USER_DEFINED_COINBASE
  // userId -> userCoinbaseInfo
  std::unordered_map<int32_t, string> idCoinbaseInfos_;
  int64_t lastTime_;
#endif

  // workerName
  mutex workerNameLock_;
  std::deque<WorkerName> workerNameQ_;
  Server *server_;

  thread threadInsertWorkerName_;
  void runThreadInsertWorkerName();
  int32_t insertWorkerName();

  thread threadUpdate_;
  void runThreadUpdate();
  int32_t incrementalUpdateUsers();

public:
  UserInfo(const string &apiUrl, Server *server);
  ~UserInfo();

  void stop();
  bool setupThreads();

  int32_t getUserId(const string userName);

#ifdef USER_DEFINED_COINBASE
  string  getCoinbaseInfo(int32_t userId);
#endif

  void addWorker(const int32_t userId, const int64_t workerId,
                 const string &workerName, const string &minerAgent);
};

////////////////////////////////// StratumJobEx ////////////////////////////////
//
// StratumJobEx is use to wrap StratumJob
//
class StratumJobEx {
  // 0: MINING, 1: STALE
  atomic<int32_t> state_;

public:
  bool isClean_;
  StratumJob *sjob_;

public:
  StratumJobEx(StratumJob *sjob, bool isClean);
  virtual ~StratumJobEx();

  void markStale();
  bool isStale();

};


///////////////////////////////////// Server ///////////////////////////////////
class Server {
  // NetIO
  struct sockaddr_in sin_;
  struct event_base* base_;
  struct event* signal_event_;
  struct evconnlistener* listener_;
  std::set<unique_ptr<StratumSession>> connections_;
  mutex connsLock_;

public:
  // kafka producers
  KafkaProducer *kafkaProducerShareLog_;
  KafkaProducer *kafkaProducerSolvedShare_;
  KafkaProducer *kafkaProducerCommonEvents_;

  //
  // WARNING: if enable simulator, all share will be accepted. only for test.
  //
  bool isEnableSimulator_;

  //
  // WARNING: if enable it, will make block and submit it even it's not a
  //          solved share. use to test submit block.
  //
  bool isSubmitInvalidBlock_;

#ifndef WORK_WITH_STRATUM_SWITCHER
  SessionIDManager *sessionIDManager_;
#endif

  //
  // WARNING: if enable, difficulty sent to miners is always minerDifficulty_. 
  //          for development
  //
  bool isDevModeEnable_;
  //
  // WARNING: difficulty to send to miners. for development
  //
  float minerDifficulty_;
  const int32_t kShareAvgSeconds_;
  JobRepository *jobRepository_;
  UserInfo *userInfo_;
  shared_ptr<DiffController> defaultDifficultyController_;
  uint8 serverId_;

protected:
  Server(const int32_t shareAvgSeconds);

  virtual bool setupInternal(StratumServer* sserver){ return true; };

public:
  virtual ~Server();

  bool setup(StratumServer* sserver);
  void run();
  void stop();

  void sendMiningNotifyToAll(shared_ptr<StratumJobEx> exJobPtr);

  void addConnection(unique_ptr<StratumSession> connection);
  void removeConnection(StratumSession &connection);

  static void listenerCallback(struct evconnlistener* listener,
                               evutil_socket_t socket,
                               struct sockaddr* saddr,
                               int socklen, void* server);
  static void readCallback (struct bufferevent *, void *connection);
  static void eventCallback(struct bufferevent *, short, void *connection);

  void sendShare2Kafka      (const uint8_t *data, size_t len);
  void sendCommonEvents2Kafka(const string &message);

  virtual unique_ptr<StratumSession> createConnection(struct bufferevent *bev, struct sockaddr *saddr, uint32_t sessionID) = 0;

protected:
  virtual JobRepository* createJobRepository(const char *kafkaBrokers,
                                    const char *consumerTopic,
                                     const string &fileLastNotifyTime) = 0;

};

template<typename TJobRepository>
class ServerBase : public Server
{
public:
  TJobRepository* GetJobRepository(){ return static_cast<TJobRepository*>(jobRepository_); }
protected:
  ServerBase(const int32_t shareAvgSeconds) : Server(shareAvgSeconds) { }

private:
  using Server::jobRepository_;
};

////////////////////////////////// StratumServer ///////////////////////////////
class StratumServer {

public:
  atomic<bool> running_;

  shared_ptr<Server> server_;
  string ip_;
  unsigned short port_;
  uint8_t serverId_;  // global unique, range: [1, 255]

  string fileLastNotifyTime_;

  string kafkaBrokers_;
  string userAPIUrl_;

  // if enable simulator, all share will be accepted
  bool isEnableSimulator_;

  // if enable it, will make block and submit
  bool isSubmitInvalidBlock_;
  
  // if enable, difficulty sent to miners is always minerDifficulty_
  bool isDevModeEnable_;

  // difficulty to send to miners. for development
  float minerDifficulty_;
  
  string consumerTopic_;
  uint32 maxJobDelay_;
  shared_ptr<DiffController> defaultDifficultyController_;
  string solvedShareTopic_;
  string shareTopic_;
  string commonEventsTopic_;

  StratumServer(const char *ip, const unsigned short port,
                const char *kafkaBrokers,
                const string &userAPIUrl,
                const uint8_t serverId, const string &fileLastNotifyTime,
                bool isEnableSimulator,
                bool isSubmitInvalidBlock,
                bool isDevModeEnable,
                float minerDifficulty,
                const string &consumerTopic,
                uint32 maxJobDelay,
                shared_ptr<DiffController> defaultDifficultyController,
                const string& solvedShareTopic,
                const string& shareTopic,
                const string& commonEventsTopic);
  ~StratumServer();
  bool createServer(const string &type, const int32_t shareAvgSeconds, const libconfig::Config &config);
  bool init();
  void stop();
  void run();
};


#endif

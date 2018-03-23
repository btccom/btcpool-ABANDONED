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

#include <map>
#include <unordered_map>
#include <vector>
#include <memory>
#include <bitset>

#include <arpa/inet.h>
#include <sys/socket.h>

#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/listener.h>
#include <event2/util.h>
#include <event2/event.h>
#include <glog/logging.h>

#include "Kafka.h"
#include "MySQLConnection.h"
#include "Stratum.h"
#include "StratumSession.h"

class Server;
class StratumJobEx;


#ifndef WORK_WITH_STRATUM_SWITCHER

//////////////////////////////// SessionIDManager //////////////////////////////
// DO NOT CHANGE
#define MAX_SESSION_INDEX_SERVER   0x00FFFFFEu   // 16777214

enum StratumServerType
{
  BTC = 1,
  ETH
};

// thread-safe
class SessionIDManager {
  //
  //  SESSION ID: UINT32_T
  //
  //   xxxxxxxx     xxxxxxxx xxxxxxxx xxxxxxxx
  //  ----------    --------------------------
  //  server ID          session id
  //   [1, 255]        range: [0, MAX_SESSION_INDEX_SERVER]
  //
  uint8_t serverId_;
  std::bitset<MAX_SESSION_INDEX_SERVER + 1> sessionIds_;

  int32_t count_;  // how many ids are used now
  uint32_t allocIdx_;
  mutex lock_;

  bool _ifFull();

public:
  SessionIDManager(const uint8_t serverId);

  bool ifFull();
  bool allocSessionId(uint32_t *sessionID);
  void freeSessionId(uint32_t sessionId);
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
  uint256 latestPrevBlockHash_;

  thread threadConsume_;
  StratumServerType serverType_;

private:
  void runThreadConsume();
  void consumeStratumJob(rd_kafka_message_t *rkmessage);
  void tryCleanExpiredJobs();
  void checkAndSendMiningNotify();

public:
  JobRepository(const char *kafkaBrokers, const char *consumerTopic, const string &fileLastNotifyTime, Server *server);
  virtual ~JobRepository();

  void stop();
  bool setupThreadConsume();
  void markAllJobsAsStale();
  StratumJobEx *createStratumJobEx(StratumServerType type, StratumJob *sjob, bool isClean);
  StratumJob *createStratumJob();
  virtual void broadcastStratumJob(StratumJob *sjob);
  void sendMiningNotify(shared_ptr<StratumJobEx> exJob);
  shared_ptr<StratumJobEx> getStratumJobEx(const uint64_t jobId);
  shared_ptr<StratumJobEx> getLatestStratumJobEx();
};

class JobRepositoryEth : public JobRepository
{
public:
  JobRepositoryEth(const char *kafkaBrokers, const char *consumerTopic, const string &fileLastNotifyTime, Server *server);
  virtual ~JobRepositoryEth();
  virtual void broadcastStratumJob(StratumJob *sjob);
  bool compute(ethash_h256_t const header, uint64_t nonce, ethash_return_value_t& r);

private:
  void newLight(StratumJobEth* job);
  void newLight(uint64_t blkNum);
  void deleteLight();
  void deleteLightNoLock();

  ethash_light_t light_;
  ethash_light_t nextLight_;
  uint64_t epochs_;
  mutex lightLock_;
};

class JobRepositorySia : public JobRepository
{
public:
  JobRepositorySia(const char *kafkaBrokers, const char *consumerTopic, const string &fileLastNotifyTime, Server *server);
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
  void generateCoinbaseTx(std::vector<char> *coinbaseBin,
                          const uint32_t extraNonce1,
                          const string &extraNonce2Hex,
                          string *userCoinbaseInfo = nullptr);

public:
  bool isClean_;
  StratumJob *sjob_;
  string miningNotify1_;
  string miningNotify2_;
  string coinbase1_;
  string miningNotify3_;
  string miningNotify3Clean_;

public:
  StratumJobEx(StratumJob *sjob, bool isClean);
  virtual ~StratumJobEx();

  void markStale();
  bool isStale();

  void generateBlockHeader(CBlockHeader  *header,
                           std::vector<char> *coinbaseBin,
                           const uint32_t extraNonce1,
                           const string &extraNonce2Hex,
                           const vector<uint256> &merkleBranch,
                           const uint256 &hashPrevBlock,
                           const uint32_t nBits, const int32_t nVersion,
                           const uint32_t nTime, const uint32_t nonce,
                           string *userCoinbaseInfo = nullptr);
  virtual void makeMiningNotifyStr();
};

class StratumJobExEth : public StratumJobEx {
public:
  StratumJobExEth(StratumJob *sjob, bool isClean);
  virtual void makeMiningNotifyStr();
};

///////////////////////////////////// Server ///////////////////////////////////
class Server {
  // NetIO
  struct sockaddr_in sin_;
  struct event_base* base_;
  struct event* signal_event_;
  struct evconnlistener* listener_;
  std::map<evutil_socket_t, StratumSession *> connections_;
  mutex connsLock_;

public:
  // kafka producers
  KafkaProducer *kafkaProducerShareLog_;
  KafkaProducer *kafkaProducerSolvedShare_;
  KafkaProducer *kafkaProducerNamecoinSolvedShare_;
  KafkaProducer *kafkaProducerCommonEvents_;
  KafkaProducer *kafkaProducerRskSolvedShare_;

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

public:
  Server(const int32_t shareAvgSeconds);
  virtual ~Server();

  bool setup(const char *ip, const unsigned short port, const char *kafkaBrokers,
             const string &userAPIUrl,
             const uint8_t serverId, const string &fileLastNotifyTime,
             bool isEnableSimulator,
             bool isSubmitInvalidBlock,
             bool isDevModeEnable,
             float minerDifficulty,
             const string &consumerTopic);
  void run();
  void stop();

  void sendMiningNotifyToAll(shared_ptr<StratumJobEx> exJobPtr);

  void addConnection   (evutil_socket_t fd, StratumSession *connection);
  void removeConnection(evutil_socket_t fd);

  static void listenerCallback(struct evconnlistener* listener,
                               evutil_socket_t socket,
                               struct sockaddr* saddr,
                               int socklen, void* server);
  static void readCallback (struct bufferevent *, void *connection);
  static void eventCallback(struct bufferevent *, short, void *connection);

  int checkShare(const Share &share,
                 const uint32 extraNonce1, const string &extraNonce2Hex,
                 const uint32_t nTime, const uint32_t nonce,
                 const uint256 &jobTarget, const string &workFullName,
                 string *userCoinbaseInfo = nullptr);

  void sendShare2Kafka      (const uint8_t *data, size_t len);
  void sendSolvedShare2Kafka(const FoundBlock *foundBlock,
                             const std::vector<char> &coinbaseBin);
  void sendCommonEvents2Kafka(const string &message);

  virtual JobRepository* createJobRepository(const char *kafkaBrokers,
                                    const char *consumerTopic,
                                     const string &fileLastNotifyTime,
                                     Server *server);

  virtual StratumSession* createSession(evutil_socket_t fd, struct bufferevent *bev,
                               Server *server, struct sockaddr *saddr,
                               const int32_t shareAvgSeconds,
                               const uint32_t sessionID);
};

class ServerEth : public Server
{
public:
  ServerEth(const int32_t shareAvgSeconds) : Server(shareAvgSeconds) {}
  int checkShare(const Share &share,
                 const uint64_t nonce,
                 const uint256 header,
                 const uint256 mixHash);
  void sendSolvedShare2Kafka(const string& strNonce, const string& strHeader, const string& strMix);

  virtual JobRepository* createJobRepository(const char *kafkaBrokers,
                                    const char *consumerTopic,
                                     const string &fileLastNotifyTime,
                                     Server *server);

  virtual StratumSession* createSession(evutil_socket_t fd, struct bufferevent *bev,
                               Server *server, struct sockaddr *saddr,
                               const int32_t shareAvgSeconds,
                               const uint32_t sessionID);
};

class ServerSia : public Server
{
public:
  ServerSia(const int32_t shareAvgSeconds) : Server(shareAvgSeconds) {}

  virtual JobRepository* createJobRepository(const char *kafkaBrokers,
                                     const char *consumerTopic,     
                                     const string &fileLastNotifyTime,
                                     Server *server);

  virtual StratumSession* createSession(evutil_socket_t fd, struct bufferevent *bev,
                               Server *server, struct sockaddr *saddr,
                               const int32_t shareAvgSeconds,
                               const uint32_t sessionID);
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

  StratumServer(const char *ip, const unsigned short port,
                const char *kafkaBrokers,
                const string &userAPIUrl,
                const uint8_t serverId, const string &fileLastNotifyTime,
                bool isEnableSimulator,
                bool isSubmitInvalidBlock,
                bool isDevModeEnable,
                float minerDifficulty,
                const string &consumerTopic);
  ~StratumServer();
  bool createServer(string type, const int32_t shareAvgSeconds);
  bool init();
  void stop();
  void run();
};


#endif

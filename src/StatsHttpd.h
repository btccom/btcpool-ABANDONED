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
#ifndef STATSHTTPD_H_
#define STATSHTTPD_H_

#include "Common.h"
#include "Kafka.h"
#include "MySQLConnection.h"
#include "RedisConnection.h"
#include "Statistics.h"
#include "Network.h"

#include <map>
#include <event2/event.h>

#define STATS_SLIDING_WINDOW_SECONDS 3600

///////////////////////////////  WorkerStatus  /////////////////////////////////
// some miners use the same userName & workerName in different meachines, they
// will be the same StatsWorkerItem, the unique key is (userId_ + workId_)
class WorkerStatus {
public:
  // share, base on sliding window
  uint64_t accept5m_ = 0;

  uint64_t accept15m_ = 0;
  uint64_t stale15m_ = 0;
  uint64_t reject15m_ = 0;

  uint64_t accept1h_ = 0;
  uint64_t stale1h_ = 0;
  uint64_t reject1h_ = 0;

  uint32_t acceptCount_ = 0;

  IpAddress lastShareIP_ = 0;
  uint64_t lastShareTime_ = 0;

  string rejectDetail15m_;
  string rejectDetail1h_;
};

////////////////////////////////  WorkerShares  ////////////////////////////////
// thread safe
template <class SHARE>
class WorkerShares {
  mutex lock_;
  int64_t workerId_ = 0;
  int32_t userId_ = 0;

  uint32_t acceptCount_ = 0;

  IpAddress lastShareIP_;
  uint64_t lastShareTime_ = 0;

  class AcceptShareWindow : public StatsWindow<uint64_t> {
  public:
    AcceptShareWindow()
      : StatsWindow<uint64_t>(
            WorkerShares::acceptShareTime(STATS_SLIDING_WINDOW_SECONDS)) {}
  };

  class RejectShareWindow : public StatsWindow<uint64_t> {
  public:
    RejectShareWindow()
      : StatsWindow<uint64_t>(
            WorkerShares::rejectShareTime(STATS_SLIDING_WINDOW_SECONDS)) {}
  };

  AcceptShareWindow acceptShares_; // record accuracy: 10s
  RejectShareWindow staleShares_; // record accuracy: 60s
  std::map<uint32_t /* reason */, RejectShareWindow /* shares */>
      rejectShares_; // record accuracy: 60s

  // Adjust the following values to change the accuracy.
  // Please note that high accuracy will result in more memory usage.
  inline static time_t acceptShareTime(time_t seconds) { return seconds / 10; }
  inline static time_t rejectShareTime(time_t seconds) { return seconds / 60; }

public:
  WorkerShares(const int64_t workerId, const int32_t userId);
  virtual ~WorkerShares() = default;

  //  void serialize(...);
  //  bool unserialize(const ...);

  void processShare(SHARE &share, bool acceptStale);
  WorkerStatus getWorkerStatus();
  void getWorkerStatus(WorkerStatus &status);
  bool isExpired();

private:
  virtual void updateAcceptDiff(uint64_t diff){};
  virtual void updateRejectDiff(SHARE &share) const {};
};

template <class SHARE>
class WorkerSharesNormalized : public WorkerShares<SHARE> {
public:
  using WorkerShares<SHARE>::WorkerShares;

private:
  uint64_t lastAcceptDiff_ = 1;
  virtual void updateAcceptDiff(uint64_t diff) override;
  virtual void updateRejectDiff(SHARE &share) const override;
};

////////////////////////////////  StatsServer  ////////////////////////////////
// Interface, used as a pointer type.
class StatsServer {
public:
  virtual ~StatsServer(){};
  virtual bool init() = 0;
  virtual void stop() = 0;
  virtual void run() = 0;
};

////////////////////////////////  StatsServerT  ////////////////////////////////
//
// 1. consume topic 'ShareLog'
// 2. httpd: API for request alive worker status (realtime)
// 3. flush worker status to DB
//
template <class SHARE>
class StatsServerT : public StatsServer {
protected:
  struct ServerStatus {
    uint32_t uptime_ = 0;
    uint64_t requestCount_ = 0;
    uint64_t workerCount_ = 0;
    uint64_t userCount_ = 0;
    uint64_t responseBytes_ = 0;
    WorkerStatus poolStatus_;
  };

  enum RedisPublishPolicy {
    REDIS_PUBLISH_USER_UPDATE = 1,
    REDIS_PUBLISH_WORKER_UPDATE = 2
  };

  enum RedisIndexPolicy {
    REDIS_INDEX_NONE = 0,
    REDIS_INDEX_ACCEPT_1M = 1,
    REDIS_INDEX_ACCEPT_5M = 2,
    REDIS_INDEX_ACCEPT_15M = 4,
    REDIS_INDEX_REJECT_15M = 8,
    REDIS_INDEX_ACCEPT_1H = 16,
    REDIS_INDEX_REJECT_1H = 32,
    REDIS_INDEX_ACCEPT_COUNT = 64,
    REDIS_INDEX_LAST_SHARE_IP = 128,
    REDIS_INDEX_LAST_SHARE_TIME = 256,
    REDIS_INDEX_WORKER_NAME = 512,
    REDIS_INDEX_MINER_AGENT = 1024,
    REDIS_INDEX_STALE_15M = 2048,
    REDIS_INDEX_STALE_1H = 4096
  };

  struct WorkerIndexBuffer {
    size_t size_ = 0;

    std::vector<string> accept5m_;
    std::vector<string> accept15m_;
    std::vector<string> stale15m_;
    std::vector<string> reject15m_;
    std::vector<string> accept1h_;
    std::vector<string> stale1h_;
    std::vector<string> reject1h_;
    std::vector<string> acceptCount_;
    std::vector<string> lastShareIP_;
    std::vector<string> lastShareTime_;
  };

  atomic<bool> running_;
  atomic<int64_t> totalWorkerCount_;
  atomic<int64_t> totalUserCount_;
  time_t uptime_ = 0;

  pthread_rwlock_t rwlock_; // for workerSet_
  std::unordered_map<
      WorkerKey /* userId + workerId */,
      shared_ptr<WorkerShares<SHARE>>>
      workerSet_;
  std::unordered_map<int32_t /* userId*/, shared_ptr<WorkerShares<SHARE>>>
      userSet_;
  std::unordered_map<int32_t /* userId */, int32_t /* workerNum */>
      userWorkerCount_;
  WorkerShares<SHARE> poolWorker_; // worker status for the pool

  KafkaSimpleConsumer kafkaConsumer_; // consume topic: 'ShareLog'
  thread threadConsume_;

  KafkaSimpleConsumer
      kafkaConsumerCommonEvents_; // consume topic: 'CommonEvents'
  thread threadConsumeCommonEvents_;

  // you can disable it and use the standalone worker_update tool.
  bool updateWorkerName_ = true;
  // Used to initialize the offset of kafka consumers
  int expectedOnlineWorkers = 100000;

  MySQLConnection *poolDB_ = nullptr; // flush workers to table.mining_workers
  MySQLConnection *poolDBCommonEvents_ =
      nullptr; // insert or update workers from table.mining_workers

  RedisConnection *redisCommonEvents_ =
      nullptr; // writing workers' meta infomations
  std::vector<RedisConnection *> redisGroup_; // flush hashrate to this group
  uint32_t redisConcurrency_ = 1; // how many threads are writing to Redis at
                                  // the same time
  string redisKeyPrefix_;
  int redisKeyExpire_ = 0;
  uint32_t redisPublishPolicy_ = 0; // @see statshttpd.cfg
  uint32_t redisIndexPolicy_ = 0; // @see statshttpd.cfg

  time_t kFlushDBInterval_ = 20;
  atomic<bool> isInserting_; // flag mark if we are flushing db
  atomic<bool> isUpdateRedis_; // flag mark if we are flushing redis

  atomic<time_t>
      lastShareTime_; // the generating time of the last consumed share
  atomic<bool> isInitializing_; // if true, the database will not be flushed and
                                // the HTTP API will return an error

  atomic<time_t> lastFlushTime_; // the last db flush time
  string fileLastFlushTime_; // write last db flush time to the file

  shared_ptr<DuplicateShareChecker<SHARE>>
      dupShareChecker_; // Used to detect duplicate share attacks.

  bool acceptStale_ = false; // Whether stale shares are accepted

  // single user mode
  bool singleUserMode_ = false;
  int32_t singleUserId_ = 0;

  // httpd
  struct event_base *base_ = nullptr;
  string httpdHost_;
  unsigned short httpdPort_ = 8080;

public:
  atomic<uint64_t> requestCount_;
  atomic<uint64_t> responseBytes_;

protected:
  void runThreadConsume();
  void consumeShareLog(rd_kafka_message_t *rkmessage);

  void runThreadConsumeCommonEvents();
  void consumeCommonEvents(rd_kafka_message_t *rkmessage);
  bool updateWorkerStatusToDB(
      const int32_t userId,
      const int64_t workerId,
      const char *workerName,
      const char *minerAgent);
  bool updateWorkerStatusToRedis(
      const int32_t userId,
      const int64_t workerId,
      const char *workerName,
      const char *minerAgent);
  void updateWorkerStatusIndexToRedis(
      const int32_t userId,
      const string &key,
      const string &score,
      const string &value);

  void _processShare(WorkerKey &key, SHARE &share);
  void processShare(SHARE &share);
  virtual bool filterShare(const SHARE &share) { return true; }
  void getWorkerStatusBatch(
      const vector<WorkerKey> &keys, vector<WorkerStatus> &workerStatus);
  WorkerStatus mergeWorkerStatus(const vector<WorkerStatus> &workerStatus);

  void flushWorkersAndUsersToDB();
  void _flushWorkersAndUsersToDBThread();

  void flushWorkersAndUsersToRedis();
  void _flushWorkersAndUsersToRedisThread();
  void _flushWorkersAndUsersToRedisThread(uint32_t threadStep);
  bool checkRedis(uint32_t threadStep);
  // Tasks are evenly distributed to each thread.
  // For example, 6 items are assigned to two threads.
  // The first thread is responsible for the first 3,
  // and the second thread is responsible for the next 3.
  void flushWorkersToRedis(uint32_t threadStep);
  void flushUsersToRedis(uint32_t threadStep);
  void addIndexToBuffer(
      WorkerIndexBuffer &buffer,
      const int64_t workerId,
      const WorkerStatus &status);
  void flushIndexToRedis(
      RedisConnection *redis,
      std::unordered_map<int32_t /*userId*/, WorkerIndexBuffer>
          &indexBufferMap);
  void flushIndexToRedis(
      RedisConnection *redis, WorkerIndexBuffer &buffer, const int32_t userId);
  void flushIndexToRedis(
      RedisConnection *redis, const std::vector<string> &commandVector);

  void removeExpiredWorkers();
  bool setupThreadConsume();
  void runHttpd();

  string getRedisKeyMiningWorker(const int32_t userId, const int64_t workerId);
  string getRedisKeyMiningWorker(const int32_t userId);
  string getRedisKeyIndex(const int32_t userId, const string &indexName);

public:
  StatsServerT(
      const libconfig::Config &cfg,
      shared_ptr<DuplicateShareChecker<SHARE>> dupShareChecker);
  virtual ~StatsServerT();

  bool init();
  void stop();
  void run();

  ServerStatus getServerStatus();

  static void httpdServerStatus(struct evhttp_request *req, void *arg);
  static void httpdGetWorkerStatus(struct evhttp_request *req, void *arg);
  static void httpdGetFlushDBTime(struct evhttp_request *req, void *arg);

  void getWorkerStatus(
      struct evbuffer *evb,
      const char *pUserId,
      const char *pWorkerId,
      const char *pIsMerge);
};

#include "StatsHttpd.inl"

////////////////////////////  Alias  ////////////////////////////

#endif // STATSHTTPD_H_

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
#ifndef STATISTICS_H_
#define STATISTICS_H_

#include "Common.h"
#include "Kafka.h"
#include "MySQLConnection.h"
#include "Stratum.h"

#include <event2/event.h>
#include <event2/http.h>
#include <event2/buffer.h>
#include <event2/util.h>
#include <event2/keyvalq_struct.h>

#include <string.h>
#include <pthread.h>
#include <memory>

#define STATS_SLIDING_WINDOW_SECONDS 3600


////////////////////////////////// StatsWindow /////////////////////////////////
// none thread safe
template <typename T>
class StatsWindow {
  int64_t maxRingIdx_;  // max ring idx
  int32_t windowSize_;
  std::vector<T> elements_;

public:
  StatsWindow(const int windowSize);
  // TODO
//  bool unserialize(const ...);
//  void serialize(...);

  void clear();

  bool insert(const int64_t ringIdx, const T val);

  T sum(int64_t beginRingIdx, int len);
  T sum(int64_t beginRingIdx);

  void mapMultiply(const T val);
  void mapDivide  (const T val);
};

//----------------------

template <typename T>
StatsWindow<T>::StatsWindow(const int windowSize)
:maxRingIdx_(-1), windowSize_(windowSize), elements_(windowSize) {
}

template <typename T>
void StatsWindow<T>::mapMultiply(const T val) {
  for (size_t i = 0; i < windowSize_; i++) {
    elements_[i] *= val;
  }
}

template <typename T>
void StatsWindow<T>::mapDivide(const T val) {
  for (size_t i = 0; i < windowSize_; i++) {
    elements_[i] /= val;
  }
}

template <typename T>
void StatsWindow<T>::clear() {
  maxRingIdx_ = -1;
  elements_.clear();
  elements_.resize(windowSize_);
}

template <typename T>
bool StatsWindow<T>::insert(const int64_t curRingIdx, const T val) {
  if (maxRingIdx_ > curRingIdx + windowSize_) {  // too small index, drop it
    return false;
  }

  if (maxRingIdx_ == -1/* first insert */ ||
      curRingIdx - maxRingIdx_ > windowSize_/* all data expired */) {
    clear();
    maxRingIdx_ = curRingIdx;
  }

  while (maxRingIdx_ < curRingIdx) {
    maxRingIdx_++;
    elements_[maxRingIdx_ % windowSize_] = 0;  // reset
  }

  elements_[curRingIdx % windowSize_] += val;
  return true;
}

template <typename T>
T StatsWindow<T>::sum(int64_t beginRingIdx, int len) {
  T sum = 0;
  len = std::min(len, windowSize_);
  if (len <= 0 || beginRingIdx - len >= maxRingIdx_) {
    return 0;
  }
  int64_t endRingIdx = beginRingIdx - len;
  if (beginRingIdx > maxRingIdx_) {
    beginRingIdx = maxRingIdx_;
  }
  while (beginRingIdx > endRingIdx) {
    sum += elements_[beginRingIdx % windowSize_];
    beginRingIdx--;
  }
  return sum;
}

template <typename T>
T StatsWindow<T>::sum(int64_t beginRingIdx) {
  return sum(beginRingIdx, windowSize_);
}


///////////////////////////////  WorkerStatus  /////////////////////////////////
// some miners use the same userName & workerName in different meachines, they
// will be the same StatsWorkerItem, the unique key is (userId_ + workId_)
class WorkerStatus {
public:
  // share, base on sliding window
  uint64_t accept1m_;
  uint64_t accept5m_;

  uint64_t accept15m_;
  uint64_t reject15m_;

  uint64_t accept1h_;
  uint64_t reject1h_;

  uint32_t acceptCount_;

  uint32_t lastShareIP_;
  uint32_t lastShareTime_;

  WorkerStatus():
  accept1m_(0), accept5m_(0), accept15m_(0), reject15m_(0),
  accept1h_(0), reject1h_(0),
  acceptCount_(0), lastShareIP_(0), lastShareTime_(0)
  {
  }

  // all members are int, so we don't need to write copy constructor
};


////////////////////////////////  WorkerShares  ////////////////////////////////
// thread safe
class WorkerShares {
  mutex lock_;
  int64_t workerId_;
  int32_t userId_;

  uint32_t acceptCount_;

  uint32_t lastShareIP_;
  uint32_t lastShareTime_;

  StatsWindow<uint64_t> acceptShareSec_;
  StatsWindow<uint64_t> rejectShareMin_;

public:
  WorkerShares(const int64_t workerId, const int32_t userId);

//  void serialize(...);
//  bool unserialize(const ...);

  void processShare(const Share &share);
  WorkerStatus getWorkerStatus();
  void getWorkerStatus(WorkerStatus &status);
  bool isExpired();
};


//////////////////////////////////  WorkerKey  /////////////////////////////////
class WorkerKey {
public:
  int32_t userId_;
  int64_t workerId_;

  WorkerKey(const int32_t userId, const int64_t workerId):
  userId_(userId), workerId_(workerId) {}

  WorkerKey& operator=(const WorkerKey &r) {
    userId_   = r.userId_;
    workerId_ = r.workerId_;
    return *this;
  }

  bool operator==(const WorkerKey &r) const {
    if (userId_ == r.userId_ && workerId_ == r.workerId_) {
      return true;
    }
    return false;
  }
};

// we use WorkerKey in std::unordered_map, so need to write it's hash func
namespace std {
template<>
class hash<WorkerKey> {
public:
  size_t operator()(const WorkerKey &k) const
  {
    size_t h1 = std::hash<int32_t>()(k.userId_);
    size_t h2 = std::hash<int64_t>()(k.workerId_);
    return h1 ^ ( h2 << 1 );
  }
};
}


////////////////////////////////  StatsServer  ////////////////////////////////
//
// 1. consume topic 'ShareLog'
// 2. httpd: API for request alive worker status (realtime)
// 3. flush worker status to DB
//
class StatsServer {
  struct ServerStatus {
    uint32_t uptime_;
    uint64_t requestCount_;
    uint64_t workerCount_;
    uint64_t userCount_;
    uint64_t responseBytes_;
    WorkerStatus poolStatus_;
  };

  atomic<bool> running_;
  atomic<int64_t> totalWorkerCount_;
  atomic<int64_t> totalUserCount_;
  time_t uptime_;

  pthread_rwlock_t rwlock_;  // for workerSet_
  std::unordered_map<WorkerKey/* userId + workerId */, shared_ptr<WorkerShares> > workerSet_;
  std::unordered_map<int32_t /* userId */, int32_t> userWorkerCount_;
  WorkerShares poolWorker_;  // worker status for the pool

  KafkaConsumer kafkaConsumer_;  // consume topic: 'ShareLog'
  thread threadConsume_;

  KafkaConsumer kafkaConsumerCommonEvents_;  // consume topic: 'CommonEvents'
  thread threadConsumeCommonEvents_;

  MySQLConnection  poolDB_;             // flush workers to table.mining_workers
  MySQLConnection  poolDBCommonEvents_; // insert or update workers from table.mining_workers
  time_t kFlushDBInterval_;
  atomic<bool> isInserting_;     // flag mark if we are flushing db

  time_t lastShareTime_; // the generating time of the last share it consumes.
  
  string fileLastFlushTime_;     // write last db flush time to the file

  // httpd
  struct event_base *base_;
  string httpdHost_;
  unsigned short httpdPort_;

  void runThreadConsume();
  void consumeShareLog(rd_kafka_message_t *rkmessage);

  void runThreadConsumeCommonEvents();
  void consumeCommonEvents(rd_kafka_message_t *rkmessage);
  bool updateWorkerStatus(const int32_t userId, const int64_t workerId,
                          const char *workerName, const char *minerAgent);

  void _processShare(WorkerKey &key1, WorkerKey &key2, const Share &share);
  void processShare(const Share &share);
  void getWorkerStatusBatch(const vector<WorkerKey> &keys,
                            vector<WorkerStatus> &workerStatus);
  WorkerStatus mergeWorkerStatus(const vector<WorkerStatus> &workerStatus);

  void _flushWorkersToDBThread();
  void flushWorkersToDB();
  void removeExpiredWorkers();
  bool setupThreadConsume();
  void runHttpd();

public:
  atomic<uint64_t> requestCount_;
  atomic<uint64_t> responseBytes_;

public:
  StatsServer(const char *kafkaBrokers, const string &httpdHost,
              unsigned short httpdPort, const MysqlConnectInfo &poolDBInfo,
              const time_t kFlushDBInterval, const string &fileLastFlushTime);
  ~StatsServer();

  bool init();
  void stop();
  void run();


  ServerStatus getServerStatus();

  static void httpdServerStatus   (struct evhttp_request *req, void *arg);
  static void httpdGetWorkerStatus(struct evhttp_request *req, void *arg);

  void getWorkerStatus(struct evbuffer *evb, const char *pUserId,
                       const char *pWorkerId, const char *pIsMerge);
};



//////////////////////////////  ShareLogWriter  ///////////////////////////////
//
// 1. consume topic 'ShareLog'
// 2. write sharelog to Disk
//
class ShareLogWriter {
  atomic<bool> running_;
  string dataDir_;  // where to put sharelog data files

  // key:   timestamp - (timestamp % 86400)
  // value: FILE *
  std::map<uint32_t, FILE *> fileHandlers_;
  std::vector<Share> shares_;

  KafkaHighLevelConsumer hlConsumer_;  // consume topic: 'ShareLog'

  FILE* getFileHandler(uint32_t ts);
  void consumeShareLog(rd_kafka_message_t *rkmessage);
  bool flushToDisk();
  void tryCloseOldHanders();

public:
  ShareLogWriter(const char *kafkaBrokers, const string &dataDir,
                 const string &kafkaGroupID);
  ~ShareLogWriter();

  void stop();
  void run();
};



/////////////////////////////////  ShareStats  /////////////////////////////////
class ShareStats {
public:
  uint64_t shareAccept_;
  uint64_t shareReject_;
  double   rejectRate_;
  int64_t  earn_;

  ShareStats(): shareAccept_(0U), shareReject_(0U), rejectRate_(0.0), earn_(0) {}
};



///////////////////////////////  ShareStatsDay  ////////////////////////////////
// thread-safe
class ShareStatsDay {
public:
  // hours
  uint64_t shareAccept1h_[24];
  uint64_t shareReject1h_[24];
  double   score1h_[24];  // only accept share

  // daily
  uint64_t shareAccept1d_;
  uint64_t shareReject1d_;
  double   score1d_;

  // mark which hour data has been modified: 23, 22, ...., 0
  uint32_t modifyHoursFlag_;
  mutex lock_;

  ShareStatsDay();

  void processShare(uint32_t hourIdx, const Share &share);
  void getShareStatsHour(uint32_t hourIdx, ShareStats *stats);
  void getShareStatsDay(ShareStats *stats);
};


///////////////////////////////  ShareLogDumper  ///////////////////////////////
class ShareLogDumper {
  string filePath_;  // sharelog data file path
  std::set<int32_t> uids_;  // if empty dump all user's shares
  bool isDumpAll_;

  void parseShareLog(const uint8_t *buf, size_t len);
  void parseShare(const Share *share);

public:
  ShareLogDumper(const string &dataDir, time_t timestamp, const std::set<int32_t> &uids);
  ~ShareLogDumper();

  void dump2stdout();
};

///////////////////////////////  ShareLogParser  ///////////////////////////////
//
// 1. read sharelog data files
// 2. calculate share & score
// 3. write stats data to DB
//
class ShareLogParser {
  pthread_rwlock_t rwlock_;
  // key: WorkerKey, value: share stats
  std::unordered_map<WorkerKey/* userID + workerID */, shared_ptr<ShareStatsDay>> workersStats_;

  time_t date_;      // date_ % 86400 == 0
  string filePath_;  // sharelog data file path

  //
  // for processGrowingShareLog()
  //
  FILE *f_;        // file handler
  uint8_t *buf_;   // fread buffer
  // 48 * 1000000 = 48,000,000 ~ 48 MB
  static const size_t kMaxElementsNum_ = 1000000;  // num of Share
  off_t lastPosition_;

  MySQLConnection  poolDB_;  // save stats data

  inline int32_t getHourIdx(uint32_t ts) {
    // %H	Hour in 24h format (00-23)
    return atoi(date("%H", ts).c_str());
  }

  void parseShareLog(const uint8_t *buf, size_t len);
  void parseShare(const Share *share);

  void generateDailyData(shared_ptr<ShareStatsDay> stats,
                         const int32_t userId, const int64_t workerId,
                         vector<string> *valuesWorkersDay,
                         vector<string> *valuesUsersDay,
                         vector<string> *valuesPoolDay);
  void generateHoursData(shared_ptr<ShareStatsDay> stats,
                         const int32_t userId, const int64_t workerId,
                         vector<string> *valuesWorkersHour,
                         vector<string> *valuesUsersHour,
                         vector<string> *valuesPoolHour);
  void flushHourOrDailyData(const vector<string> values,
                            const string &tableName,
                            const string &extraFields);
  void removeExpiredDataFromDB();

public:
  ShareLogParser(const string &dataDir, time_t timestamp,
                 const MysqlConnectInfo &poolDBInfo);
  ~ShareLogParser();

  bool init();

  // flush data to DB
  bool flushToDB();

  // get share stats day handler
  shared_ptr<ShareStatsDay> getShareStatsDayHandler(const WorkerKey &key);

  // read unchanged share data bin file, for example yestoday's file. it will
  // use mmap() to get high performance. call only once will process
  // the whole bin file
  bool processUnchangedShareLog();

  // today's file is still growing, return processed shares number.
  int64_t processGrowingShareLog();
  bool isReachEOF();  // only for growing file
};



////////////////////////////  ShareLogParserServer  ////////////////////////////
//
// read share binlog, parse shares, calc stats data than save them to database
// table.stats_xxxx. meanwhile hold there stats data in memory so it could
// provide httpd service. web/app could read the latest data from it's http API.
//
class ShareLogParserServer {
  struct ServerStatus {
    uint32_t uptime_;
    uint64_t requestCount_;
    uint64_t responseBytes_;
    uint32_t date_;  // Y-m-d
    vector<ShareStats> stats;  // first is today and latest 3 hours
  };

  //-----------------
  atomic<bool> running_;
  pthread_rwlock_t rwlock_;
  time_t uptime_;

  // share log daily
  time_t date_;      // date_ % 86400 == 0
  shared_ptr<ShareLogParser> shareLogParser_;
  string dataDir_;
  MysqlConnectInfo poolDBInfo_;  // save stats data
  time_t kFlushDBInterval_;

  // httpd
  struct event_base *base_;
  string httpdHost_;
  unsigned short httpdPort_;

  thread threadShareLogParser_;

  void getServerStatus(ServerStatus &s);
  void getShareStats(struct evbuffer *evb, const char *pUserId,
                     const char *pWorkerId, const char *pHour);
  void _getShareStats(const vector<WorkerKey> &keys, const vector<int32_t> &hours,
                      vector<ShareStats> &shareStats);

  void runThreadShareLogParser();
  bool initShareLogParser(time_t datets);
  bool setupThreadShareLogParser();
  void trySwithBinFile(shared_ptr<ShareLogParser> shareLogParser);
  void runHttpd();

public:
  atomic<uint64_t> requestCount_;
  atomic<uint64_t> responseBytes_;

public:
  ShareLogParserServer(const string dataDir, const string &httpdHost,
                       unsigned short httpdPort,
                       const MysqlConnectInfo &poolDBInfo,
                       const uint32_t kFlushDBInterval);
  ~ShareLogParserServer();

  void stop();
  void run();

  static void httpdServerStatus(struct evhttp_request *req, void *arg);
  static void httpdShareStats  (struct evhttp_request *req, void *arg);
};

#endif

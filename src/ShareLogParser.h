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
#ifndef SHARELOGPARSER_H_
#define SHARELOGPARSER_H_

#include "MySQLConnection.h"
#include "Statistics.h"
#include "zlibstream/zstr.hpp"

#include <event2/event.h>
#include <event2/http.h>
#include <event2/buffer.h>
#include <event2/keyvalq_struct.h>

///////////////////////////////  ShareLogDumper  ///////////////////////////////
// Interface, used as a pointer type.
class ShareLogDumper {
public:
  virtual ~ShareLogDumper(){};
  virtual void dump2stdout() = 0;
};

///////////////////////////////  ShareLogDumperT ///////////////////////////////
// print share.toString() to stdout
template <class SHARE>
class ShareLogDumperT : public ShareLogDumper {
  string filePath_; // sharelog data file path
  std::set<int32_t> uids_; // if empty dump all user's shares
  bool isDumpAll_;

  // single user mode
  bool singleUserMode_ = false;
  int32_t singleUserId_ = 0;

  void parseShareLog(const uint8_t *buf, size_t len);
  void parseShare(const SHARE *share);

public:
  ShareLogDumperT(
      const libconfig::Config &cfg,
      time_t timestamp,
      const std::set<int32_t> &uids);
  ~ShareLogDumperT();

  void dump2stdout();
};

///////////////////////////////  ShareLogParser  ///////////////////////////////
// Interface, used as a pointer type.
class ShareLogParser {
public:
  virtual ~ShareLogParser() {}
  virtual bool init() = 0;
  virtual bool flushToDB(bool removeExpiredData = true) = 0;
  virtual bool processUnchangedShareLog() = 0;
};
///////////////////////////////  ShareLogParserT ///////////////////////////////
//
// 1. read sharelog data files
// 2. calculate share & score
// 3. write stats data to DB
//
template <class SHARE>
class ShareLogParserT : public ShareLogParser {
  pthread_rwlock_t rwlock_;
  // key: WorkerKey, value: share stats
  std::unordered_map<
      WorkerKey /* userID + workerID */,
      shared_ptr<ShareStatsDay<SHARE>>>
      workersStats_;

  time_t date_; // date_ % 86400 == 0
  string filePath_; // sharelog data file path
  const string chainType_;
  string rpcUrl_;

  //
  // for processGrowingShareLog()
  //
  zstr::ifstream *f_; // file handler
  uint8_t *buf_; // fread buffer
  // 48 * 1000000 = 48,000,000 ~ 48 MB
  int32_t kMaxElementsNum_; // num of shares
  size_t incompleteShareSize_;
  uint32_t bufferlength_;

  static constexpr uint32_t MAX_READING_ERRORS = 5;
  // The number of read errors. If MAX_READING_ERRORS is exceeded,
  // the program will exit
  uint32_t readingErrors_ = 0;

  MySQLConnection poolDB_; // save stats data

  shared_ptr<DuplicateShareChecker<SHARE>>
      dupShareChecker_; // Used to detect duplicate share attacks.

  bool acceptStale_; // Whether stale shares are accepted

  // single user mode
  bool singleUserMode_ = false;
  int32_t singleUserId_ = 0;

  inline int32_t getHourIdx(uint32_t ts) {
    // %H	Hour in 24h format (00-23)
    return atoi(date("%H", ts).c_str());
  }

  inline void handleReadingError() {
    if (++readingErrors_ > MAX_READING_ERRORS) {
      LOG(FATAL) << "Too many reading errors, the program will exit!";
    }
  }

  inline void resetReadingError() {
    if (readingErrors_) {
      readingErrors_ = 0;
    }
  }

  void parseShareLog(const uint8_t *buf, size_t len);
  void parseShare(SHARE &share);
  virtual bool filterShare(const SHARE &share) { return true; }

  void generateDailyData(
      shared_ptr<ShareStatsDay<SHARE>> stats,
      const int32_t userId,
      const int64_t workerId,
      vector<string> *valuesWorkersDay,
      vector<string> *valuesUsersDay,
      vector<string> *valuesPoolDay);
  void generateHoursData(
      shared_ptr<ShareStatsDay<SHARE>> stats,
      const int32_t userId,
      const int64_t workerId,
      vector<string> *valuesWorkersHour,
      vector<string> *valuesUsersHour,
      vector<string> *valuesPoolHour);
  void flushHourOrDailyData(
      const vector<string> values,
      const string &tableName,
      const string &extraFields);

  void removeExpiredDataFromDB();

public:
  ShareLogParserT(
      const libconfig::Config &cfg,
      time_t timestamp,
      shared_ptr<DuplicateShareChecker<SHARE>> dupShareChecker);
  virtual ~ShareLogParserT();

  bool init() override;

  // flush data to DB
  bool flushToDB(bool removeExpiredData = true) override;

  // get share stats day handler
  shared_ptr<ShareStatsDay<SHARE>>
  getShareStatsDayHandler(const WorkerKey &key);

  // read unchanged share data bin file, for example yestoday's file. it will
  // use mmap() to get high performance. call only once will process
  // the whole bin file
  bool processUnchangedShareLog() override;

  // today's file is still growing, return processed shares number.
  int64_t processGrowingShareLog();
  bool isReachEOF(); // only for growing file
};

////////////////////////////  ShareLogParserServer  ////////////////////////////
// Interface, used as a pointer type.
class ShareLogParserServer {
public:
  virtual ~ShareLogParserServer(){};
  virtual void stop() = 0;
  virtual void run() = 0;
};

////////////////////////////  ShareLogParserServerT ////////////////////////////
//
// read share binlog, parse shares, calc stats data than save them to database
// table.stats_xxxx. meanwhile hold there stats data in memory so it could
// provide httpd service. web/app could read the latest data from it's http API.
//
template <class SHARE>
class ShareLogParserServerT : public ShareLogParserServer {
protected:
  struct ServerStatus {
    uint32_t uptime_;
    uint64_t requestCount_;
    uint64_t responseBytes_;
    uint32_t date_; // Y-m-d
    vector<ShareStats> stats; // first is today and latest 3 hours
  };

  //-----------------
  const libconfig::Config &cfg_;

  atomic<bool> running_;
  pthread_rwlock_t rwlock_;
  time_t uptime_;
  // share log daily
  time_t date_; // date_ % 86400 == 0
  shared_ptr<ShareLogParserT<SHARE>> shareLogParser_;
  const string chainType_;
  string dataDir_;
  time_t kFlushDBInterval_;
  shared_ptr<DuplicateShareChecker<SHARE>>
      dupShareChecker_; // Used to detect duplicate share attacks.

  // httpd
  struct event_base *base_;
  string httpdHost_;
  unsigned short httpdPort_;

  thread threadShareLogParser_;

  void getServerStatus(ServerStatus &s);
  void getShareStats(
      struct evbuffer *evb,
      const char *pUserId,
      const char *pWorkerId,
      const char *pHour);
  void _getShareStats(
      const vector<WorkerKey> &keys,
      const vector<int32_t> &hours,
      vector<ShareStats> &shareStats);

  void runThreadShareLogParser();
  bool initShareLogParser(time_t datets);
  virtual shared_ptr<ShareLogParserT<SHARE>>
  createShareLogParser(time_t datets);
  bool setupThreadShareLogParser();
  void trySwitchBinFile(shared_ptr<ShareLogParserT<SHARE>> shareLogParser);
  void runHttpd();

public:
  atomic<uint64_t> requestCount_;
  atomic<uint64_t> responseBytes_;

public:
  ShareLogParserServerT(
      const libconfig::Config &cfg,
      shared_ptr<DuplicateShareChecker<SHARE>> dupShareChecker);
  virtual ~ShareLogParserServerT();

  void stop();
  void run();

  static void httpdServerStatus(struct evhttp_request *req, void *arg);
  static void httpdShareStats(struct evhttp_request *req, void *arg);
};

#include "ShareLogParser.inl"

///////////////////////////////  Alias  ///////////////////////////////

#endif // SHARELOGPARSER_H_

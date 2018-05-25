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

#include "Common.h"
#include "Kafka.h"
#include "MySQLConnection.h"
#include "Stratum.h"
#include "Statistics.h"

#include <event2/event.h>
#include <event2/http.h>
#include <event2/buffer.h>
#include <event2/util.h>
#include <event2/keyvalq_struct.h>

#include <string.h>
#include <pthread.h>
#include <memory>


///////////////////////////////  ShareLogDumper  ///////////////////////////////
class ShareLogDumper {
  string filePath_;  // sharelog data file path
  std::set<int32_t> uids_;  // if empty dump all user's shares
  bool isDumpAll_;

  void parseShareLog(const uint8_t *buf, size_t len);
  void parseShare(const Share *share);

public:
  ShareLogDumper(const char *chainType, const string &dataDir, time_t timestamp, const std::set<int32_t> &uids);
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
  ShareLogParser(const char *chainType, const string &dataDir,
                 time_t timestamp, const MysqlConnectInfo &poolDBInfo);
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
  const string chainType_;
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
  ShareLogParserServer(const char *chainType, const string dataDir,
                       const string &httpdHost, unsigned short httpdPort,
                       const MysqlConnectInfo &poolDBInfo,
                       const uint32_t kFlushDBInterval);
  ~ShareLogParserServer();

  void stop();
  void run();

  static void httpdServerStatus(struct evhttp_request *req, void *arg);
  static void httpdShareStats  (struct evhttp_request *req, void *arg);
};

#endif // SHARELOGPARSER_H_

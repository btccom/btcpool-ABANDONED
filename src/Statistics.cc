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
#include "Statistics.h"

#include "Common.h"
#include "Stratum.h"
#include "Utils.h"
#include "utilities_js.hpp"

#include <algorithm>
#include <string>

#include <boost/algorithm/string.hpp>
#include <boost/thread.hpp>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static
string getStatsFilePath(const string &dataDir, time_t ts) {
  bool needSlash = false;
  if (dataDir.length() > 0 && *dataDir.rbegin() != '/') {
    needSlash = true;
  }
  // filename: sharelog-2016-07-12.bin
  return Strings::Format("%s%ssharelog-%s.bin",
                         dataDir.c_str(), needSlash ? "/" : "",
                         date("%F", ts).c_str());
}

////////////////////////////////  WorkerShares  ////////////////////////////////
WorkerShares::WorkerShares(const int64_t workerId, const int32_t userId):
workerId_(workerId), userId_(userId), acceptCount_(0),
lastShareIP_(0), lastShareTime_(0),
acceptShareSec_(STATS_SLIDING_WINDOW_SECONDS),
rejectShareMin_(STATS_SLIDING_WINDOW_SECONDS/60)
{
  assert(STATS_SLIDING_WINDOW_SECONDS >= 3600);
}

void WorkerShares::processShare(const Share &share) {
  ScopeLock sl(lock_);
  const time_t now = time(nullptr);
  if (now > share.timestamp_ + STATS_SLIDING_WINDOW_SECONDS) {
    return;
  }

  if (share.result_ == Share::Result::ACCEPT) {
    acceptCount_++;
    acceptShareSec_.insert(share.timestamp_,    share.share_);
  } else {
    rejectShareMin_.insert(share.timestamp_/60, share.share_);
  }

  lastShareIP_   = share.ip_;
  lastShareTime_ = share.timestamp_;
}

WorkerStatus WorkerShares::getWorkerStatus() {
  ScopeLock sl(lock_);
  const time_t now = time(nullptr);
  WorkerStatus s;

  s.accept1m_  = acceptShareSec_.sum(now, 60);
  s.accept5m_  = acceptShareSec_.sum(now, 300);
  s.accept15m_ = acceptShareSec_.sum(now, 900);
  s.reject15m_ = rejectShareMin_.sum(now/60, 15);

  s.accept1h_ = acceptShareSec_.sum(now, 3600);
  s.reject1h_ = rejectShareMin_.sum(now/60, 60);

  s.acceptCount_   = acceptCount_;
  s.lastShareIP_   = lastShareIP_;
  s.lastShareTime_ = lastShareTime_;

  return s;
}

void WorkerShares::getWorkerStatus(WorkerStatus &s) {
  ScopeLock sl(lock_);
  const time_t now = time(nullptr);

  s.accept1m_  = acceptShareSec_.sum(now, 60);
  s.accept5m_  = acceptShareSec_.sum(now, 300);
  s.accept15m_ = acceptShareSec_.sum(now, 900);
  s.reject15m_ = rejectShareMin_.sum(now/60, 15);

  s.accept1h_ = acceptShareSec_.sum(now, 3600);
  s.reject1h_ = rejectShareMin_.sum(now/60, 60);

  s.acceptCount_   = acceptCount_;
  s.lastShareIP_   = lastShareIP_;
  s.lastShareTime_ = lastShareTime_;
}

bool WorkerShares::isExpired() {
  ScopeLock sl(lock_);
  return (lastShareTime_ + STATS_SLIDING_WINDOW_SECONDS) < (uint32_t)time(nullptr);
}


////////////////////////////////  StatsServer  ////////////////////////////////
StatsServer::StatsServer(const char *kafkaBrokers, const string &httpdHost,
                         unsigned short httpdPort, const MysqlConnectInfo &poolDBInfo,
                         const time_t kFlushDBInterval, const string &fileLastFlushTime):
running_(true), totalWorkerCount_(0), totalUserCount_(0), uptime_(time(nullptr)),
poolWorker_(0u/* worker id */, 0/* user id */),
kafkaConsumer_(kafkaBrokers, KAFKA_TOPIC_SHARE_LOG, 0/* patition */),
kafkaConsumerCommonEvents_(kafkaBrokers, KAFKA_TOPIC_COMMON_EVENTS, 0/* patition */),
poolDB_(poolDBInfo), poolDBCommonEvents_(poolDBInfo),
kFlushDBInterval_(kFlushDBInterval), isInserting_(false),
fileLastFlushTime_(fileLastFlushTime),
base_(nullptr), httpdHost_(httpdHost), httpdPort_(httpdPort),
requestCount_(0), responseBytes_(0)
{
  pthread_rwlock_init(&rwlock_, nullptr);
}

StatsServer::~StatsServer() {
  stop();

  if (threadConsume_.joinable())
    threadConsume_.join();
 
  if (threadConsumeCommonEvents_.joinable())
    threadConsumeCommonEvents_.join();

  pthread_rwlock_destroy(&rwlock_);
}

bool StatsServer::init() {
  if (!poolDB_.ping()) {
    LOG(INFO) << "db ping failure";
    return false;
  }

  if (!poolDBCommonEvents_.ping()) {
    LOG(INFO) << "common events db ping failure";
    return false;
  }

  // check db conf (only poolDB_ needs)
  {
  	string value = poolDB_.getVariable("max_allowed_packet");
    if (atoi(value.c_str()) < 16 * 1024 *1024) {
      LOG(INFO) << "db conf 'max_allowed_packet' is less than 16*1024*1024";
      return false;
    }
  }

  return true;
}

void StatsServer::stop() {
  if (!running_)
    return;

  LOG(INFO) << "stop StatsServer...";

  running_ = false;
  event_base_loopexit(base_, NULL);
}

void StatsServer::processShare(const Share &share) {
  const time_t now = time(nullptr);

  // ignore too old shares
  if (now > share.timestamp_ + STATS_SLIDING_WINDOW_SECONDS) {
    return;
  }
  poolWorker_.processShare(share);

  WorkerKey key1(share.userId_, share.workerHashId_);
  WorkerKey key2(share.userId_, 0/* 0 means all workers of this user */);
  _processShare(key1, key2, share);
}

void StatsServer::_processShare(WorkerKey &key1, WorkerKey &key2, const Share &share) {
  assert(key2.workerId_ == 0);  // key2 is user's total stats

  pthread_rwlock_rdlock(&rwlock_);
  auto itr1 = workerSet_.find(key1);
  auto itr2 = workerSet_.find(key2);
  pthread_rwlock_unlock(&rwlock_);

  shared_ptr<WorkerShares> workerShare1 = nullptr, workerShare2 = nullptr;

  if (itr1 != workerSet_.end()) {
    itr1->second->processShare(share);
  } else {
    workerShare1 = make_shared<WorkerShares>(share.workerHashId_, share.userId_);
    workerShare1->processShare(share);
  }

  if (itr2 != workerSet_.end()) {
    itr2->second->processShare(share);
  } else {
    workerShare2 = make_shared<WorkerShares>(share.workerHashId_, share.userId_);
    workerShare2->processShare(share);
  }

  if (workerShare1 != nullptr || workerShare2 != nullptr) {
    pthread_rwlock_wrlock(&rwlock_);    // write lock
    if (workerShare1 != nullptr) {
      workerSet_[key1] = workerShare1;
      totalWorkerCount_++;
      userWorkerCount_[key1.userId_]++;
    }
    if (workerShare2 != nullptr) {
      workerSet_[key2] = workerShare2;
      totalUserCount_++;
    }
    pthread_rwlock_unlock(&rwlock_);
  }
}

void StatsServer::flushWorkersToDB() {
  LOG(INFO) << "flush mining workers to DB...";
  if (isInserting_) {
    LOG(WARNING) << "last flush is not finish yet, ingore";
    return;
  }

  isInserting_ = true;
  boost::thread t(boost::bind(&StatsServer::_flushWorkersToDBThread, this));
}

void StatsServer::_flushWorkersToDBThread() {
  //
  // merge two table items
  // table.`mining_workers` unique index: `puid` + `worker_id`
  //
  const string mergeSQL = "INSERT INTO `mining_workers` "
  " SELECT * FROM `mining_workers_tmp` "
  " ON DUPLICATE KEY "
  " UPDATE "
  "  `mining_workers`.`accept_1m`      =`mining_workers_tmp`.`accept_1m`, "
  "  `mining_workers`.`accept_5m`      =`mining_workers_tmp`.`accept_5m`, "
  "  `mining_workers`.`accept_15m`     =`mining_workers_tmp`.`accept_15m`, "
  "  `mining_workers`.`reject_15m`     =`mining_workers_tmp`.`reject_15m`, "
  "  `mining_workers`.`accept_1h`      =`mining_workers_tmp`.`accept_1h`, "
  "  `mining_workers`.`reject_1h`      =`mining_workers_tmp`.`reject_1h`, "
  "  `mining_workers`.`accept_count`   =`mining_workers_tmp`.`accept_count`,"
  "  `mining_workers`.`last_share_ip`  =`mining_workers_tmp`.`last_share_ip`,"
  "  `mining_workers`.`last_share_time`=`mining_workers_tmp`.`last_share_time`,"
  "  `mining_workers`.`updated_at`     =`mining_workers_tmp`.`updated_at` ";
  // fields for table.mining_workers
  const string fields = "`worker_id`,`puid`,`group_id`,`accept_1m`, `accept_5m`,"
  "`accept_15m`, `reject_15m`, `accept_1h`,`reject_1h`, `accept_count`, `last_share_ip`,"
  " `last_share_time`, `created_at`, `updated_at`";
  // values for multi-insert sql
  vector<string> values;
  size_t counter = 0;

  if (!poolDB_.ping()) {
    LOG(ERROR) << "can't connect to pool DB";
    goto finish;
  }

  // get all workes status
  pthread_rwlock_rdlock(&rwlock_);  // read lock
  for (auto itr = workerSet_.begin(); itr != workerSet_.end(); itr++) {
    counter++;

    const int32_t userId   = itr->first.userId_;
    const int64_t workerId = itr->first.workerId_;
    shared_ptr<WorkerShares> workerShare = itr->second;
    const WorkerStatus status = workerShare->getWorkerStatus();

    char ipStr[INET_ADDRSTRLEN] = {0};
    inet_ntop(AF_INET, &(status.lastShareIP_), ipStr, INET_ADDRSTRLEN);
    const string nowStr = date("%F %T", time(nullptr));

    values.push_back(Strings::Format("%" PRId64",%d,%d,%" PRIu64",%" PRIu64","
                                     "%" PRIu64",%" PRIu64","  // accept_15m, reject_15m
                                     "%" PRIu64",%" PRIu64","  // accept_1h,  reject_1h
                                     "%d,\"%s\","
                                     "\"%s\",\"%s\",\"%s\"",
                                     workerId, userId,
                                     -1 * userId,  /* default group id */
                                     status.accept1m_, status.accept5m_,
                                     status.accept15m_, status.reject15m_,
                                     status.accept1h_, status.reject1h_,
                                     status.acceptCount_, ipStr,
                                     date("%F %T", status.lastShareTime_).c_str(),
                                     nowStr.c_str(), nowStr.c_str()));
  }
  pthread_rwlock_unlock(&rwlock_);

  if (values.size() == 0) {
    LOG(INFO) << "no active workers";
    goto finish;
  }

  if (!poolDB_.execute("DROP TABLE IF EXISTS `mining_workers_tmp`;")) {
    LOG(ERROR) << "DROP TABLE `mining_workers_tmp` failure";
    goto finish;
  }
  if (!poolDB_.execute("CREATE TABLE `mining_workers_tmp` like `mining_workers`;")) {
    LOG(ERROR) << "TRUNCATE TABLE `mining_workers_tmp` failure";
    goto finish;
  }

  //check conditions before multi-insert, if there is no need, do info LOG instead of ERROR.
  // It's a common case when the pool is run in the testnet with computer miner instead of ASCII miners.
  if(fields.size() == 0 || values.size() == 0 || tmpTableName.length() == 0){
  	LOG(INFO) << "multi-insert table." << tmpTableName << " failure because of empty info";
  	goto finish;
  }

  if (!multiInsert(poolDB_, "mining_workers_tmp", fields, values)) {
    LOG(ERROR) << "mul-insert table.mining_workers_tmp failure";
    goto finish;
  }

  // merge items
  if (!poolDB_.update(mergeSQL)) {
    LOG(ERROR) << "merge mining_workers failure";
    goto finish;
  }
  LOG(INFO) << "flush mining workers to DB... done, items: " << counter;
  
  // save flush timestamp to file, for monitor system
  if (!fileLastFlushTime_.empty())
  	writeTime2File(fileLastFlushTime_.c_str(), (uint32_t)time(nullptr));

finish:
  isInserting_ = false;
}

void StatsServer::removeExpiredWorkers() {
  size_t expiredCnt = 0;

  pthread_rwlock_wrlock(&rwlock_);  // write lock

  // delete all expired workers
  for (auto itr = workerSet_.begin(); itr != workerSet_.end(); ) {
    const int32_t userId   = itr->first.userId_;
    const int64_t workerId = itr->first.workerId_;
    shared_ptr<WorkerShares> workerShare = itr->second;

    if (workerShare->isExpired()) {
      if (workerId == 0) {
        totalUserCount_--;
      } else {
        totalWorkerCount_--;
        userWorkerCount_[userId]--;
      }
      expiredCnt++;

      itr = workerSet_.erase(itr);
    } else {
      itr++;
    }
  }

  pthread_rwlock_unlock(&rwlock_);

  LOG(INFO) << "removed expired workers: " << expiredCnt;
}

void StatsServer::getWorkerStatusBatch(const vector<WorkerKey> &keys,
                                       vector<WorkerStatus> &workerStatus) {
  workerStatus.resize(keys.size());

  vector<shared_ptr<WorkerShares> > ptrs;
  ptrs.resize(keys.size());

  // find all shared pointer
  pthread_rwlock_rdlock(&rwlock_);
  for (size_t i = 0; i < keys.size(); i++) {
    auto itr = workerSet_.find(keys[i]);
    if (itr == workerSet_.end()) {
      ptrs[i] = nullptr;
    } else {
      ptrs[i] = itr->second;
    }
  }
  pthread_rwlock_unlock(&rwlock_);

  // foreach get worker status
  for (size_t i = 0; i < ptrs.size(); i++) {
    if (ptrs[i] != nullptr)
      ptrs[i]->getWorkerStatus(workerStatus[i]);
  }
}

WorkerStatus StatsServer::mergeWorkerStatus(const vector<WorkerStatus> &workerStatus) {
  WorkerStatus s;

  if (workerStatus.size() == 0)
    return s;

  for (size_t i = 0; i < workerStatus.size(); i++) {
    s.accept1m_    += workerStatus[i].accept1m_;
    s.accept5m_    += workerStatus[i].accept5m_;
    s.accept15m_   += workerStatus[i].accept15m_;
    s.reject15m_   += workerStatus[i].reject15m_;
    s.accept1h_    += workerStatus[i].accept1h_;
    s.reject1h_    += workerStatus[i].reject1h_;
    s.acceptCount_ += workerStatus[i].acceptCount_;

    if (workerStatus[i].lastShareTime_ > s.lastShareTime_) {
      s.lastShareTime_ = workerStatus[i].lastShareTime_;
      s.lastShareIP_   = workerStatus[i].lastShareIP_;
    }
  }
  return s;
}

void StatsServer::consumeShareLog(rd_kafka_message_t *rkmessage) {
  // check error
  if (rkmessage->err) {
    if (rkmessage->err == RD_KAFKA_RESP_ERR__PARTITION_EOF) {
      // Reached the end of the topic+partition queue on the broker.
      // Not really an error.
      //      LOG(INFO) << "consumer reached end of " << rd_kafka_topic_name(rkmessage->rkt)
      //      << "[" << rkmessage->partition << "] "
      //      << " message queue at offset " << rkmessage->offset;
      // acturlly
      return;
    }

    LOG(ERROR) << "consume error for topic " << rd_kafka_topic_name(rkmessage->rkt)
    << "[" << rkmessage->partition << "] offset " << rkmessage->offset
    << ": " << rd_kafka_message_errstr(rkmessage);

    if (rkmessage->err == RD_KAFKA_RESP_ERR__UNKNOWN_PARTITION ||
        rkmessage->err == RD_KAFKA_RESP_ERR__UNKNOWN_TOPIC) {
      LOG(FATAL) << "consume fatal";
    }
    return;
  }

  Share share;
  if (rkmessage->len != sizeof(Share)) {
    LOG(ERROR) << "sharelog message size(" << rkmessage->len << ") is not: " << sizeof(Share);
    return;
  }
  memcpy((uint8_t *)&share, (const uint8_t *)rkmessage->payload, rkmessage->len);

  if (!share.isValid()) {
    LOG(ERROR) << "invalid share: " << share.toString();
    return;
  }

  processShare(share);
}

bool StatsServer::setupThreadConsume() {
  // kafkaConsumer_
  {
    //
    // assume we have 100,000 online workers and every share per 10 seconds,
    // so in 60 mins there will be 100000/10*3600 = 36,000,000 shares.
    // data size will be 36,000,000 * sizeof(Share) = 1,728,000,000 Bytes.
    //
    const int32_t kConsumeLatestN = 100000/10*3600;  // 36,000,000

    map<string, string> consumerOptions;
    // fetch.wait.max.ms:
    // Maximum time the broker may wait to fill the response with fetch.min.bytes.
    consumerOptions["fetch.wait.max.ms"] = "200";

    if (kafkaConsumer_.setup(RD_KAFKA_OFFSET_TAIL(kConsumeLatestN),
                             &consumerOptions) == false) {
      LOG(INFO) << "setup consumer fail";
      return false;
    }

    if (!kafkaConsumer_.checkAlive()) {
      LOG(ERROR) << "kafka brokers is not alive";
      return false;
    }
  }

  // kafkaConsumerCommonEvents_
  {
    // assume we have 100,000 online workers
    const int32_t kConsumeLatestN = 100000;

    map<string, string> consumerOptions;
    // fetch.wait.max.ms:
    // Maximum time the broker may wait to fill the response with fetch.min.bytes.
    consumerOptions["fetch.wait.max.ms"] = "600";

    if (kafkaConsumerCommonEvents_.setup(RD_KAFKA_OFFSET_TAIL(kConsumeLatestN),
                                         &consumerOptions) == false) {
      LOG(INFO) << "setup common events consumer fail";
      return false;
    }
  
    if (!kafkaConsumerCommonEvents_.checkAlive()) {
      LOG(ERROR) << "common events kafka brokers is not alive";
      return false;
    }
  }

  // run threads
  threadConsume_ = thread(&StatsServer::runThreadConsume, this);
  threadConsumeCommonEvents_ = thread(&StatsServer::runThreadConsumeCommonEvents, this);
  
  return true;
}

void StatsServer::runThreadConsume() {
  LOG(INFO) << "start sharelog consume thread";
  time_t lastCleanTime   = time(nullptr);
  time_t lastFlushDBTime = time(nullptr);

  const time_t kExpiredCleanInterval = 60*30;
  const int32_t kTimeoutMs = 1000;  // consumer timeout

  while (running_) {
    //
    // try to remove expired workers
    //
    if (lastCleanTime + kExpiredCleanInterval < time(nullptr)) {
      removeExpiredWorkers();
      lastCleanTime = time(nullptr);
    }

    //
    // flush workers to table.mining_workers
    //
    if (lastFlushDBTime + kFlushDBInterval_ < time(nullptr)) {
      // will use thread to flush data to DB.
      // it's very fast because we use insert statement with multiple values
      // and merge table when flush data to DB.
      flushWorkersToDB();
      lastFlushDBTime = time(nullptr);
    }

    //
    // consume message
    //
    rd_kafka_message_t *rkmessage;
    rkmessage = kafkaConsumer_.consumer(kTimeoutMs);

    // timeout, most of time it's not nullptr and set an error:
    //          rkmessage->err == RD_KAFKA_RESP_ERR__PARTITION_EOF
    if (rkmessage == nullptr) {
      continue;
    }
    // consume share log
    consumeShareLog(rkmessage);
    rd_kafka_message_destroy(rkmessage);  /* Return message to rdkafka */

  }
  LOG(INFO) << "stop sharelog consume thread";

  stop();  // if thread exit, we must call server to stop
}

void StatsServer::runThreadConsumeCommonEvents() {
  LOG(INFO) << "start common events consume thread";

  const int32_t kTimeoutMs = 3000;  // consumer timeout

  while (running_) {
    //
    // consume message
    //
    rd_kafka_message_t *rkmessage;
    rkmessage = kafkaConsumerCommonEvents_.consumer(kTimeoutMs);

    // timeout, most of time it's not nullptr and set an error:
    //          rkmessage->err == RD_KAFKA_RESP_ERR__PARTITION_EOF
    if (rkmessage == nullptr) {
      continue;
    }

    // consume share log
    consumeCommonEvents(rkmessage);
    rd_kafka_message_destroy(rkmessage);  /* Return message to rdkafka */
  }

  LOG(INFO) << "stop common events consume thread";
}

void StatsServer::consumeCommonEvents(rd_kafka_message_t *rkmessage) {
  // check error
  if (rkmessage->err) {
    if (rkmessage->err == RD_KAFKA_RESP_ERR__PARTITION_EOF) {
      // Reached the end of the topic+partition queue on the broker.
      // Not really an error.
      //      LOG(INFO) << "consumer reached end of " << rd_kafka_topic_name(rkmessage->rkt)
      //      << "[" << rkmessage->partition << "] "
      //      << " message queue at offset " << rkmessage->offset;
      // acturlly
      return;
    }

    LOG(ERROR) << "consume error for topic " << rd_kafka_topic_name(rkmessage->rkt)
    << "[" << rkmessage->partition << "] offset " << rkmessage->offset
    << ": " << rd_kafka_message_errstr(rkmessage);

    if (rkmessage->err == RD_KAFKA_RESP_ERR__UNKNOWN_PARTITION ||
        rkmessage->err == RD_KAFKA_RESP_ERR__UNKNOWN_TOPIC) {
      LOG(FATAL) << "consume fatal";
    }
    return;
  }

  const char *message = (const char*)rkmessage->payload;
  DLOG(INFO) << "A New Common Event: " << string(message, rkmessage->len);

  JsonNode r;
  if (!JsonNode::parse(message, message + rkmessage->len, r)) {
    LOG(ERROR) << "decode common event failure";
    return;
  }

  // check fields
  if (r["type"].type()    != Utilities::JS::type::Str ||
      r["content"].type() != Utilities::JS::type::Obj) {
    LOG(ERROR) << "common event missing some fields";
    return;
  }

  // update worker status
  if (r["type"].str() == "worker_update") {
    // check fields
    if (r["content"]["user_id"].type()     != Utilities::JS::type::Int ||
        r["content"]["worker_id"].type()   != Utilities::JS::type::Int ||
        r["content"]["worker_name"].type() != Utilities::JS::type::Str ||
        r["content"]["miner_agent"].type() != Utilities::JS::type::Str) {
      LOG(ERROR) << "common event `worker_update` missing some fields";
      return;
    }

    int32_t userId    = r["content"]["user_id"].int32();
    int64_t workerId  = r["content"]["worker_id"].int64();
    string workerName = filterWorkerName(r["content"]["worker_name"].str());
    string minerAgent = filterWorkerName(r["content"]["miner_agent"].str());

    updateWorkerStatus(userId, workerId, workerName.c_str(), minerAgent.c_str());
  }

}

bool StatsServer::updateWorkerStatus(const int32_t userId, const int64_t workerId,
                                     const char *workerName, const char *minerAgent) {
  string sql;
  char **row = nullptr;
  MySQLResult res;
  const string nowStr = date("%F %T");

  // find the miner
  sql = Strings::Format("SELECT `group_id`,`worker_name` FROM `mining_workers` "
                        " WHERE `puid`=%d AND `worker_id`= %" PRId64"",
                        userId, workerId);
  poolDBCommonEvents_.query(sql, res);

  if (res.numRows() != 0 && (row = res.nextRow()) != nullptr) {
    const int32_t groupId = atoi(row[0]);

    // group Id == 0: means the miner's status is 'deleted'
    // we need to move from 'deleted' group to 'default' group.
    sql = Strings::Format("UPDATE `mining_workers` SET `group_id`=%d, "
                          " `worker_name`=\"%s\", `miner_agent`=\"%s\", "
                          " `updated_at`=\"%s\" "
                          " WHERE `puid`=%d AND `worker_id`= %" PRId64"",
                          groupId == 0 ? userId * -1 : groupId,
                          workerName, minerAgent,
                          nowStr.c_str(),
                          userId, workerId);
    if (poolDBCommonEvents_.execute(sql) == false) {
      LOG(ERROR) << "update worker status failure";
      return false;
    }
  }
  else {
    // we have to use 'ON DUPLICATE KEY UPDATE', because 'statshttpd' may insert
    // items to table.mining_workers between we 'select' and 'insert' gap.
    // 'statshttpd' will always set an empty 'worker_name'.
    sql = Strings::Format("INSERT INTO `mining_workers`(`puid`,`worker_id`,"
                          " `group_id`,`worker_name`,`miner_agent`,"
                          " `created_at`,`updated_at`) "
                          " VALUES(%d,%" PRId64",%d,\"%s\",\"%s\",\"%s\",\"%s\")"
                          " ON DUPLICATE KEY UPDATE "
                          " `worker_name`= \"%s\",`miner_agent`=\"%s\",`updated_at`=\"%s\" ",
                          userId, workerId,
                          userId * -1,  // default group id
                          workerName, minerAgent,
                          nowStr.c_str(), nowStr.c_str(),
                          workerName, minerAgent,
                          nowStr.c_str());
    if (poolDBCommonEvents_.execute(sql) == false) {
      LOG(ERROR) << "insert worker name failure";
      return false;
    }
  }

  return true;
}

StatsServer::ServerStatus StatsServer::getServerStatus() {
  ServerStatus s;

  s.uptime_        = (uint32_t)(time(nullptr) - uptime_);
  s.requestCount_  = requestCount_;
  s.workerCount_   = totalWorkerCount_;
  s.userCount_     = totalUserCount_;
  s.responseBytes_ = responseBytes_;
  s.poolStatus_    = poolWorker_.getWorkerStatus();

  return s;
}

void StatsServer::httpdServerStatus(struct evhttp_request *req, void *arg) {
  evhttp_add_header(evhttp_request_get_output_headers(req),
                    "Content-Type", "text/json");
  StatsServer *server = (StatsServer *)arg;
  server->requestCount_++;

  struct evbuffer *evb = evbuffer_new();
  StatsServer::ServerStatus s = server->getServerStatus();

  evbuffer_add_printf(evb, "{\"err_no\":0,\"err_msg\":\"\","
                      "\"data\":{\"uptime\":\"%04u d %02u h %02u m %02u s\","
                      "\"request\":%" PRIu64",\"repbytes\":%" PRIu64","
                      "\"pool\":{\"accept\":[%" PRIu64",%" PRIu64",%" PRIu64",%" PRIu64"],"
                      "\"reject\":[0,0,%" PRIu64",%" PRIu64"],\"accept_count\":%" PRIu32","
                      "\"workers\":%" PRIu64",\"users\":%" PRIu64""
                      "}}}",
                      s.uptime_/86400, (s.uptime_%86400)/3600,
                      (s.uptime_%3600)/60, s.uptime_%60,
                      s.requestCount_, s.responseBytes_,
                      // accept
                      s.poolStatus_.accept1m_, s.poolStatus_.accept5m_,
                      s.poolStatus_.accept15m_, s.poolStatus_.accept1h_,
                      // reject
                      s.poolStatus_.reject15m_, s.poolStatus_.reject1h_,
                      s.poolStatus_.acceptCount_,
                      s.workerCount_, s.userCount_);

  server->responseBytes_ += evbuffer_get_length(evb);
  evhttp_send_reply(req, HTTP_OK, "OK", evb);
  evbuffer_free(evb);
}

void StatsServer::httpdGetWorkerStatus(struct evhttp_request *req, void *arg) {
  evhttp_add_header(evhttp_request_get_output_headers(req),
                    "Content-Type", "text/json");
  StatsServer *server = (StatsServer *)arg;
  server->requestCount_++;

  evhttp_cmd_type rMethod = evhttp_request_get_command(req);
  char *query = nullptr;  // remember free it

  if (rMethod == EVHTTP_REQ_GET) {
    // GET
    struct evhttp_uri *uri = evhttp_uri_parse(evhttp_request_get_uri(req));
    const char *uriQuery = nullptr;
    if (uri != nullptr && (uriQuery = evhttp_uri_get_query(uri)) != nullptr) {
      query = strdup(uriQuery);
      evhttp_uri_free(uri);
    }
  }
  else if (rMethod == EVHTTP_REQ_POST) {
    // POST
    struct evbuffer *evbIn = evhttp_request_get_input_buffer(req);
    size_t len = 0;
    if (evbIn != nullptr && (len = evbuffer_get_length(evbIn)) > 0) {
      query = (char *)malloc(len + 1);
      evbuffer_copyout(evbIn, query, len);
      query[len] = '\0';  // evbuffer is not include '\0'
    }
  }

  // evbuffer for output
  struct evbuffer *evb = evbuffer_new();

  // query is empty, return
  if (query == nullptr) {
    evbuffer_add_printf(evb, "{\"err_no\":1,\"err_msg\":\"invalid args\"}");
    evhttp_send_reply(req, HTTP_OK, "OK", evb);
    evbuffer_free(evb);

    return;
  }

  // parse query
  struct evkeyvalq params;
  evhttp_parse_query_str(query, &params);
  const char *pUserId   = evhttp_find_header(&params, "user_id");
  const char *pWorkerId = evhttp_find_header(&params, "worker_id");
  const char *pIsMerge  = evhttp_find_header(&params, "is_merge");

  if (pUserId == nullptr || pWorkerId == nullptr) {
    evbuffer_add_printf(evb, "{\"err_no\":1,\"err_msg\":\"invalid args\"}");
    evhttp_send_reply(req, HTTP_OK, "OK", evb);
    goto finish;
  }

  evbuffer_add_printf(evb, "{\"err_no\":0,\"err_msg\":\"\",\"data\":{");
  server->getWorkerStatus(evb, pUserId, pWorkerId, pIsMerge);
  evbuffer_add_printf(evb, "}}");

  server->responseBytes_ += evbuffer_get_length(evb);
  evhttp_send_reply(req, HTTP_OK, "OK", evb);

finish:
  evhttp_clear_headers(&params);
  evbuffer_free(evb);
  if (query)
    free(query);
}

void StatsServer::getWorkerStatus(struct evbuffer *evb, const char *pUserId,
                                  const char *pWorkerId, const char *pIsMerge) {
  assert(pWorkerId != nullptr);
  const int32_t userId = atoi(pUserId);

  bool isMerge = false;
  if (pIsMerge != nullptr && (*pIsMerge == 'T' || *pIsMerge == 't')) {
      isMerge = true;
  }

  vector<string> vWorkerIdsStr;
  string pWorkerIdStr = pWorkerId;
  boost::split(vWorkerIdsStr, pWorkerIdStr, boost::is_any_of(","));

  vector<WorkerKey> keys;
  keys.reserve(vWorkerIdsStr.size());
  for (size_t i = 0; i < vWorkerIdsStr.size(); i++) {
    const int64_t workerId = strtoll(vWorkerIdsStr[i].c_str(), nullptr, 10);
    keys.push_back(WorkerKey(userId, workerId));
  }

  vector<WorkerStatus> workerStatus;
  getWorkerStatusBatch(keys, workerStatus);

  if (isMerge) {
    WorkerStatus merged = mergeWorkerStatus(workerStatus);
    workerStatus.clear();
    workerStatus.push_back(merged);
  }

  size_t i = 0;
  for (const auto &status : workerStatus) {
    char ipStr[INET_ADDRSTRLEN] = {0};
    inet_ntop(AF_INET, &(status.lastShareIP_), ipStr, INET_ADDRSTRLEN);

    // extra infomations
    string extraInfo;
    if (!isMerge && keys[i].workerId_ == 0) {  // all workers of this user
      pthread_rwlock_rdlock(&rwlock_);
      extraInfo = Strings::Format(",\"workers\":%d", userWorkerCount_[userId]);
      pthread_rwlock_unlock(&rwlock_);
    }

    evbuffer_add_printf(evb,
                        "%s\"%" PRId64"\":{\"accept\":[%" PRIu64",%" PRIu64",%" PRIu64",%" PRIu64"]"
                        ",\"reject\":[0,0,%" PRIu64",%" PRIu64"],\"accept_count\":%" PRIu32""
                        ",\"last_share_ip\":\"%s\",\"last_share_time\":%u"
                        "%s}",
                        (i == 0 ? "" : ","),
                        (isMerge ? 0 : keys[i].workerId_),
                        status.accept1m_, status.accept5m_, status.accept15m_, status.accept1h_,
                        status.reject15m_, status.reject1h_,
                        status.acceptCount_,
                        ipStr, status.lastShareTime_,
                        extraInfo.length() ? extraInfo.c_str() : "");
    i++;
  }
}

void StatsServer::runHttpd() {
  struct evhttp_bound_socket *handle;
  struct evhttp *httpd;

  base_ = event_base_new();
  httpd = evhttp_new(base_);

  evhttp_set_allowed_methods(httpd, EVHTTP_REQ_GET | EVHTTP_REQ_POST | EVHTTP_REQ_HEAD);
  evhttp_set_timeout(httpd, 5 /* timeout in seconds */);

  evhttp_set_cb(httpd, "/",               StatsServer::httpdServerStatus, this);
  evhttp_set_cb(httpd, "/worker_status",  StatsServer::httpdGetWorkerStatus, this);
  evhttp_set_cb(httpd, "/worker_status/", StatsServer::httpdGetWorkerStatus, this);

  handle = evhttp_bind_socket_with_handle(httpd, httpdHost_.c_str(), httpdPort_);
  if (!handle) {
    LOG(ERROR) << "couldn't bind to port: " << httpdPort_ << ", host: " << httpdHost_ << ", exiting.";
    return;
  }
  event_base_dispatch(base_);
}

void StatsServer::run() {
  if (setupThreadConsume() == false) {
    return;
  }

  runHttpd();
}



//////////////////////////////  ShareLogWriter  ///////////////////////////////
ShareLogWriter::ShareLogWriter(const char *kafkaBrokers,
                               const string &dataDir,
                               const string &kafkaGroupID)
:running_(true), dataDir_(dataDir),
hlConsumer_(kafkaBrokers, KAFKA_TOPIC_SHARE_LOG, 0/* patition */, kafkaGroupID)
{
}

ShareLogWriter::~ShareLogWriter() {
  // close file handlers
  for (auto & itr : fileHandlers_) {
    LOG(INFO) << "fclose file handler, date: " << date("%F", itr.first);
    fclose(itr.second);
  }
  fileHandlers_.clear();
}

void ShareLogWriter::stop() {
  if (!running_)
    return;

  running_ = false;
}

FILE* ShareLogWriter::getFileHandler(uint32_t ts) {
  if (fileHandlers_.find(ts) != fileHandlers_.end()) {
    return fileHandlers_[ts];
  }

  const string filePath = getStatsFilePath(dataDir_, ts);
  LOG(INFO) << "fopen: " << filePath;

  FILE *f = fopen(filePath.c_str(), "ab");  // append mode, bin file
  if (f == nullptr) {
    LOG(FATAL) << "fopen file fail: " << filePath;
    return nullptr;
  }

  fileHandlers_[ts] = f;
  return f;
}

void ShareLogWriter::consumeShareLog(rd_kafka_message_t *rkmessage) {
  // check error
  if (rkmessage->err) {
    if (rkmessage->err == RD_KAFKA_RESP_ERR__PARTITION_EOF) {
      // Reached the end of the topic+partition queue on the broker.
      // Not really an error.
      //      LOG(INFO) << "consumer reached end of " << rd_kafka_topic_name(rkmessage->rkt)
      //      << "[" << rkmessage->partition << "] "
      //      << " message queue at offset " << rkmessage->offset;
      // acturlly
      return;
    }

    LOG(ERROR) << "consume error for topic " << rd_kafka_topic_name(rkmessage->rkt)
    << "[" << rkmessage->partition << "] offset " << rkmessage->offset
    << ": " << rd_kafka_message_errstr(rkmessage);

    if (rkmessage->err == RD_KAFKA_RESP_ERR__UNKNOWN_PARTITION ||
        rkmessage->err == RD_KAFKA_RESP_ERR__UNKNOWN_TOPIC) {
      LOG(FATAL) << "consume fatal";
    }
    return;
  }

  if (rkmessage->len != sizeof(Share)) {
    LOG(ERROR) << "sharelog message size(" << rkmessage->len << ") is not: " << sizeof(Share);
    return;
  }

  shares_.push_back(Share());
  Share *share = &(*shares_.rbegin());

  memcpy((uint8_t *)share, (const uint8_t *)rkmessage->payload, rkmessage->len);

  if (!share->isValid()) {
    LOG(ERROR) << "invalid share: " << share->toString();
    shares_.pop_back();
    return;
  }
}

void ShareLogWriter::tryCloseOldHanders() {
  while (fileHandlers_.size() > 3) {
    // Maps (and sets) are sorted, so the first element is the smallest,
    // and the last element is the largest.
    auto itr = fileHandlers_.begin();

    LOG(INFO) << "fclose file handler, date: " << date("%F", itr->first);
    fclose(itr->second);

    fileHandlers_.erase(itr);
  }
}

bool ShareLogWriter::flushToDisk() {
  std::set<FILE*> usedHandlers;

  for (const auto& share : shares_) {
    const uint32_t ts = share.timestamp_ - (share.timestamp_ % 86400);
    FILE *f = getFileHandler(ts);
    if (f == nullptr)
      return false;

    usedHandlers.insert(f);
    fwrite((uint8_t *)&share, sizeof(Share), 1, f);
  }

  shares_.clear();

  for (auto & f : usedHandlers) {
    fflush(f);
  }

  // should call this after write data
  tryCloseOldHanders();

  return true;
}

void ShareLogWriter::run() {
  time_t lastFlushTime = time(nullptr);
  const int32_t kFlushDiskInterval = 2;
  const int32_t kTimeoutMs = 1000;

  if (!hlConsumer_.setup()) {
    LOG(ERROR) << "setup sharelog consumer fail";
    return;
  }

  while (running_) {
    //
    // flush data to disk
    //
    if (shares_.size() > 0 &&
        time(nullptr) > kFlushDiskInterval + lastFlushTime) {
      flushToDisk();
      lastFlushTime = time(nullptr);
    }

    //
    // consume message
    //
    rd_kafka_message_t *rkmessage;
    rkmessage = hlConsumer_.consumer(kTimeoutMs);

    // timeout, most of time it's not nullptr and set an error:
    //          rkmessage->err == RD_KAFKA_RESP_ERR__PARTITION_EOF
    if (rkmessage == nullptr) {
      continue;
    }

    // consume share log
    consumeShareLog(rkmessage);
    rd_kafka_message_destroy(rkmessage);  /* Return message to rdkafka */
  }

  // flush left shares
  if (shares_.size() > 0)
    flushToDisk();
}



///////////////////////////////  ShareStatsDay  ////////////////////////////////
ShareStatsDay::ShareStatsDay() {
  memset((uint8_t *)&shareAccept1h_[0], 0, sizeof(shareAccept1h_));
  memset((uint8_t *)&shareReject1h_[0], 0, sizeof(shareReject1h_));
  memset((uint8_t *)&score1h_[0],       0, sizeof(score1h_));
  shareAccept1d_   = 0;
  shareReject1d_   = 0;
  score1d_         = 0.0;
  modifyHoursFlag_ = 0x0u;
}

void ShareStatsDay::processShare(uint32_t hourIdx, const Share &share) {
  ScopeLock sl(lock_);

  if (share.result_ == Share::Result::ACCEPT) {
    shareAccept1h_[hourIdx] += share.share_;
    shareAccept1d_          += share.share_;

    score1h_[hourIdx] += share.score();
    score1d_          += share.score();
  } else {
    shareReject1h_[hourIdx] += share.share_;
    shareReject1d_          += share.share_;
  }
  modifyHoursFlag_ |= (0x01u << hourIdx);
}

void ShareStatsDay::getShareStatsHour(uint32_t hourIdx, ShareStats *stats) {
  ScopeLock sl(lock_);
  if (hourIdx > 23)
    return;

  stats->shareAccept_ = shareAccept1h_[hourIdx];
  stats->shareReject_ = shareReject1h_[hourIdx];
  stats->earn_        = score1h_[hourIdx] * BLOCK_REWARD;
  if (stats->shareReject_)
  	stats->rejectRate_  = (stats->shareReject_ * 1.0 / (stats->shareAccept_ + stats->shareReject_));
  else
    stats->rejectRate_ = 0.0;
}

void ShareStatsDay::getShareStatsDay(ShareStats *stats) {
  ScopeLock sl(lock_);
  stats->shareAccept_ = shareAccept1d_;
  stats->shareReject_ = shareReject1d_;
  stats->earn_        = score1d_ * BLOCK_REWARD;
  if (stats->shareReject_)
    stats->rejectRate_  = (stats->shareReject_ * 1.0 / (stats->shareAccept_ + stats->shareReject_));
  else
    stats->rejectRate_ = 0.0;
}


///////////////////////////////  ShareLogDumper  ///////////////////////////////
ShareLogDumper::ShareLogDumper(const string &dataDir, time_t timestamp,
                               const std::set<int32_t> &uids)
: uids_(uids), isDumpAll_(false)
{
  filePath_ = getStatsFilePath(dataDir, timestamp);

  if (uids_.empty())
    isDumpAll_ = true;
}

ShareLogDumper::~ShareLogDumper() {
}

void ShareLogDumper::dump2stdout() {
  FILE *f = nullptr;

  // open file
  LOG(INFO) << "open file: " << filePath_;
  if ((f = fopen(filePath_.c_str(), "rb")) == nullptr) {
    LOG(ERROR) << "open file fail: " << filePath_;
    return;
  }

  // 2000000 * 48 = 96,000,000 Bytes
  const uint32_t kElements = 2000000;
  size_t readNum;
  string buf;
  buf.resize(kElements * sizeof(Share));

  while (1) {
    readNum = fread((uint8_t *)buf.data(), sizeof(Share), kElements, f);

    if (readNum == 0) {
      if (feof(f)) {
        LOG(INFO) << "End-of-File reached: " << filePath_;
        break;
      }
      LOG(INFO) << "read 0 bytes: " << filePath_;
      continue;
    }

    parseShareLog((uint8_t *)buf.data(), readNum * sizeof(Share));
  };

  fclose(f);
}

void ShareLogDumper::parseShareLog(const uint8_t *buf, size_t len) {
  assert(len % sizeof(Share) == 0);
  const size_t size = len / sizeof(Share);

  for (size_t i = 0; i < size; i++) {
    parseShare((Share *)(buf + sizeof(Share)*i));
  }
}

void ShareLogDumper::parseShare(const Share *share) {
  if (!share->isValid()) {
    LOG(ERROR) << "invalid share: " << share->toString();
    return;
  }

  if (isDumpAll_ || uids_.find(share->userId_) != uids_.end()) {
    // print to stdout
    std::cout << share->toString() << std::endl;
  }
}

///////////////////////////////  ShareLogParser  ///////////////////////////////
ShareLogParser::ShareLogParser(const string &dataDir, time_t timestamp,
                               const MysqlConnectInfo &poolDBInfo)
: date_(timestamp), f_(nullptr), buf_(nullptr), lastPosition_(0), poolDB_(poolDBInfo)
{
  pthread_rwlock_init(&rwlock_, nullptr);

  {
    // for the pool
    WorkerKey pkey(0, 0);
    workersStats_[pkey] = std::make_shared<ShareStatsDay>();
  }
  filePath_ = getStatsFilePath(dataDir, timestamp);

  // prealloc memory
  buf_ = (uint8_t *)malloc(kMaxElementsNum_ * sizeof(Share));
}

ShareLogParser::~ShareLogParser() {
  if (f_)
    fclose(f_);

  if (buf_)
    free(buf_);
}

bool ShareLogParser::init() {
  // check db
  if (!poolDB_.ping()) {
    LOG(ERROR) << "connect to db fail";
    return false;
  }

  // try to open file
  FILE *f = fopen(filePath_.c_str(), "rb");

  if (f == nullptr) {
    LOG(ERROR) << "open file fail, try create it: " << filePath_;

    f = fopen(filePath_.c_str(), "ab");

    if (f == nullptr) {
      LOG(ERROR) << "create file fail: " << filePath_;
      return false;
    }
    else {
      LOG(INFO) << "create file success: " << filePath_;
    }
  }
  else {
    LOG(INFO) << "open file success: " << filePath_;
  }

  fclose(f);

  return true;
}

void ShareLogParser::parseShareLog(const uint8_t *buf, size_t len) {
  assert(len % sizeof(Share) == 0);
  const size_t size = len / sizeof(Share);

  for (size_t i = 0; i < size; i++) {
    parseShare((Share *)(buf + sizeof(Share)*i));
  }
}

void ShareLogParser::parseShare(const Share *share) {
  if (!share->isValid()) {
    LOG(ERROR) << "invalid share: " << share->toString();
    return;
  }

  WorkerKey wkey(share->userId_, share->workerHashId_);
  WorkerKey ukey(share->userId_, 0);
  WorkerKey pkey(0, 0);

  pthread_rwlock_wrlock(&rwlock_);
  if (workersStats_.find(wkey) == workersStats_.end()) {
    workersStats_[wkey] = std::make_shared<ShareStatsDay>();
  }
  if (workersStats_.find(ukey) == workersStats_.end()) {
    workersStats_[ukey] = std::make_shared<ShareStatsDay>();
  }
  pthread_rwlock_unlock(&rwlock_);

  const uint32_t hourIdx = getHourIdx(share->timestamp_);
  workersStats_[wkey]->processShare(hourIdx, *share);
  workersStats_[ukey]->processShare(hourIdx, *share);
  workersStats_[pkey]->processShare(hourIdx, *share);
}

bool ShareLogParser::processUnchangedShareLog() {
  FILE *f = nullptr;

  // open file
  LOG(INFO) << "open file: " << filePath_;
  if ((f = fopen(filePath_.c_str(), "rb")) == nullptr) {
    LOG(ERROR) << "open file fail: " << filePath_;
    return false;
  }

  // 2000000 * 48 = 96,000,000 Bytes
  const uint32_t kElements = 2000000;
  size_t readNum;
  string buf;
  buf.resize(kElements * sizeof(Share));

  while (1) {
    readNum = fread((uint8_t *)buf.data(), sizeof(Share), kElements, f);

    if (readNum == 0) {
      if (feof(f)) {
        LOG(INFO) << "End-of-File reached: " << filePath_;
        break;
      }
      LOG(INFO) << "read 0 bytes: " << filePath_;
      continue;
    }

    parseShareLog((uint8_t *)buf.data(), readNum * sizeof(Share));
  };

  fclose(f);
  return true;
}

int64_t ShareLogParser::processGrowingShareLog() {
  size_t readNum = 0;

  if (f_ == nullptr) {
    if ((f_ = fopen(filePath_.c_str(), "rb")) == nullptr) {
      LOG(ERROR) << "open file fail: " << filePath_;
      return -1;
    }
  }
  assert(f_ != nullptr);

  // seek to last position. we manager the file indicator by our own.
  fseek(f_, lastPosition_, SEEK_SET);

  //
  // no need to set buffer memory to zero before fread
  // return: the total number of elements successfully read is returned.
  //
  // fread():
  // C11 at 7.21.8.1.2 and 7.21.8.2.2 says: If an error occurs, the resulting
  // value of the file position indicator for the stream is indeterminate.
  //
  readNum = fread(buf_, sizeof(Share), kMaxElementsNum_, f_);
  if (readNum == 0)
    return 0;

  const size_t bufSize = readNum * sizeof(Share);
  lastPosition_ += bufSize;
  assert(lastPosition_ % sizeof(Share) == 0);

  // parse shares
  parseShareLog(buf_, bufSize);

  return readNum;
}

bool ShareLogParser::isReachEOF() {
  struct stat sb;
  int fd = open(filePath_.c_str(), O_RDONLY);
  if (fd == -1) {
    LOG(ERROR) << "open file fail: " << filePath_;
    return true;  // if error we consider as EOF
  }
  if (fstat(fd, &sb) == -1) {
    LOG(ERROR) << "fstat fail: " << filePath_;
    return true;
  }
  close(fd);

  return lastPosition_ == sb.st_size;
}

void ShareLogParser::generateHoursData(shared_ptr<ShareStatsDay> stats,
                                       const int32_t userId,
                                       const int64_t workerId,
                                       vector<string> *valuesWorkersHour,
                                       vector<string> *valuesUsersHour,
                                       vector<string> *valuesPoolHour) {
  assert(sizeof(stats->shareAccept1h_) / sizeof(stats->shareAccept1h_[0]) == 24);
  assert(sizeof(stats->shareReject1h_) / sizeof(stats->shareReject1h_[0]) == 24);
  assert(sizeof(stats->score1h_)       / sizeof(stats->score1h_[0])       == 24);

  string table, extraValues;
  // worker
  if (userId != 0 && workerId != 0) {
    extraValues = Strings::Format("% " PRId64",%d,", workerId, userId);
    table = "stats_workers_hour";
  }
  // user
  else if (userId != 0 && workerId == 0) {
    extraValues = Strings::Format("%d,", userId);
    table = "stats_users_hour";
  }
  // pool
  else if (userId == 0 && workerId == 0) {
    table = "stats_pool_hour";
  }
  else {
    LOG(ERROR) << "unknown stats type";
    return;
  }

  // loop hours from 00 -> 03
  for (size_t i = 0; i < 24; i++) {
    string valuesStr;
    {
      ScopeLock sl(stats->lock_);
      const uint32_t flag = (0x01U << i);
      if ((stats->modifyHoursFlag_ & flag) == 0x0u) {
        continue;
      }
      const string hourStr = Strings::Format("%s%02d", date("%Y%m%d", date_).c_str(), i);
      const int32_t hour = atoi(hourStr.c_str());

      const uint64_t accept   = stats->shareAccept1h_[i];  // alias
      const uint64_t reject   = stats->shareReject1h_[i];
      double rejectRate = 0.0;
      if (reject)
      	rejectRate = (double)reject / (accept + reject);
      const string nowStr   = date("%F %T");
      const string scoreStr = score2Str(stats->score1h_[i]);
      const int64_t earn    = stats->score1h_[i] * BLOCK_REWARD;

      valuesStr = Strings::Format("%s %d,%" PRIu64",%" PRIu64","
                                  "  %lf,'%s',%" PRId64",'%s','%s'",
                                  extraValues.c_str(),
                                  hour, accept, reject, rejectRate, scoreStr.c_str(),
                                  earn, nowStr.c_str(), nowStr.c_str());
    }  // for scope lock

    if (table == "stats_workers_hour") {
      valuesWorkersHour->push_back(valuesStr);
    } else if (table == "stats_users_hour") {
      valuesUsersHour->push_back(valuesStr);
    } else if (table == "stats_pool_hour") {
      valuesPoolHour->push_back(valuesStr);
    }
  } /* /for */
}

void ShareLogParser::flushHourOrDailyData(const vector<string> values,
                                          const string &tableName,
                                          const string &extraFields) {
  string mergeSQL;
  string fields;

  // in case two process use the same tmp table name, we add process id into
  // tmp table name.
  const string tmpTableName = Strings::Format("%s_tmp_%d",
                                              tableName.c_str(), getpid());

  if (!poolDB_.ping()) {
    LOG(ERROR) << "can't connect to pool DB";
    return;
  }

  // drop tmp table
  const string sqlDropTmpTable = Strings::Format("DROP TABLE IF EXISTS `%s`;",
                                                 tmpTableName.c_str());
  // create tmp table
  const string createTmpTable = Strings::Format("CREATE TABLE `%s` like `%s`;",
                                                tmpTableName.c_str(), tableName.c_str());

  if (!poolDB_.execute(sqlDropTmpTable)) {
    LOG(ERROR) << "DROP TABLE `" << tmpTableName << "` failure";
    return;
  }
  if (!poolDB_.execute(createTmpTable)) {
    LOG(ERROR) << "CREATE TABLE `" << tmpTableName << "` failure";
    return;
  }

  // fields for table.stats_xxxxx_hour
  fields = Strings::Format("%s `share_accept`,`share_reject`,`reject_rate`,"
                           "`score`,`earn`,`created_at`,`updated_at`", extraFields.c_str());

  //check conditions before multi-insert, if there is no need, do info LOG instead of ERROR.
  // It's a common case when the pool is run in the testnet with computer miner instead of ASCII miners
  if(fields.size() == 0 || values.size() == 0 || tmpTableName.length() == 0){
  	LOG(INFO) << "multi-insert table." << tmpTableName << " failure because of empty info";
  	return;
  }

  if (!multiInsert(poolDB_, tmpTableName, fields, values)) {
    LOG(ERROR) << "multi-insert table." << tmpTableName << " failure";
    return;
  }

  // merge two table items
  mergeSQL = Strings::Format("INSERT INTO `%s` "
                             " SELECT * FROM `%s` AS `t2` "
                             " ON DUPLICATE KEY "
                             " UPDATE "
                             "  `share_accept` = `t2`.`share_accept`, "
                             "  `share_reject` = `t2`.`share_reject`, "
                             "  `reject_rate`  = `t2`.`reject_rate`, "
                             "  `score`        = `t2`.`score`, "
                             "  `earn`         = `t2`.`earn`, "
                             "  `updated_at`   = `t2`.`updated_at` ",
                             tableName.c_str(), tmpTableName.c_str());
  if (!poolDB_.update(mergeSQL)) {
    LOG(ERROR) << "merge mining_workers failure";
    return;
  }

  if (!poolDB_.execute(sqlDropTmpTable)) {
    LOG(ERROR) << "DROP TABLE `" << tmpTableName << "` failure";
    return;
  }
}

void ShareLogParser::generateDailyData(shared_ptr<ShareStatsDay> stats,
                                       const int32_t userId,
                                       const int64_t workerId,
                                       vector<string> *valuesWorkersDay,
                                       vector<string> *valuesUsersDay,
                                       vector<string> *valuesPoolDay) {
  string table, extraValues;
  // worker
  if (userId != 0 && workerId != 0) {
    extraValues = Strings::Format("% " PRId64",%d,", workerId, userId);
    table = "stats_workers_day";
  }
  // user
  else if (userId != 0 && workerId == 0) {
    extraValues = Strings::Format("%d,", userId);
    table = "stats_users_day";
  }
  // pool
  else if (userId == 0 && workerId == 0) {
    table = "stats_pool_day";
  }
  else {
    LOG(ERROR) << "unknown stats type";
    return;
  }

  string valuesStr;
  {
    ScopeLock sl(stats->lock_);
    const int32_t day = atoi(date("%Y%m%d", date_).c_str());

    const uint64_t accept   = stats->shareAccept1d_;  // alias
    const uint64_t reject   = stats->shareReject1d_;
    double rejectRate = 0.0;
    if (reject)
      rejectRate = (double)reject / (accept + reject);
    const string nowStr   = date("%F %T");
    const string scoreStr = score2Str(stats->score1d_);
    const int64_t earn    = stats->score1d_ * BLOCK_REWARD;

    valuesStr = Strings::Format("%s %d,%" PRIu64",%" PRIu64","
                                "  %lf,'%s',%" PRId64",'%s','%s'",
                                extraValues.c_str(),
                                day, accept, reject, rejectRate, scoreStr.c_str(),
                                earn, nowStr.c_str(), nowStr.c_str());
  }  // for scope lock

  if (table == "stats_workers_day") {
    valuesWorkersDay->push_back(valuesStr);
  } else if (table == "stats_users_day") {
    valuesUsersDay->push_back(valuesStr);
  } else if (table == "stats_pool_day") {
    valuesPoolDay->push_back(valuesStr);
  }
}

shared_ptr<ShareStatsDay> ShareLogParser::getShareStatsDayHandler(const WorkerKey &key) {
  pthread_rwlock_rdlock(&rwlock_);
  auto itr = workersStats_.find(key);
  pthread_rwlock_unlock(&rwlock_);

  if (itr != workersStats_.end()) {
    return itr->second;
  }
  return nullptr;
}

void ShareLogParser::removeExpiredDataFromDB() {
  static time_t lastRemoveTime = 0u;
  string sql;

  // check if we need to remove, 3600 = 1 hour
  if (lastRemoveTime + 3600 > time(nullptr)) {
    return;
  }

  // set the last remove timestamp
  lastRemoveTime = time(nullptr);

  //
  // table.stats_workers_day
  //
  {
    const int32_t kDailyDataKeepDays_workers = 90; // 3 months
    const string dayStr = date("%Y%m%d",
                               time(nullptr) - 86400 * kDailyDataKeepDays_workers);
    sql = Strings::Format("DELETE FROM `stats_workers_day` WHERE `day` < '%s'",
                          dayStr.c_str());
    if (poolDB_.execute(sql)) {
      LOG(INFO) << "delete expired workers daily data before '"<< dayStr
      << "', count: " << poolDB_.affectedRows();
    }
  }

  //
  // table.stats_workers_hour
  //
  {
    const int32_t kHourDataKeepDays_workers = 24*3;  // 3 days
    const string hourStr = date("%Y%m%d%H",
                               time(nullptr) - 3600 * kHourDataKeepDays_workers);
    sql = Strings::Format("DELETE FROM `stats_workers_hour` WHERE `hour` < '%s'",
                          hourStr.c_str());
    if (poolDB_.execute(sql)) {
      LOG(INFO) << "delete expired workers hour data before '"<< hourStr
      << "', count: " << poolDB_.affectedRows();
    }
  }

  //
  // table.stats_users_hour
  //
  {
    const int32_t kHourDataKeepDays_users = 24*30;  // 30 days
    const string hourStr = date("%Y%m%d%H",
                                time(nullptr) - 3600 * kHourDataKeepDays_users);
    sql = Strings::Format("DELETE FROM `stats_users_hour` WHERE `hour` < '%s'",
                          hourStr.c_str());
    if (poolDB_.execute(sql)) {
      LOG(INFO) << "delete expired users hour data before '"<< hourStr
      << "', count: " << poolDB_.affectedRows();
    }
  }
}

bool ShareLogParser::flushToDB() {
  if (!poolDB_.ping()) {
    LOG(ERROR) << "connect db fail";
    return false;
  }

  LOG(INFO) << "start flush to DB...";

  //
  // we must finish the workersStats_ loop asap
  //
  vector<WorkerKey> keys;
  vector<shared_ptr<ShareStatsDay>> stats;

  pthread_rwlock_rdlock(&rwlock_);
  for (const auto &itr : workersStats_) {
    if (itr.second->modifyHoursFlag_ == 0x0u) {
      continue;  // no new data, ignore
    }
    keys.push_back(itr.first);
    stats.push_back(itr.second);  // shared_ptr increase ref here
  }
  pthread_rwlock_unlock(&rwlock_);

  LOG(INFO) << "dumped workers stats";

  vector<string> valuesWorkersHour;
  vector<string> valuesUsersHour;
  vector<string> valuesPoolHour;

  vector<string> valuesWorkersDay;
  vector<string> valuesUsersDay;
  vector<string> valuesPoolDay;

  for (size_t i = 0; i < keys.size(); i++) {
    //
    // the lock is in flushDailyData() & flushHoursData(), so maybe we lost
    // some data between func gaps, but it's not important. we will exec
    // processUnchangedShareLog() after the day has been past, no data will lost by than.
    //
    generateHoursData(stats[i], keys[i].userId_, keys[i].workerId_,
                      &valuesWorkersHour, &valuesUsersHour, &valuesPoolHour);
    generateDailyData(stats[i], keys[i].userId_, keys[i].workerId_,
                      &valuesWorkersDay, &valuesUsersDay, &valuesPoolDay);

    stats[i]->modifyHoursFlag_ = 0x0u;  // reset flag
  }

  LOG(INFO) << "generated sql values";
  size_t counter = 0;

  // flush hours data
  flushHourOrDailyData(valuesWorkersHour, "stats_workers_hour", "`worker_id`,`puid`,`hour`,");
  flushHourOrDailyData(valuesUsersHour,   "stats_users_hour"  , "`puid`,`hour`,");
  flushHourOrDailyData(valuesPoolHour,    "stats_pool_hour"   , "`hour`,");
  counter += valuesWorkersHour.size() + valuesUsersHour.size() + valuesPoolHour.size();

  // flush daily data
  flushHourOrDailyData(valuesWorkersDay, "stats_workers_day", "`worker_id`,`puid`,`day`,");
  flushHourOrDailyData(valuesUsersDay,   "stats_users_day"  , "`puid`,`day`,");
  flushHourOrDailyData(valuesPoolDay,    "stats_pool_day"   , "`day`,");
  counter += valuesWorkersDay.size() + valuesUsersDay.size() + valuesPoolDay.size();

  // done: daily data and hour data
  LOG(INFO) << "flush to DB... done, items: " << counter;

  // clean expired data
  removeExpiredDataFromDB();

  return true;
}




////////////////////////////  ShareLogParserServer  ////////////////////////////
ShareLogParserServer::ShareLogParserServer(const string dataDir,
                                           const string &httpdHost,
                                           unsigned short httpdPort,
                                           const MysqlConnectInfo &poolDBInfo,
                                           const uint32_t kFlushDBInterval):
running_(true), dataDir_(dataDir),
poolDBInfo_(poolDBInfo), kFlushDBInterval_(kFlushDBInterval),
base_(nullptr), httpdHost_(httpdHost), httpdPort_(httpdPort),
requestCount_(0), responseBytes_(0)
{
  const time_t now = time(nullptr);

  uptime_ = now;
  date_   = now - (now % 86400);

  pthread_rwlock_init(&rwlock_, nullptr);
}

ShareLogParserServer::~ShareLogParserServer() {
  stop();

  if (threadShareLogParser_.joinable())
    threadShareLogParser_.join();

  pthread_rwlock_destroy(&rwlock_);
}

void ShareLogParserServer::stop() {
  if (!running_)
    return;

  LOG(INFO) << "stop ShareLogParserServer...";

  running_ = false;
  event_base_loopexit(base_, NULL);
}

bool ShareLogParserServer::initShareLogParser(time_t datets) {
  pthread_rwlock_wrlock(&rwlock_);

  // reset
  date_ = datets - (datets % 86400);
  shareLogParser_ = nullptr;

  // set new obj
  shared_ptr<ShareLogParser> parser(new ShareLogParser(dataDir_, date_, poolDBInfo_));
  if (!parser->init()) {
    LOG(ERROR) << "parser check failure, date: " << date("%F", date_);
    pthread_rwlock_unlock(&rwlock_);
    return false;
  }

  shareLogParser_ = parser;
  pthread_rwlock_unlock(&rwlock_);
  return true;
}

void ShareLogParserServer::getShareStats(struct evbuffer *evb, const char *pUserId,
                                         const char *pWorkerId, const char *pHour) {
  vector<string> vHoursStr;
  vector<string> vWorkerIdsStr;
  vector<WorkerKey> keys;
  vector<int32_t>   hours;  // range: -23, -22, ..., 0, 24
  const int32_t userId = atoi(pUserId);

  // split by ','
  {
    string pHourStr = pHour;
    boost::split(vHoursStr, pHourStr, boost::is_any_of(","));

    string pWorkerIdStr = pWorkerId;
    boost::split(vWorkerIdsStr, pWorkerIdStr, boost::is_any_of(","));
  }

  // get worker keys
  keys.reserve(vWorkerIdsStr.size());
  for (size_t i = 0; i < vWorkerIdsStr.size(); i++) {
    const int64_t workerId = strtoll(vWorkerIdsStr[i].c_str(), nullptr, 10);
    keys.push_back(WorkerKey(userId, workerId));
  }

  // get hours
  hours.reserve(vHoursStr.size());
  for (const auto itr : vHoursStr) {
    hours.push_back(atoi(itr.c_str()));
  }

  vector<ShareStats> shareStats;
  shareStats.resize(keys.size() * hours.size());
  _getShareStats(keys, hours, shareStats);

  // output json string
  for (size_t i = 0; i < keys.size(); i++) {
    evbuffer_add_printf(evb, "%s\"%" PRId64"\":[", (i == 0 ? "" : ","), keys[i].workerId_);

    for (size_t j = 0; j < hours.size(); j++) {
      ShareStats *s = &shareStats[i * hours.size() + j];
      const int32_t hour = hours[j];

      double rejectRate = 0.0;
      if (s->shareReject_ != 0)
      	rejectRate = 1.0 * s->shareReject_ / (s->shareAccept_ + s->shareReject_);

      evbuffer_add_printf(evb,
                          "%s{\"hour\":%d,\"accept\":%" PRIu64",\"reject\":%" PRIu64","
                          "\"reject_rate\":%lf,\"earn\":%" PRId64"}",
                          (j == 0 ? "" : ","), hour,
                          s->shareAccept_, s->shareReject_, rejectRate, s->earn_);
    }
    evbuffer_add_printf(evb, "]");
  }
}

void ShareLogParserServer::_getShareStats(const vector<WorkerKey> &keys,
                                          const vector<int32_t> &hours,
                                          vector<ShareStats> &shareStats) {
  pthread_rwlock_rdlock(&rwlock_);
  shared_ptr<ShareLogParser> shareLogParser = shareLogParser_;
  pthread_rwlock_unlock(&rwlock_);

  if (shareLogParser == nullptr)
    return;

  for (size_t i = 0; i < keys.size(); i++) {
    shared_ptr<ShareStatsDay> statsDay = shareLogParser->getShareStatsDayHandler(keys[i]);
    if (statsDay == nullptr)
      continue;

    for (size_t j = 0; j < hours.size(); j++) {
      ShareStats *stats = &shareStats[i * hours.size() + j];
      const int32_t hour = hours[j];

      if (hour == 24) {
        statsDay->getShareStatsDay(stats);
      } else if (hour <= 0 && hour >= -23) {
        const uint32_t hourIdx = atoi(date("%H").c_str()) + hour;
        statsDay->getShareStatsHour(hourIdx, stats);
      }
    }
  }
}

void ShareLogParserServer::httpdShareStats(struct evhttp_request *req,
                                           void *arg) {
  evhttp_add_header(evhttp_request_get_output_headers(req),
                    "Content-Type", "text/json");
  ShareLogParserServer *server = (ShareLogParserServer *)arg;
  server->requestCount_++;

  evhttp_cmd_type rMethod = evhttp_request_get_command(req);
  char *query = nullptr;  // remember free it

  if (rMethod == EVHTTP_REQ_GET) {
    // GET
    struct evhttp_uri *uri = evhttp_uri_parse(evhttp_request_get_uri(req));
    const char *uriQuery = nullptr;
    if (uri != nullptr && (uriQuery = evhttp_uri_get_query(uri)) != nullptr) {
      query = strdup(uriQuery);
      evhttp_uri_free(uri);
    }
  }
  else if (rMethod == EVHTTP_REQ_POST) {
    // POST
    struct evbuffer *evbIn = evhttp_request_get_input_buffer(req);
    size_t len = 0;
    if (evbIn != nullptr && (len = evbuffer_get_length(evbIn)) > 0) {
      query = (char *)malloc(len + 1);
      evbuffer_copyout(evbIn, query, len);
      query[len] = '\0';  // evbuffer is not include '\0'
    }
  }

  // evbuffer for output
  struct evbuffer *evb = evbuffer_new();

  // query is empty, return
  if (query == nullptr) {
    evbuffer_add_printf(evb, "{\"err_no\":1,\"err_msg\":\"invalid args\"}");
    evhttp_send_reply(req, HTTP_OK, "OK", evb);
    evbuffer_free(evb);

    return;
  }

  // parse query
  struct evkeyvalq params;
  evhttp_parse_query_str(query, &params);
  const char *pUserId   = evhttp_find_header(&params, "user_id");
  const char *pWorkerId = evhttp_find_header(&params, "worker_id");
  const char *pHour     = evhttp_find_header(&params, "hour");

  if (pUserId == nullptr || pWorkerId == nullptr || pHour == nullptr) {
    evbuffer_add_printf(evb, "{\"err_no\":1,\"err_msg\":\"invalid args\"}");
    evhttp_send_reply(req, HTTP_OK, "OK", evb);
    goto finish;
  }

  evbuffer_add_printf(evb, "{\"err_no\":0,\"err_msg\":\"\",\"data\":{");
  server->getShareStats(evb, pUserId, pWorkerId, pHour);
  evbuffer_add_printf(evb, "}}");

  server->responseBytes_ += evbuffer_get_length(evb);
  evhttp_send_reply(req, HTTP_OK, "OK", evb);

finish:
  evhttp_clear_headers(&params);
  evbuffer_free(evb);
  if (query)
    free(query);
}

void ShareLogParserServer::getServerStatus(ShareLogParserServer::ServerStatus &s) {
  s.date_          = date_;
  s.uptime_        = (uint32_t)(time(nullptr) - uptime_);
  s.requestCount_  = requestCount_;
  s.responseBytes_ = responseBytes_;

  pthread_rwlock_rdlock(&rwlock_);
  shared_ptr<ShareLogParser> shareLogParser = shareLogParser_;
  pthread_rwlock_unlock(&rwlock_);

  WorkerKey pkey(0, 0);
  shared_ptr<ShareStatsDay> statsDayPtr = shareLogParser->getShareStatsDayHandler(pkey);

  s.stats.resize(2);
  statsDayPtr->getShareStatsDay(&(s.stats[0]));
  statsDayPtr->getShareStatsHour(atoi(date("%H").c_str()), &(s.stats[1]));
}

void ShareLogParserServer::httpdServerStatus(struct evhttp_request *req, void *arg) {
  evhttp_add_header(evhttp_request_get_output_headers(req),
                    "Content-Type", "text/json");
  ShareLogParserServer *server = (ShareLogParserServer *)arg;
  server->requestCount_++;

  struct evbuffer *evb = evbuffer_new();

  ShareLogParserServer::ServerStatus s;
  server->getServerStatus(s);

  double rejectRate0 = 0.0, rejectRate1 = 0.0;
  if (s.stats[0].shareReject_)
    rejectRate0 = s.stats[0].shareReject_ / (s.stats[0].shareAccept_ + s.stats[0].shareReject_);
  if (s.stats[1].shareReject_)
    rejectRate1 = s.stats[1].shareReject_ / (s.stats[1].shareAccept_ + s.stats[1].shareReject_);

  time_t now = time(nullptr);
  if (now % 3600 == 0)
    now += 2;  // just in case the denominator is zero

  evbuffer_add_printf(evb, "{\"err_no\":0,\"err_msg\":\"\","
                      "\"data\":{\"uptime\":\"%04u d %02u h %02u m %02u s\","
                      "\"request\":%" PRIu64",\"repbytes\":%" PRIu64","
                      "\"pool\":{\"today\":{"
                      "\"hashrate_t\":%lf,\"accept\":%" PRIu64","
                      "\"reject\":%" PRIu64",\"reject_rate\":%lf,\"earn\":%" PRId64"},"
                      "\"curr_hour\":{\"hashrate_t\":%lf,\"accept\":%" PRIu64","
                      "\"reject\":%" PRIu64",\"reject_rate\":%lf,\"earn\":%" PRId64"}}"
                      "}}",
                      s.uptime_/86400, (s.uptime_%86400)/3600,
                      (s.uptime_%3600)/60, s.uptime_%60,
                      s.requestCount_, s.responseBytes_,
                      // pool today
                      share2HashrateT(s.stats[0].shareAccept_, now % 86400),
                      s.stats[0].shareAccept_,
                      s.stats[0].shareReject_, rejectRate0, s.stats[0].earn_,
                      // pool current hour
                      share2HashrateT(s.stats[1].shareAccept_, now % 3600),
                      s.stats[1].shareAccept_,
                      s.stats[1].shareReject_, rejectRate1, s.stats[1].earn_);

  server->responseBytes_ += evbuffer_get_length(evb);
  evhttp_send_reply(req, HTTP_OK, "OK", evb);
  evbuffer_free(evb);
}

void ShareLogParserServer::runHttpd() {
  struct evhttp_bound_socket *handle;
  struct evhttp *httpd;

  base_ = event_base_new();
  httpd = evhttp_new(base_);

  evhttp_set_allowed_methods(httpd, EVHTTP_REQ_GET | EVHTTP_REQ_POST | EVHTTP_REQ_HEAD);
  evhttp_set_timeout(httpd, 5 /* timeout in seconds */);

  evhttp_set_cb(httpd, "/",             ShareLogParserServer::httpdServerStatus, this);
  evhttp_set_cb(httpd, "/share_stats",  ShareLogParserServer::httpdShareStats,   this);
  evhttp_set_cb(httpd, "/share_stats/", ShareLogParserServer::httpdShareStats,   this);

  handle = evhttp_bind_socket_with_handle(httpd, httpdHost_.c_str(), httpdPort_);
  if (!handle) {
    LOG(ERROR) << "couldn't bind to port: " << httpdPort_ << ", host: " << httpdHost_ << ", exiting.";
    return;
  }
  event_base_dispatch(base_);
}

bool ShareLogParserServer::setupThreadShareLogParser() {
  threadShareLogParser_ = thread(&ShareLogParserServer::runThreadShareLogParser, this);
  return true;
}

void ShareLogParserServer::runThreadShareLogParser() {
  LOG(INFO) << "thread sharelog parser start";

  time_t lastFlushDBTime = 0;

  while (running_) {
    // get ShareLogParser
    pthread_rwlock_rdlock(&rwlock_);
    shared_ptr<ShareLogParser> shareLogParser = shareLogParser_;
    pthread_rwlock_unlock(&rwlock_);

    // maybe last switch has been fail, we need to check and try again
    if (shareLogParser == nullptr) {
      if (initShareLogParser(time(nullptr)) == false) {
        LOG(ERROR) << "initShareLogParser fail";
        sleep(3);
        continue;
      }
    }
    assert(shareLogParser != nullptr);

    while (running_) {
      int64_t res = shareLogParser->processGrowingShareLog();
      if (res <= 0) {
        break;
      }
      DLOG(INFO) << "process share: " << res;
    }
    sleep(1);

    // flush data to db
    if (time(nullptr) > lastFlushDBTime + kFlushDBInterval_) {
      shareLogParser->flushToDB();  // will wait util all data flush to DB
      lastFlushDBTime = time(nullptr);
    }

    // check if need to switch bin file
    trySwithBinFile(shareLogParser);

  } /* while */

  LOG(INFO) << "thread sharelog parser stop";

  stop();  // if thread exit, we must call server to stop
}

void ShareLogParserServer::trySwithBinFile(shared_ptr<ShareLogParser> shareLogParser) {
  assert(shareLogParser != nullptr);

  const time_t now = time(nullptr);
  const time_t beginTs = now - (now % 86400);

  if (beginTs == date_)
    return;  // still today

  //
  // switch file when:
  //   1. today has been pasted at least 5 seconds
  //   2. last bin file has reached EOF
  //   3. new file exists
  //
  const string filePath = getStatsFilePath(dataDir_, now);
  if (now > beginTs + 5 &&
      shareLogParser->isReachEOF() &&
      fileExists(filePath.c_str()))
  {
    shareLogParser->flushToDB();  // flush data

    bool res = initShareLogParser(now);
    if (!res) {
      LOG(ERROR) << "trySwithBinFile fail";
    }
  }
}

void ShareLogParserServer::run() {
  // use current timestamp when first setup
  if (initShareLogParser(time(nullptr)) == false) {
    return;
  }

  if (setupThreadShareLogParser() == false) {
    return;
  }

  runHttpd();
}


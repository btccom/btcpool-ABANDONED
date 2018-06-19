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
#include "StatsHttpd.h"

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

#include <chainparams.h>
#include "BitcoinUtils.h"


////////////////////////////////  WorkerShares  ////////////////////////////////
template <class SHARE>
WorkerShares<SHARE>::WorkerShares(const int64_t workerId, const int32_t userId):
workerId_(workerId), userId_(userId), acceptCount_(0),
lastShareIP_(0), lastShareTime_(0),
acceptShareSec_(STATS_SLIDING_WINDOW_SECONDS),
rejectShareMin_(STATS_SLIDING_WINDOW_SECONDS/60)
{
  assert(STATS_SLIDING_WINDOW_SECONDS >= 3600);
}

template <class SHARE>
void WorkerShares<SHARE>::processShare(const SHARE &share) {
  ScopeLock sl(lock_);
  const time_t now = time(nullptr);
  if (now > share.timestamp_ + STATS_SLIDING_WINDOW_SECONDS) {
    return;
  }

  if (share.status_ == StratumStatus::ACCEPT || share.status_ == StratumStatus::SOLVED) {
    acceptCount_++;
    acceptShareSec_.insert(share.timestamp_,    share.shareDiff_);
  } else {
    rejectShareMin_.insert(share.timestamp_/60, share.shareDiff_);
  }

  lastShareIP_   = share.ip_;
  lastShareTime_ = share.timestamp_;
}

template <class SHARE>
WorkerStatus WorkerShares<SHARE>::getWorkerStatus() {
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

template <class SHARE>
void WorkerShares<SHARE>::getWorkerStatus(WorkerStatus &s) {
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

template <class SHARE>
bool WorkerShares<SHARE>::isExpired() {
  ScopeLock sl(lock_);
  return (lastShareTime_ + STATS_SLIDING_WINDOW_SECONDS) < (uint32_t)time(nullptr);
}


////////////////////////////////  StatsServerT  ////////////////////////////////
template <class SHARE>
StatsServerT<SHARE>::StatsServerT(const char *kafkaBrokers, const char *kafkaShareTopic, const char *kafkaCommonEventsTopic,
                                  const string &httpdHost, unsigned short httpdPort,
                                  const MysqlConnectInfo &poolDBInfo,
                                  const time_t kFlushDBInterval, const string &fileLastFlushTime,
                                  shared_ptr<DuplicateShareChecker<SHARE>> dupShareChecker):
running_(true), totalWorkerCount_(0), totalUserCount_(0), uptime_(time(nullptr)),
poolWorker_(0u/* worker id */, 0/* user id */),
kafkaConsumer_(kafkaBrokers, kafkaShareTopic, 0/* patition */),
kafkaConsumerCommonEvents_(kafkaBrokers, kafkaCommonEventsTopic, 0/* patition */),
poolDB_(poolDBInfo), poolDBCommonEvents_(poolDBInfo),
kFlushDBInterval_(kFlushDBInterval), isInserting_(false),
lastShareTime_(0), isInitializing_(true), lastFlushTime_(0),
fileLastFlushTime_(fileLastFlushTime), dupShareChecker_(dupShareChecker),
base_(nullptr), httpdHost_(httpdHost), httpdPort_(httpdPort),
requestCount_(0), responseBytes_(0)
{
  pthread_rwlock_init(&rwlock_, nullptr);
}

template <class SHARE>
StatsServerT<SHARE>::~StatsServerT() {
  stop();

  if (threadConsume_.joinable())
    threadConsume_.join();
 
  if (threadConsumeCommonEvents_.joinable())
    threadConsumeCommonEvents_.join();

  pthread_rwlock_destroy(&rwlock_);
}

template <class SHARE>
bool StatsServerT<SHARE>::init() {
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

template <class SHARE>
void StatsServerT<SHARE>::stop() {
  if (!running_)
    return;

  LOG(INFO) << "stop StatsServerT...";

  running_ = false;
  event_base_loopexit(base_, NULL);
}

template <class SHARE>
void StatsServerT<SHARE>::processShare(const SHARE &share) {
  const time_t now = time(nullptr);

  lastShareTime_ = share.timestamp_;
  height_ = share.height_;
  // ignore too old shares
  if (now > share.timestamp_ + STATS_SLIDING_WINDOW_SECONDS) {
    return;
  }
  poolWorker_.processShare(share);

  WorkerKey key1(share.userId_, share.workerHashId_);
  WorkerKey key2(share.userId_, 0/* 0 means all workers of this user */);
  _processShare(key1, key2, share);
}

template <class SHARE>
void StatsServerT<SHARE>::_processShare(WorkerKey &key1, WorkerKey &key2, const SHARE &share) {
  assert(key2.workerId_ == 0);  // key2 is user's total stats

  pthread_rwlock_rdlock(&rwlock_);
  auto itr1 = workerSet_.find(key1);
  auto itr2 = workerSet_.find(key2);
  pthread_rwlock_unlock(&rwlock_);

  shared_ptr<WorkerShares<SHARE>> workerShare1 = nullptr, workerShare2 = nullptr;

  if (itr1 != workerSet_.end()) {
    itr1->second->processShare(share);
  } else {
    workerShare1 = make_shared<WorkerShares<SHARE>>(share.workerHashId_, share.userId_);
    workerShare1->processShare(share);
  }

  if (itr2 != workerSet_.end()) {
    itr2->second->processShare(share);
  } else {
    workerShare2 = make_shared<WorkerShares<SHARE>>(share.workerHashId_, share.userId_);
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

template <class SHARE>
void StatsServerT<SHARE>::flushWorkersToDB() {
  LOG(INFO) << "flush mining workers to DB...";
  if (isInserting_) {
    LOG(WARNING) << "last flush is not finish yet, ingore";
    return;
  }

  isInserting_ = true;
  boost::thread t(boost::bind(&StatsServerT<SHARE>::_flushWorkersToDBThread, this));
}

template <class SHARE>
void StatsServerT<SHARE>::_flushWorkersToDBThread() {
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
    shared_ptr<WorkerShares<SHARE>> workerShare = itr->second;
    const WorkerStatus status = workerShare->getWorkerStatus();

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
                                     status.acceptCount_, status.lastShareIP_.toString().c_str(),
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
  
  lastFlushTime_ = time(nullptr);
  // save flush timestamp to file, for monitor system
  if (!fileLastFlushTime_.empty())
  	writeTime2File(fileLastFlushTime_.c_str(), lastFlushTime_);

finish:
  isInserting_ = false;
}

template <class SHARE>
void StatsServerT<SHARE>::removeExpiredWorkers() {
  size_t expiredCnt = 0;

  pthread_rwlock_wrlock(&rwlock_);  // write lock

  // delete all expired workers
  for (auto itr = workerSet_.begin(); itr != workerSet_.end(); ) {
    const int32_t userId   = itr->first.userId_;
    const int64_t workerId = itr->first.workerId_;
    shared_ptr<WorkerShares<SHARE>> workerShare = itr->second;

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

template <class SHARE>
void StatsServerT<SHARE>::getWorkerStatusBatch(const vector<WorkerKey> &keys,
                                       vector<WorkerStatus> &workerStatus) {
  workerStatus.resize(keys.size());

  vector<shared_ptr<WorkerShares<SHARE>> > ptrs;
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

template <class SHARE>
WorkerStatus StatsServerT<SHARE>::mergeWorkerStatus(const vector<WorkerStatus> &workerStatus) {
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

template <class SHARE>
void StatsServerT<SHARE>::consumeShareLog(rd_kafka_message_t *rkmessage) {
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

  SHARE share;
  if (rkmessage->len != sizeof(SHARE)) {
    LOG(ERROR) << "sharelog message size(" << rkmessage->len << ") is not: " << sizeof(SHARE);
    return;
  }
  memcpy((uint8_t *)&share, (const uint8_t *)rkmessage->payload, rkmessage->len);

  if (!share.isValid()) {
    LOG(ERROR) << "invalid share: " << share.toString();
    return;
  }
  if (dupShareChecker_ && !dupShareChecker_->addShare(share)) {
    LOG(INFO) << "duplicate share attack: " << share.toString();
    return;
  }

  processShare(share);
}

template <class SHARE>
bool StatsServerT<SHARE>::setupThreadConsume() {
  // kafkaConsumer_
  {
    //
    // assume we have 100,000 online workers and every share per 10 seconds,
    // so in 60 mins there will be 100000/10*3600 = 36,000,000 shares.
    // data size will be 36,000,000 * sizeof(SHARE) = 1,728,000,000 Bytes.
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
  threadConsume_ = thread(&StatsServerT<SHARE>::runThreadConsume, this);
  threadConsumeCommonEvents_ = thread(&StatsServerT<SHARE>::runThreadConsumeCommonEvents, this);
  
  return true;
}

template <class SHARE>
void StatsServerT<SHARE>::runThreadConsume() {
  LOG(INFO) << "start sharelog consume thread";
  time_t lastCleanTime     = time(nullptr);
  time_t lastFlushDBTime   = time(nullptr);

  const time_t kExpiredCleanInterval = 60*30;
  const int32_t kTimeoutMs = 1000;  // consumer timeout

  while (running_) {
    // don't flush database while consuming history shares.
    // otherwise, users' hashrate will be updated to 0 when statshttpd restarted.
    if (isInitializing_) {

      if (lastFlushDBTime + kFlushDBInterval_ < time(nullptr)) {
        // the initialization ends after consuming a share that generated in the last minute.
        if (lastShareTime_ + 60 < time(nullptr)) {
          LOG(INFO) << "consuming history shares: " << date("%F %T", lastShareTime_);
          lastFlushDBTime = time(nullptr);
        } else {
          isInitializing_ = false;
        }
      }

    } else {
      
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

template <class SHARE>
void StatsServerT<SHARE>::runThreadConsumeCommonEvents() {
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

template <class SHARE>
void StatsServerT<SHARE>::consumeCommonEvents(rd_kafka_message_t *rkmessage) {
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

template <class SHARE>
bool StatsServerT<SHARE>::updateWorkerStatus(const int32_t userId, const int64_t workerId,
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

template <class SHARE>
typename StatsServerT<SHARE>::ServerStatus StatsServerT<SHARE>::getServerStatus() {
  ServerStatus s;

  s.uptime_        = (uint32_t)(time(nullptr) - uptime_);
  s.requestCount_  = requestCount_;
  s.workerCount_   = totalWorkerCount_;
  s.userCount_     = totalUserCount_;
  s.responseBytes_ = responseBytes_;
  s.poolStatus_    = poolWorker_.getWorkerStatus();

  return s;
}

template <class SHARE>
void StatsServerT<SHARE>::httpdServerStatus(struct evhttp_request *req, void *arg) {
  evhttp_add_header(evhttp_request_get_output_headers(req),
                    "Content-Type", "text/json");
  StatsServerT *server = (StatsServerT *)arg;
  server->requestCount_++;

  struct evbuffer *evb = evbuffer_new();

  // service is initializing, response a error message
  if (server->isInitializing_) {
    evbuffer_add_printf(evb, "{\"err_no\":2,\"err_msg\":\"service is initializing...\"}");
    evhttp_send_reply(req, HTTP_OK, "OK", evb);
    evbuffer_free(evb);

    return;
  }
  
  StatsServerT<SHARE>::ServerStatus s = server->getServerStatus();

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

template <class SHARE>
void StatsServerT<SHARE>::httpdGetWorkerStatus(struct evhttp_request *req, void *arg) {
  evhttp_add_header(evhttp_request_get_output_headers(req),
                    "Content-Type", "text/json");
  StatsServerT *server = (StatsServerT *)arg;
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

  // service is initializing, response a error message
  if (server->isInitializing_) {
    evbuffer_add_printf(evb, "{\"err_no\":2,\"err_msg\":\"service is initializing...\"}");
    evhttp_send_reply(req, HTTP_OK, "OK", evb);
    evbuffer_free(evb);

    return;
  }

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

template <class SHARE>
void StatsServerT<SHARE>::getWorkerStatus(struct evbuffer *evb, const char *pUserId,
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
                        ",\"last_share_ip\":\"%s\",\"last_share_time\":%" PRIu64
                        "%s}",
                        (i == 0 ? "" : ","),
                        (isMerge ? 0 : keys[i].workerId_),
                        status.accept1m_, status.accept5m_, status.accept15m_, status.accept1h_,
                        status.reject15m_, status.reject1h_,
                        status.acceptCount_,
                        status.lastShareIP_.toString().c_str(), status.lastShareTime_,
                        extraInfo.length() ? extraInfo.c_str() : "");
    i++;
  }
}

template <class SHARE>
void StatsServerT<SHARE>::httpdGetFlushDBTime(struct evhttp_request *req, void *arg) {
  evhttp_add_header(evhttp_request_get_output_headers(req),
                    "Content-Type", "text/json");
  StatsServerT *server = (StatsServerT *)arg;
  server->requestCount_++;

  struct evbuffer *evb = evbuffer_new();

  // service is initializing, return
  if (server->isInitializing_) {
    evbuffer_add_printf(evb, "{\"err_no\":2,\"err_msg\":\"service is initializing...\"}");
    evhttp_send_reply(req, HTTP_OK, "OK", evb);
    evbuffer_free(evb);

    return;
  }
  
  evbuffer_add_printf(evb, "{\"err_no\":0,\"err_msg\":\"\",\"data\":{\"flush_db_time\":%" PRId64 "}}", (int64_t)server->lastFlushTime_);

  server->responseBytes_ += evbuffer_get_length(evb);
  evhttp_send_reply(req, HTTP_OK, "OK", evb);
  evbuffer_free(evb);
}

template <class SHARE>
void StatsServerT<SHARE>::runHttpd() {
  struct evhttp_bound_socket *handle;
  struct evhttp *httpd;

  base_ = event_base_new();
  httpd = evhttp_new(base_);

  evhttp_set_allowed_methods(httpd, EVHTTP_REQ_GET | EVHTTP_REQ_POST | EVHTTP_REQ_HEAD);
  evhttp_set_timeout(httpd, 5 /* timeout in seconds */);

  evhttp_set_cb(httpd, "/",               StatsServerT<SHARE>::httpdServerStatus, this);
  evhttp_set_cb(httpd, "/worker_status",  StatsServerT<SHARE>::httpdGetWorkerStatus, this);
  evhttp_set_cb(httpd, "/worker_status/", StatsServerT<SHARE>::httpdGetWorkerStatus, this);
  evhttp_set_cb(httpd, "/flush_db_time",  StatsServerT<SHARE>::httpdGetFlushDBTime, this);

  handle = evhttp_bind_socket_with_handle(httpd, httpdHost_.c_str(), httpdPort_);
  if (!handle) {
    LOG(ERROR) << "couldn't bind to port: " << httpdPort_ << ", host: " << httpdHost_ << ", exiting.";
    return;
  }
  event_base_dispatch(base_);
}

template <class SHARE>
void StatsServerT<SHARE>::run() {
  if (setupThreadConsume() == false) {
    return;
  }

  runHttpd();
}

///////////////  template instantiation ///////////////
// Without this, some linking errors will issued.
// If you add a new derived class of Share, add it at the following.
template class WorkerShares<ShareBitcoin>;
template class WorkerShares<ShareEth>;

template class StatsServerT<ShareBitcoin>;
template class StatsServerT<ShareEth>;

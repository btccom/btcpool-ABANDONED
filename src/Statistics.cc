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
#include "Stratum.h"

#include <algorithm>
#include <string>

#include <boost/algorithm/string.hpp>
#include <boost/thread.hpp>

////////////////////////////////  WorkerShares  ////////////////////////////////
WorkerShares::WorkerShares(const int64_t workerId, const int32_t userId):
workerId_(workerId), userId_(userId), acceptCount_(0),
lastShareIP_(0), lastShareTime_(0),
acceptShareSec_(STATS_SLIDING_WINDOW_SECONDS),
rejectShareMin_(STATS_SLIDING_WINDOW_SECONDS/60)
{
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

  s.acceptCount_   = acceptCount_;
  s.lastShareIP_   = lastShareIP_;
  s.lastShareTime_ = lastShareTime_;
}

bool WorkerShares::isExpired() {
  ScopeLock sl(lock_);
  return (lastShareTime_ + STATS_SLIDING_WINDOW_SECONDS) < (uint32_t)time(nullptr);
}


////////////////////////////////  StatsServer  ////////////////////////////////
StatsServer::StatsServer(const char *kafkaBrokers, string httpdHost,
                         unsigned short httpdPort, const MysqlConnectInfo &poolDBInfo):
running_(true), totalWorkerCount_(0), totalUserCount_(0), upTime_(time(nullptr)),
poolWorker_(0u/* worker id */, 0/* user id */),
kafkaConsumer_(kafkaBrokers, KAFKA_TOPIC_SHARE_LOG, 0/* patition */),
poolDBInfo_(poolDBInfo), isInserting_(false),
base_(nullptr), httpdHost_(httpdHost), httpdPort_(httpdPort),
requestCount_(0), responseBytes_(0)
{
  pthread_rwlock_init(&rwlock_, nullptr);
}

StatsServer::~StatsServer() {
  stop();

  if (threadConsume_.joinable())
    threadConsume_.join();

  pthread_rwlock_destroy(&rwlock_);
}

void StatsServer::stop() {
  if (!running_)
    return;

  running_ = false;
  event_base_loopexit(base_, NULL);
}

void StatsServer::processShare(const Share &share) {
  const time_t now = time(nullptr);
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
  MySQLConnection db(poolDBInfo_);

  // merge two table items
  const string mergeSQL = "INSERT INTO `mining_workers` "
  " SELECT * FROM `mining_workers_tmp` "
  " ON DUPLICATE KEY "
  " UPDATE "
  "  `mining_workers`.`accept_1m`      =`mining_workers_tmp`.`accept_1m`, "
  "  `mining_workers`.`accept_5m`      =`mining_workers_tmp`.`accept_5m`, "
  "  `mining_workers`.`accept_15m`     =`mining_workers_tmp`.`accept_15m`, "
  "  `mining_workers`.`reject_15m`     =`mining_workers_tmp`.`reject_15m`,"
  "  `mining_workers`.`accept_count`   =`mining_workers_tmp`.`accept_count`,"
  "  `mining_workers`.`last_share_ip`  =`mining_workers_tmp`.`last_share_ip`,"
  "  `mining_workers`.`last_share_time`=`mining_workers_tmp`.`last_share_time`,"
  "  `mining_workers`.`updated_at`     =`mining_workers_tmp`.`updated_at` ";
  // fields for table.mining_workers
  const string fields = "`worker_id`,`uid`,`accept_1m`, `accept_5m`,"
  "`accept_15m`, `reject_15m`, `accept_count`, `last_share_ip`,"
  " `last_share_time`, `created_at`, `updated_at`";
  // values for multi-insert sql
  vector<string> values;

  if (!db.ping()) {
    LOG(ERROR) << "can't connect to pool DB";
    goto finish;
  }

  // get all workes status
  pthread_rwlock_rdlock(&rwlock_);  // read lock
  for (auto itr = workerSet_.begin(); itr != workerSet_.end(); itr++) {
    const int32_t userId   = itr->first.userId_;
    const int64_t workerId = itr->first.workerId_;
    shared_ptr<WorkerShares> workerShare = itr->second;
    const WorkerStatus status = workerShare->getWorkerStatus();

    char ipStr[INET_ADDRSTRLEN] = {0};
    inet_ntop(AF_INET, &(status.lastShareIP_), ipStr, INET_ADDRSTRLEN);
    const string nowStr = date("%F %T", time(nullptr));

    values.push_back(Strings::Format("%" PRId64",%d,%" PRIu64",%" PRIu64","
                                     "%" PRIu64",%" PRIu64",%d,\"%s\","
                                     "\"%s\",\"%s\",\"%s\"",
                                     workerId, userId,
                                     status.accept1m_, status.accept5m_, status.accept15m_,
                                     status.reject15m_, status.acceptCount_, ipStr,
                                     date("%F %T", status.lastShareTime_).c_str(),
                                     nowStr.c_str(), nowStr.c_str()));
  }
  pthread_rwlock_unlock(&rwlock_);

  if (values.size() == 0) {
    LOG(INFO) << "no active workers";
    goto finish;
  }

  if (!db.execute("DROP TABLE IF EXISTS `mining_workers_tmp`;")) {
    LOG(ERROR) << "DROP TABLE `mining_workers_tmp` failure";
    goto finish;
  }
  if (!db.execute("CREATE TABLE `mining_workers_tmp` like `mining_workers`;")) {
    LOG(ERROR) << "TRUNCATE TABLE `mining_workers_tmp` failure";
    goto finish;
  }

  if (!multiInsert(db, "mining_workers_tmp", fields, values)) {
    LOG(ERROR) << "mul-insert table.mining_workers_tmp failure";
    goto finish;
  }

  // merge items
  if (!db.update(mergeSQL)) {
    LOG(ERROR) << "merge mining_workers failure";
    goto finish;
  }
  LOG(INFO) << "flush mining workers to DB... done";

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
  const int32_t kConsumeLatestN = 10000 * (900 / 10);
  if (kafkaConsumer_.setup(RD_KAFKA_OFFSET_TAIL(kConsumeLatestN)) == false) {
    LOG(INFO) << "setup consumer fail";
    return false;
  }

  if (!kafkaConsumer_.checkAlive()) {
    LOG(ERROR) << "kafka brokers is not alive";
    return false;
  }

  threadConsume_ = thread(&StatsServer::runThreadConsume, this);
  return true;
}

void StatsServer::runThreadConsume() {
  LOG(INFO) << "start sharelog consume thread";
  time_t lastCleanTime   = time(nullptr);
  time_t lastFlushDBTime = time(nullptr);

  const time_t kFlushDBInterval      = 20;
  const time_t kExpiredCleanInterval = 60*30;
  const int32_t kTimeoutMs = 1000;

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
    if (lastFlushDBTime + kFlushDBInterval < time(nullptr)) {
      // will use thread to flush data to DB
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
}

StatsServer::ServerStatus StatsServer::getServerStatus() {
  ServerStatus s;

  s.uptime_        = (uint32_t)(time(nullptr) - upTime_);
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

  evbuffer_add_printf(evb, "{\"error_no\":0,\"error_msg\":\"\","
                      "\"result\":{\"uptime\":\"%02u d %02u h %02u m %02u s\","
                      "\"request\":%" PRIu64",\"repbytes\":%" PRIu64","
                      "\"pool\":{\"accept\":[%" PRIu64",%" PRIu64",%" PRIu64"],"
                      "\"reject\":[0,0,%" PRIu64"],\"accept_count\":%" PRIu32","
                      "\"workers\":%" PRIu64",\"users\":%" PRIu64""
                      "}}}",
                      s.uptime_/86400, (s.uptime_%86400)/3600,
                      (s.uptime_%3600)/60, s.uptime_%60,
                      s.requestCount_, s.responseBytes_,
                      s.poolStatus_.accept1m_, s.poolStatus_.accept5m_,
                      s.poolStatus_.accept15m_, s.poolStatus_.reject15m_,
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
  const char *query = nullptr;
  struct evkeyvalq params;

  if (rMethod == EVHTTP_REQ_GET) {
    // GET
    struct evhttp_uri *uri = evhttp_uri_parse(evhttp_request_get_uri(req));
    if (uri != nullptr) {
      query = evhttp_uri_get_query(uri);
    }
  }
  else if (rMethod == EVHTTP_REQ_POST) {
    // POST
    struct evbuffer *evbIn = evhttp_request_get_input_buffer(req);
    string data;
    data.resize(evbuffer_get_length(evbIn));
    evbuffer_copyout(evbIn, (uint8_t *)data.data(), evbuffer_get_length(evbIn));
    data.push_back('\0');  // evbuffer is not include '\0'
    query = data.c_str();
  }

  evhttp_parse_query_str(query, &params);
  const char *pUserId   = evhttp_find_header(&params, "user_id");
  const char *pWorkerId = evhttp_find_header(&params, "worker_id");
  const char *pIsMerge  = evhttp_find_header(&params, "is_merge");

  struct evbuffer *evb = evbuffer_new();

  if (pUserId == nullptr || pWorkerId == nullptr) {
    evbuffer_add_printf(evb, "{\"error_no\":1,\"error_msg\":\"invalid args\"}");
    evhttp_send_reply(req, HTTP_OK, "OK", evb);
    evbuffer_free(evb);
    return;
  }

  evbuffer_add_printf(evb, "{\"error_no\":0,\"error_msg\":\"\",\"result\":[");
  server->getWorkerStatus(evb, pUserId, pWorkerId, pIsMerge);
  evbuffer_add_printf(evb, "]}");

  server->responseBytes_ += evbuffer_get_length(evb);
  evhttp_send_reply(req, HTTP_OK, "OK", evb);
  evbuffer_free(evb);
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

  bool isFirst = true;
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
                        "%s{\"worker_id\":%" PRId64",\"accept\":[%" PRIu64",%" PRIu64",%" PRIu64"]"
                        ",\"reject\":[0,0,%" PRIu64"],\"accept_count\":%" PRIu32""
                        ",\"last_share_ip\":\"%s\",\"last_share_time\":%u"
                        "%s}",
                        (isFirst ? "" : ","),
                        (isMerge ? 0 : keys[i].workerId_),
                        status.accept1m_, status.accept5m_, status.accept15m_,
                        status.reject15m_, status.acceptCount_,
                        ipStr, status.lastShareTime_,
                        extraInfo.length() ? extraInfo.c_str() : "");
    isFirst = false;
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



//////////////////////////////  StatsFileWriter  ///////////////////////////////
StatsFileWriter::StatsFileWriter(const char *kafkaBrokers,
                                 const string &dataDir)
:running_(true), dataDir_(dataDir),
hlConsumer_(kafkaBrokers, KAFKA_TOPIC_SHARE_LOG, 0/* patition */,
            "StatsFileWriter" /* kafka group.id */)
{
  if (dataDir_.length() > 0 && *dataDir_.rbegin() != '/') {
    dataDir_ += "/";  // add '/'
  }
}

StatsFileWriter::~StatsFileWriter() {
  if (!running_)
    return;

  running_ = false;

  // close file handlers
  for (auto & itr : fileHandlers_) {
    fclose(itr.second);
  }
  fileHandlers_.clear();
}

FILE* StatsFileWriter::getFileHandler(uint32_t ts) {
  if (fileHandlers_.find(ts) != fileHandlers_.end()) {
    return fileHandlers_[ts];
  }

  // filename: sharelog-2016-07-12.bin
  const string filePath = Strings::Format("%ssharelog-%s.bin", dataDir_.c_str(),
                                          date("%F", ts).c_str());
  // append mode, bin file
  LOG(INFO) << "fopen: " << filePath;
  FILE *f = fopen(filePath.c_str(), "ab");
  if (f == nullptr) {
    LOG(FATAL) << "fopen file fail: " << filePath;
    return nullptr;
  }

  fileHandlers_[ts] = f;
  return f;
}

void StatsFileWriter::consumeShareLog(rd_kafka_message_t *rkmessage) {
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

void StatsFileWriter::tryCloseOldHanders() {
  while (fileHandlers_.size() > 3) {
    // Maps (and sets) are sorted, so the first element is the smallest,
    // and the last element is the largest.
    auto itr = fileHandlers_.begin();

    LOG(INFO) << "fclose file handler, date: " << date("%F", itr->first);
    fclose(itr->second);

    fileHandlers_.erase(itr);
  }
}

bool StatsFileWriter::flushToDisk() {
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

void StatsFileWriter::run() {
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
}



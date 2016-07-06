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


////////////////////////////////  WorkerShares  ////////////////////////////////
WorkerShares::WorkerShares(const int64_t workerId, const int32_t userId):
workerId_(workerId), userId_(userId),
acceptShareSec_(STATS_SLIDING_WINDOW_SECONDS),
rejectShareMin_(STATS_SLIDING_WINDOW_SECONDS/60)
{
  acceptCount_   = 0;
  lastShareIP_   = 0;
  lastShareTime_ = 0;
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

  s.workId_ = workerId_;
  s.userId_ = userId_;

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

  s.workId_ = workerId_;
  s.userId_ = userId_;

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
                         unsigned short httpdPort):
running_(true),
kafkaConsumer_(kafkaBrokers, KAFKA_TOPIC_SHARE_LOG, 0/* patition */),
base_(nullptr), httpdHost_(httpdHost), httpdPort_(httpdPort)
{
  pthread_rwlock_init(rwlock_, nullptr);
}

StatsServer::~StatsServer() {
  stop();

  if (threadConsume_.joinable())
    threadConsume_.join();

  pthread_rwlock_destroy(rwlock_);
}

void StatsServer::processShare(const Share &share) {
  const time_t now = time(nullptr);
  if (now > share.timestamp_ + STATS_SLIDING_WINDOW_SECONDS) {
    return;
  }

  WorkerKey key(share.userId_, share.workerHashId_);

  pthread_rwlock_rdlock(rwlock_);
  auto itr = workerSet_.find(key);
  pthread_rwlock_unlock(rwlock_);

  if (itr != workerSet_.end()) {
    itr->second->processShare(share);
    return;
  }

  shared_ptr<WorkerShares> workerShare = make_shared<WorkerShares>(share.workerHashId_,
                                                                   share.userId_);
  workerShare->processShare(share);

  pthread_rwlock_wrlock(rwlock_);
  workerSet_[key] = workerShare;
  pthread_rwlock_unlock(rwlock_);
}

void StatsServer::getWorkerStatusBatch(const vector<WorkerKey> &keys,
                                       vector<WorkerStatus> &workerStatus) {
  assert(workerStatus.size() == keys.size());

  vector<shared_ptr<WorkerShares> > ptrs;
  ptrs.resize(keys.size());

  // find all shared pointer
  pthread_rwlock_rdlock(rwlock_);
  for (size_t i = 0; i < keys.size(); i++) {
    auto itr = workerSet_.find(keys[i]);
    if (itr == workerSet_.end()) {
      ptrs[i] = nullptr;
    } else {
      ptrs[i] = itr->second;
    }
  }
  pthread_rwlock_unlock(rwlock_);

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

  s.userId_ = workerStatus[0].userId_;
  // 'workId_' will be ignore
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

  threadConsume_ = thread(&StatsServer::runThreadConsume, this);
  return true;
}

void StatsServer::runThreadConsume() {
  LOG(INFO) << "start sharelog consume thread";

  const int32_t kTimeoutMs = 1000;
  while (running_) {
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

void StatsServer::httpdServerStatus(struct evhttp_request *req, void *arg) {
  StatsServer *server = (StatsServer *)arg;
  struct evbuffer *buf = evbuffer_new();
  evhttp_send_reply(req, HTTP_OK, "OK", buf);
}

void StatsServer::httpdGetWorkerStatus(struct evhttp_request *req, void *arg) {
  StatsServer *server = (StatsServer *)arg;
}

void StatsServer::runHttpd() {
  struct evhttp_bound_socket *handle;
  struct evhttp *httpd;

  base_ = event_base_new();
  httpd = evhttp_new(base_);

  evhttp_set_cb(httpd, "/",               StatsServer::httpdServerStatus, this);
  evhttp_set_cb(httpd, "/work_status",    StatsServer::httpdGetWorkerStatus, this);
  evhttp_set_cb(httpd, "/work_status/",   StatsServer::httpdGetWorkerStatus, this);

  handle = evhttp_bind_socket_with_handle(httpd, httpdHost_.c_str(), httpdPort_);
  if (!handle) {
    LOG(ERROR) << "couldn't bind to port: " << httpdPort_ << ", host: " << httpdHost_ << ", exiting.";
    return;
  }

  event_base_dispatch(base_);
}

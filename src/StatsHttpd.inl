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

#include "utilities_js.hpp"

#include <boost/algorithm/string.hpp>
#include <boost/thread.hpp>

#include <event2/http.h>
#include <event2/buffer.h>
#include <event2/keyvalq_struct.h>

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
  if (now > share.timestamp() + STATS_SLIDING_WINDOW_SECONDS) {
    return;
  }

  if (StratumStatus::isAccepted(share.status())) {
    acceptCount_++;
    acceptShareSec_.insert(share.timestamp(),    share.sharediff());
  } else {
    rejectShareMin_.insert(share.timestamp()/60, share.sharediff());
  }

  lastShareIP_.fromString(share.ip());
  lastShareTime_ = share.timestamp();
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
                                  const MysqlConnectInfo *poolDBInfo, const RedisConnectInfo *redisInfo,
                                  const uint32_t redisConcurrency, const string &redisKeyPrefix,
                                  const int redisKeyExpire, const int redisPublishPolicy, const int redisIndexPolicy,
                                  const time_t kFlushDBInterval, const string &fileLastFlushTime,
                                  shared_ptr<DuplicateShareChecker<SHARE>> dupShareChecker):
running_(true), totalWorkerCount_(0), totalUserCount_(0), uptime_(time(nullptr)),
poolWorker_(0u/* worker id */, 0/* user id */),
kafkaConsumer_(kafkaBrokers, kafkaShareTopic, 0/* patition */),
kafkaConsumerCommonEvents_(kafkaBrokers, kafkaCommonEventsTopic, 0/* patition */),
poolDB_(nullptr), poolDBCommonEvents_(nullptr),
redisCommonEvents_(nullptr), redisConcurrency_(redisConcurrency),
redisKeyPrefix_(redisKeyPrefix), redisKeyExpire_(redisKeyExpire),
redisPublishPolicy_(redisPublishPolicy), redisIndexPolicy_(redisIndexPolicy),
kFlushDBInterval_(kFlushDBInterval),
isInserting_(false), isUpdateRedis_(false),
lastShareTime_(0), isInitializing_(true), lastFlushTime_(0),
fileLastFlushTime_(fileLastFlushTime), dupShareChecker_(dupShareChecker),
base_(nullptr), httpdHost_(httpdHost), httpdPort_(httpdPort),
requestCount_(0), responseBytes_(0)
{
  if (poolDBInfo != nullptr) {
    poolDB_ = new MySQLConnection(*poolDBInfo);
    poolDBCommonEvents_ = new MySQLConnection(*poolDBInfo);
  }

  if (redisInfo != nullptr) {
    redisCommonEvents_ = new RedisConnection(*redisInfo);
    
    for (uint32_t i=0; i<redisConcurrency; i++) {
      RedisConnection *redis = new RedisConnection(*redisInfo);
      redisGroup_.push_back(redis);
    }
  }

  pthread_rwlock_init(&rwlock_, nullptr);
}

template <class SHARE>
StatsServerT<SHARE>::~StatsServerT() {
  stop();

  if (threadConsume_.joinable())
    threadConsume_.join();
 
  if (threadConsumeCommonEvents_.joinable())
    threadConsumeCommonEvents_.join();

  if (poolDB_ != nullptr) {
    poolDB_->close();
    delete poolDB_;
    poolDB_ = nullptr;
  }

  if (poolDBCommonEvents_ != nullptr) {
    poolDBCommonEvents_->close();
    delete poolDBCommonEvents_;
    poolDBCommonEvents_ = nullptr;
  }

  if (redisCommonEvents_ != nullptr) {
    redisCommonEvents_->close();
    delete redisCommonEvents_;
    redisCommonEvents_ = nullptr;
  }

  while (!redisGroup_.empty()) {
    RedisConnection *redis = redisGroup_.back();
    if (redis != nullptr) {
      redis->close();
      delete redis;
    }
    redisGroup_.pop_back();
  }

  pthread_rwlock_destroy(&rwlock_);
}

template <class SHARE>
string StatsServerT<SHARE>::getRedisKeyMiningWorker(const int32_t userId, const int64_t workerId) {
    string key = redisKeyPrefix_;
    key += "mining_workers/pu/";
    key += std::to_string(userId);
    key += "/wk/";
    key += std::to_string(workerId);
    return key;
}

template <class SHARE>
string StatsServerT<SHARE>::getRedisKeyMiningWorker(const int32_t userId) {
    string key = redisKeyPrefix_;
    key += "mining_workers/pu/";
    key += std::to_string(userId);
    key += "/all";
    return key;
}

template <class SHARE>
string StatsServerT<SHARE>::getRedisKeyIndex(const int32_t userId, const string &indexName) {
    string key = redisKeyPrefix_;
    key += "mining_workers/pu/";
    key += std::to_string(userId);
    key += "/sort/";
    key += indexName;
    return key;
}

template <class SHARE>
bool StatsServerT<SHARE>::init() {
  if (poolDB_ != nullptr) {
    if (!poolDB_->ping()) {
      LOG(INFO) << "db ping failure";
      return false;
    }

  // check db conf (only poolDB_ needs)
  	string value = poolDB_->getVariable("max_allowed_packet");
    if (atoi(value.c_str()) < 16 * 1024 *1024) {
      LOG(INFO) << "db conf 'max_allowed_packet' is less than 16*1024*1024";
      return false;
    }
  }

  if (poolDBCommonEvents_ != nullptr && !poolDBCommonEvents_->ping()) {
    LOG(INFO) << "common events db ping failure";
    return false;
  }

  if (redisCommonEvents_ != nullptr && !redisCommonEvents_->ping()) {
    LOG(INFO) << "common events redis ping failure";
    return false;
  }

  for (size_t i=0; i<redisGroup_.size(); i++) {
    if (redisGroup_[i] != nullptr && !redisGroup_[i]->ping()) {
      LOG(INFO) << "redis " << i << " in redisGroup ping failure";
      return false;
    }
  }

  return true;
}

template <class SHARE>
void StatsServerT<SHARE>::stop() {
  if (!running_)
    return;

  LOG(INFO) << "stop StatsServer...";

  running_ = false;
  event_base_loopexit(base_, NULL);
}

template <class SHARE>
void StatsServerT<SHARE>::processShare(const SHARE &share) {
  const time_t now = time(nullptr);

  lastShareTime_ = share.timestamp();

  // ignore too old shares
  if (now > share.timestamp() + STATS_SLIDING_WINDOW_SECONDS) {
    return;
  }
  poolWorker_.processShare(share);

  WorkerKey key(share.userid(), share.workerhashid());
  _processShare(key, share);
}

template <class SHARE>
void StatsServerT<SHARE>::_processShare(WorkerKey &key, const SHARE &share) {
  const  int32_t userId = key.userId_;

  pthread_rwlock_rdlock(&rwlock_);
  auto workerItr = workerSet_.find(key);
  auto userItr = userSet_.find(userId);
  pthread_rwlock_unlock(&rwlock_);

  shared_ptr<WorkerShares<SHARE>> workerShare = nullptr, userShare = nullptr;

  if (workerItr != workerSet_.end()) {
    workerItr->second->processShare(share);
  } else {
    workerShare = make_shared<WorkerShares<SHARE>>(share.workerhashid(), share.userid());
    workerShare->processShare(share);
  }

  if (userItr != userSet_.end()) {
    userItr->second->processShare(share);
  } else {
    userShare = make_shared<WorkerShares<SHARE>>(share.workerhashid(), share.userid());
    userShare->processShare(share);
  }

  if (workerShare != nullptr || userShare != nullptr) {
    pthread_rwlock_wrlock(&rwlock_);    // write lock
    if (workerShare != nullptr) {
      workerSet_[key] = workerShare;
      totalWorkerCount_++;
      userWorkerCount_[key.userId_]++;
    }
    if (userShare != nullptr) {
      userSet_[userId] = userShare;
      totalUserCount_++;
    }
    pthread_rwlock_unlock(&rwlock_);
  }
}

template <class SHARE>
void StatsServerT<SHARE>::flushWorkersAndUsersToRedis() {
  LOG(INFO) << "flush to redis...";
  if (isUpdateRedis_) {
    LOG(WARNING) << "last redis flush is not finish yet, ignore";
    return;
  }

  isUpdateRedis_ = true;
  boost::thread t(boost::bind(&StatsServerT<SHARE>::_flushWorkersAndUsersToRedisThread, this));
}

template <class SHARE>
void StatsServerT<SHARE>::_flushWorkersAndUsersToRedisThread() {
  std::vector<boost::thread> threadPool;

  assert(redisGroup_.size() == redisConcurrency_);
  for (uint32_t i=0; i<redisConcurrency_; i++) {
    threadPool.push_back(
      boost::thread(boost::bind(&StatsServerT<SHARE>::_flushWorkersAndUsersToRedisThread, this, i))
    );
  }

  for (auto &t : threadPool) {
    if (t.joinable()) {
      t.join();
    }
  }

  pthread_rwlock_rdlock(&rwlock_);
  LOG(INFO) << "flush to redis... done, " << workerSet_.size() << " workers, " << userSet_.size() << " users";
  pthread_rwlock_unlock(&rwlock_);

  isUpdateRedis_ = false;
}

template <class SHARE>
void StatsServerT<SHARE>::_flushWorkersAndUsersToRedisThread(uint32_t threadStep) {
  if (!checkRedis(threadStep)) {
    return;
  }
  flushWorkersToRedis(threadStep);
  flushUsersToRedis(threadStep);
}

template <class SHARE>
bool StatsServerT<SHARE>::checkRedis(uint32_t threadStep) {
  if (threadStep > redisGroup_.size() - 1) {
    LOG(ERROR) << "checkRedis(" << threadStep << "): "
               << "threadStep out of range, should less than " << threadStep << "!";
    return false;
  }

  {
    RedisConnection *redis = redisGroup_[threadStep];

    if (!redis->ping()) {
      LOG(ERROR) << "can't connect to pool redis " << threadStep;
      return false;
    }
  }

  return true;
}

template <class SHARE>
void StatsServerT<SHARE>::flushWorkersToRedis(uint32_t threadStep) {
  RedisConnection *redis = redisGroup_[threadStep];
  size_t workerCounter = 0;
  std::unordered_map<int32_t /*userId*/, WorkerIndexBuffer> indexBufferMap;

  pthread_rwlock_rdlock(&rwlock_);  // read lock
  LOG(INFO) << "redis (thread " << threadStep << "): flush workers, rd locked";
  
  size_t stepSize = workerSet_.size() / redisConcurrency_;
  if (workerSet_.size() % redisConcurrency_ != 0) {
    // +1 to avoid missing the last few items.
    // Example: 5 / 2 = 2. Each thread handles 2 items and the fifth was missing.
    stepSize++;
  }

  size_t offsetBegin = stepSize * threadStep;

  auto itr = workerSet_.begin();

  // move to the beginning position
  for (size_t i=0; i<offsetBegin && itr!=workerSet_.end(); i++, itr++);

  // flush all workes status in a stepSize
  for (size_t i=0; i<stepSize && itr!=workerSet_.end(); i++, itr++) {
    workerCounter++;

    const int32_t userId   = itr->first.userId_;
    const int64_t workerId = itr->first.workerId_;
    shared_ptr<WorkerShares<SHARE>> workerShare = itr->second;
    const WorkerStatus status = workerShare->getWorkerStatus();

    string key = getRedisKeyMiningWorker(userId, workerId);

    // update info
    redis->prepare({"HMSET", key,
                      "accept_1m", std::to_string(status.accept1m_),
                      "accept_5m", std::to_string(status.accept5m_),
                      "accept_15m", std::to_string(status.accept15m_),
                      "reject_15m", std::to_string(status.reject15m_),
                      "accept_1h", std::to_string(status.accept1h_),
                      "reject_1h", std::to_string(status.reject1h_),
                      "accept_count", std::to_string(status.acceptCount_),
                      "last_share_ip", status.lastShareIP_.toString(),
                      "last_share_time", std::to_string(status.lastShareTime_),
                      "updated_at", std::to_string(time(nullptr))
                  });
    // set key expire
    if (redisKeyExpire_ > 0) {
      redis->prepare({"EXPIRE", key, std::to_string(redisKeyExpire_)});
    }
    // publish notification
    if (redisPublishPolicy_ & REDIS_PUBLISH_WORKER_UPDATE) {
      redis->prepare({"PUBLISH", key, "1"});
    }

    // add index to buffer
    if (redisIndexPolicy_ != REDIS_INDEX_NONE) {
      addIndexToBuffer(indexBufferMap[userId], workerId, status);
    }
  }

  pthread_rwlock_unlock(&rwlock_); // unlock
  LOG(INFO) << "redis (thread " << threadStep << "): flush workers, rd unlock";

  if (workerCounter == 0) {
    LOG(INFO) << "redis (thread " << threadStep << "): no active workers";
    return;
  }

  for (size_t i=0; i<workerCounter; i++) {
    // update info
    {
      RedisResult r = redis->execute();
      if (r.type() != REDIS_REPLY_STATUS || r.str() != "OK") {
        LOG(INFO) << "redis (thread " << threadStep << ") HMSET failed, "
                               << "item index: " << i << ", "
                               << "reply type: " << r.type() << ", "
                               << "reply str: " << r.str();
      }
    }
    // set key expire
    if (redisKeyExpire_ > 0) {
      RedisResult r = redis->execute();
      if (r.type() != REDIS_REPLY_INTEGER || r.integer() != 1) {
        LOG(INFO) << "redis (thread " << threadStep << ") EXPIRE failed, "
                                 << "item index: " << i << ", "
                                 << "reply type: " << r.type() << ", "
                                 << "reply integer: " << r.integer() << ","
                                 << "reply str: " << r.str();
      }
    }
    // notification
    if (redisPublishPolicy_ & REDIS_PUBLISH_WORKER_UPDATE) {
      RedisResult r = redis->execute();
      if (r.type() != REDIS_REPLY_INTEGER) {
        LOG(INFO) << "redis (thread " << threadStep << ") PUBLISH failed, "
                                 << "item index: " << i << ", "
                                 << "reply type: " << r.type() << ", "
                                 << "reply str: " << r.str();
      }
    }
  }

  // flush indexes
  if (redisIndexPolicy_ != REDIS_INDEX_NONE) {
    flushIndexToRedis(redis, indexBufferMap);
  }

  LOG(INFO) << "flush workers to redis (thread " << threadStep << ") done, workers: " << workerCounter;
  return;
}

template <class SHARE>
void StatsServerT<SHARE>::flushIndexToRedis(RedisConnection *redis,
                    std::unordered_map<int32_t /*userId*/, WorkerIndexBuffer> &indexBufferMap) {

  for (auto itr = indexBufferMap.begin(); itr != indexBufferMap.end(); itr++) {
    flushIndexToRedis(redis, itr->second, itr->first);
  }

}

template <class SHARE>
void StatsServerT<SHARE>::flushIndexToRedis(RedisConnection *redis, WorkerIndexBuffer &buffer, const int32_t userId) {
  // accept_1m
  if (redisIndexPolicy_ & REDIS_INDEX_ACCEPT_1M) {
    buffer.accept1m_.insert(buffer.accept1m_.begin(), {"ZADD", getRedisKeyIndex(userId, "accept_1m")});
    flushIndexToRedis(redis, buffer.accept1m_);
  }
  // accept_5m
  if (redisIndexPolicy_ & REDIS_INDEX_ACCEPT_5M) {
    buffer.accept5m_.insert(buffer.accept5m_.begin(), {"ZADD", getRedisKeyIndex(userId, "accept_5m")});
    flushIndexToRedis(redis, buffer.accept5m_);
  }
  // accept_15m
  if (redisIndexPolicy_ & REDIS_INDEX_ACCEPT_15M) {
    buffer.accept15m_.insert(buffer.accept15m_.begin(), {"ZADD", getRedisKeyIndex(userId, "accept_15m")});
    flushIndexToRedis(redis, buffer.accept15m_);
  }
  // reject_15m
  if (redisIndexPolicy_ & REDIS_INDEX_REJECT_15M) {
    buffer.reject15m_.insert(buffer.reject15m_.begin(), {"ZADD", getRedisKeyIndex(userId, "reject_15m")});
    flushIndexToRedis(redis, buffer.reject15m_);
  }
  // accept_1h
  if (redisIndexPolicy_ & REDIS_INDEX_ACCEPT_1H) {
    buffer.accept1h_.insert(buffer.accept1h_.begin(), {"ZADD", getRedisKeyIndex(userId, "accept_1h")});
    flushIndexToRedis(redis, buffer.accept1h_);
  }
  // reject_1h
  if (redisIndexPolicy_ & REDIS_INDEX_REJECT_1H) {
    buffer.reject1h_.insert(buffer.reject1h_.begin(), {"ZADD", getRedisKeyIndex(userId, "reject_1h")});
    flushIndexToRedis(redis, buffer.reject1h_);
  }
  // accept_count
  if (redisIndexPolicy_ & REDIS_INDEX_ACCEPT_COUNT) {
    buffer.acceptCount_.insert(buffer.acceptCount_.begin(), {"ZADD", getRedisKeyIndex(userId, "accept_count")});
    flushIndexToRedis(redis, buffer.acceptCount_);
  }
  // last_share_ip
  if (redisIndexPolicy_ & REDIS_INDEX_LAST_SHARE_IP) {
    buffer.lastShareIP_.insert(buffer.lastShareIP_.begin(), {"ZADD", getRedisKeyIndex(userId, "last_share_ip")});
    flushIndexToRedis(redis, buffer.lastShareIP_);
  }
  // last_share_time
  if (redisIndexPolicy_ & REDIS_INDEX_LAST_SHARE_TIME) {
    buffer.lastShareTime_.insert(buffer.lastShareTime_.begin(), {"ZADD", getRedisKeyIndex(userId, "last_share_time")});
    flushIndexToRedis(redis, buffer.lastShareTime_);
  }
}

template <class SHARE>
void StatsServerT<SHARE>::addIndexToBuffer(WorkerIndexBuffer &buffer, const int64_t workerId, const WorkerStatus &status) {
  // accept_1m
  if (redisIndexPolicy_ & REDIS_INDEX_ACCEPT_1M) {
    buffer.accept1m_.push_back(std::to_string(status.accept1m_));
    buffer.accept1m_.push_back(std::to_string(workerId));
  }
  // accept_5m
  if (redisIndexPolicy_ & REDIS_INDEX_ACCEPT_5M) {
    buffer.accept5m_.push_back(std::to_string(status.accept5m_));
    buffer.accept5m_.push_back(std::to_string(workerId));
  }
  // accept_15m
  if (redisIndexPolicy_ & REDIS_INDEX_ACCEPT_15M) {
    buffer.accept15m_.push_back(std::to_string(status.accept15m_));
    buffer.accept15m_.push_back(std::to_string(workerId));
  }
  // reject_15m
  if (redisIndexPolicy_ & REDIS_INDEX_REJECT_15M) {
    buffer.reject15m_.push_back(std::to_string(status.reject15m_));
    buffer.reject15m_.push_back(std::to_string(workerId));
  }
  // accept_1h
  if (redisIndexPolicy_ & REDIS_INDEX_ACCEPT_1H) {
    buffer.accept1h_.push_back(std::to_string(status.accept1h_));
    buffer.accept1h_.push_back(std::to_string(workerId));
  }
  // reject_1h
  if (redisIndexPolicy_ & REDIS_INDEX_REJECT_1H) {
    buffer.reject1h_.push_back(std::to_string(status.reject1h_));
    buffer.reject1h_.push_back(std::to_string(workerId));
  }
  // accept_count
  if (redisIndexPolicy_ & REDIS_INDEX_ACCEPT_COUNT) {
    buffer.acceptCount_.push_back(std::to_string(status.acceptCount_));
    buffer.acceptCount_.push_back(std::to_string(workerId));
  }
  // last_share_ip
  if (redisIndexPolicy_ & REDIS_INDEX_LAST_SHARE_IP) {
    buffer.lastShareIP_.push_back(std::to_string(status.lastShareIP_.addrUint64[1]));
    buffer.lastShareIP_.push_back(std::to_string(workerId));
  }
  // last_share_time
  if (redisIndexPolicy_ & REDIS_INDEX_LAST_SHARE_TIME) {
    buffer.lastShareTime_.push_back(std::to_string(status.lastShareTime_));
    buffer.lastShareTime_.push_back(std::to_string(workerId));
  }

  buffer.size_ ++;
}

template <class SHARE>
void StatsServerT<SHARE>::flushIndexToRedis(RedisConnection *redis, const std::vector<string> &commandVector) {
  redis->prepare(commandVector);
  RedisResult r = redis->execute();
  if (r.type() != REDIS_REPLY_INTEGER) {
    LOG(INFO) << "redis ZADD failed, "
              << "item key: " << commandVector[1] << ", "
              << "reply type: " << r.type() << ", "
              << "reply str: " << r.str();
  }
}

template <class SHARE>
void StatsServerT<SHARE>::flushUsersToRedis(uint32_t threadStep) {
  RedisConnection *redis = redisGroup_[threadStep];
  size_t userCounter = 0;

  pthread_rwlock_rdlock(&rwlock_);  // read lock
  LOG(INFO) << "redis (thread " << threadStep << "): flush users, rd locked";

  size_t stepSize = userSet_.size() / redisConcurrency_;
  if (userSet_.size() % redisConcurrency_ != 0) {
    // +1 to avoid missing the last few items.
    // Example: 5 / 2 = 2. Each thread handles 2 items and the fifth was missing.
    stepSize++;
  }

  size_t offsetBegin = stepSize * threadStep;

  auto itr = userSet_.begin();

  // move to the beginning position
  for (size_t i=0; i<offsetBegin && itr!=userSet_.end(); i++, itr++);

  // flush all users status in a stepSize
  for (size_t i=0; i<stepSize && itr!=userSet_.end(); i++, itr++) {
    userCounter++;

    const int32_t userId   = itr->first;
    shared_ptr<WorkerShares<SHARE>> workerShare = itr->second;
    const WorkerStatus status = workerShare->getWorkerStatus();
    const int32_t workerCount = userWorkerCount_[userId];

    string key = getRedisKeyMiningWorker(userId);

    // update info
    redis->prepare({"HMSET", key,
                      "worker_count", std::to_string(workerCount),
                      "accept_1m", std::to_string(status.accept1m_),
                      "accept_5m", std::to_string(status.accept5m_),
                      "accept_15m", std::to_string(status.accept15m_),
                      "reject_15m", std::to_string(status.reject15m_),
                      "accept_1h", std::to_string(status.accept1h_),
                      "reject_1h", std::to_string(status.reject1h_),
                      "accept_count", std::to_string(status.acceptCount_),
                      "last_share_ip", status.lastShareIP_.toString(),
                      "last_share_time", std::to_string(status.lastShareTime_),
                      "updated_at", std::to_string(time(nullptr))
                  });
    // set key expire
    if (redisKeyExpire_ > 0) {
      redis->prepare({"EXPIRE", key, std::to_string(redisKeyExpire_)});
    }
    // publish notification
    if (redisPublishPolicy_ & REDIS_PUBLISH_USER_UPDATE) {
      redis->prepare({"PUBLISH", key, std::to_string(workerCount)});
    }
  }

  pthread_rwlock_unlock(&rwlock_); // unlock
  LOG(INFO) << "redis (thread " << threadStep << "): flush users, rd unlock";

  if (userCounter == 0) {
    LOG(INFO) << "redis (thread " << threadStep << "): no active users";
    return;
  }

  for (size_t i=0; i<userCounter; i++) {
    // update info
    {
      RedisResult r = redis->execute();
      if (r.type() != REDIS_REPLY_STATUS || r.str() != "OK") {
        LOG(INFO) << "redis (thread " << threadStep << ") HMSET failed, "
                               << "item index: " << i << ", "
                               << "reply type: " << r.type() << ", "
                               << "reply str: " << r.str();
      }
    }
    // set key expire
    if (redisKeyExpire_ > 0) {
      RedisResult r = redis->execute();
      if (r.type() != REDIS_REPLY_INTEGER || r.integer() != 1) {
        LOG(INFO) << "redis (thread " << threadStep << ") EXPIRE failed, "
                                 << "item index: " << i << ", "
                                 << "reply type: " << r.type() << ", "
                                 << "reply integer: " << r.integer() << ","
                                 << "reply str: " << r.str();
      }
    }
    // publish notification
    if (redisPublishPolicy_ & REDIS_PUBLISH_USER_UPDATE) {
      RedisResult r = redis->execute();
      if (r.type() != REDIS_REPLY_INTEGER) {
        LOG(INFO) << "redis (thread " << threadStep << ") PUBLISH failed, "
                                 << "item index: " << i << ", "
                                 << "reply type: " << r.type() << ", "
                                 << "reply str: " << r.str();
      }
    }
  }

  LOG(INFO) << "flush users to redis (thread " << threadStep << ") done, users: " << userCounter;
  return;
}

template <class SHARE>
void StatsServerT<SHARE>::flushWorkersAndUsersToDB() {
  LOG(INFO) << "flush to DB...";
  if (isInserting_) {
    LOG(WARNING) << "last DB flush is not finish yet, ignore";
    return;
  }

  isInserting_ = true;
  boost::thread t(boost::bind(&StatsServerT<SHARE>::_flushWorkersAndUsersToDBThread, this));
}

template <class SHARE>
void StatsServerT<SHARE>::_flushWorkersAndUsersToDBThread() {
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
  size_t workerCounter = 0;
  size_t userCounter = 0;

  if (!poolDB_->ping()) {
    LOG(ERROR) << "can't connect to pool DB";
    goto finish;
  }

  pthread_rwlock_rdlock(&rwlock_);  // read lock
  LOG(INFO) << "flush DB: rd locked";

  // get all workes status
  for (auto itr = workerSet_.begin(); itr != workerSet_.end(); itr++) {
    workerCounter++;

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

  // get all users status
  for (auto itr = userSet_.begin(); itr != userSet_.end(); itr++) {
    userCounter++;

    const int32_t userId   = itr->first;
    const int64_t workerId = 0;
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
  LOG(INFO) << "flush DB: rd unlock";

  if (values.size() == 0) {
    LOG(INFO) << "flush to DB: no active workers";
    goto finish;
  }

  if (!poolDB_->execute("DROP TEMPORARY TABLE IF EXISTS `mining_workers_tmp`;")) {
    LOG(ERROR) << "DROP TEMPORARY TABLE `mining_workers_tmp` failure";
    goto finish;
  }
  if (!poolDB_->execute("CREATE TEMPORARY TABLE `mining_workers_tmp` like `mining_workers`;")) {
    LOG(ERROR) << "CREATE TEMPORARY TABLE `mining_workers_tmp` failure";
    // something went wrong with the current mysql connection, try to reconnect.
    poolDB_->reconnect();
    goto finish;
  }

  if (!multiInsert(*poolDB_, "mining_workers_tmp", fields, values)) {
    LOG(ERROR) << "mul-insert table.mining_workers_tmp failure";
    goto finish;
  }

  // merge items
  if (!poolDB_->update(mergeSQL)) {
    LOG(ERROR) << "merge mining_workers failure";
    goto finish;
  }
  LOG(INFO) << "flush to DB... done, workers: " << workerCounter << ", users: " << userCounter;

  lastFlushTime_ = time(nullptr);
  // save flush timestamp to file, for monitor system
  if (!fileLastFlushTime_.empty())
  	writeTime2File(fileLastFlushTime_.c_str(), lastFlushTime_);

finish:
  isInserting_ = false;
}

template <class SHARE>
void StatsServerT<SHARE>::removeExpiredWorkers() {
  size_t expiredWorkerCount = 0;
  size_t expiredUserCount = 0;

  pthread_rwlock_wrlock(&rwlock_);  // write lock

  // delete all expired workers
  for (auto itr = workerSet_.begin(); itr != workerSet_.end(); ) {
    const int32_t userId   = itr->first.userId_;
    shared_ptr<WorkerShares<SHARE>> workerShare = itr->second;

    if (workerShare->isExpired()) {
      itr = workerSet_.erase(itr);

      expiredWorkerCount++;
      totalWorkerCount_--;
      userWorkerCount_[userId]--;
      
      if (userWorkerCount_[userId] <= 0) {
        userWorkerCount_.erase(userId);
      }
    } else {
      itr++;
    }
  }

  // delete all expired users
  for (auto itr = userSet_.begin(); itr != userSet_.end(); ) {
    shared_ptr<WorkerShares<SHARE>> workerShare = itr->second;

    if (workerShare->isExpired()) {
      itr = userSet_.erase(itr);

      expiredUserCount++;
      totalUserCount_--;
    } else {
      itr++;
    }
  }

  pthread_rwlock_unlock(&rwlock_);

  LOG(INFO) << "removed expired workers: " << expiredWorkerCount << ", users: " << expiredUserCount;
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
    if (keys[i].workerId_ == 0) {
      // find user
      auto itr = userSet_.find(keys[i].userId_);
      if (itr == userSet_.end()) {
        ptrs[i] = nullptr;
      } else {
        ptrs[i] = itr->second;
      }
    } else {
      // find worker
      auto itr = workerSet_.find(keys[i]);
      if (itr == workerSet_.end()) {
        ptrs[i] = nullptr;
      } else {
        ptrs[i] = itr->second;
      }
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

  if (!share.UnserializeWithVersion((const uint8_t *)(rkmessage->payload), rkmessage->len)) {
    LOG(ERROR) << "parse share from kafka message failed rkmessage->len = "<< rkmessage->len ;
    return;
  }



  if (!share.isValid()) {
    LOG(ERROR) << "invalid share!" ;
    return;
  }
  if (dupShareChecker_ && !dupShareChecker_->addShare(share)) {
    LOG(INFO) << "duplicate share attack: " ;
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
    bool noNewShares = false;

    {
      //
      // consume message
      //
      rd_kafka_message_t *rkmessage;
      rkmessage = kafkaConsumer_.consumer(kTimeoutMs);

      // timeout, most of time it's not nullptr and set an error:
      //          rkmessage->err == RD_KAFKA_RESP_ERR__PARTITION_EOF
      if (rkmessage != nullptr) {
        // consume share log (lastShareTime_ will be updated)
        consumeShareLog(rkmessage);
        rd_kafka_message_destroy(rkmessage);  /* Return message to rdkafka */
      } else {
        noNewShares = true;
      }
    }

    // don't flush database while consuming history shares.
    // otherwise, users' hashrate will be updated to 0 when statshttpd restarted.
    if (isInitializing_) {
      if (lastFlushDBTime + kFlushDBInterval_ < time(nullptr)) {
        // the initialization state ends after consuming a share that generated in the last minute.
        // If no shares received at the first consumption (lastShareTime_ == 0), the initialization state ends too.
        if (!noNewShares && lastShareTime_ + 60 < time(nullptr)) {
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
        if (poolDB_ != nullptr) {
          flushWorkersAndUsersToDB();
        }
        if (redisGroup_.size() > 0) {
          flushWorkersAndUsersToRedis();
        }
        lastFlushDBTime = time(nullptr);
      }
    }

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

    if (poolDBCommonEvents_ != nullptr) {
      updateWorkerStatusToDB(userId, workerId, workerName.c_str(), minerAgent.c_str());
    }
    if (redisCommonEvents_ != nullptr) {
      updateWorkerStatusToRedis(userId, workerId, workerName.c_str(), minerAgent.c_str());
    }
  }

}

template <class SHARE>
bool StatsServerT<SHARE>::updateWorkerStatusToRedis(const int32_t userId, const int64_t workerId,
                                     const char *workerName, const char *minerAgent) {
  string key = getRedisKeyMiningWorker(userId, workerId);

  // update info
  {
    redisCommonEvents_->prepare({"HMSET", key,
                      "worker_name", workerName,
                      "miner_agent", minerAgent,
                      "updated_at", std::to_string(time(nullptr))
                    });
    RedisResult r = redisCommonEvents_->execute();

    if (r.type() != REDIS_REPLY_STATUS || r.str() != "OK") {
      LOG(INFO) << "redis HMSET failed, item key: " << key << ", "
                                    << "reply type: " << r.type() << ", "
                                    << "reply str: " << r.str();

      // try ping & reconnect redis, so last update may success
      if (!redisCommonEvents_->ping()) {
        LOG(ERROR) << "updateWorkerStatusToRedis: can't connect to pool redis";
      }

      return false;
    }
  }

  // set key expire
  if (redisKeyExpire_ > 0) {
    redisCommonEvents_->prepare({"EXPIRE", key, std::to_string(redisKeyExpire_)});
    RedisResult r = redisCommonEvents_->execute();

    if (r.type() != REDIS_REPLY_INTEGER || r.integer() != 1) {
      LOG(INFO) << "redis EXPIRE failed, item key: " << key << ", "
                              << "reply type: " << r.type() << ", "
                              << "reply integer: " << r.integer() << ","
                              << "reply str: " << r.str();

      // try ping & reconnect redis, so last update may success
      if (!redisCommonEvents_->ping()) {
        LOG(ERROR) << "updateWorkerStatusToRedis: can't connect to pool redis";
      }

      return false;
    }
  }

  // update index
  if (redisIndexPolicy_ & REDIS_INDEX_WORKER_NAME) {
    updateWorkerStatusIndexToRedis(userId, "worker_name", workerName, std::to_string(workerId));
  }
  if (redisIndexPolicy_ & REDIS_INDEX_MINER_AGENT) {
    updateWorkerStatusIndexToRedis(userId, "miner_agent", minerAgent, std::to_string(workerId));
  }

  // publish notification
  if (redisPublishPolicy_ & REDIS_PUBLISH_WORKER_UPDATE) {
    redisCommonEvents_->prepare({"PUBLISH", key, "0"});
    RedisResult r = redisCommonEvents_->execute();

    if (r.type() != REDIS_REPLY_INTEGER) {
      LOG(INFO) << "redis PUBLISH failed, item key: " << key << ", "
                                << "reply type: " << r.type() << ", "
                                << "reply str: " << r.str();

      // try ping & reconnect redis, so last update may success
      if (!redisCommonEvents_->ping()) {
        LOG(ERROR) << "updateWorkerStatusToRedis: can't connect to pool redis";
      }

      return false;
    }
  }

  return true;
}

template <class SHARE>
void StatsServerT<SHARE>::updateWorkerStatusIndexToRedis(const int32_t userId, const string &key,
                                                 const string &score, const string &value) {
  
  // convert string to number
  uint64_t scoreRank = getAlphaNumRank(score);

  redisCommonEvents_->prepare({"ZADD", getRedisKeyIndex(userId, key), std::to_string(scoreRank), value});
  RedisResult r = redisCommonEvents_->execute();

  if (r.type() != REDIS_REPLY_INTEGER) {
    LOG(INFO) << "redis ZADD failed, item key: " << key << ", "
              << "reply type: " << r.type() << ", "
              << "reply str: " << r.str();
  }
}

template <class SHARE>
bool StatsServerT<SHARE>::updateWorkerStatusToDB(const int32_t userId, const int64_t workerId,
                                     const char *workerName, const char *minerAgent) {
  string sql;
  char **row = nullptr;
  MySQLResult res;
  const string nowStr = date("%F %T");

  // find the miner
  sql = Strings::Format("SELECT `group_id` FROM `mining_workers` "
                        " WHERE `puid`=%d AND `worker_id`= %" PRId64"",
                        userId, workerId);
  poolDBCommonEvents_->query(sql, res);

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
  }

  if (poolDBCommonEvents_->execute(sql) == false) {
    LOG(ERROR) << "insert worker name failure";

    // try to reconnect mysql, so last update may success
    if (!poolDBCommonEvents_->reconnect()) {
      LOG(ERROR) << "updateWorkerStatusToDB: can't connect to pool DB";
    }

    return false;
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
  StatsServerT<SHARE> *server = (StatsServerT<SHARE> *)arg;
  server->requestCount_++;

  struct evbuffer *evb = evbuffer_new();

  // service is initializing, return a error
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
  StatsServerT<SHARE> *server = (StatsServerT<SHARE> *)arg;
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

  // service is initializing, return
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
  StatsServerT<SHARE> *server = (StatsServerT<SHARE> *)arg;
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

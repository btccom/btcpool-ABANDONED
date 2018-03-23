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
#include  <iostream>
#include  <iomanip>
#include "StratumServer.h"

#include "Common.h"
#include "Kafka.h"
#include "MySQLConnection.h"
#include "Utils.h"

#include <arith_uint256.h>
#include <utilstrencodings.h>
#include <hash.h>
#include <inttypes.h>
#include "rsk/RskSolvedShareData.h"

#include "utilities_js.hpp"

using namespace std;

#ifndef WORK_WITH_STRATUM_SWITCHER

//////////////////////////////// SessionIDManager //////////////////////////////
SessionIDManager::SessionIDManager(const uint8_t serverId) :
serverId_(serverId), count_(0), allocIdx_(0)
{
  sessionIds_.reset();
}

bool SessionIDManager::ifFull() {
  ScopeLock sl(lock_);
  return _ifFull();
}

bool SessionIDManager::_ifFull() {
  if (count_ >= (int32_t)(MAX_SESSION_INDEX_SERVER + 1)) {
    return true;
  }
  return false;
}

bool SessionIDManager::allocSessionId(uint32_t *sessionID) {
  ScopeLock sl(lock_);

  if (_ifFull())
    return false;

  // find an empty bit
  while (sessionIds_.test(allocIdx_) == true) {
    allocIdx_++;
    if (allocIdx_ > MAX_SESSION_INDEX_SERVER) {
      allocIdx_ = 0;
    }
  }

  // set to true
  sessionIds_.set(allocIdx_, true);
  count_++;

  *sessionID = (((uint32_t)serverId_ << 24) | allocIdx_);
  return true;
}

void SessionIDManager::freeSessionId(uint32_t sessionId) {
  ScopeLock sl(lock_);

  const uint32_t idx = (sessionId & 0x00FFFFFFu);
  sessionIds_.set(idx, false);
  count_--;
}

#endif // #ifndef WORK_WITH_STRATUM_SWITCHER


////////////////////////////////// JobRepository ///////////////////////////////
JobRepository::JobRepository(const char *kafkaBrokers, const char *consumerTopic, const string &fileLastNotifyTime, Server *server):
running_(true),
kafkaConsumer_(kafkaBrokers, consumerTopic, 0/*patition*/),
server_(server), fileLastNotifyTime_(fileLastNotifyTime),
kMaxJobsLifeTime_(300),
kMiningNotifyInterval_(30),  // TODO: make as config arg
lastJobSendTime_(0),
serverType_(BTC) // TODO: make as config arg
{
  assert(kMiningNotifyInterval_ < kMaxJobsLifeTime_);
}

JobRepository::~JobRepository() {
  if (threadConsume_.joinable())
    threadConsume_.join();
}

shared_ptr<StratumJobEx> JobRepository::getStratumJobEx(const uint64_t jobId) {
  ScopeLock sl(lock_);
  auto itr = exJobs_.find(jobId);
  if (itr != exJobs_.end()) {
    return itr->second;
  }
  return nullptr;
}

shared_ptr<StratumJobEx> JobRepository::getLatestStratumJobEx() {
  ScopeLock sl(lock_);
  if (exJobs_.size()) {
    return exJobs_.rbegin()->second;
  }
  LOG(WARNING) << "getLatestStratumJobEx fail";
  return nullptr;
}

void JobRepository::stop() {
  if (!running_) {
    return;
  }
  running_ = false;
  LOG(INFO) << "stop job repository";
}

bool JobRepository::setupThreadConsume() {
  const int32_t kConsumeLatestN = 1;

  // we need to consume the latest one
  map<string, string> consumerOptions;
  consumerOptions["fetch.wait.max.ms"] = "10";
  if (kafkaConsumer_.setup(RD_KAFKA_OFFSET_TAIL(kConsumeLatestN),
                           &consumerOptions) == false) {
    LOG(INFO) << "setup consumer fail";
    return false;
  }

  if (!kafkaConsumer_.checkAlive()) {
    LOG(ERROR) << "kafka brokers is not alive";
    return false;
  }

  threadConsume_ = thread(&JobRepository::runThreadConsume, this);
  return true;
}

void JobRepository::runThreadConsume() {
  LOG(INFO) << "start job repository consume thread";

  const int32_t kTimeoutMs = 1000;
  while (running_) {
    rd_kafka_message_t *rkmessage;
    rkmessage = kafkaConsumer_.consumer(kTimeoutMs);

    // timeout, most of time it's not nullptr and set an error:
    //          rkmessage->err == RD_KAFKA_RESP_ERR__PARTITION_EOF
    if (rkmessage == nullptr) {
      continue;
    }

    // consume stratum job
    consumeStratumJob(rkmessage);
    rd_kafka_message_destroy(rkmessage);  /* Return message to rdkafka */

    // check if we need to send mining notify
    checkAndSendMiningNotify();

    tryCleanExpiredJobs();
  }
  LOG(INFO) << "stop job repository consume thread";
}

void JobRepository::broadcastStratumJob(StratumJob *sjob) {
  bool isClean = false;
  if (latestPrevBlockHash_ != sjob->prevHash_) {
    isClean = true;
    latestPrevBlockHash_ = sjob->prevHash_;
    LOG(INFO) << "received new height stratum job, height: " << sjob->height_
    << ", prevhash: " << sjob->prevHash_.ToString();
  }

  bool isRskClean = sjob->isRskCleanJob_;

  // 
  // The `clean_jobs` field should be `true` ONLY IF a new block found in Bitcoin blockchains.
  // Most miner implements will never submit their previous shares if the field is `true`.
  // There will be a huge loss of hashrates and earnings if the field is often `true`.
  // 
  // There is the definition from <https://slushpool.com/help/manual/stratum-protocol>:
  // 
  // clean_jobs - When true, server indicates that submitting shares from previous jobs
  // don't have a sense and such shares will be rejected. When this flag is set,
  // miner should also drop all previous jobs.
  // 
  shared_ptr<StratumJobEx> exJob(createStratumJobEx(serverType_, sjob, isClean));
  {
    ScopeLock sl(lock_);

    if (isClean) {
      // mark all jobs as stale, should do this before insert new job
      for (auto it : exJobs_) {
        it.second->markStale();
      }
    }

    // insert new job
    exJobs_[sjob->jobId_] = exJob;
  }

  // if job has clean flag, call server to send job
  if (isClean || isRskClean) {
    sendMiningNotify(exJob);
    return;
  }

  // if last job is an empty block job(clean=true), we need to send a
  // new non-empty job as quick as possible.
  if (isClean == false && exJobs_.size() >= 2) {
    auto itr = exJobs_.rbegin();
    shared_ptr<StratumJobEx> exJob1 = itr->second;
    itr++;
    shared_ptr<StratumJobEx> exJob2 = itr->second;

    if (exJob2->isClean_ == true &&
        exJob2->sjob_->merkleBranch_.size() == 0 &&
        exJob1->sjob_->merkleBranch_.size() != 0) {
      sendMiningNotify(exJob);
    }
  }
}

StratumJob* JobRepository::createStratumJob() {
  StratumJob* sjob = nullptr;
  switch(serverType_) {
    case BTC:
      sjob = new StratumJob();
      break;
    case ETH:
      sjob = new StratumJobEth();
      break;
  }
  return sjob;
}

void JobRepository::consumeStratumJob(rd_kafka_message_t *rkmessage) {
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

  StratumJob *sjob = createStratumJob();
  bool res = sjob->unserializeFromJson((const char *)rkmessage->payload,
                                       rkmessage->len);
  if (res == false) {
    LOG(ERROR) << "unserialize stratum job fail";
    delete sjob;
    return;
  }
  // make sure the job is not expired.
  if (jobId2Time(sjob->jobId_) + 60 < time(nullptr)) {
    LOG(ERROR) << "too large delay from kafka to receive topic 'StratumJob'";
    delete sjob;
    return;
  }
  // here you could use Map.find() without lock, it's sure
  // that everyone is using this Map readonly now
  if (exJobs_.find(sjob->jobId_) != exJobs_.end()) {
    LOG(ERROR) << "jobId already existed";
    delete sjob;
    return;
  }

  broadcastStratumJob(sjob);
}

StratumJobEx* JobRepository::createStratumJobEx(StratumServerType type, StratumJob *sjob, bool isClean){
  StratumJobEx* job = NULL;

  switch (type) {
    case BTC: {
      job = new StratumJobEx(sjob, isClean);
      break;
    }
    case ETH: {
      job = new StratumJobExEth(sjob, isClean);
      break;
    }
  }

  if (job)
    job->makeMiningNotifyStr();

  return job;
}

void JobRepository::markAllJobsAsStale() {
  ScopeLock sl(lock_);
  for (auto it : exJobs_) {
    it.second->markStale();
  }
}

void JobRepository::checkAndSendMiningNotify() {
  // last job is 'expried', send a new one
  if (exJobs_.size() &&
      lastJobSendTime_ + kMiningNotifyInterval_ <= time(nullptr))
  {
    shared_ptr<StratumJobEx> exJob = exJobs_.rbegin()->second;
    sendMiningNotify(exJob);
  }
}

void JobRepository::sendMiningNotify(shared_ptr<StratumJobEx> exJob) {
  static uint64_t lastJobId = 0;
  if (lastJobId == exJob->sjob_->jobId_) {
    LOG(ERROR) << "no new jobId, ignore to send mining notify";
    return;
  }

  // send job to all clients
  server_->sendMiningNotifyToAll(exJob);
  lastJobSendTime_ = time(nullptr);
  lastJobId = exJob->sjob_->jobId_;

  // write last mining notify time to file
  if (!fileLastNotifyTime_.empty())
    writeTime2File(fileLastNotifyTime_.c_str(), (uint32_t)lastJobSendTime_);
}

void JobRepository::tryCleanExpiredJobs() {
  ScopeLock sl(lock_);

  const uint32_t nowTs = (uint32_t)time(nullptr);
  while (exJobs_.size()) {
    // Maps (and sets) are sorted, so the first element is the smallest,
    // and the last element is the largest.
    auto itr = exJobs_.begin();

    const time_t jobTime = (time_t)(itr->first >> 32);
    if (nowTs < jobTime + kMaxJobsLifeTime_) {
      break;  // not expired
    }

    // remove expired job
    exJobs_.erase(itr);

    LOG(INFO) << "remove expired stratum job, id: " << itr->first
    << ", time: " << date("%F %T", jobTime);
  }
}

////////////////////////////////// JobRepositoryEth ///////////////////////////////
JobRepositoryEth::JobRepositoryEth(const char *kafkaBrokers, const char *consumerTopic, const string &fileLastNotifyTime, Server *server):
JobRepository(kafkaBrokers, consumerTopic, fileLastNotifyTime, server),
light_(nullptr), 
nextLight_(nullptr),
epochs_(0xffffffffffffffff)
{
  serverType_ = ETH;
  kMaxJobsLifeTime_ = 60;
}

void JobRepositoryEth::broadcastStratumJob(StratumJob *sjob) {
  LOG(INFO) << "broadcastStratumJob " << sjob->jobId_;
  bool isClean = true;
  
  shared_ptr<StratumJobEx> exJob(createStratumJobEx(serverType_, sjob, isClean));
  {
    ScopeLock sl(lock_);

    if (isClean) {
      // mark all jobs as stale, should do this before insert new job
      for (auto it : exJobs_) {
        it.second->markStale();
      }
    }

    // insert new job
    exJobs_[sjob->jobId_] = exJob;
  }

  //send job first
  sendMiningNotify(exJob);
  //then, create light for verification
  newLight(dynamic_cast<StratumJobEth*>(sjob));
}

JobRepositoryEth::~JobRepositoryEth() {
  deleteLight();
}

void JobRepositoryEth::newLight(StratumJobEth* job) {
  if (nullptr == job)
    return;

  newLight(job->blockNumber_);
}

void JobRepositoryEth::newLight(uint64_t blkNum)
{
  uint64_t const epochs = blkNum / ETHASH_EPOCH_LENGTH;
  //same seed do nothing
  if (epochs == epochs_)
    return;
  epochs_ = epochs;

  LOG(INFO) << "creating light for blk num... " << blkNum;
  time_t now = time(nullptr);
  time_t elapse;
  {
    ScopeLock sl(lightLock_);
    //deleteLightNoLock();
    if (nullptr == nextLight_)
      light_ = ethash_light_new(blkNum);
    else {
      //get pre-generated light if exists
      ethash_light_delete(light_);
      light_ =  nextLight_;
    }
    if (nullptr == light_)
      LOG(FATAL) << "create light for blk num: " << blkNum << " failed";
    else
    {
      elapse = time(nullptr) - now;
      LOG(INFO) << "create light for blk num: " << blkNum << " takes " << elapse << " seconds";
    }
  }

  now = time(nullptr);
  uint64_t nextBlkNum = blkNum + ETHASH_EPOCH_LENGTH;
  LOG(INFO) << "creating light for blk num... " << nextBlkNum;
  nextLight_ = ethash_light_new(nextBlkNum);
  elapse = time(nullptr) - now;
  LOG(INFO) << "create light for blk num: " << nextBlkNum << " takes " << elapse << " seconds";
}

void JobRepositoryEth::deleteLight()
{
  ScopeLock sl(lightLock_);
  deleteLightNoLock();
}

void JobRepositoryEth::deleteLightNoLock() {
  if (light_ != nullptr) {
    ethash_light_delete(light_);
    light_ = nullptr;
  }

  if (nextLight_ != nullptr) {
    ethash_light_delete(nextLight_);
    nextLight_ = nullptr;
  }
}

bool JobRepositoryEth::compute(ethash_h256_t const header, uint64_t nonce, ethash_return_value_t &r)
{
  ScopeLock sl(lightLock_);
  if (light_ != nullptr)
  {
    r = ethash_light_compute(light_, header, nonce);
    // LOG(INFO) << "ethash_light_compute: " << r.success << ", result: ";
    // for (int i = 0; i < 32; ++i)
    //   LOG(INFO) << hex << (int)r.result.b[i];

    // LOG(INFO) << "mixed hash: ";
    // for (int i = 0; i < 32; ++i)
    //   LOG(INFO) << hex << (int)r.mix_hash.b[i];

    return r.success;
  }
  return false;
}

//////////////////////////////////// JobRepositorySia /////////////////////////////////
JobRepositorySia::JobRepositorySia(const char *kafkaBrokers, const char *consumerTopic, const string &fileLastNotifyTime, Server *server) : 
JobRepository(kafkaBrokers, consumerTopic, fileLastNotifyTime, server)
{
}

//////////////////////////////////// UserInfo /////////////////////////////////
UserInfo::UserInfo(const string &apiUrl, Server *server):
running_(true), apiUrl_(apiUrl), lastMaxUserId_(0),
server_(server)
{
  pthread_rwlock_init(&rwlock_, nullptr);
}

UserInfo::~UserInfo() {
  stop();

  if (threadUpdate_.joinable())
    threadUpdate_.join();

  if (threadInsertWorkerName_.joinable())
    threadInsertWorkerName_.join();

  pthread_rwlock_destroy(&rwlock_);
}

void UserInfo::stop() {
  if (!running_)
    return;

  running_ = false;
}

int32_t UserInfo::getUserId(const string userName) {
  pthread_rwlock_rdlock(&rwlock_);
  auto itr = nameIds_.find(userName);
  pthread_rwlock_unlock(&rwlock_);

  if (itr != nameIds_.end()) {
    return itr->second;
  }
  return 0;  // not found
}

#ifdef USER_DEFINED_COINBASE
////////////////////// User defined coinbase enabled //////////////////////

// getCoinbaseInfo
string UserInfo::getCoinbaseInfo(int32_t userId) {
  pthread_rwlock_rdlock(&rwlock_);
  auto itr = idCoinbaseInfos_.find(userId);
  pthread_rwlock_unlock(&rwlock_);

  if (itr != idCoinbaseInfos_.end()) {
    return itr->second;
  }
  return "";  // not found
}

int32_t UserInfo::incrementalUpdateUsers() {
  //
  // WARNING: The API is incremental update, we use `?last_id=` to make sure
  //          always get the new data. Make sure you have use `last_id` in API.
  //
  const string url = Strings::Format("%s?last_id=%d&last_time=%" PRId64, apiUrl_.c_str(), lastMaxUserId_, lastTime_);
  string resp;
  if (!httpGET(url.c_str(), resp, 10000/* timeout ms */)) {
    LOG(ERROR) << "http get request user list fail, url: " << url;
    return -1;
  }

  JsonNode r;
  if (!JsonNode::parse(resp.c_str(), resp.c_str() + resp.length(), r)) {
    LOG(ERROR) << "decode json fail, json: " << resp;
    return -1;
  }
  if (r["data"].type() == Utilities::JS::type::Undefined) {
    LOG(ERROR) << "invalid data, should key->value, type: " << (int)r["data"].type();
    return -1;
  }
  JsonNode data = r["data"];

  auto vUser = data["users"].children();
  if (vUser->size() == 0) {
    return 0;
  }
  lastTime_ = data["time"].int64();

  pthread_rwlock_wrlock(&rwlock_);
  for (JsonNode &itr : *vUser) {

    const string  userName(itr.key_start(), itr.key_end() - itr.key_start());

    if (itr.type() != Utilities::JS::type::Obj) {
      LOG(ERROR) << "invalid data, should key  - value" << std::endl;
      return -1;
    }

    int32 userId = itr["puid"].int32();
    string coinbaseInfo = itr["coinbase"].str();

    // resize coinbaseInfo to USER_DEFINED_COINBASE_SIZE bytes
    if (coinbaseInfo.size() > USER_DEFINED_COINBASE_SIZE) {
      coinbaseInfo.resize(USER_DEFINED_COINBASE_SIZE);
    } else {
      // padding '\x20' at both beginning and ending of coinbaseInfo
      int beginPaddingLen = (USER_DEFINED_COINBASE_SIZE - coinbaseInfo.size()) / 2;
      coinbaseInfo.insert(0, beginPaddingLen, '\x20');
      coinbaseInfo.resize(USER_DEFINED_COINBASE_SIZE, '\x20');
    }

    if (userId > lastMaxUserId_) {
      lastMaxUserId_ = userId;
    }
    nameIds_[userName] = userId;

    // get user's coinbase info
    LOG(INFO) << "user id: " << userId << ", coinbase info: " << coinbaseInfo;
    idCoinbaseInfos_[userId] = coinbaseInfo;

  }
  pthread_rwlock_unlock(&rwlock_);

  return vUser->size();
}

/////////////////// End of user defined coinbase enabled ///////////////////
#else
////////////////////// User defined coinbase disabled //////////////////////

int32_t UserInfo::incrementalUpdateUsers() {
  //
  // WARNING: The API is incremental update, we use `?last_id=` to make sure
  //          always get the new data. Make sure you have use `last_id` in API.
  //
  const string url = Strings::Format("%s?last_id=%d", apiUrl_.c_str(), lastMaxUserId_);
  string resp;
  if (!httpGET(url.c_str(), resp, 10000/* timeout ms */)) {
    LOG(ERROR) << "http get request user list fail, url: " << url;
    return -1;
  }

  JsonNode r;
  if (!JsonNode::parse(resp.c_str(), resp.c_str() + resp.length(), r)) {
    LOG(ERROR) << "decode json fail, json: " << resp;
    return -1;
  }
  if (r["data"].type() == Utilities::JS::type::Undefined) {
    LOG(ERROR) << "invalid data, should key->value, type: " << (int)r["data"].type();
    return -1;
  }
  auto vUser = r["data"].children();
  if (vUser->size() == 0) {
    return 0;
  }

  pthread_rwlock_wrlock(&rwlock_);
  for (const auto &itr : *vUser) {
    const string  userName(itr.key_start(), itr.key_end() - itr.key_start());
    const int32_t userId   = itr.int32();
    if (userId > lastMaxUserId_) {
      lastMaxUserId_ = userId;
    }
    nameIds_.insert(std::make_pair(userName, userId));
  }
  pthread_rwlock_unlock(&rwlock_);

  return vUser->size();
}

/////////////////// End of user defined coinbase disabled ///////////////////
#endif

void UserInfo::runThreadUpdate() {
  const time_t updateInterval = 10;  // seconds
  time_t lastUpdateTime = time(nullptr);

  while (running_) {
    if (lastUpdateTime + updateInterval > time(nullptr)) {
      usleep(500000);  // 500ms
      continue;
    }

    int32_t res = incrementalUpdateUsers();
    lastUpdateTime = time(nullptr);

    if (res > 0)
      LOG(INFO) << "update users count: " << res;
  }
}

bool UserInfo::setupThreads() {
  //
  // get all user list, incremental update model.
  //
  // We use `offset` in incrementalUpdateUsers(), will keep update uitl no more
  // new users. Most of http API have timeout limit, so can't return lots of
  // data in one request.
  //
  while (1) {
    int32_t res = incrementalUpdateUsers();
    if (res == 0)
      break;

    if (res == -1) {
      LOG(ERROR) << "update user list failure";
      return false;
    }

    LOG(INFO) << "update users count: " << res;
  }

  threadUpdate_ = thread(&UserInfo::runThreadUpdate, this);
  threadInsertWorkerName_ = thread(&UserInfo::runThreadInsertWorkerName, this);
  return true;
}

void UserInfo::addWorker(const int32_t userId, const int64_t workerId,
                         const string &workerName, const string &minerAgent) {
  ScopeLock sl(workerNameLock_);

  // insert to Q
  workerNameQ_.push_back(WorkerName());
  workerNameQ_.rbegin()->userId_   = userId;
  workerNameQ_.rbegin()->workerId_ = workerId;

  // worker name
  snprintf(workerNameQ_.rbegin()->workerName_,
           sizeof(workerNameQ_.rbegin()->workerName_),
           "%s", workerName.c_str());
  // miner agent
  snprintf(workerNameQ_.rbegin()->minerAgent_,
           sizeof(workerNameQ_.rbegin()->minerAgent_),
           "%s", minerAgent.c_str());
}

void UserInfo::runThreadInsertWorkerName() {
  while (running_) {
    if (insertWorkerName() > 0) {
      continue;
    }
    sleep(1);
  }
}

int32_t UserInfo::insertWorkerName() {
  std::deque<WorkerName>::iterator itr = workerNameQ_.end();
  {
    ScopeLock sl(workerNameLock_);
    if (workerNameQ_.size() == 0)
      return 0;
    itr = workerNameQ_.begin();
  }

  if (itr == workerNameQ_.end())
    return 0;


  // sent events to kafka: worker_update
  {
    string eventJson;
    eventJson = Strings::Format("{\"created_at\":\"%s\","
                                 "\"type\":\"worker_update\","
                                 "\"content\":{"
                                     "\"user_id\":%d,"
                                     "\"worker_id\":%ld,"
                                     "\"worker_name\":\"%s\","
                                     "\"miner_agent\":\"%s\""
                                "}}",
                                date("%F %T").c_str(),
                                itr->userId_,
                                itr->workerId_,
                                itr->workerName_,
                                itr->minerAgent_);
    server_->sendCommonEvents2Kafka(eventJson);
  }


  {
    ScopeLock sl(workerNameLock_);
    workerNameQ_.pop_front();
  }
  return 1;
}



////////////////////////////////// StratumJobEx ////////////////////////////////
StratumJobEx::StratumJobEx(StratumJob *sjob, bool isClean):
state_(0), isClean_(isClean), sjob_(sjob)
{
  assert(sjob != nullptr);
  //makeMiningNotifyStr();
}

StratumJobEx::~StratumJobEx() {
  if (sjob_) {
    delete sjob_;
    sjob_ = nullptr;
  }
}

void StratumJobEx::makeMiningNotifyStr() {
  string merkleBranchStr;
  {
    // '"'+ 64 + '"' + ',' = 67 bytes
    merkleBranchStr.reserve(sjob_->merkleBranch_.size() * 67);
    for (size_t i = 0; i < sjob_->merkleBranch_.size(); i++) {
      //
      // do NOT use GetHex() or uint256.ToString(), need to dump the memory
      //
      string merklStr;
      Bin2Hex(sjob_->merkleBranch_[i].begin(), 32, merklStr);
      merkleBranchStr.append("\"" + merklStr + "\",");
    }
    if (merkleBranchStr.length()) {
      merkleBranchStr.resize(merkleBranchStr.length() - 1);  // remove last ','
    }
  }

  // we don't put jobId here, session will fill with the shortJobId
  miningNotify1_ = "{\"id\":null,\"method\":\"mining.notify\",\"params\":[\"";

  miningNotify2_ = Strings::Format("\",\"%s\",\"",
                                   sjob_->prevHashBeStr_.c_str());

  // coinbase1_ may be modified when USER_DEFINED_COINBASE enabled,
  // so put it into a single variable.
  coinbase1_ = sjob_->coinbase1_.c_str();

  miningNotify3_ = Strings::Format("\",\"%s\""
                                   ",[%s]"
                                   ",\"%08x\",\"%08x\",\"%08x\",%s"
                                   "]}\n",
                                   sjob_->coinbase2_.c_str(),
                                   merkleBranchStr.c_str(),
                                   sjob_->nVersion_, sjob_->nBits_, sjob_->nTime_,
                                   isClean_ ? "true" : "false");
  // always set clean to true, reset of them is the same with miningNotify2_
  miningNotify3Clean_ = Strings::Format("\",\"%s\""
                                   ",[%s]"
                                   ",\"%08x\",\"%08x\",\"%08x\",true"
                                   "]}\n",
                                   sjob_->coinbase2_.c_str(),
                                   merkleBranchStr.c_str(),
                                   sjob_->nVersion_, sjob_->nBits_, sjob_->nTime_);

}

void StratumJobEx::markStale() {
  // 0: MINING, 1: STALE
  state_ = 1;
}

bool StratumJobEx::isStale() {
  // 0: MINING, 1: STALE
  return (state_ == 1);
}

void StratumJobEx::generateCoinbaseTx(std::vector<char> *coinbaseBin,
                                      const uint32_t extraNonce1,
                                      const string &extraNonce2Hex,
                                      string *userCoinbaseInfo) {
  string coinbaseHex;
  const string extraNonceStr = Strings::Format("%08x%s", extraNonce1, extraNonce2Hex.c_str());
  string coinbase1 = sjob_->coinbase1_;

#ifdef USER_DEFINED_COINBASE
  if (userCoinbaseInfo != nullptr) {
    string userCoinbaseHex;
    Bin2Hex((uint8*)(*userCoinbaseInfo).c_str(), (*userCoinbaseInfo).size(), userCoinbaseHex);
    // replace the last `userCoinbaseHex.size()` bytes to `userCoinbaseHex`
    coinbase1.replace(coinbase1.size()-userCoinbaseHex.size(), userCoinbaseHex.size(), userCoinbaseHex);
  }
#endif

  coinbaseHex.append(coinbase1);
  coinbaseHex.append(extraNonceStr);
  coinbaseHex.append(sjob_->coinbase2_);
  Hex2Bin((const char *)coinbaseHex.c_str(), *coinbaseBin);
}

void StratumJobEx::generateBlockHeader(CBlockHeader *header,
                                       std::vector<char> *coinbaseBin,
                                       const uint32_t extraNonce1,
                                       const string &extraNonce2Hex,
                                       const vector<uint256> &merkleBranch,
                                       const uint256 &hashPrevBlock,
                                       const uint32_t nBits, const int32_t nVersion,
                                       const uint32_t nTime, const uint32_t nonce,
                                       string *userCoinbaseInfo) {
  generateCoinbaseTx(coinbaseBin, extraNonce1, extraNonce2Hex, userCoinbaseInfo);

  header->hashPrevBlock = hashPrevBlock;
  header->nVersion      = nVersion;
  header->nBits         = nBits;
  header->nTime         = nTime;
  header->nNonce        = nonce;

  // hashMerkleRoot
  header->hashMerkleRoot = Hash(coinbaseBin->begin(), coinbaseBin->end());

  for (const uint256 & step : merkleBranch) {
    header->hashMerkleRoot = Hash(BEGIN(header->hashMerkleRoot),
                                  END  (header->hashMerkleRoot),
                                  BEGIN(step),
                                  END  (step));
  }
}

////////////////////////////////// StratumServer ///////////////////////////////
StratumServer::StratumServer(const char *ip, const unsigned short port,
                             const char *kafkaBrokers, const string &userAPIUrl,
                             const uint8_t serverId, const string &fileLastNotifyTime,
                             bool isEnableSimulator, bool isSubmitInvalidBlock,
                             bool isDevModeEnable, float minerDifficulty,
                             const string &consumerTopic)
:running_(true),
ip_(ip), port_(port), serverId_(serverId),
fileLastNotifyTime_(fileLastNotifyTime),
kafkaBrokers_(kafkaBrokers), userAPIUrl_(userAPIUrl),
isEnableSimulator_(isEnableSimulator), isSubmitInvalidBlock_(isSubmitInvalidBlock),
isDevModeEnable_(isDevModeEnable), minerDifficulty_(minerDifficulty),
consumerTopic_(consumerTopic)
{
}

StratumServer::~StratumServer() {
}

bool StratumServer::createServer(string type, const int32_t shareAvgSeconds) {
  LOG(INFO) << "createServer type: " << type << ", shareAvgSeconds: " << shareAvgSeconds;
  if ("BTC" == type)
    server_ = make_shared<Server> (shareAvgSeconds);
  else if ("ETH" == type)
    server_ = make_shared<ServerEth> (shareAvgSeconds);
  else if ("SIA" == type)
    server_ = make_shared<ServerSia> (shareAvgSeconds);
  else 
    return false;
  return server_ != nullptr;
}

bool StratumServer::init() {
  if (!server_->setup(ip_.c_str(), port_, kafkaBrokers_.c_str(),
                     userAPIUrl_, serverId_, fileLastNotifyTime_,
                     isEnableSimulator_, isSubmitInvalidBlock_,
                     isDevModeEnable_, minerDifficulty_, consumerTopic_)) {
    LOG(ERROR) << "fail to setup server";
    return false;
  }
  return true;
}

void StratumServer::stop() {
  if (!running_) {
    return;
  }
  running_ = false;
  server_->stop();
  LOG(INFO) << "stop stratum server";
}

void StratumServer::run() {
  server_->run();
}

///////////////////////////////////// Server ///////////////////////////////////
Server::Server(const int32_t shareAvgSeconds):
base_(nullptr), signal_event_(nullptr), listener_(nullptr),
kafkaProducerShareLog_(nullptr),
kafkaProducerSolvedShare_(nullptr),
kafkaProducerNamecoinSolvedShare_(nullptr),
kafkaProducerCommonEvents_(nullptr),
kafkaProducerRskSolvedShare_(nullptr),
isEnableSimulator_(false), isSubmitInvalidBlock_(false),

#ifndef WORK_WITH_STRATUM_SWITCHER
sessionIDManager_(nullptr),
#endif

isDevModeEnable_(false), minerDifficulty_(1.0),
kShareAvgSeconds_(shareAvgSeconds),
jobRepository_(nullptr), userInfo_(nullptr)
{
}

Server::~Server() {
  if (signal_event_ != nullptr) {
    event_free(signal_event_);
  }
  if (listener_ != nullptr) {
    evconnlistener_free(listener_);
  }
  if (base_ != nullptr) {
    event_base_free(base_);
  }
  if (kafkaProducerShareLog_ != nullptr) {
    delete kafkaProducerShareLog_;
  }
  if (kafkaProducerSolvedShare_ != nullptr) {
    delete kafkaProducerSolvedShare_;
  }
  if (kafkaProducerNamecoinSolvedShare_ != nullptr) {
    delete kafkaProducerNamecoinSolvedShare_;
  }
  if (kafkaProducerRskSolvedShare_ != nullptr) {
    delete kafkaProducerRskSolvedShare_;
  }
  if (kafkaProducerCommonEvents_ != nullptr) {
    delete kafkaProducerCommonEvents_;
  }
  if (jobRepository_ != nullptr) {
    delete jobRepository_;
  }
  if (userInfo_ != nullptr) {
    delete userInfo_;
  }

#ifndef WORK_WITH_STRATUM_SWITCHER
  if (sessionIDManager_ != nullptr) {
    delete sessionIDManager_;
  }
#endif
}

// JobRepository *Server::createJobRepository(StratumServerType type,
//                                            const char *kafkaBrokers,
//                                            const string &fileLastNotifyTime,
//                                            Server *server)
// {
//   JobRepository *jobRepo = nullptr;
//   switch (type)
//   {
//   case BTC:
//     jobRepo = new JobRepository(kafkaBrokers, fileLastNotifyTime, this);
//     break;
//   case ETH:
//     jobRepo = new JobRepositoryEth(kafkaBrokers, fileLastNotifyTime, this);
//     break;
//   }
//   return jobRepo;
// }

JobRepository *Server::createJobRepository(const char *kafkaBrokers,
                                           const char *consumerTopic,
                                           const string &fileLastNotifyTime,
                                           Server *server)
{
  return new JobRepository(kafkaBrokers, consumerTopic, fileLastNotifyTime, this);
}

StratumSession *Server::createSession(evutil_socket_t fd, struct bufferevent *bev,
                                      Server *server, struct sockaddr *saddr,
                                      const int32_t shareAvgSeconds,
                                      const uint32_t sessionID)
{
  return new StratumSession(fd, bev, server, saddr,
                     server->kShareAvgSeconds_,
                     sessionID);
}

// StratumSession* Server::createSession(StratumServerType type, evutil_socket_t fd, struct bufferevent *bev,
//                                       Server *server, struct sockaddr *saddr,
//                                       const int32_t shareAvgSeconds,
//                                       const uint32_t sessionID)
// {
//   StratumSession *conn = nullptr;
//   switch (type)
//   {
//   case BTC:
//     conn = new StratumSession(fd, bev, server, saddr,
//                               server->kShareAvgSeconds_,
//                               sessionID);
//     break;
//   case ETH:
//     conn = new StratumSessionEth(fd, bev, server, saddr,
//                                  server->kShareAvgSeconds_,
//                                  sessionID);
//     break;
//   }

//   if (!conn->initialize()) {
//     delete conn;
//     conn = nullptr;
//   }

//   return conn;
// }

bool Server::setup(const char *ip, const unsigned short port,
                   const char *kafkaBrokers,
                   const string &userAPIUrl,
                   const uint8_t serverId, const string &fileLastNotifyTime,
                   bool isEnableSimulator, bool isSubmitInvalidBlock,
                   bool isDevModeEnable, float minerDifficulty, const string &consumerTopic) {
  if (isEnableSimulator) {
    isEnableSimulator_ = true;
    LOG(WARNING) << "Simulator is enabled, all share will be accepted";
  }

  if (isSubmitInvalidBlock) {
    isSubmitInvalidBlock_ = true;
    LOG(WARNING) << "submit invalid block is enabled, all block will be submited";
  }

  if (isDevModeEnable) {
    isDevModeEnable_ = true;
    minerDifficulty_ = minerDifficulty;
    LOG(INFO) << "development mode is enabled with difficulty: " << minerDifficulty;
  }

  kafkaProducerSolvedShare_ = new KafkaProducer(kafkaBrokers,
                                                KAFKA_TOPIC_SOLVED_SHARE,
                                                RD_KAFKA_PARTITION_UA);
  kafkaProducerNamecoinSolvedShare_ = new KafkaProducer(kafkaBrokers,
                                                        KAFKA_TOPIC_NMC_SOLVED_SHARE,
                                                        RD_KAFKA_PARTITION_UA);
  kafkaProducerRskSolvedShare_ = new KafkaProducer(kafkaBrokers,
                                                        KAFKA_TOPIC_RSK_SOLVED_SHARE,
                                                        RD_KAFKA_PARTITION_UA);
  kafkaProducerShareLog_ = new KafkaProducer(kafkaBrokers,
                                             KAFKA_TOPIC_SHARE_LOG,
                                             RD_KAFKA_PARTITION_UA);
  kafkaProducerCommonEvents_ = new KafkaProducer(kafkaBrokers,
                                                 KAFKA_TOPIC_COMMON_EVENTS,
                                                 RD_KAFKA_PARTITION_UA);

  // job repository
  jobRepository_ = createJobRepository(kafkaBrokers, consumerTopic.c_str(), fileLastNotifyTime, this);
  if (!jobRepository_->setupThreadConsume()) {
    return false;
  }

  // user info
  userInfo_ = new UserInfo(userAPIUrl, this);
  if (!userInfo_->setupThreads()) {
    return false;
  }

#ifndef WORK_WITH_STRATUM_SWITCHER
  sessionIDManager_ = new SessionIDManager(serverId);
#endif

  // kafkaProducerShareLog_
  {
    map<string, string> options;
    // we could delay 'sharelog' in producer
    // 10000000 * sizeof(Share) ~= 480 MB
    options["queue.buffering.max.messages"] = "10000000";
    // send every second
    options["queue.buffering.max.ms"] = "1000";
    // 10000 * sizeof(Share) ~= 480 KB
    options["batch.num.messages"] = "10000";

    if (!kafkaProducerShareLog_->setup(&options)) {
      LOG(ERROR) << "kafka kafkaProducerShareLog_ setup failure";
      return false;
    }
    if (!kafkaProducerShareLog_->checkAlive()) {
      LOG(ERROR) << "kafka kafkaProducerShareLog_ is NOT alive";
      return false;
    }
  }

  // kafkaProducerSolvedShare_
  {
    map<string, string> options;
    // set to 1 (0 is an illegal value here), deliver msg as soon as possible.
    options["queue.buffering.max.ms"] = "1";
    if (!kafkaProducerSolvedShare_->setup(&options)) {
      LOG(ERROR) << "kafka kafkaProducerSolvedShare_ setup failure";
      return false;
    }
    if (!kafkaProducerSolvedShare_->checkAlive()) {
      LOG(ERROR) << "kafka kafkaProducerSolvedShare_ is NOT alive";
      return false;
    }
  }

  // kafkaProducerNamecoinSolvedShare_
  {
    map<string, string> options;
    // set to 1 (0 is an illegal value here), deliver msg as soon as possible.
    options["queue.buffering.max.ms"] = "1";
    if (!kafkaProducerNamecoinSolvedShare_->setup(&options)) {
      LOG(ERROR) << "kafka kafkaProducerNamecoinSolvedShare_ setup failure";
      return false;
    }
    if (!kafkaProducerNamecoinSolvedShare_->checkAlive()) {
      LOG(ERROR) << "kafka kafkaProducerNamecoinSolvedShare_ is NOT alive";
      return false;
    }
  }

  // kafkaProducerRskSolvedShare_
  {
    map<string, string> options;
    // set to 1 (0 is an illegal value here), deliver msg as soon as possible.
    options["queue.buffering.max.ms"] = "1";
    if (!kafkaProducerRskSolvedShare_->setup(&options)) {
      LOG(ERROR) << "kafka kafkaProducerRskSolvedShare_ setup failure";
      return false;
    }
    if (!kafkaProducerRskSolvedShare_->checkAlive()) {
      LOG(ERROR) << "kafka kafkaProducerRskSolvedShare_ is NOT alive";
      return false;
    }
  }

  // kafkaProducerCommonEvents_
  {
    map<string, string> options;
    options["queue.buffering.max.messages"] = "500000";
    options["queue.buffering.max.ms"] = "1000";  // send every second
    options["batch.num.messages"]     = "10000";

    if (!kafkaProducerCommonEvents_->setup(&options)) {
      LOG(ERROR) << "kafka kafkaProducerCommonEvents_ setup failure";
      return false;
    }
    if (!kafkaProducerCommonEvents_->checkAlive()) {
      LOG(ERROR) << "kafka kafkaProducerCommonEvents_ is NOT alive";
      return false;
    }
  }

  base_ = event_base_new();
  if(!base_) {
    LOG(ERROR) << "server: cannot create base";
    return false;
  }

  memset(&sin_, 0, sizeof(sin_));
  sin_.sin_family = AF_INET;
  sin_.sin_port   = htons(port);
  sin_.sin_addr.s_addr = htonl(INADDR_ANY);
  if (ip && inet_pton(AF_INET, ip, &sin_.sin_addr) == 0) {
    LOG(ERROR) << "invalid ip: " << ip;
    return false;
  }

  listener_ = evconnlistener_new_bind(base_,
                                      Server::listenerCallback,
                                      (void*)this,
                                      LEV_OPT_REUSEABLE|LEV_OPT_CLOSE_ON_FREE,
                                      -1, (struct sockaddr*)&sin_, sizeof(sin_));
  if(!listener_) {
    LOG(ERROR) << "cannot create listener: " << ip << ":" << port;
    return false;
  }
  return true;
}

void Server::run() {
  if(base_ != NULL) {
    //    event_base_loop(base_, EVLOOP_NONBLOCK);
    event_base_dispatch(base_);
  }
}

void Server::stop() {
  LOG(INFO) << "stop tcp server event loop";
  event_base_loopexit(base_, NULL);

  jobRepository_->stop();
  userInfo_->stop();
}

void Server::sendMiningNotifyToAll(shared_ptr<StratumJobEx> exJobPtr) {
  //
  // http://www.sgi.com/tech/stl/Map.html
  //
  // Map has the important property that inserting a new element into a map
  // does not invalidate iterators that point to existing elements. Erasing
  // an element from a map also does not invalidate any iterators, except,
  // of course, for iterators that actually point to the element that is
  // being erased.
  //

  ScopeLock sl(connsLock_);
  std::map<evutil_socket_t, StratumSession *>::iterator itr = connections_.begin();
  while (itr != connections_.end()) {
    StratumSession *conn = itr->second;  // alias

    if (conn->isDead()) {
#ifndef WORK_WITH_STRATUM_SWITCHER
      sessionIDManager_->freeSessionId(conn->getSessionId());
#endif

      delete conn;
      itr = connections_.erase(itr);
    } else {

      conn->sendMiningNotify(exJobPtr);
      ++itr;
    }
  }
}

void Server::addConnection(evutil_socket_t fd, StratumSession *connection) {
  ScopeLock sl(connsLock_);
  connections_.insert(std::pair<evutil_socket_t, StratumSession *>(fd, connection));
}

void Server::removeConnection(evutil_socket_t fd) {
  //
  // if we are here, means the related evbuffer has already been locked.
  // don't lock connsLock_ in this function, it will cause deadlock.
  //
  auto itr = connections_.find(fd);
  if (itr == connections_.end()) {
    return;
  }

  // mark to delete
  itr->second->markAsDead();
}

void Server::listenerCallback(struct evconnlistener* listener,
                              evutil_socket_t fd,
                              struct sockaddr *saddr,
                              int socklen, void* data)
{
  Server *server = static_cast<Server *>(data);
  struct event_base  *base = (struct event_base*)server->base_;
  struct bufferevent *bev;
  uint32_t sessionID = 0u;

#ifndef WORK_WITH_STRATUM_SWITCHER
  // can't alloc session Id
  if (server->sessionIDManager_->allocSessionId(&sessionID) == false) {
    close(fd);
    return;
  }
#endif

  bev = bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE|BEV_OPT_THREADSAFE);
  if(bev == nullptr) {
    LOG(ERROR) << "error constructing bufferevent!";
    server->stop();
    return;
  }

  // create stratum session
  StratumSession *conn = server->createSession(fd, bev, server, saddr,
                                       server->kShareAvgSeconds_,
                                       sessionID);
  if (!conn->initialize())
  {
    delete conn;
    return;
  }
  // set callback functions
  bufferevent_setcb(bev,
                    Server::readCallback, nullptr,
                    Server::eventCallback, (void*)conn);
  // By default, a newly created bufferevent has writing enabled.
  bufferevent_enable(bev, EV_READ|EV_WRITE);

  server->addConnection(fd, conn);
}

void Server::readCallback(struct bufferevent* bev, void *connection) {
  StratumSession *conn = static_cast<StratumSession *>(connection);
  conn->readBuf(bufferevent_get_input(bev));
}

void Server::eventCallback(struct bufferevent* bev, short events,
                              void *connection) {
  StratumSession *conn = static_cast<StratumSession *>(connection);
  Server       *server = static_cast<Server *>(conn->server_);

  // should not be 'BEV_EVENT_CONNECTED'
  assert((events & BEV_EVENT_CONNECTED) != BEV_EVENT_CONNECTED);

  if (events & BEV_EVENT_EOF) {
    LOG(INFO) << "socket closed";
  }
  else if (events & BEV_EVENT_ERROR) {
    LOG(INFO) << "got an error on the socket: "
    << evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR());
  }
  else if (events & BEV_EVENT_TIMEOUT) {
    LOG(INFO) << "socket read/write timeout, events: " << events;
  }
  else {
    LOG(ERROR) << "unhandled socket events: " << events;
  }
  server->removeConnection(conn->fd_);
}

int Server::checkShare(const Share &share,
                       const uint32 extraNonce1, const string &extraNonce2Hex,
                       const uint32_t nTime, const uint32_t nonce,
                       const uint256 &jobTarget, const string &workFullName,
                       string *userCoinbaseInfo) {
  shared_ptr<StratumJobEx> exJobPtr = jobRepository_->getStratumJobEx(share.jobId_);
  if (exJobPtr == nullptr) {
    return StratumError::JOB_NOT_FOUND;
  }
  StratumJob *sjob = exJobPtr->sjob_;

  if (exJobPtr->isStale()) {
    return StratumError::JOB_NOT_FOUND;
  }
  if (nTime <= sjob->minTime_) {
    return StratumError::TIME_TOO_OLD;
  }
  if (nTime > sjob->nTime_ + 600) {
    return StratumError::TIME_TOO_NEW;
  }

  CBlockHeader header;
  std::vector<char> coinbaseBin;
  exJobPtr->generateBlockHeader(&header, &coinbaseBin,
                                extraNonce1, extraNonce2Hex,
                                sjob->merkleBranch_, sjob->prevHash_,
                                sjob->nBits_, sjob->nVersion_, nTime, nonce,
                                userCoinbaseInfo);
  uint256 blkHash = header.GetHash();

  arith_uint256 bnBlockHash     = UintToArith256(blkHash);
  arith_uint256 bnNetworkTarget = UintToArith256(sjob->networkTarget_);

  //
  // found new block
  //
  if (isSubmitInvalidBlock_ == true || bnBlockHash <= bnNetworkTarget) {
    //
    // build found block
    //
    FoundBlock foundBlock;
    foundBlock.jobId_    = share.jobId_;
    foundBlock.workerId_ = share.workerHashId_;
    foundBlock.userId_   = share.userId_;
    foundBlock.height_   = sjob->height_;
    memcpy(foundBlock.header80_, (const uint8_t *)&header, sizeof(CBlockHeader));
    snprintf(foundBlock.workerFullName_, sizeof(foundBlock.workerFullName_),
             "%s", workFullName.c_str());
    // send
    sendSolvedShare2Kafka(&foundBlock, coinbaseBin);

    // mark jobs as stale
    jobRepository_->markAllJobsAsStale();

    LOG(INFO) << ">>>> found a new block: " << blkHash.ToString()
    << ", jobId: " << share.jobId_ << ", userId: " << share.userId_
    << ", by: " << workFullName << " <<<<";
  }

  // print out high diff share, 2^10 = 1024
  if ((bnBlockHash >> 10) <= bnNetworkTarget) {
    LOG(INFO) << "high diff share, blkhash: " << blkHash.ToString()
    << ", diff: " << TargetToDiff(blkHash)
    << ", networkDiff: " << TargetToDiff(sjob->networkTarget_)
    << ", by: " << workFullName;
  }

  //
  // found new RSK block
  //
  if (!sjob->blockHashForMergedMining_.empty() &&
      (isSubmitInvalidBlock_ == true || bnBlockHash <= UintToArith256(sjob->rskNetworkTarget_))) {
    //
    // build data needed to submit block to RSK
    //
    RskSolvedShareData shareData;
    shareData.jobId_    = share.jobId_;
    shareData.workerId_ = share.workerHashId_;
    shareData.userId_   = share.userId_;
    // height = matching bitcoin block height
    shareData.height_   = sjob->height_;
    snprintf(shareData.feesForMiner_, sizeof(shareData.feesForMiner_), "%s", sjob->feesForMiner_.c_str());
    snprintf(shareData.rpcAddress_, sizeof(shareData.rpcAddress_), "%s", sjob->rskdRpcAddress_.c_str());
    snprintf(shareData.rpcUserPwd_, sizeof(shareData.rpcUserPwd_), "%s", sjob->rskdRpcUserPwd_.c_str());
    memcpy(shareData.header80_, (const uint8_t *)&header, sizeof(CBlockHeader));
    snprintf(shareData.workerFullName_, sizeof(shareData.workerFullName_), "%s", workFullName.c_str());
    
    //
    // send to kafka topic
    //
    string buf;
    buf.resize(sizeof(RskSolvedShareData) + coinbaseBin.size());
    uint8_t *p = (uint8_t *)buf.data();

    // RskSolvedShareData
    memcpy(p, (const uint8_t *)&shareData, sizeof(RskSolvedShareData));
    p += sizeof(RskSolvedShareData);

    // coinbase TX
    memcpy(p, coinbaseBin.data(), coinbaseBin.size());

    kafkaProducerRskSolvedShare_->produce(buf.data(), buf.size());

    //
    // log the finding
    //
    LOG(INFO) << ">>>> found a new RSK block: " << blkHash.ToString()
    << ", jobId: " << share.jobId_ << ", userId: " << share.userId_
    << ", by: " << workFullName << " <<<<";
  }

  //
  // found namecoin block
  //
  if (sjob->nmcAuxBits_ != 0 &&
      (isSubmitInvalidBlock_ == true || bnBlockHash <= UintToArith256(sjob->nmcNetworkTarget_))) {
    //
    // build namecoin solved share message
    //
    string blockHeaderHex;
    Bin2Hex((const uint8_t *)&header, sizeof(CBlockHeader), blockHeaderHex);
    DLOG(INFO) << "blockHeaderHex: " << blockHeaderHex;

    string coinbaseTxHex;
    Bin2Hex((const uint8_t *)coinbaseBin.data(), coinbaseBin.size(), coinbaseTxHex);
    DLOG(INFO) << "coinbaseTxHex: " << coinbaseTxHex;

    const string nmcAuxSolvedShare = Strings::Format("{\"job_id\":%" PRIu64","
                                                     " \"aux_block_hash\":\"%s\","
                                                     " \"block_header\":\"%s\","
                                                     " \"coinbase_tx\":\"%s\","
                                                     " \"rpc_addr\":\"%s\","
                                                     " \"rpc_userpass\":\"%s\""
                                                     "}",
                                                     share.jobId_,
                                                     sjob->nmcAuxBlockHash_.ToString().c_str(),
                                                     blockHeaderHex.c_str(),
                                                     coinbaseTxHex.c_str(),
                                                     sjob->nmcRpcAddr_.size()     ? sjob->nmcRpcAddr_.c_str()     : "",
                                                     sjob->nmcRpcUserpass_.size() ? sjob->nmcRpcUserpass_.c_str() : "");
    // send found namecoin aux block to kafka
    kafkaProducerNamecoinSolvedShare_->produce(nmcAuxSolvedShare.data(),
                                               nmcAuxSolvedShare.size());

    LOG(INFO) << ">>>> found namecoin block: " << sjob->nmcHeight_ << ", "
    << sjob->nmcAuxBlockHash_.ToString()
    << ", jobId: " << share.jobId_ << ", userId: " << share.userId_
    << ", by: " << workFullName << " <<<<";
  }

  // check share diff
  if (isEnableSimulator_ == false && bnBlockHash >= UintToArith256(jobTarget)) {
    return StratumError::LOW_DIFFICULTY;
  }

  DLOG(INFO) << "blkHash: " << blkHash.ToString() << ", jobTarget: "
  << jobTarget.ToString() << ", networkTarget: " << sjob->networkTarget_.ToString();

  // reach here means an valid share
  return StratumError::NO_ERROR;
}

void Server::sendShare2Kafka(const uint8_t *data, size_t len) {
  kafkaProducerShareLog_->produce(data, len);
}

void Server::sendSolvedShare2Kafka(const FoundBlock *foundBlock,
                                   const std::vector<char> &coinbaseBin) {
  //
  // solved share message:  FoundBlock + coinbase_Tx
  //
  string buf;
  buf.resize(sizeof(FoundBlock) + coinbaseBin.size());
  uint8_t *p = (uint8_t *)buf.data();

  // FoundBlock
  memcpy(p, (const uint8_t *)foundBlock, sizeof(FoundBlock));
  p += sizeof(FoundBlock);

  // coinbase TX
  memcpy(p, coinbaseBin.data(), coinbaseBin.size());

  kafkaProducerSolvedShare_->produce(buf.data(), buf.size());
}

void Server::sendCommonEvents2Kafka(const string &message) {
  kafkaProducerCommonEvents_->produce(message.data(), message.size());
}

////////////////////////////////// ServierEth ///////////////////////////////
int ServerEth::checkShare(const Share &share,
                          const uint64_t nonce,
                          const uint256 header,
                          const uint256 mixHash)
{
  //accept every share in simulator mode
  if (isEnableSimulator_)
  {
    usleep(20000);
    return StratumError::NO_ERROR;
  }

  JobRepositoryEth *jobRepo = dynamic_cast<JobRepositoryEth *>(jobRepository_);
  if (nullptr == jobRepo)
    return StratumError::ILLEGAL_PARARMS;

  shared_ptr<StratumJobEx> exJobPtr = jobRepository_->getStratumJobEx(share.jobId_);
  if (nullptr == exJobPtr)
  {
    return StratumError::JOB_NOT_FOUND;
  }

  if (exJobPtr->isStale())
  {
    return StratumError::JOB_NOT_FOUND;
  }

  StratumJob *sjob = exJobPtr->sjob_;
  //LOG(INFO) << "checking share nonce: " << hex << nonce << ", header: " << header.GetHex() << ", mixHash: " << mixHash.GetHex();
  ethash_return_value_t r;
  ethash_h256_t ethashHeader = {0};
  Uint256ToEthash256(header, ethashHeader);

  // for (int i = 0; i < 32; ++i)
  //   LOG(INFO) << "ethash_h256_t byte " << i << ": " << hex << (int)ethashHeader.b[i];
  timeval start, end;
  long mtime, seconds, useconds;
  gettimeofday(&start, NULL);
  bool ret = jobRepo->compute(ethashHeader, nonce, r);
  gettimeofday(&end, NULL);
  seconds = end.tv_sec - start.tv_sec;
  useconds = end.tv_usec - start.tv_usec;
  mtime = ((seconds)*1000 + useconds / 1000.0) + 0.5;
  LOG(INFO) << "light compute takes " << mtime << " ms";

  if (!ret || !r.success)
  {
    LOG(ERROR) << "light cache creation error";
    return StratumError::INTERNAL_ERROR;
  }

  uint256 mix = Ethash256ToUint256(r.mix_hash);
  if (mix != mixHash)
  {
    LOG(ERROR) << "mix hash does not match: " << mix.GetHex();
    return StratumError::INTERNAL_ERROR;
  }

  uint256 shareTarget = Ethash256ToUint256(r.result);
  //DLOG(INFO) << "comapre share target: " << shareTarget.GetHex() << ", network target: " << sjob->rskNetworkTarget_.GetHex();
  //can not compare directly because unit256 uses memcmp
  if (UintToArith256(sjob->rskNetworkTarget_) < UintToArith256(shareTarget))
    return StratumError::LOW_DIFFICULTY;

  return StratumError::NO_ERROR;
}

void ServerEth::sendSolvedShare2Kafka(const string &strNonce, const string &strHeader, const string &strMix)
{
  string msg = Strings::Format("{\"nonce\":\"%s\",\"header\":\"%s\",\"mix\":\"%s\"}", strNonce.c_str(), strHeader.c_str(), strMix.c_str());
  kafkaProducerSolvedShare_->produce(msg.c_str(), msg.length());
}

StratumSession *ServerEth::createSession(evutil_socket_t fd, struct bufferevent *bev,
                                         Server *server, struct sockaddr *saddr,
                                         const int32_t shareAvgSeconds,
                                         const uint32_t sessionID)
{
  return new StratumSessionEth(fd, bev, server, saddr,
                        server->kShareAvgSeconds_,
                        sessionID);
}

JobRepository *ServerEth::createJobRepository(const char *kafkaBrokers,
                                            const char *consumerTopic,
                                           const string &fileLastNotifyTime,
                                           Server *server)
{
  return new JobRepositoryEth(kafkaBrokers, consumerTopic, fileLastNotifyTime, this);
}

////////////////////////////////// ServierSia ///////////////////////////////
StratumSession *ServerSia::createSession(evutil_socket_t fd, struct bufferevent *bev,
                                         Server *server, struct sockaddr *saddr,
                                         const int32_t shareAvgSeconds,
                                         const uint32_t sessionID)
{
  return new StratumSessionSia(fd, bev, server, saddr,
                        server->kShareAvgSeconds_,
                        sessionID);
}

JobRepository *ServerSia::createJobRepository(const char *kafkaBrokers,
                                            const char *consumerTopic,
                                           const string &fileLastNotifyTime,
                                           Server *server)
{
  return new JobRepositorySia(kafkaBrokers, consumerTopic, fileLastNotifyTime, this);
}

////////////////////////////////// StratumJobExEth ///////////////////////////////
StratumJobExEth::StratumJobExEth(StratumJob *sjob, bool isClean) : StratumJobEx(sjob, isClean)
{
}

void StratumJobExEth::makeMiningNotifyStr()
{
  // StratumJobEth *ethJob = dynamic_cast<StratumJobEth *>(sjob_);
  // if (nullptr == ethJob)
  //   return;

  // First parameter of params array is job ID (must be HEX number of any
  // size). Second parameter is seedhash. Seedhash is sent with every job to
  // support possible multipools, which may switch between coins quickly.
  // Third parameter is headerhash. Last parameter is boolean cleanjobs.
  // If set to true, tbbhen miner needs to clear queue of jobs and immediatelly
  // start working on new provided job, because all old jobs shares will
  // result with stale share error.
  // Miner uses seedhash to identify DAG, then tries to find share below
  // target (which is created out of provided difficulty) with headerhash,
  // extranonce and own minernonce.

  //the boundary condition ("target"), 2^256 / difficulty.
  //How to calculate difficulty: 2 strings division?
  //no set difficulty api, manuplate target and distribute to miner?

  //string header = ethJob->blockHashForMergedMining_.substr(2, 64);
  //string seed = ethJob->seedHash_.substr(2, 64);
  //string strShareTarget = std::move(Eth_DifficultyToTarget(shareDifficulty_));
  //LOG(INFO) << "new stratum job mining.notify: share difficulty=" << shareDifficulty_ << ", share target=" << strShareTarget;
  // miningNotify1_ = Strings::Format("{\"id\":8,\"jsonrpc\":\"2.0\",\"method\":\"mining.notify\","
  //                                  "\"params\":[\"%s\",\"%s\",\"%s\",\"%s\", false]}\n",
  //                                  header.c_str(),
  //                                  header.c_str(),
  //                                  seed.c_str(),
  //                                  strShareTarget.c_str());
}

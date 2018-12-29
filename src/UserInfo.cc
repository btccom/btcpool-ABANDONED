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
#include "StratumServer.h"
#include "UserInfo.h"

//////////////////////////////////// UserInfo /////////////////////////////////
UserInfo::UserInfo(StratumServer *server, const libconfig::Config &config):
running_(true), lastMaxUserId_(0),
caseInsensitive_(true),
server_(server)
{
  // optional
  config.lookupValue("users.case_insensitive", caseInsensitive_);
  // required (exception will be threw if inexists)
  apiUrl_ = config.lookup("users.list_id_api_url").c_str();
  
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

void UserInfo::regularUserName(string &userName) {
  if (caseInsensitive_) {
    std::transform(userName.begin(), userName.end(), userName.begin(), ::tolower);
  }
}

int32_t UserInfo::getUserId(string userName) {
  regularUserName(userName);

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

    string userName(itr.key_start(), itr.key_end() - itr.key_start());
    regularUserName(userName);

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
    string userName(itr.key_start(), itr.key_end() - itr.key_start());
    regularUserName(userName);

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
                                     "\"worker_id\":%" PRId64 ","
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

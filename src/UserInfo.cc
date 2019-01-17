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
UserInfo::UserInfo(StratumServer *server, const libconfig::Config &config)
: running_(true)
, caseInsensitive_(true)
, stripUserSuffix_(false)
, userSuffixSeparator_("_")
, server_(server)
{
  // optional
  config.lookupValue("users.case_insensitive", caseInsensitive_);

  LOG(INFO) << "UserInfo: user name will be case " << (caseInsensitive_ ? "insensitive" : "sensitive");

  config.lookupValue("users.strip_user_suffix", stripUserSuffix_);
  config.lookupValue("users.user_suffix_separator", userSuffixSeparator_);

  if (stripUserSuffix_) {
    if (userSuffixSeparator_.empty()) {
      LOG(FATAL) << "users.strip_user_suffix enabled but users.user_suffix_separator is empty!";
    }

    LOG(INFO) << "UserInfo: suffix " << userSuffixSeparator_ << "* will be stripped from user name";
  }

  auto addChainVars = [&](const string &apiUrl) {
    chains_.push_back({
      apiUrl,
      new pthread_rwlock_t(), // rwlock_
      {}, // nameIds_
      0,  // lastMaxUserId_
#ifdef USER_DEFINED_COINBASE
      {}, // idCoinbaseInfos_
      0,  // lastTime_
#endif
      new std::mutex(), // workerNameLock_
      {}, // workerNameQ_
      {}, // threadInsertWorkerName_
      {}  // threadUpdate_
    });

    pthread_rwlock_init(chains_.rbegin()->nameIdlock_, nullptr);
  };

  bool multiChains = false;
  config.lookupValue("sserver.multi_chains", multiChains);

  if (multiChains) {
    const Setting &chains = config.lookup("chains");
    for (int i = 0; i < chains.getLength(); i++) {
      addChainVars(chains[i].lookup("users_list_id_api_url"));
    }
    if (chains_.empty()) {
      LOG(FATAL) << "sserver.multi_chains enabled but chains is empty!";
    }
    if (chains_.size() > 1) {
      zk_ = server->getZookeeper(config);
      zkUserChainMapDir_ = config.lookup("users.zookeeper_userchain_map").c_str();
      if (zkUserChainMapDir_.empty()) {
        LOG(FATAL) << "users.zookeeper_userchain_map cannot be empty!";
      }
      if (zkUserChainMapDir_[zkUserChainMapDir_.size()-1] != '/') {
        zkUserChainMapDir_ += '/';
      }
    }
  }
  else {
    // required (exception will be threw if inexists)
    addChainVars(config.lookup("users.list_id_api_url"));
  }
  
  pthread_rwlock_init(&nameChainlock_, nullptr);
}

UserInfo::~UserInfo() {
  stop();

  for (ChainVars &chain : chains_) {
    if (chain.threadUpdate_.joinable())
      chain.threadUpdate_.join();

    if (chain.threadInsertWorkerName_.joinable())
      chain.threadInsertWorkerName_.join();

    pthread_rwlock_destroy(chain.nameIdlock_);
    delete chain.nameIdlock_;
    delete chain.workerNameLock_;
  }
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
  if (stripUserSuffix_) {
    size_t pos = userName.rfind(userSuffixSeparator_);
    if (pos != userName.npos) {
      userName = userName.substr(0, pos);
      DLOG(INFO) << "User Suffix Stripped: " << userName;
    }
  }
}

bool UserInfo::getChainIdFromZookeeper(const string &userName, size_t &chainId) {
  try {
    // Prevent buffer overflow attacks on zookeeper
    if (userName.size() > 200) {
      LOG(WARNING) << "UserInfo::getChainIdFromZookeeper(): too long username: " << userName;
      return false;
    }

    string nodePath = zkUserChainMapDir_ + userName;
    string chainName;
    chainName.resize(64);
    if (zk_->getValueW(nodePath, chainName, handleZookeeperEvent, this)) {
      DLOG(INFO) << "zk userchain map: " << userName << " : " << chainName;
      for (chainId = 0; chainId < server_->chains_.size(); chainId++) {
        if (chainName == server_->chains_[chainId].name_) {
          // add to cache
          pthread_rwlock_wrlock(&nameChainlock_);
          nameChains_[userName] = chainId;
          pthread_rwlock_unlock(&nameChainlock_);
          return true;
        }
      }
      // cannot find the chain, warning and ignore it
      LOG(WARNING) << "UserInfo::getChainIdFromZookeeper(): Unknown chain name '"<< chainName << "' in zookeeper node '" << nodePath << "'.";
    }
    else {
      LOG(INFO) << "cannot find mining chain in zookeeper, user name: " << userName << " (" << nodePath << ")";
    }
  }
  catch (const std::exception &ex) {
    LOG(ERROR) << "UserInfo::getChainIdFromZookeeper(): zk_->getValueW() failed: " << ex.what();
  }
  catch (...) {
    LOG(ERROR) << "UserInfo::getChainIdFromZookeeper(): unknown exception";
  }
  return false;
}

void UserInfo::handleZookeeperEvent(zhandle_t *zh, int type, int state, const char *path, void *pUserInfo) {
  DLOG(INFO) << "Zookeeper::globalWatcher: type:" << type << ", state:" << state << ", path:" << path;
  UserInfo *userInfo = (UserInfo *)pUserInfo;
  string nodePath(path);
  string userName = nodePath.substr(userInfo->zkUserChainMapDir_.size());

  // lookup cache
  pthread_rwlock_rdlock(&userInfo->nameChainlock_);
  auto itr = userInfo->nameChains_.find(userName);
  pthread_rwlock_unlock(&userInfo->nameChainlock_);

  if (itr == userInfo->nameChains_.end()) {
    LOG(INFO) << "No workers of user " << userName << " online, switching request will be ignored";
    return;
  }
  size_t currentChainId = itr->second;
  size_t newChainId;
  if (!userInfo->getChainIdFromZookeeper(userName, newChainId)) {
    LOG(ERROR) << "UserInfo::handleZookeeperEvent(): cannot get chain id from zookeeper, switching request will be ignored";
    return;
  }
  if (currentChainId == newChainId) {
    LOG(INFO) << "Ignore empty switching request for user '" << userName << "': "
              << userInfo->server_->chainName(currentChainId) << " -> " << userInfo->server_->chainName(newChainId);
    return;
  }

  size_t switchedSessions = userInfo->server_->switchChain(userName, newChainId);

  if (switchedSessions == 0) {
    LOG(INFO) << "No workers of user " << userName << " online, subsequent switching request will be ignored";
    // clear cache
    pthread_rwlock_wrlock(&userInfo->nameChainlock_);
    auto itr = userInfo->nameChains_.find(userName);
    if (itr != userInfo->nameChains_.end()) {
      userInfo->nameChains_.erase(itr);
    }
    pthread_rwlock_unlock(&userInfo->nameChainlock_);
  }
  
  LOG(INFO) << "User '" << userName << "' (" << switchedSessions << " miners) switched chain: "
            << userInfo->server_->chainName(currentChainId) << " -> " << userInfo->server_->chainName(newChainId);
}

bool UserInfo::getChainId(string userName, size_t &chainId) {
  if (chains_.size() == 1) {
    chainId = 0;
    return true;
  }

  regularUserName(userName);
  
  // lookup name -> chain cache map
  pthread_rwlock_rdlock(&nameChainlock_);
  auto itr = nameChains_.find(userName);
  pthread_rwlock_unlock(&nameChainlock_);

  if (itr != nameChains_.end()) {
    chainId = itr->second;
    return true;
  }

  // lookup zookeeper
  if (getChainIdFromZookeeper(userName, chainId)) {
    return true;
  }

  // lookup each chain
  // The first chain's id that find the user will be returned.
  for (chainId = 0; chainId < chains_.size(); chainId++) {
    ChainVars &chain = chains_[chainId];

    pthread_rwlock_rdlock(chain.nameIdlock_);
    auto itr = chain.nameIds_.find(userName);
    pthread_rwlock_unlock(chain.nameIdlock_);

    if (itr != chain.nameIds_.end()) {
      // chainId has been assigned to the correct value
      DLOG(INFO) << "userName: " << userName << ", chainId: " << chainId;
      return true;
    }
  }

  // Not found in all chains
  return false;
}

int32_t UserInfo::getUserId(size_t chainId, string userName) {
  ChainVars &chain = chains_[chainId];
  regularUserName(userName);

  pthread_rwlock_rdlock(chain.nameIdlock_);
  auto itr = chain.nameIds_.find(userName);
  pthread_rwlock_unlock(chain.nameIdlock_);

  if (itr != chain.nameIds_.end()) {
    return itr->second;
  }
  return 0;  // not found
}

#ifdef USER_DEFINED_COINBASE
////////////////////// User defined coinbase enabled //////////////////////

// getCoinbaseInfo
string UserInfo::getCoinbaseInfo(size_t chainId, int32_t userId) {
  ChainVars &chain = chains_[chainId];
  pthread_rwlock_rdlock(&chain.nameIdlock_);
  auto itr = chain.idCoinbaseInfos_.find(userId);
  pthread_rwlock_unlock(&chain.nameIdlock_);

  if (itr != chain.idCoinbaseInfos_.end()) {
    return itr->second;
  }
  return "";  // not found
}

int32_t UserInfo::incrementalUpdateUsers(size_t chainId) {
  ChainVars &chain = chains_[chainId];

  //
  // WARNING: The API is incremental update, we use `?last_id=*&last_time=*` to make sure
  //          always get the new data. Make sure you have use `last_id` and `last_time` in API.
  //
  const string url = Strings::Format("%s?last_id=%d&last_time=%" PRId64, chain.apiUrl_.c_str(), chain.lastMaxUserId_, chain.lastTime_);
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
  chain.lastTime_ = data["time"].int64();

  pthread_rwlock_wrlock(&chain.nameIdlock_);
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

    if (userId > chain.lastMaxUserId_) {
      chain.lastMaxUserId_ = userId;
    }
    chain.nameIds_[userName] = userId;

    // get user's coinbase info
    LOG(INFO) << "user id: " << userId << ", coinbase info: " << coinbaseInfo;
    chain.idCoinbaseInfos_[userId] = coinbaseInfo;

  }
  pthread_rwlock_unlock(&chain.nameIdlock_);

  return vUser->size();
}

/////////////////// End of user defined coinbase enabled ///////////////////
#else
////////////////////// User defined coinbase disabled //////////////////////

int32_t UserInfo::incrementalUpdateUsers(size_t chainId) {
  ChainVars &chain = chains_[chainId];

  //
  // WARNING: The API is incremental update, we use `?last_id=` to make sure
  //          always get the new data. Make sure you have use `last_id` in API.
  //
  const string url = Strings::Format("%s?last_id=%d", chain.apiUrl_.c_str(), chain.lastMaxUserId_);
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

  pthread_rwlock_wrlock(chain.nameIdlock_);
  for (const auto &itr : *vUser) {
    string userName(itr.key_start(), itr.key_end() - itr.key_start());
    regularUserName(userName);

    const int32_t userId   = itr.int32();
    if (userId > chain.lastMaxUserId_) {
      chain.lastMaxUserId_ = userId;
    }

    chain.nameIds_.insert(std::make_pair(userName, userId));
  }
  pthread_rwlock_unlock(chain.nameIdlock_);

  return vUser->size();
}

/////////////////// End of user defined coinbase disabled ///////////////////
#endif

void UserInfo::runThreadUpdate(size_t chainId) {
  //
  // get all user list, incremental update model.
  //
  // We use `offset` in incrementalUpdateUsers(), will keep update uitl no more
  // new users. Most of http API have timeout limit, so can't return lots of
  // data in one request.
  //

  const time_t updateInterval = 10;  // seconds
  time_t lastUpdateTime = time(nullptr);

  while (running_) {
    int32_t res = incrementalUpdateUsers(chainId);
    lastUpdateTime = time(nullptr);

    if (res > 0) {
      LOG(INFO) << "chain " << server_->chainName(chainId) << " update users count: " << res;
    }
    
    if (lastUpdateTime + updateInterval > time(nullptr)) {
      usleep(500000);  // 500ms
      continue;
    }
  }
}

bool UserInfo::setupThreads() {
  for (size_t chainId =0; chainId < chains_.size(); chainId++) {
    ChainVars &chain = chains_[chainId];

    chain.threadUpdate_ = thread(&UserInfo::runThreadUpdate, this, chainId);
    chain.threadInsertWorkerName_ = thread(&UserInfo::runThreadInsertWorkerName, this, chainId);
  }

  return true;
}

void UserInfo::addWorker(const size_t chainId,
                         const int32_t userId, const int64_t workerId,
                         const string &workerName, const string &minerAgent) {
  ChainVars &chain = chains_[chainId];
  ScopeLock sl(*chain.workerNameLock_);

  // insert to Q
  chain.workerNameQ_.push_back(WorkerName());
  chain.workerNameQ_.rbegin()->userId_   = userId;
  chain.workerNameQ_.rbegin()->workerId_ = workerId;

  // worker name
  snprintf(chain.workerNameQ_.rbegin()->workerName_,
           sizeof(chain.workerNameQ_.rbegin()->workerName_),
           "%s", workerName.c_str());
  // miner agent
  snprintf(chain.workerNameQ_.rbegin()->minerAgent_,
           sizeof(chain.workerNameQ_.rbegin()->minerAgent_),
           "%s", minerAgent.c_str());
}

void UserInfo::removeWorker(const size_t chainId, const int32_t userId, const int64_t workerId) {
  // no action at current
}

void UserInfo::runThreadInsertWorkerName(size_t chainId) {
  while (running_) {
    if (insertWorkerName(chainId) > 0) {
      continue;
    }
    sleep(1);
  }
}

int32_t UserInfo::insertWorkerName(size_t chainId) {
  ChainVars &chain = chains_[chainId];
  std::deque<WorkerName>::iterator itr = chain.workerNameQ_.end();
  {
    ScopeLock sl(*chain.workerNameLock_);
    if (chain.workerNameQ_.size() == 0)
      return 0;
    itr = chain.workerNameQ_.begin();
  }

  if (itr == chain.workerNameQ_.end())
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
    server_->sendCommonEvents2Kafka(chainId, eventJson);
  }


  {
    ScopeLock sl(*chain.workerNameLock_);
    chain.workerNameQ_.pop_front();
  }
  return 1;
}

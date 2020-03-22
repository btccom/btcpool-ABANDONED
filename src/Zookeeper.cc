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

#include <algorithm>
#include <bitset>
#include <pthread.h>
#include <unistd.h>
#include <assert.h>
#include <zookeeper/zookeeper.h>
#include <zookeeper/proto.h>
#include <glog/logging.h>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <nlohmann/json.hpp>

#include "Zookeeper.h"
#include "Utils.h"
#include "Network.h"

using JSON = nlohmann::json;
using JSONException = nlohmann::detail::exception;

using std::bitset;

//------------------------------- ZookeeperException
//-------------------------------

ZookeeperException::ZookeeperException(const string &what_arg)
  : std::runtime_error(what_arg) {
  // no more action than its parent
}

//------------------------------- ZookeeperLock -------------------------------

ZookeeperLock::ZookeeperLock(
    Zookeeper *zk, string parentPath, function<void()> lockLostCallback)
  : zk_(zk)
  , locked_(false)
  , lockLostCallback_(lockLostCallback)
  , parentPath_(parentPath) {
  auto uuidGen = boost::uuids::random_generator();
  uuid_ = boost::uuids::to_string(uuidGen());
}

void ZookeeperLock::createLockNode() {
  string nodePath = parentPath_ + "/node";

  zk_->createNodesRecursively(parentPath_);
  zk_->createLockNode(nodePath, nodePathWithSeq_, uuid_);

  nodeName_ = nodePathWithSeq_.substr(parentPath_.size() + 1);

  LOG(INFO) << "ZookeeperLock: lock node " << nodePathWithSeq_ << " (" << uuid_
            << ")";
}

int ZookeeperLock::getSelfPosition(const vector<string> &nodes) {
  LOG(INFO) << "ZookeeperLock: fight for lock " << parentPath_ << " with "
            << nodes.size() << " clients";

  int selfPosition = -1;
  for (size_t i = 0; i < nodes.size(); i++) {
    if (nodeName_ == nodes[i]) {
      selfPosition = i;
      LOG(INFO) << "  * " << i << ". " << nodes[i];
    } else {
      LOG(INFO) << "    " << i << ". " << nodes[i];
    }
  }

  return selfPosition;
}

bool ZookeeperLock::isLocked() {
  return locked_;
}

void ZookeeperLock::getLock() {
  createLockNode();

  // Wait the lock.
  // It isn't a busy waiting because the thread will be
  // blocked with pthread_mutex_lock() until zookeeper
  // event awake it.
  for (;;) {
    vector<string> nodes = getLockNodes();
    // it should not be 0 because of a new node added by the process.
    assert(nodes.size() > 0);

    int selfPosition = getSelfPosition(nodes);
    // self should not out of the range.
    assert(selfPosition >= 0 && selfPosition < (int)nodes.size());

    // The first client will get the lock.
    if (selfPosition == 0) {
      LOG(INFO) << "ZookeeperLock: got the lock " << parentPath_;
      locked_ = true;
      break;
    }

    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

    // Set a callback function to awake the thread.
    // Watching the node that is one ahead of self.
    string watchingNode = parentPath_ + "/" + nodes[selfPosition - 1];
    zk_->watchNode(watchingNode, getLockWatcher, &mutex);

    // Block to wait for the previous client to release the lock.
    // Lock twice so the thread blocked.
    // The callback function getLockWatcher() will unlock the mutex.
    pthread_mutex_lock(&mutex);
    pthread_mutex_lock(&mutex);
  }
}

vector<string> ZookeeperLock::getLockNodes() {
  vector<string> nodes = zk_->getChildren(parentPath_);
  std::sort(nodes.begin(), nodes.end());
  return nodes;
}

void ZookeeperLock::getLockWatcher(
    zhandle_t *zh, int type, int state, const char *path, void *pMutex) {
  if (pMutex == nullptr) {
    return;
  }
  DLOG(INFO) << "ZookeeperLock::getLockWatcher: type:" << type
             << ", state:" << state
             << ", path:" << (path != nullptr ? path : "");
  pthread_mutex_unlock((pthread_mutex_t *)pMutex);
}

void ZookeeperLock::recoveryLock() {
  if (!locked_) {
    return;
  }

  LOG(INFO) << "ZookeeperLock: recovery lock " << nodePathWithSeq_ << " ("
            << uuid_ << ")";

  try {
    string currUuid = zk_->getValue(nodePathWithSeq_, uuid_.size());
    if (currUuid == uuid_) {
      LOG(INFO) << "ZookeeperLock: lock " << nodePathWithSeq_ << " ("
                << currUuid << ") doesn't lose";
      return;
    }
  } catch (const ZookeeperException &ex) {
    // ignore
  }

  LOG(WARNING) << "ZookeeperLock: lock " << nodePathWithSeq_ << " (" << uuid_
               << ") lost, try get a new one";
  locked_ = false;

  createLockNode();
  vector<string> nodes = getLockNodes();
  // it should not be 0 because of a new node added by the process.
  assert(nodes.size() > 0);

  LOG(INFO) << "ZookeeperLock: fight for lock " << parentPath_ << " with "
            << nodes.size() << " clients";

  int selfPosition = getSelfPosition(nodes);
  // self should not out of the range.
  assert(selfPosition >= 0 && selfPosition < (int)nodes.size());

  // The first client will get the lock.
  if (selfPosition != 0) {
    // remove the lock from zookeeper and manager
    zk_->deleteNode(nodePathWithSeq_);
    zk_->removeLock(shared_ptr<ZookeeperLock>(this));

    if (!lockLostCallback_) {
      throw ZookeeperException(
          string("ZookeeperLock: cannot recovery lock ") + parentPath_);
    }

    lockLostCallback_();
    return;
  }

  LOG(INFO) << "ZookeeperLock: lock recovered as " << nodePathWithSeq_ << " ("
            << uuid_ << ")";
  locked_ = true;
  return;
}

//------------------------------- ZookeeperUniqIdT
//-------------------------------

template <uint8_t IBITS>
ZookeeperUniqIdT<IBITS>::ZookeeperUniqIdT(
    Zookeeper *zk,
    string parentPath,
    const string &userData,
    function<void()> idLostCallback)
  : zk_(zk)
  , assigned_(false)
  , idLostCallback_(idLostCallback)
  , parentPath_(parentPath)
  , id_(0) {
  auto uuidGen = boost::uuids::random_generator();
  uuid_ = boost::uuids::to_string(uuidGen());

  JSON json = {
      {"uuid", uuid_},
      {"created_at", date("%F %T")},
      {"host",
       {
           {"hostname", IpAddress::getHostName()},
           {"ip", IpAddress::getInterfaceAddrs()},
       }},
      {"data", userData},
  };

  data_ = json.dump();
}

template <uint8_t IBITS>
size_t ZookeeperUniqIdT<IBITS>::assignID() {
  bitset<kIdUpperLimit> idSet;
  idSet.set(0); // 0 is a invalid id

  vector<string> nodes = getIdNodes();
  for (const string &idStr : nodes) {
    size_t id = strtoull(idStr.c_str(), nullptr, 10);
    idSet.set(id);
  }

  if (idSet.count() >= kIdUpperLimit) {
    throw ZookeeperException(
        string("ZookeeperUniqIdT: cannot assign an ID, ID pool is full"));
  }

  id_ = 0;
  for (size_t testedId = kIdLowerLimit + 1; testedId < kIdUpperLimit;
       testedId++) {
    if (!idSet.test(testedId) && createIdNode(testedId)) {
      id_ = testedId;
      break;
    }
  }

  if (id_ <= kIdLowerLimit || id_ >= kIdUpperLimit) {
    throw ZookeeperException(
        string("ZookeeperUniqIdT: got a invalid id ") + std::to_string(id_) +
        ", but id should be " + std::to_string(kIdLowerLimit) + " < id < " +
        std::to_string(kIdUpperLimit));
  }

  LOG(INFO) << "ZookeeperUniqIdT: id " << nodePath_ << " (" << uuid_
            << ") assigned";
  assigned_ = true;
  return id_;
}

template <uint8_t IBITS>
void ZookeeperUniqIdT<IBITS>::recoveryID() {
  if (!assigned_) {
    return;
  }

  LOG(INFO) << "ZookeeperUniqIdT: recovery id " << nodePath_ << " (" << uuid_
            << ")";

  try {
    string currData = zk_->getValue(nodePath_, data_.size());
    if (currData == data_) {
      LOG(INFO) << "ZookeeperUniqIdT: id " << nodePath_ << " (" << uuid_
                << ") doesn't lose";
      return;
    }
  } catch (const ZookeeperException &ex) {
    // ignore
  }

  LOG(WARNING) << "ZookeeperUniqIdT: id " << nodePath_ << " (" << uuid_
               << ") lost, try get a new one";
  assigned_ = false;

  if (!createIdNode(id_)) {
    zk_->removeUniqId(shared_ptr<ZookeeperUniqId>(this));

    if (!idLostCallback_) {
      throw ZookeeperException(
          string("ZookeeperUniqIdT: cannot recovery id ") + nodePath_);
    }

    idLostCallback_();
    return;
  }

  LOG(INFO) << "ZookeeperUniqIdT: id " << nodePath_ << " (" << uuid_
            << ") recovered";
  assigned_ = true;
  return;
}

template <uint8_t IBITS>
bool ZookeeperUniqIdT<IBITS>::isAssigned() {
  return assigned_;
}

template <uint8_t IBITS>
vector<string> ZookeeperUniqIdT<IBITS>::getIdNodes() {
  zk_->createNodesRecursively(parentPath_);
  return zk_->getChildren(parentPath_);
}

template <uint8_t IBITS>
bool ZookeeperUniqIdT<IBITS>::createIdNode(size_t id) {
  nodePath_ = parentPath_ + "/" + std::to_string(id);

  try {
    zk_->createNodesRecursively(parentPath_);
    zk_->createEphemeralNode(nodePath_, data_);
    return true;
  } catch (const ZookeeperException &ex) {
    LOG(WARNING) << "ZookeeperUniqIdT::createIdNode: assign id " << nodePath_
                 << " failed: " << ex.what();
    return false;
  }
}

// Class template instantiation
template class ZookeeperUniqIdT<8>;

ZookeeperValueWatcher::ZookeeperValueWatcher(
    Zookeeper &zk,
    const string &path,
    size_t sizeLimit,
    std::function<void(const std::string &)> callback)
  : zk_{zk}
  , path_{path}
  , sizeLimit_{sizeLimit}
  , callback_{move(callback)}
  , zkWatcher_{zk.registerConnectionWatcher(
        [this](bool connected) { handleConnection(connected); })} {
  watchValue();
}

void ZookeeperValueWatcher::handleConnection(bool connected) {
  if (connected) {
    watchValue();
  }
}

void ZookeeperValueWatcher::watchValue() {
  try {
    zk_.watchNode(path_, &ZookeeperValueWatcher::watchCallback, this);
    callback_(zk_.getValue(path_, sizeLimit_));
  } catch (const std::exception &ex) {
    LOG(ERROR) << "ZookeeperValueWatcher::watchValue() exception: "
               << ex.what();
  } catch (...) {
    LOG(ERROR) << "ZookeeperValueWatcher::watchValue(): unknown exception";
  }
}

void ZookeeperValueWatcher::watchCallback(
    zhandle_t *zh, int type, int state, const char *path, void *data) {
  if (type == ZOO_CREATED_EVENT || type == ZOO_DELETED_EVENT ||
      type == ZOO_CHANGED_EVENT) {
    static_cast<ZookeeperValueWatcher *>(data)->watchValue();
  }
}

//------------------------------- Zookeeper -------------------------------

void Zookeeper::globalWatcher(
    zhandle_t *zh, int type, int state, const char *path, void *pZookeeper) {
  try {
    if (pZookeeper == nullptr) {
      return;
    }

    DLOG(INFO) << "Zookeeper::globalWatcher: type:" << type
               << ", state:" << state
               << ", path:" << (path != nullptr ? path : "");

    Zookeeper *zk = (Zookeeper *)pZookeeper;

    if (type != ZOO_SESSION_EVENT) {
      return;
    }

    if (zk->connected_) {
      if (state == ZOO_CONNECTED_STATE) {
        LOG(INFO) << "Zookeeper: reconnected to broker.";
        zk->connectionSignal_(true);
        zk->recoveryLock();
        zk->recoveryUniqId();

        // Execute custom reconnect handles.
        for (auto func : zk->reconnectHandles_) {
          func();
        }
      } else if (state == ZOO_CONNECTING_STATE) {
        LOG(ERROR) << "Zookeeper: lost the connection from broker.";
      } else if (state == ZOO_AUTH_FAILED_STATE) {
        LOG(ERROR) << "Zookeeper: the session auth failed. Try reconnect.";
        zk->connectionSignal_(false);
        zk->recoverySession();
      } else if (state == ZOO_EXPIRED_SESSION_STATE) {
        LOG(ERROR) << "Zookeeper: the session is expired. Try reconnect.";
        zk->connectionSignal_(false);
        zk->recoverySession();
      }
    } else {
      if (state == ZOO_CONNECTED_STATE) {
        LOG(INFO) << "Zookeeper: connected to broker.";
        zk->connected_ = true;
        pthread_cond_signal(&zk->watchctx_.cond);

        // Execute custom reconnect handles.
        // Run here after recoverySession() is complete.
        for (auto func : zk->reconnectHandles_) {
          func();
        }
      }
    }
  } catch (const std::exception &ex) {
    LOG(FATAL) << ex.what();
    google::ShutdownGoogleLogging();
  }
}

Zookeeper::Zookeeper(const string &brokers)
  : brokers_(brokers)
  , zh_(nullptr)
  , connected_(false) {
  pthread_cond_init(&watchctx_.cond, NULL);
  pthread_mutex_init(&watchctx_.lock, NULL);
  pthread_mutex_lock(&watchctx_.lock);

  connect();
}

boost::signals2::connection
Zookeeper::registerConnectionWatcher(std::function<void(bool)> watcher) {
  return connectionSignal_.connect(move(watcher));
}

void Zookeeper::connect() {
  LOG(INFO) << "Zookeeper: connecting to brokers: " << brokers_;

  zh_ = zookeeper_init(
      brokers_.c_str(),
      Zookeeper::globalWatcher,
      ZOOKEEPER_CONNECT_TIMEOUT,
      nullptr,
      this,
      0);

  if (zh_ == nullptr) {
    throw ZookeeperException(string("Zookeeper init failed: ") + zerror(errno));
  }

  for (int i = 0; i <= 20 && zoo_state(zh_) != ZOO_CONNECTED_STATE; i += 5) {
    LOG(INFO) << "Zookeeper: connecting to brokers in " << i << "s";

    struct timeval now;
    struct timespec outtime;

    gettimeofday(&now, NULL);

    outtime.tv_sec = now.tv_sec + 5;
    outtime.tv_nsec = 0;

    int waitResult =
        pthread_cond_timedwait(&watchctx_.cond, &watchctx_.lock, &outtime);
    if (waitResult == 0) {
      break;
    }
  }

  if (zoo_state(zh_) != ZOO_CONNECTED_STATE) {
    ZookeeperException ex("Zookeeper: connecting to zookeeper brokers failed!");
    throw ex;
  }
}

Zookeeper::~Zookeeper() {
  disconnect();
}

void Zookeeper::disconnect() {
  connected_ = false;
  if (zh_ != nullptr) {
    zookeeper_close(zh_);
  }
}

void Zookeeper::getLock(
    const string &lockPath, function<void()> lockLostCallback) {
  auto lock = std::make_shared<ZookeeperLock>(this, lockPath, lockLostCallback);
  lock->getLock();
  locks_.push_back(lock);
}

uint8_t Zookeeper::getUniqIdUint8(
    string parentPath,
    const string &userData,
    function<void()> idLostCallback) {
  auto zkId = shared_ptr<ZookeeperUniqId>(
      new ZookeeperUniqIdT<8>(this, parentPath, userData, idLostCallback));
  uint8_t id = (uint8_t)zkId->assignID();
  uniqIds_.push_back(zkId);
  return id;
}

bool Zookeeper::removeLock(shared_ptr<ZookeeperLock> lock) {
  auto itr = locks_.begin();
  while (itr != locks_.end()) {
    if (*itr == lock) {
      locks_.erase(itr);
      return true;
    }
  }
  return false;
}

bool Zookeeper::removeUniqId(shared_ptr<ZookeeperUniqId> id) {
  auto itr = uniqIds_.begin();
  while (itr != uniqIds_.end()) {
    if (*itr == id) {
      uniqIds_.erase(itr);
      return true;
    }
  }
  return false;
}

void Zookeeper::registerReconnectHandle(std::function<void()> func) {
  reconnectHandles_.push_back(func);
}

void Zookeeper::recoveryLock() {
  for (auto lock : locks_) {
    lock->recoveryLock();
  }
}

void Zookeeper::recoveryUniqId() {
  for (auto id : uniqIds_) {
    id->recoveryID();
  }
}

void Zookeeper::recoverySession() {
  disconnect();
  connect();
  recoveryLock();
  recoveryUniqId();
}

void Zookeeper::watchNode(
    string path, ZookeeperWatcherCallback func, void *data) {
  int stat = zoo_wexists(zh_, path.c_str(), func, data, nullptr);

  if (stat != ZOK) {
    throw ZookeeperException(
        string("Zookeeper::existsW: watch node ") + path +
        " failed: " + zerror(stat));
  }
}

string Zookeeper::getValue(const string &nodePath, size_t sizeLimit) {
  string data;

  data.resize(sizeLimit);
  int size = data.size();
  int stat =
      zoo_get(zh_, nodePath.c_str(), 0, (char *)data.data(), &size, nullptr);

  if (stat != ZOK) {
    throw ZookeeperException(
        string("Zookeeper::getNode: get node ") + nodePath +
        " failed:" + zerror(stat));
  }

  // size may be -1 if the node has no value
  if (size < 0) {
    size = 0;
  }
  data.resize(size);
  return data;
}

bool Zookeeper::getValueW(
    const string &nodePath,
    string &value,
    size_t sizeLimit,
    ZookeeperWatcherCallback func,
    void *data) {

  value.resize(sizeLimit);
  int size = value.size();
  int stat = zoo_wget(
      zh_, nodePath.c_str(), func, data, (char *)value.data(), &size, nullptr);

  if (stat != ZOK) {
    return false;
  }

  // size may be -1 if the node has no value
  if (size < 0) {
    size = 0;
  }
  value.resize(size);
  return true;
}

vector<string> Zookeeper::getChildren(const string &parentPath) {
  struct String_vector nodes = {0, nullptr};
  vector<string> children;

  int stat = zoo_get_children(zh_, parentPath.c_str(), 0, &nodes);

  if (stat != ZOK) {
    throw ZookeeperException(
        string("Zookeeper::getChildren: get children for ") + parentPath +
        " failed:" + zerror(stat));
  }

  for (int i = 0; i < nodes.count; i++) {
    children.push_back(nodes.data[i]);
  }

  return children;
}

bool Zookeeper::getChildrenW(
    const string &parentPath,
    vector<string> &children,
    ZookeeperWatcherCallback func,
    void *data) {
  struct String_vector nodes = {0, nullptr};
  int stat = zoo_wget_children(zh_, parentPath.c_str(), func, data, &nodes);

  if (stat != ZOK) {
    return false;
  }

  for (int i = 0; i < nodes.count; i++) {
    children.push_back(nodes.data[i]);
  }
  return true;
}

void Zookeeper::createNode(const string &nodePath, const string &value) {
  int stat = zoo_create(
      zh_,
      nodePath.c_str(),
      value.c_str(),
      value.size(),
      &ZOO_OPEN_ACL_UNSAFE,
      0,
      nullptr,
      0);

  if (stat != ZOK) {
    throw ZookeeperException(
        string("Zookeeper::createNode: create node ") + nodePath +
        " failed: " + zerror(stat));
  }
}

void Zookeeper::createLockNode(
    const string &nodePath, string &nodePathWithSeq, const string &value) {
  nodePathWithSeq.resize(
      nodePath.size() + 20); // only need 10 bytes, 20 for safety.

  // The ZOO_EPHEMERAL node will disapper if the client offline.
  // ZOO_SEQUENCE will appened a increasing sequence after nodePath (assign to
  // nodePathWithSeq). The sequence number is always fixed length of 10 digits,
  // 0 padded.
  int stat = zoo_create(
      zh_,
      nodePath.c_str(),
      value.c_str(),
      value.size(),
      &ZOO_READ_ACL_UNSAFE,
      ZOO_EPHEMERAL | ZOO_SEQUENCE,
      (char *)nodePathWithSeq.data(),
      nodePathWithSeq.size());

  if (stat != ZOK) {
    throw ZookeeperException(
        string("Zookeeper::createLockNode: create node ") + nodePath +
        " failed: " + zerror(stat));
  }

  // Remove extra bytes
  nodePathWithSeq.resize(strlen(nodePathWithSeq.c_str()));
}

void Zookeeper::createEphemeralNode(
    const string &nodePath, const string &value) {
  // The ZOO_EPHEMERAL node will disapper if the client offline.
  int stat = zoo_create(
      zh_,
      nodePath.c_str(),
      value.c_str(),
      value.size(),
      &ZOO_READ_ACL_UNSAFE,
      ZOO_EPHEMERAL,
      nullptr,
      0);

  if (stat != ZOK) {
    throw ZookeeperException(
        string("Zookeeper::createEphemeralNode: create node ") + nodePath +
        " failed: " + zerror(stat));
  }
}

void Zookeeper::createNodesRecursively(const string &nodePath) {
  // make it end with "/" so the last part of path will be created in the loop
  string path = nodePath + "/";
  int pathLen = path.length();

  // the path should be 2 or more words and the first char must be '/'
  // (we cannot create the root node "/")
  assert(pathLen >= 2 && path[0] == '/');

  // pos=1: skip the root node "/"
  for (int pos = 1; pos < pathLen; pos++) {
    if (path[pos] == '/') {
      path[pos] = '\0';

      zoo_create(
          zh_, path.c_str(), nullptr, -1, &ZOO_OPEN_ACL_UNSAFE, 0, nullptr, 0);

      path[pos] = '/';
    }
  }

  int stat = zoo_exists(zh_, nodePath.c_str(), 0, nullptr);

  if (stat != ZOK) {
    throw ZookeeperException(
        string("Zookeeper::createNodesRecursively: cannot create node ") +
        nodePath);
  }
}

void Zookeeper::setNode(const string &nodePath, const string &value) {
  int stat = zoo_set(zh_, nodePath.c_str(), value.data(), value.size(), -1);

  if (stat != ZOK) {
    throw ZookeeperException(
        string("Zookeeper::setNode: set node ") + nodePath +
        " failed: " + zerror(stat));
  }
}

void Zookeeper::deleteNode(const string &nodePath) {
  int stat = zoo_delete(zh_, nodePath.c_str(), -1);

  if (stat != ZOK) {
    throw ZookeeperException(
        string("Zookeeper::deleteNode: delete node ") + nodePath +
        " failed: " + zerror(stat));
  }
}

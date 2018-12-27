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

#include <pthread.h>
#include <unistd.h>
#include <assert.h>
#include <zookeeper/zookeeper.h>
#include <zookeeper/proto.h>
#include <glog/logging.h>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/uuid_generators.hpp>

#include "Zookeeper.h"

ZookeeperException::ZookeeperException(const string &what_arg) : std::runtime_error(what_arg) {
  // no more action than its parent
}

ZookeeperLock::ZookeeperLock(Zookeeper *zk, string parentPath, function<void()> lockLostCallback)
  : zk_(zk)
  , locked_(false)
  , lockLostCallback_(lockLostCallback)
  , parentPath_(parentPath)
{
  auto uuidGen = boost::uuids::random_generator();
  uuid_ = boost::uuids::to_string(uuidGen());
}

void ZookeeperLock::createLockNode() {
  string nodePath = parentPath_ + "/node";

  zk_->createNodesRecursively(parentPath_);
  zk_->createLockNode(nodePath, nodePathWithSeq_, uuid_);

  nodeName_ = nodePathWithSeq_.substr(parentPath_.size() + 1);

  LOG(INFO) << "ZookeeperLock: lock node " << nodePathWithSeq_ << " (" << uuid_ << ")";
}

int ZookeeperLock::getSelfPosition(const vector<string> &nodes) {
  LOG(INFO) << "ZookeeperLock: fight for lock " << parentPath_ << " with " << nodes.size() << " clients";

  int selfPosition = -1;
  for (size_t i=0; i<nodes.size(); i++) {
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
  return std::move(nodes);
}

void ZookeeperLock::getLockWatcher(zhandle_t *zh, int type, int state, const char *path, void *pMutex) {
  DLOG(INFO) << "ZookeeperLock::getLockWatcher: type:" << type << ", state:" << state << ", path:" << path;
  pthread_mutex_unlock((pthread_mutex_t *)pMutex);
}

void ZookeeperLock::recoveryLock() {
  if (!locked_) {
    return;
  }

  LOG(INFO) << "ZookeeperLock: recovery lock " << nodePathWithSeq_ << " (" << uuid_ << ")";

  try {
    string uuid = zk_->getValue(nodePathWithSeq_, uuid_.size());
    if (uuid == uuid_) {
      LOG(INFO) << "ZookeeperLock: lock " << nodePathWithSeq_ << " (" << uuid << ") doesn't lose";
      return;
    }
  }
  catch (const ZookeeperException &ex) {
    // ignore
  }

  LOG(WARNING) << "ZookeeperLock: lock " << nodePathWithSeq_ << " (" << uuid_ << ") lost, try get a new one";
  locked_ = false;

  createLockNode();
  vector<string> nodes = getLockNodes();
  // it should not be 0 because of a new node added by the process.
  assert(nodes.size() > 0);

  LOG(INFO) << "ZookeeperLock: fight for lock " << parentPath_ << " with " << nodes.size() << " clients";

  int selfPosition = getSelfPosition(nodes);
  // self should not out of the range.
  assert(selfPosition >= 0 && selfPosition < (int)nodes.size());

  // The first client will get the lock.
  if (selfPosition != 0) {
    if (!lockLostCallback_) {
      throw ZookeeperException(string("ZookeeperLock: cannot recovery lock ") + parentPath_);
    }

    lockLostCallback_();
    return;
  }

  LOG(INFO) << "ZookeeperLock: lock recovered as " << nodePathWithSeq_ << " (" << uuid_ << ")";
  locked_ = true;
  return;
}

void Zookeeper::globalWatcher(zhandle_t *zh, int type, int state, const char *path, void *pZookeeper) {
  DLOG(INFO) << "Zookeeper::globalWatcher: type:" << type << ", state:" << state << ", path:" << path;

  Zookeeper *zk = (Zookeeper *)pZookeeper;

  if (type != ZOO_SESSION_EVENT) {
    return;
  }

  if (zk->connected_) {
    if (state == ZOO_CONNECTED_STATE) {
      LOG(INFO) << "Zookeeper: reconnected to broker.";
      zk->recoveryLock();
    }
    else if (state == ZOO_CONNECTING_STATE) {
      LOG(ERROR) << "Zookeeper: lost the connection from broker.";
    }
    else if (state == ZOO_AUTH_FAILED_STATE) {
      LOG(ERROR) << "Zookeeper: the session auth failed. Try reconnect.";
      zk->recoverySession();
    }
    else if (state == ZOO_EXPIRED_SESSION_STATE) {
      LOG(ERROR) << "Zookeeper: the session is expired. Try reconnect.";
      zk->recoverySession();
    }
  }
  else {
    if (state == ZOO_CONNECTED_STATE) {
      LOG(INFO) << "Zookeeper: connected to broker.";
      zk->connected_ = true;
    }
  }
}

Zookeeper::Zookeeper(const string &brokers)
  : brokers_(brokers)
  , zh_(nullptr)
  , connected_(false)
{
  connect();
}

void Zookeeper::connect() {
  zh_ = zookeeper_init(brokers_.c_str(), Zookeeper::globalWatcher, ZOOKEEPER_CONNECT_TIMEOUT, nullptr, this, 0);

  if (zh_ == nullptr) {
    throw ZookeeperException(string("Zookeeper init failed: ") + zerror(errno));
  }
  
  for (int i=0; i<=20 && zoo_state(zh_)!=ZOO_CONNECTED_STATE; i+=5)
  {
    LOG(INFO) << "Zookeeper: connecting to zookeeper brokers: " << i << "s";

    sleep(5);
  }
  
  if (zoo_state(zh_)!=ZOO_CONNECTED_STATE)
  {
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

void Zookeeper::getLock(const string &lockPath, function<void()> lockLostCallback) {
  auto lock = std::make_shared<ZookeeperLock>(this, lockPath, lockLostCallback);
  lock->getLock();
  locks_.push_back(lock);
}

void Zookeeper::recoveryLock() {
  for (auto lock : locks_) {
    lock->recoveryLock();
  }
}

void Zookeeper::recoverySession() {
  disconnect();
  connect();
  recoveryLock();
}

void Zookeeper::watchNode(string path, ZookeeperWatcherCallback func, void *data) {
  int stat = zoo_wexists(zh_, path.c_str(), func, data, nullptr);

  if (stat != ZOK) {
    throw ZookeeperException(string("Zookeeper::existsW: watch node ") + path +
      " failed: " + zerror(stat));
  }
}

string Zookeeper::getValue(const string &nodePath, size_t sizeLimit) {
  string data;
  data.resize(sizeLimit);

  int size = data.size();
  int stat = zoo_get(zh_, nodePath.c_str(), 0, (char*)data.data(), &size, nullptr);

  if (stat != ZOK) {
    throw ZookeeperException(string("Zookeeper::getNode: get node ") + nodePath +
      " failed:" + zerror(stat));
  }

  data.resize(size);
  return std::move(data);
}

vector<string> Zookeeper::getChildren(const string &parentPath) {
  struct String_vector nodes = {0, nullptr};
  vector<string> children;

  int stat = zoo_get_children(zh_, parentPath.c_str(), 0, &nodes);

  if (stat != ZOK) {
    throw ZookeeperException(string("Zookeeper::getChildren: get children for ") + parentPath +
      " failed:" + zerror(stat));
  }

  for (int i=0; i<nodes.count; i++) {
    children.push_back(nodes.data[i]);
  }

  return children;
}

void Zookeeper::createLockNode(const string &nodePath, string &nodePathWithSeq, const string &value) {
  nodePathWithSeq.resize(nodePath.size() + 20); // only need 10 bytes, 20 for safety.

  // The ZOO_EPHEMERAL node will disapper if the client offline.
  // ZOO_SEQUENCE will appened a increasing sequence after nodePath (assign to nodePathWithSeq).
  // The sequence number is always fixed length of 10 digits, 0 padded.
  int stat = zoo_create(zh_, nodePath.c_str(), value.c_str(), value.size(), &ZOO_READ_ACL_UNSAFE,
    ZOO_EPHEMERAL | ZOO_SEQUENCE, (char*)nodePathWithSeq.data(), nodePathWithSeq.size());

  if (stat != ZOK) {
    throw ZookeeperException(string("Zookeeper::createLockNode: create node ") + nodePath +
      " failed: " + zerror(stat));
  }

  // Remove extra bytes
  nodePathWithSeq.resize(strlen(nodePathWithSeq.c_str()));
}

void Zookeeper::createNodesRecursively(const string &nodePath) {
  // make it end with "/" so the last part of path will be created in the loop
  string path = nodePath + "/";
  int pathLen = path.length();

  // the path should be 2 or more words and the first char must be '/'
  // (we cannot create the root node "/")
  assert(pathLen >= 2 && path[0] == '/');
  
  // pos=1: skip the root node "/"
  for (int pos=1; pos<pathLen; pos++) {
    if (path[pos] == '/') {
      path[pos] = '\0';

      zoo_create(zh_, path.c_str(), nullptr, -1, &ZOO_OPEN_ACL_UNSAFE, 0, nullptr, 0);

      path[pos] = '/';
    }
  }

  int stat = zoo_exists(zh_, nodePath.c_str(), 0, nullptr);

  if (stat != ZOK) {
    throw ZookeeperException(string("Zookeeper::createNodesRecursively: cannot create node ") + nodePath);
  }
}

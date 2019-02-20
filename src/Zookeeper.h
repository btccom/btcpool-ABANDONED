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
#ifndef BTCPOOL_ZOOKEEPER_H_ // <zookeeper/zookeeper.h> has defined ZOOKEEPER_H_
#define BTCPOOL_ZOOKEEPER_H_ // add a prefix BTCPOOL_ to avoid the conflict

#include <pthread.h>
#include <zookeeper/zookeeper.h>
#include <zookeeper/proto.h>

#include <string>
#include <atomic>
#include <memory>
#include <vector>
#include <functional>

using std::string;
using std::atomic;
using std::shared_ptr;
using std::vector;
using std::function;

using ZookeeperWatcherCallback =
    void(zhandle_t *zh, int type, int state, const char *path, void *data);

class Zookeeper;
class ZookeeperLock;
class ZookeeperException;

#define ZOOKEEPER_CONNECT_TIMEOUT 10000 //(ms)

// Exception of Zookeeper
class ZookeeperException : public std::runtime_error {
public:
  explicit ZookeeperException(const string &what_arg);
};

// A Distributed Lock with Zookeeper
class ZookeeperLock {
protected:
  Zookeeper *zk_;
  atomic<bool> locked_;
  function<void()> lockLostCallback_;
  string parentPath_; // example: /locks/jobmaker
  string nodePathWithSeq_; // example: /locks/jobmaker/node0000000010
  string nodeName_; // example: node0000000010
  string uuid_; // example: d3460f9f-d364-4fa9-b41f-4c5fbafc1863

public:
  ZookeeperLock(
      Zookeeper *zk, string parentPath, function<void()> lockLostCallback);
  void getLock();
  void recoveryLock();
  bool isLocked();

protected:
  void createLockNode();
  vector<string> getLockNodes();
  int getSelfPosition(const vector<string> &nodes);
  static void getLockWatcher(
      zhandle_t *zh, int type, int state, const char *path, void *pMutex);
};

// A Distributed Unique ID Allocator
class ZookeeperUniqId {
public:
  virtual ~ZookeeperUniqId() = default;
  virtual size_t assignID() = 0;
  virtual void recoveryID() = 0;
  virtual bool isAssigned() = 0;
};

// template IBITS: index bits
template <uint8_t IBITS>
class ZookeeperUniqIdT : public ZookeeperUniqId {
protected:
  // A valid id should satisfy the following conditions:
  //     kIdLowerLimit < id < kIdUpperLimit
  const static size_t kIdLowerLimit = 0;
  const static size_t kIdUpperLimit = (1 << IBITS);

  Zookeeper *zk_;
  atomic<bool> assigned_;
  function<void()> idLostCallback_;
  string parentPath_; // example: /ids/jobmaker
  string nodePath_; // example: /ids/jobmaker/23
  size_t id_; // example: 23
  string uuid_; // example: d3460f9f-d364-4fa9-b41f-4c5fbafc1863
  string data_; // should be a valid JSON object

public:
  ZookeeperUniqIdT(
      Zookeeper *zk,
      string parentPath,
      const string &userData,
      function<void()> idLostCallback);
  size_t assignID() override;
  void recoveryID() override;
  bool isAssigned() override;

protected:
  vector<string> getIdNodes();
  bool createIdNode(size_t id);
};

class Zookeeper {
protected:
  string brokers_;
  zhandle_t *zh_; // zookeeper handle
  atomic<bool> connected_;

  // Used to unlock the main thread when the connection is ready.
  struct watchctx_t {
    pthread_cond_t cond;
    pthread_mutex_t lock;
  } watchctx_;

  vector<shared_ptr<ZookeeperLock>> locks_;
  vector<shared_ptr<ZookeeperUniqId>> uniqIds_;

public:
  Zookeeper(const string &brokers);
  virtual ~Zookeeper();

  void
  getLock(const string &lockPath, function<void()> lockLostCallback = nullptr);
  uint8_t getUniqIdUint8(
      string parentPath,
      const string &userData = "",
      function<void()> idLostCallback = nullptr);

  string getValue(const string &nodePath, size_t sizeLimit);
  bool getValueW(
      const string &nodePath,
      string &value,
      ZookeeperWatcherCallback func,
      void *data);
  vector<string> getChildren(const string &parentPath);
  void watchNode(string path, ZookeeperWatcherCallback func, void *data);
  void createNode(const string &nodePath, const string &value);
  void createLockNode(
      const string &nodePath, string &nodePathWithSeq, const string &value);
  void createEphemeralNode(const string &nodePath, const string &value);
  void createNodesRecursively(const string &nodePath);
  void deleteNode(const string &nodePath);

  bool removeLock(shared_ptr<ZookeeperLock> lock);
  bool removeUniqId(shared_ptr<ZookeeperUniqId> id);

protected:
  static void globalWatcher(
      zhandle_t *zh, int type, int state, const char *path, void *pZookeeper);

  void connect();
  void disconnect();
  void recoveryLock();
  void recoveryUniqId();
  void recoverySession();
};

#endif // BTCPOOL_ZOOKEEPER_H_

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

#include <zookeeper/zookeeper.h>
#include <zookeeper/proto.h>

#include <string>
#include <atomic>
#include <memory>
#include <vector>
#include <algorithm>
#include <functional>

using std::string;
using std::atomic;
using std::shared_ptr;
using std::vector;
using std::function;

using ZookeeperWatcherCallback = void (zhandle_t *zh, int type, int state, const char *path, void *data);

class Zookeeper;
class ZookeeperLock;
class ZookeeperException;

#define ZOOKEEPER_CONNECT_TIMEOUT 10000 //(ms)

class ZookeeperException : public std::runtime_error {
public:
  explicit ZookeeperException(const string &what_arg);
};

class ZookeeperLock {
protected:
  Zookeeper *zk_;
  atomic<bool> locked_;
  function<void()> lockLostCallback_;
  string parentPath_;      // example: /locks/jobmaker
  string nodePathWithSeq_; // example: /locks/jobmaker/node0000000010
  string nodeName_;        // example: node0000000010
  string uuid_;            // example: d3460f9f-d364-4fa9-b41f-4c5fbafc1863

public:
  ZookeeperLock(Zookeeper *zk, string parentPath, function<void()> lockLostCallback);
  void getLock();
  void recoveryLock();
  bool isLocked();

protected:
  void createLockNode();
  vector<string> getLockNodes();
  int getSelfPosition(const vector<string> &nodes);
  static void getLockWatcher(zhandle_t *zh, int type, int state, const char *path, void *pMutex);
};

class Zookeeper {
protected:
  string brokers_;
  zhandle_t *zh_; // zookeeper handle
  atomic<bool> connected_;

  vector<shared_ptr<ZookeeperLock>> locks_;

public:
  Zookeeper(const string &brokers);
  virtual ~Zookeeper();

  void getLock(const string &lockPath, function<void()> lockLostCallback = nullptr);

  string getValue(const string &nodePath, size_t sizeLimit);
  vector<string> getChildren(const string &parentPath);
  void watchNode(string path, ZookeeperWatcherCallback func, void *data);
  void createLockNode(const string &nodePath, string &nodePathWithSeq, const string &value);
  void createNodesRecursively(const string &nodePath);

protected:
  static void globalWatcher(zhandle_t *zh, int type, int state, const char *path, void *pZookeeper);
  
  void connect();
  void disconnect();
  void recoveryLock();
  void recoverySession();
};

#endif // BTCPOOL_ZOOKEEPER_H_

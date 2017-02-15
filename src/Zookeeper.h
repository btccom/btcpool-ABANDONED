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

#include "Common.h"

#include <zookeeper/zookeeper.h>
#include <zookeeper/proto.h>

#define ZOOKEEPER_CONNECT_TIMEOUT 10000 //(ms)
#define ZOOKEEPER_LOCK_PATH_MAX_LEN 255

class ZookeeperException : public std::runtime_error {
public:
  explicit ZookeeperException(const string &what_arg);
};

class Zookeeper {
  // zookeeper handle
  zhandle_t *zh;

public:
  static int nodeNameCompare(const void *pname1, const void *pname2);

  static void globalWatcher(zhandle_t *zh, int type, int state, const char *path, void *zookeeper);

  static void lockWatcher(zhandle_t *zh, int type, int state, const char *path, void *zookeeper);

  Zookeeper(const char *servers);

  virtual ~Zookeeper();
  
  void getLock(const char *lockParentPath);

  bool doGetLock(const char *lockParentPath, const char *lockNodePath);

  void createLockNode(const char *nodeParentPath, char *newNodePath, int newNodePathMaxLen);

  void createNodesRecursively(const char *nodePath);
};

#endif // BTCPOOL_ZOOKEEPER_H_

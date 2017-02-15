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
#include <zookeeper/zookeeper.h>
#include <zookeeper/proto.h>
#include <glog/logging.h>
#include "Zookeeper.h"

ZookeeperException::ZookeeperException(const string &what_arg) : std::runtime_error(what_arg) {
  // empty
}

int Zookeeper::nodeNameCompare(const void *pname1, const void *pname2) {
  return strcmp(*(const char **)pname1, *(const char **)pname2);
}

void Zookeeper::globalWatcher(zhandle_t *zh, int type, int state, const char *path, void *zookeeper) {
  DLOG(INFO) << "Zookeeper::globalWatcher: type:" << type << ", state:" << state << ", path:" << path;

  if (ZOO_SESSION_EVENT == type && ZOO_CONNECTING_STATE == state) {
    ZookeeperException ex("Zookeeper: lost the connection from broker.");
    LOG(FATAL) << ex.what();
    throw ex;
  }
}

void Zookeeper::lockWatcher(zhandle_t *zh, int type, int state, const char *path, void *pMutex) {
  pthread_mutex_unlock((pthread_mutex_t *)pMutex);
}

Zookeeper::Zookeeper(const char *servers) {
  zh = zookeeper_init(servers, Zookeeper::globalWatcher, ZOOKEEPER_CONNECT_TIMEOUT, NULL, this, 0);

  if (NULL == zh) {
    throw ZookeeperException(string("Zookeeper init failed: ") + zerror(errno));
  }
}

Zookeeper::~Zookeeper() {
  if (NULL != zh) {
    zookeeper_close(zh);
  }
}

void Zookeeper::getLock(const char *lockParentPath) {
  int len;
  char *lockNodeBasePath;
  char *lockNodeFullPath;
  
  len = strlen(lockParentPath);

  lockNodeBasePath = new char[ZOOKEEPER_LOCK_PATH_MAX_LEN];
  lockNodeFullPath = new char[ZOOKEEPER_LOCK_PATH_MAX_LEN];

  memcpy(lockNodeBasePath, lockParentPath, len);
  memcpy(lockNodeBasePath + len, "/node\0", 6);

  createNodesRecursively(lockParentPath);
  createLockNode(lockNodeBasePath, lockNodeFullPath, ZOOKEEPER_LOCK_PATH_MAX_LEN);

  delete lockNodeBasePath;

  // wait the lock
  // it isn't a busy waiting because doGetLock() will
  // block itself with pthread_mutex_lock().
  while (!doGetLock(lockParentPath, lockNodeFullPath));

  delete lockNodeFullPath;
}

bool Zookeeper::doGetLock(const char *lockParentPath, const char *lockNodePath) {
  int i = 0;
  int stat = 0;
  int myNodePosition = -1;
  const char *myNodeName = NULL;
  char watchNodePath[ZOOKEEPER_LOCK_PATH_MAX_LEN];
  struct String_vector nodes = {0, NULL};
  pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

  myNodeName = lockNodePath + strlen(lockParentPath) + 1;

  stat = zoo_get_children(zh, lockParentPath, 0, &nodes);

  if (ZOK != stat) {
    throw ZookeeperException(string("Zookeeper::doGetLock: get children for ") + lockParentPath +
      " failed:" + zerror(stat));
  }

  // it should not be 0 because of a new node added for the process.
  assert(nodes.count > 0);
  
  qsort(nodes.data, nodes.count, sizeof(nodes.data), Zookeeper::nodeNameCompare);

  LOG(INFO) << "Zookeeper: fight for lock with " << nodes.count << " clients";

  for (i=0; i<nodes.count; i++) {
    if (0 == strcmp(myNodeName, nodes.data[i])) {
      myNodePosition = i;

      LOG(INFO) << "  * " << i << ". " << nodes.data[i];
    } else {
      LOG(INFO) << "    " << i << ". " << nodes.data[i];
    }
  }

  // the first client get the lock.
  if (0 == myNodePosition) {
    LOG(INFO) << "Zookeeper: get the lock!";

    return true;

  } else {
    LOG(INFO) << "Zookeeper: wait the lock.";
    
    // myself should not be the first node or out of the range.
    assert(myNodePosition > 0 && myNodePosition < nodes.count);

    // get the before node path
    strcpy(watchNodePath, lockParentPath);
    strcat(watchNodePath, "/");
    strcat(watchNodePath, nodes.data[myNodePosition - 1]);

    LOG(INFO) << "Zookeeper: watch the lock release for " << watchNodePath;

    stat = zoo_wexists(zh, watchNodePath, Zookeeper::lockWatcher, &mutex, NULL);

    if (ZOK != stat) {
      throw ZookeeperException(string("Zookeeper::doGetLock: watch node ") + watchNodePath +
        " failed: " + zerror(stat));
    }

    // block the thread until get the lock
    // lock twice so the thread blocked
    pthread_mutex_lock(&mutex);
    pthread_mutex_lock(&mutex);

    return false;
  }
}

void Zookeeper::createLockNode(const char *nodePath, char *newNodePath, int newNodePathMaxLen) {
  int stat;

  stat = zoo_create(zh, nodePath, NULL, -1, &ZOO_READ_ACL_UNSAFE,
    ZOO_EPHEMERAL | ZOO_SEQUENCE, newNodePath, newNodePathMaxLen);

  if (ZOK != stat) {
    throw ZookeeperException(string("Zookeeper::createLockNode: create node ") + nodePath +
      " failed: " + zerror(stat));
  }
}

void Zookeeper::createNodesRecursively(const char *nodePath) {
  int len;
  char *pathBuffer;
  char *pathPoint;
  int stat;

  len = strlen(nodePath);

  pathBuffer = new char[len + 2];
  
  memcpy(pathBuffer, nodePath, len);
  memcpy(pathBuffer + len, "/\0", 2); // make it end with "/\0" so the loop exit correctly

  assert('/' == pathBuffer[0]); // the first char must be '/'
  
  pathPoint = pathBuffer + 1; // skip the first char '/'

  while ('\0' != *pathPoint) {
    if ('/' == *pathPoint) {
      *pathPoint = '\0';
      
      zoo_create(zh, pathBuffer, NULL, -1, &ZOO_OPEN_ACL_UNSAFE, 0, NULL, 0);

      *pathPoint = '/';
    }

    pathPoint ++;
  }

  delete pathBuffer;

  stat = zoo_exists(zh, nodePath, 0, NULL);

  if (ZOK != stat) {
    throw ZookeeperException(string("Zookeeper::createNodesRecursively: cannot create nodes ") + nodePath);
  }
}


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
#include <zookeeper/zookeeper.h>
#include <zookeeper/proto.h>
#include <glog/logging.h>
#include "Zookeeper.h"

ZookeeperException::ZookeeperException(const string &what_arg) : std::runtime_error(what_arg) {
  // no more action than its parent
}

int Zookeeper::nodeNameCompare(const void *pname1, const void *pname2) {
  return strcmp(*(const char **)pname1, *(const char **)pname2);
}

void Zookeeper::globalWatcher(zhandle_t *zh, int type, int state, const char *path, void *zookeeper) {
  DLOG(INFO) << "Zookeeper::globalWatcher: type:" << type << ", state:" << state << ", path:" << path;

  if (type == ZOO_SESSION_EVENT && state == ZOO_CONNECTING_STATE) {
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

  if (zh == NULL) {
    throw ZookeeperException(string("Zookeeper init failed: ") + zerror(errno));
  }
  
  for (int i=0; i<=20 && zoo_state(zh)!=ZOO_CONNECTED_STATE; i+=5)
  {
    LOG(INFO) << "Zookeeper: connecting to zookeeper brokers: " << i << "s";

	sleep(5);
  }
  
  if (zoo_state(zh)!=ZOO_CONNECTED_STATE)
  {
	ZookeeperException ex("Zookeeper: connecting to zookeeper brokers failed!");
    LOG(FATAL) << ex.what();
    throw ex;
  }
}

Zookeeper::~Zookeeper() {
  if (zh != NULL) {
    zookeeper_close(zh);
  }
}

void Zookeeper::getLock(const char *lockParentPath) {
  string lockNodePath = "";

  // Zookeeper API need the c styled string as a new name buffer.
  // It will append the path with a increasing sequence
  char *lockNodeNewPathBuffer;
  int bufferLen;
  
  // Add 100 bytes for "/node" and the appened string likes "0000000293".
  // The final node path looks like this: "/locks/jobmaker/node0000000293".
  bufferLen = strlen(lockParentPath) + 100;
  lockNodeNewPathBuffer = new char[bufferLen];

  lockNodePath = string(lockParentPath) + "/node";

  createNodesRecursively(lockParentPath);
  createLockNode(lockNodePath.c_str(), lockNodeNewPathBuffer, bufferLen);

  // Wait the lock.
  // It isn't a busy waiting because doGetLock() will
  // block itself with pthread_mutex_lock() until zookeeper
  // event awake it.
  while (!doGetLock(lockParentPath, lockNodeNewPathBuffer));

  delete[] lockNodeNewPathBuffer;
}

bool Zookeeper::doGetLock(const char *lockParentPath, const char *lockNodePath) {
  int i = 0;
  int stat = 0;
  int myNodePosition = -1;
  const char *myNodeName = NULL;
  string watchNodePath = "";
  struct String_vector nodes = {0, NULL};
  pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

  // get the name part (likes "node0000000293") from full path (likes "/locks/jobmaker/node0000000293").
  myNodeName = lockNodePath + strlen(lockParentPath) + 1;

  stat = zoo_get_children(zh, lockParentPath, 0, &nodes);

  if (stat != ZOK) {
    throw ZookeeperException(string("Zookeeper::doGetLock: get children for ") + lockParentPath +
      " failed:" + zerror(stat));
  }

  // it should not be 0 because of a new node added for the process.
  assert(nodes.count > 0);
  
  qsort(nodes.data, nodes.count, sizeof(nodes.data), Zookeeper::nodeNameCompare);

  LOG(INFO) << "Zookeeper: fight for lock with " << nodes.count << " clients";

  for (i=0; i<nodes.count; i++) {
    if (strcmp(myNodeName, nodes.data[i]) == 0) {
      myNodePosition = i;

      LOG(INFO) << "  * " << i << ". " << nodes.data[i];
    } else {
      LOG(INFO) << "    " << i << ". " << nodes.data[i];
    }
  }

  // the first client will get the lock.
  if (myNodePosition == 0) {
    LOG(INFO) << "Zookeeper: got the lock!";

    return true;

  } else {
    LOG(INFO) << "Zookeeper: wait the lock.";
    
    // myself should not be the first node or out of the range.
    assert(myNodePosition > 0 && myNodePosition < nodes.count);

    // get the previous node near myself
    watchNodePath = string(lockParentPath) + "/" + nodes.data[myNodePosition - 1];

    LOG(INFO) << "Zookeeper: watch the lock release for " << watchNodePath;

    // Watch the previous node with callback function.
    stat = zoo_wexists(zh, watchNodePath.c_str(), Zookeeper::lockWatcher, &mutex, NULL);

    if (stat != ZOK) {
      throw ZookeeperException(string("Zookeeper::doGetLock: watch node ") + watchNodePath +
        " failed: " + zerror(stat));
    }

    // block the thread for waiting watch event.
    // lock twice so the thread blocked.
    // the callback function Zookeeper::lockWatcher() will unlock the thread.
    pthread_mutex_lock(&mutex);
    pthread_mutex_lock(&mutex);

    // If the previous node "disapper", the thread awoke
    // and reture "get lock failed at this time",
    // than getLock() will try again in its loop (The next time it
    // may get the lock or just go forward a step in the queue).
    return false;
  }
}

void Zookeeper::createLockNode(const char *nodePath, char *newNodePath, int newNodePathMaxLen) {
  int stat;

  // the ZOO_EPHEMERAL node will disapper if the client offline.
  // ZOO_SEQUENCE will appened a increasing sequence after nodePath (set as newNodePath).
  stat = zoo_create(zh, nodePath, NULL, -1, &ZOO_READ_ACL_UNSAFE,
    ZOO_EPHEMERAL | ZOO_SEQUENCE, newNodePath, newNodePathMaxLen);

  if (stat != ZOK) {
    throw ZookeeperException(string("Zookeeper::createLockNode: create node ") + nodePath +
      " failed: " + zerror(stat));
  }
}

void Zookeeper::createNodesRecursively(const char *nodePath) {
  int stat = 0;
  int pos = 0;

  // make it end with "/" so the last part of path will be created in the loop
  string path = string(nodePath) + "/";
  int pathLen = path.length();

  // the path should be 2 or more words and the first char must be '/'
  // (we cannot create the root node "/")
  assert(pathLen >= 2 && path[0] == '/');
  
  // pos=1: skip the root node "/"
  for (pos=1; pos<pathLen; pos++) {
    if (path[pos] == '/') {
      path[pos] = '\0';

      zoo_create(zh, path.c_str(), NULL, -1, &ZOO_OPEN_ACL_UNSAFE, 0, NULL, 0);

      path[pos] = '/';
    }
  }

  stat = zoo_exists(zh, nodePath, 0, NULL);

  if (stat != ZOK) {
    throw ZookeeperException(string("Zookeeper::createNodesRecursively: cannot create nodes ") + nodePath);
  }
}


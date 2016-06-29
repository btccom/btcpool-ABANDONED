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
#include "StratumSession.h"


////////////////////////////////// Connection //////////////////////////////////
Connection::Connection(evutil_socket_t fd, bufferevent* bev, void* server):
bev_(bev), fd_(fd), server_(server)
{
}

void Connection::send(const char* data, size_t numBytes) {
  if (bufferevent_write(bev_, data, numBytes) == -1) {
    LOG(ERROR) << "error while sending in Connection::send()";
  }
}

//////////////////////////////// StratumWorker ////////////////////////////////
StratumWorker::StratumWorker(): userId_(0), workerHashId_(0) {}

string StratumWorker::getUserName(const string &fullName) {
  auto pos = fullName.find(".");
  if (pos == fullName.npos) {
    return fullName;
  }
  return fullName.substr(0, pos);
}

void StratumWorker::setUserIDAndNames(const int32_t userId, const string &fullName) {
  userId_ = userId;

  auto pos = fullName.find(".");
  if (pos == fullName.npos) {
    userName_   = fullName;
    workerName_ = "default";
  } else {
    userName_   = fullName.substr(0, pos);
    workerName_ = fullName.substr(pos+1);
  }

  // max length for worker name is 20
  if (workerName_.length() > 20) {
    workerName_.resize(20);
  }

  fullName_ = userName_ + "." + workerName_;
}


//////////////////////////////// StratumSession ////////////////////////////////
StratumSession::StratumSession(evutil_socket_t fd, struct bufferevent* bev, void* server)
:Connection(fd, bev, server)
{
  state_ = CONNECTED;
  extraNonce1_ = 0U;

  // usually stratum job interval is 30~60 seconds, 10 is enough for miners
  kMaxNumLocalJobs_ = 10;

  lastNoEOLPos_ = 0;
}

void StratumSession::setup() {
  // TODO:
  // set extraNonce1_
}

void StratumSession::readLine(const char *data, const size_t len) {
  LOG(INFO) << "onRead: " << data << ", len: " << len << ", " << strlen(data);
}

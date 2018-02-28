/*
 The MIT License (MIT)

 Copyright (c) [2018] [BTC.COM]

 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this softwar and associated documentation files (the "Software"), to deal
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

#include <glog/logging.h>

#include "RedisConnection.h"


/////////////////////////////// RedisResult ///////////////////////////////

RedisResult::RedisResult() :
  reply_(nullptr) {
}

RedisResult::RedisResult(redisReply *reply) :
  reply_(reply) {
}

RedisResult::~RedisResult() {
  if (reply_ != nullptr) {
    freeReplyObject(reply_);
    reply_ = nullptr;
  }
}

void RedisResult::reset(redisReply *reply) {
  if (reply_ != nullptr) {
    freeReplyObject(reply_);
  }

  reply_ = reply;
}

bool RedisResult::empty() {
  return reply_ == nullptr;
}

int RedisResult::type() {
  return reply_->type;
}

string RedisResult::str() {
  string resultStr;

  if (reply_->str != nullptr) {
    resultStr = reply_->str;
  }

  return resultStr;
}


/////////////////////////////// RedisConnection ///////////////////////////////

RedisConnection::RedisConnection(const string &host, int32_t port) :
  host_(host), port_(port), conn_(nullptr) {
}

bool RedisConnection::open() {
  conn_ = redisConnect(host_.c_str(), port_);

  if (conn_ == nullptr) {
    LOG(ERROR) << "Connect to redis failed: conn_ is nullptr";
    return false;
  }
  
  if (conn_->err) {
    LOG(ERROR) << "Connect to redis failed: " << conn_->errstr;
    close();
    return false;
  }

  return true;
}

void RedisConnection::close() {
  if (conn_ != nullptr) {
    redisFree(conn_);
    conn_ = nullptr;
  }
}

bool RedisConnection::ping() {
  RedisResult result = execute("PING");

  if (result.empty()) {
    LOG(ERROR) << "ping redis failed: result is empty.";
    return false;
  }

  if (result.type() != REDIS_REPLY_STATUS) {
    LOG(ERROR) << "ping redis failed: result type is " << result.type() << ", "
               << "expected: " << REDIS_REPLY_STATUS << " (REDIS_REPLY_STATUS).";
    return false;
  }

  if (result.str() != "PONG") {
    LOG(ERROR) << "ping redis failed: result is " << result.str() << ", expected \"PONG\".";
    return false;
  }

  return true;
}

RedisResult RedisConnection::execute(const string &command) {
  return RedisResult((redisReply*)redisCommand(conn_, command.c_str()));
}

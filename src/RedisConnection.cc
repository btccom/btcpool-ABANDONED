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

RedisResult::RedisResult()
  : reply_(nullptr) {
}

RedisResult::RedisResult(redisReply *reply)
  : reply_(reply) {
}

RedisResult::RedisResult(RedisResult &&other) {
  reset(other.reply_);
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
  if (empty()) {
    return REDIS_REPLY_NIL;
  }
  return reply_->type;
}

string RedisResult::str() {
  if (empty()) {
    return "";
  }
  if (reply_->str == nullptr) {
    return "";
  }
  return string(reply_->str, reply_->len);
}

long long RedisResult::integer() {
  if (empty()) {
    return 0;
  }
  return reply_->integer;
}

/////////////////////////////// RedisConnection ///////////////////////////////

RedisConnection::RedisConnection(const RedisConnectInfo &connInfo)
  : connInfo_(connInfo)
  , conn_(nullptr) {
}

bool RedisConnection::open() {
  conn_ = redisConnect(connInfo_.host_.c_str(), connInfo_.port_);

  if (conn_ == nullptr) {
    LOG(ERROR) << "Connect to redis failed: conn_ is nullptr";
    return false;
  }

  if (conn_->err) {
    LOG(ERROR) << "Connect to redis failed: " << conn_->errstr;
    close();
    return false;
  }

  LOG(INFO) << "redis: connect success.";
  // auth with password
  if (!connInfo_.passwd_.empty()) {
    RedisResult result = execute({"AUTH", connInfo_.passwd_});

    if (result.empty()) {
      LOG(ERROR) << "redis auth failed: result is empty.";
      close();
      return false;
    }

    if (result.type() != REDIS_REPLY_STATUS || result.str() != "OK") {
      LOG(ERROR) << "redis auth failed: result is " << result.type() << " ("
                 << result.str() << "), "
                 << "expected: " << REDIS_REPLY_STATUS << " (OK).";
      close();
      return false;
    }

    LOG(INFO) << "redis: auth success.";
  }

  return true;
}

void RedisConnection::close() {
  if (conn_ != nullptr) {
    redisFree(conn_);
    conn_ = nullptr;
  }
}

bool RedisConnection::_ping() {
  RedisResult result = execute({"PING"});

  if (result.empty()) {
    LOG(ERROR) << "ping redis failed: result is empty.";
    return false;
  }

  if (result.type() != REDIS_REPLY_STATUS || result.str() != "PONG") {
    LOG(ERROR) << "ping redis failed: result is " << result.type() << " ("
               << result.str() << "), "
               << "expected: " << REDIS_REPLY_STATUS << " (PONG).";
    return false;
  }

  return true;
}

bool RedisConnection::ping() {
  if (conn_ == nullptr) {
    if (!open()) {
      return false;
    }
  }

  if (!_ping()) {
    LOG(INFO) << "RedisConnection: ping failed, try reconnect";
    // try reconnect
    close();
    if (!open()) {
      return false;
    }
    if (!_ping()) {
      return false;
    }
  }

  return true;
}

RedisResult RedisConnection::execute(const string &command) {
  return RedisResult((redisReply *)redisCommand(conn_, command.c_str()));
}

RedisResult RedisConnection::execute(initializer_list<const string> args) {
  auto argc = args.size();
  auto argv = new const char *[argc];
  auto argvlen = new size_t[argc];

  auto arg = args.begin();
  size_t i = 0;

  while (arg != args.end()) {
    argv[i] = arg->c_str();
    argvlen[i] = arg->size();

    arg++;
    i++;
  }

  auto result =
      RedisResult((redisReply *)redisCommandArgv(conn_, argc, argv, argvlen));

  delete[] argv;
  delete[] argvlen;

  return result;
}

RedisResult RedisConnection::execute(const vector<string> &args) {
  auto argc = args.size();
  auto argv = new const char *[argc];
  auto argvlen = new size_t[argc];

  auto arg = args.begin();
  size_t i = 0;

  while (arg != args.end()) {
    argv[i] = arg->c_str();
    argvlen[i] = arg->size();

    arg++;
    i++;
  }

  auto result =
      RedisResult((redisReply *)redisCommandArgv(conn_, argc, argv, argvlen));

  delete[] argv;
  delete[] argvlen;

  return result;
}

void RedisConnection::prepare(const string &command) {
  redisAppendCommand(conn_, command.c_str());
}

void RedisConnection::prepare(initializer_list<const string> args) {
  size_t argc = args.size();
  auto argv = new const char *[argc];
  auto argvlen = new size_t[argc];

  auto arg = args.begin();
  size_t i = 0;

  while (arg != args.end()) {
    argv[i] = arg->c_str();
    argvlen[i] = arg->size();

    arg++;
    i++;
  }

  redisAppendCommandArgv(conn_, argc, argv, argvlen);

  delete[] argv;
  delete[] argvlen;
}

void RedisConnection::prepare(const vector<string> &args) {
  size_t argc = args.size();
  auto argv = new const char *[argc];
  auto argvlen = new size_t[argc];

  auto arg = args.begin();
  size_t i = 0;

  while (arg != args.end()) {
    argv[i] = arg->c_str();
    argvlen[i] = arg->size();

    arg++;
    i++;
  }

  redisAppendCommandArgv(conn_, argc, argv, argvlen);

  delete[] argv;
  delete[] argvlen;
}

RedisResult RedisConnection::execute() {
  void *reply;
  redisGetReply(conn_, &reply);
  return RedisResult((redisReply *)reply);
}

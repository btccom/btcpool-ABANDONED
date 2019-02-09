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
#ifndef REDIS_CONNECTION_H_
#define REDIS_CONNECTION_H_

#include <string>
#include <vector>
#include <hiredis/hiredis.h>

using namespace std;

/*
 * Possible values of `redisReply->type`:
 *   REDIS_REPLY_STRING    1
 *   REDIS_REPLY_ARRAY     2
 *   REDIS_REPLY_INTEGER   3
 *   REDIS_REPLY_NIL       4
 *   REDIS_REPLY_STATUS    5
 *   REDIS_REPLY_ERROR     6
 * From: <hiredis/read.h>
 */

/////////////////////////////// RedisResult ///////////////////////////////
class RedisResult {
  redisReply *reply_;

public:
  RedisResult();
  RedisResult(redisReply *reply);
  RedisResult(RedisResult &&other); // move constructor
  RedisResult(const RedisResult &other) = delete; // copy constructor
  RedisResult &operator=(const RedisResult &str) = delete; // assign operation
  ~RedisResult();

  void reset(redisReply *reply);

  bool empty();
  int type();

  string str();
  long long integer();
};

/////////////////////////////// RedisConnectInfo ///////////////////////////////
class RedisConnectInfo {
public:
  string host_;
  int32_t port_;
  string passwd_;

  RedisConnectInfo(const string &host, int32_t port, const string &passwd)
    : host_(host)
    , port_(port)
    , passwd_(passwd) {}

  RedisConnectInfo(const RedisConnectInfo &r) {
    host_ = r.host_;
    port_ = r.port_;
    passwd_ = r.passwd_;
  }

  RedisConnectInfo &operator=(const RedisConnectInfo &r) {
    host_ = r.host_;
    port_ = r.port_;
    passwd_ = r.passwd_;
    return *this;
  }
};

/////////////////////////////// RedisConnection ///////////////////////////////
class RedisConnection {
protected:
  RedisConnectInfo connInfo_;
  redisContext *conn_;

  bool _ping();

public:
  RedisConnection(const RedisConnectInfo &connInfo);

  bool open();
  void close();
  bool ping();

  // execute redis command synchronized
  RedisResult execute(const string &command);
  RedisResult execute(initializer_list<const string> args);
  RedisResult execute(const vector<string> &args);

  // execute redis command & get reply with pipeline
  void prepare(const string &command);
  void prepare(initializer_list<const string> args);
  void prepare(const vector<string> &args);
  RedisResult execute();
};

#endif

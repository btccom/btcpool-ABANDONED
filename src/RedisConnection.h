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
struct RedisResult {
  redisReply *reply_;

  RedisResult();
  RedisResult(redisReply *reply);
  ~RedisResult();

  void reset(redisReply *reply);

  bool empty();
  int type();

  string str();
};

/////////////////////////////// RedisConnection ///////////////////////////////
class RedisConnection {
protected:
  string host_;
  int32_t port_;

  redisContext *conn_;

public:
  RedisConnection(const string &host, int32_t port);

  bool open();
  void close();
  bool ping();

  RedisResult execute(const string &command);
};

#endif

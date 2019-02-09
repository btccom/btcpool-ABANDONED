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
#include "BlockMakerSia.h"

#include <boost/thread.hpp>

//////////////////////////////////////BlockMakerSia//////////////////////////////////////////////////
BlockMakerSia::BlockMakerSia(
    shared_ptr<BlockMakerDefinition> def,
    const char *kafkaBrokers,
    const MysqlConnectInfo &poolDB)
  : BlockMaker(def, kafkaBrokers, poolDB) {
}

void BlockMakerSia::processSolvedShare(rd_kafka_message_t *rkmessage) {
  if (rkmessage->len != 80) {
    LOG(ERROR) << "incorrect header len: " << rkmessage->len;
    return;
  }

  char buf[80] = {0};
  memcpy(buf, rkmessage->payload, 80);
  for (const auto &itr : def()->nodes) {
    string response;
    rpcCall(
        itr.rpcAddr_.c_str(),
        itr.rpcUserPwd_.c_str(),
        buf,
        80,
        response,
        "Sia-Agent");
    LOG(INFO) << "submission result: " << response;
  }
}

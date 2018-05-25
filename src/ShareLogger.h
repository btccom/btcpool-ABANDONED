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
#ifndef SHARELOGGER_H_
#define SHARELOGGER_H_

#include "Common.h"
#include "Kafka.h"
#include "MySQLConnection.h"
#include "Stratum.h"
#include "Statistics.h"

#include <event2/event.h>
#include <event2/http.h>
#include <event2/buffer.h>
#include <event2/util.h>
#include <event2/keyvalq_struct.h>

#include <string.h>
#include <pthread.h>
#include <memory>


//////////////////////////////  ShareLogWriter  ///////////////////////////////
//
// 1. consume topic 'ShareLog'
// 2. write sharelog to Disk
//
class ShareLogWriter {
  atomic<bool> running_;
  string dataDir_;  // where to put sharelog data files

  // key:   timestamp - (timestamp % 86400)
  // value: FILE *
  std::map<uint32_t, FILE *> fileHandlers_;
  std::vector<Share> shares_;

  KafkaHighLevelConsumer hlConsumer_;  // consume topic: 'ShareLog'

  FILE* getFileHandler(uint32_t ts);
  void consumeShareLog(rd_kafka_message_t *rkmessage);
  bool flushToDisk();
  void tryCloseOldHanders();

public:
  ShareLogWriter(const char *kafkaBrokers, const string &dataDir,
                 const string &kafkaGroupID, const char *shareLogTopic);
  ~ShareLogWriter();

  void stop();
  void run();
};


#endif // SHARELOGGER_H_

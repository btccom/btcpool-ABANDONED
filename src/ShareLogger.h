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
#include "Utils.h"

#include "zlibstream/zstr.hpp"

//////////////////////////////  ShareLogWriter  ///////////////////////////////
// Interface, used as a pointer type.
class ShareLogWriter {
public:
  virtual ~ShareLogWriter(){};
  virtual void stop() = 0;
  virtual void run() = 0;
};

/////////////////////////  ShareLogWriterBase //////////////////////////
// write sharelog to Disk
//
template <class SHARE>
class ShareLogWriterBase {
private:
  string dataDir_; // where to put sharelog data files

  // zlib/gzip compression level: -1 to 9.
  // -1: defaule level, 0: non-compression, 1: best speed, 9: best size.
  int compressionLevel_;

  // key:   timestamp - (timestamp % 86400)
  // value: zstr::ofstream *
  std::map<uint32_t, zstr::ofstream *> fileHandlers_;
  std::vector<SHARE> shares_;

  const string chainType_;

  zstr::ofstream *getFileHandler(uint32_t ts);
  void tryCloseOldHanders();

public:
  ShareLogWriterBase(
      const char *chainType,
      const string &dataDir,
      const int compressionLevel = Z_DEFAULT_COMPRESSION);
  ~ShareLogWriterBase();

  void addShare(SHARE &&share);
  size_t countShares();
  bool flushToDisk();
};

/////////////////////////  ShareLogWriterT /////////////////////////
// 1. consume topic 'ShareLog'
// 2. write sharelog to Disk
//
template <class SHARE>
class ShareLogWriterT : public ShareLogWriter,
                        protected ShareLogWriterBase<SHARE> {
  atomic<bool> running_;
  KafkaHighLevelConsumer hlConsumer_; // consume topic: shareLogTopic

  void consumeShareLog(rd_kafka_message_t *rkmessage);

public:
  ShareLogWriterT(
      const char *chainType,
      const char *kafkaBrokers,
      const string &dataDir,
      const string &kafkaGroupID,
      const char *shareLogTopic,
      const int compressionLevel = Z_DEFAULT_COMPRESSION);
  ~ShareLogWriterT();

  void stop();
  void run();
};

#include "ShareLogger.inl"

#endif // SHARELOGGER_H_

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
#include "ShareLogger.h"

#include "Common.h"
#include "Stratum.h"
#include "Utils.h"
#include "utilities_js.hpp"

#include <algorithm>
#include <string>

#include <boost/algorithm/string.hpp>
#include <boost/thread.hpp>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <chainparams.h>
#include "BitcoinUtils.h"


//////////////////////////////  ShareLogWriterT  ///////////////////////////////
template<class SHARE>
ShareLogWriterT<SHARE>::ShareLogWriterT(const char *chainType,
                               const char *kafkaBrokers,
                               const string &dataDir,
                               const string &kafkaGroupID,
                               const char *shareLogTopic)
:running_(true), dataDir_(dataDir), chainType_(chainType),
hlConsumer_(kafkaBrokers, shareLogTopic, 0/* patition */, kafkaGroupID)
{
}

template<class SHARE>
ShareLogWriterT<SHARE>::~ShareLogWriterT() {
  // close file handlers
  for (auto & itr : fileHandlers_) {
    LOG(INFO) << "fclose file handler, date: " << date("%F", itr.first);
    fclose(itr.second);
  }
  fileHandlers_.clear();
}

template<class SHARE>
void ShareLogWriterT<SHARE>::stop() {
  if (!running_)
    return;

  running_ = false;
}

template<class SHARE>
FILE * ShareLogWriterT<SHARE>::getFileHandler(uint32_t ts) {
  if (fileHandlers_.find(ts) != fileHandlers_.end()) {
    return fileHandlers_[ts];
  }

  const string filePath = getStatsFilePath(chainType_.c_str(), dataDir_, ts);
  LOG(INFO) << "fopen: " << filePath;

  FILE *f = fopen(filePath.c_str(), "ab");  // append mode, bin file
  if (f == nullptr) {
    LOG(FATAL) << "fopen file fail: " << filePath;
    return nullptr;
  }

  fileHandlers_[ts] = f;
  return f;
}

template<class SHARE>
void ShareLogWriterT<SHARE>::consumeShareLog(rd_kafka_message_t *rkmessage) {
  // check error
  if (rkmessage->err) {
    if (rkmessage->err == RD_KAFKA_RESP_ERR__PARTITION_EOF) {
      // Reached the end of the topic+partition queue on the broker.
      // Not really an error.
      //      LOG(INFO) << "consumer reached end of " << rd_kafka_topic_name(rkmessage->rkt)
      //      << "[" << rkmessage->partition << "] "
      //      << " message queue at offset " << rkmessage->offset;
      // acturlly
      return;
    }

    LOG(ERROR) << "consume error for topic " << rd_kafka_topic_name(rkmessage->rkt)
    << "[" << rkmessage->partition << "] offset " << rkmessage->offset
    << ": " << rd_kafka_message_errstr(rkmessage);

    if (rkmessage->err == RD_KAFKA_RESP_ERR__UNKNOWN_PARTITION ||
        rkmessage->err == RD_KAFKA_RESP_ERR__UNKNOWN_TOPIC) {
      LOG(FATAL) << "consume fatal";
    }
    return;
  }

  if (rkmessage->len != sizeof(SHARE)) {
    LOG(ERROR) << "sharelog message size(" << rkmessage->len << ") is not: " << sizeof(SHARE);
    return;
  }

  shares_.push_back(SHARE());
  SHARE *share = &(*shares_.rbegin());

  memcpy((uint8_t *)share, (const uint8_t *)rkmessage->payload, rkmessage->len);

  DLOG(INFO) << share->toString();
  if (!share->isValid()) {
    LOG(ERROR) << "invalid share: " << share->toString();
    shares_.pop_back();
    return;
  }
}

template<class SHARE>
void ShareLogWriterT<SHARE>::tryCloseOldHanders() {
  while (fileHandlers_.size() > 3) {
    // Maps (and sets) are sorted, so the first element is the smallest,
    // and the last element is the largest.
    auto itr = fileHandlers_.begin();

    LOG(INFO) << "fclose file handler, date: " << date("%F", itr->first);
    fclose(itr->second);

    fileHandlers_.erase(itr);
  }
}

template<class SHARE>
bool ShareLogWriterT<SHARE>::flushToDisk() {
  std::set<FILE*> usedHandlers;

  for (const auto& share : shares_) {
    const uint32_t ts = share.timestamp_ - (share.timestamp_ % 86400);
    FILE *f = getFileHandler(ts);
    if (f == nullptr)
      return false;

    usedHandlers.insert(f);
    fwrite((uint8_t *)&share, sizeof(SHARE), 1, f);
  }

  shares_.clear();

  for (auto & f : usedHandlers) {
    fflush(f);
  }

  // should call this after write data
  tryCloseOldHanders();

  return true;
}

template<class SHARE>
void ShareLogWriterT<SHARE>::run() {
  time_t lastFlushTime = time(nullptr);
  const int32_t kFlushDiskInterval = 2;
  const int32_t kTimeoutMs = 1000;

  LOG(INFO) << "setup sharelog consumer...";

  if (!hlConsumer_.setup()) {
    LOG(ERROR) << "setup sharelog consumer fail";
    return;
  }

  LOG(INFO) << "waiting sharelog messages...";

  while (running_) {
    //
    // flush data to disk
    //
    if (shares_.size() > 0 &&
        time(nullptr) > kFlushDiskInterval + lastFlushTime) {
      flushToDisk();
      lastFlushTime = time(nullptr);
    }

    //
    // consume message
    //
    rd_kafka_message_t *rkmessage;
    rkmessage = hlConsumer_.consumer(kTimeoutMs);

    // timeout, most of time it's not nullptr and set an error:
    //          rkmessage->err == RD_KAFKA_RESP_ERR__PARTITION_EOF
    if (rkmessage == nullptr) {
      continue;
    }
    
    DLOG(INFO) << "a new message, size: " << rkmessage->len;

    // consume share log
    consumeShareLog(rkmessage);
    rd_kafka_message_destroy(rkmessage);  /* Return message to rdkafka */
  }

  // flush left shares
  if (shares_.size() > 0)
    flushToDisk();
}


///////////////  template instantiation ///////////////
// Without this, some linking errors will issued.
// If you add a new derived class of Share, add it at the following.
template class ShareLogWriterT<ShareBitcoin>;
template class ShareLogWriterT<ShareEth>;
template class ShareLogWriterT<ShareBytom>;

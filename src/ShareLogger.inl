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

#include <glog/logging.h>

///////////////////////// ShareLogWriterBase /////////////////////////
template <class SHARE>
ShareLogWriterBase<SHARE>::ShareLogWriterBase(
    const char *chainType, const string &dataDir, const int compressionLevel)
  : dataDir_(dataDir)
  , compressionLevel_(compressionLevel)
  , chainType_(chainType) {
}

template <class SHARE>
ShareLogWriterBase<SHARE>::~ShareLogWriterBase() {
  // flush left shares
  if (countShares() > 0) {
    flushToDisk();
  }

  // close file handlers
  for (auto &itr : fileHandlers_) {
    LOG(INFO) << "fclose file handler, date: " << date("%F", itr.first);
    delete itr.second;
  }
  fileHandlers_.clear();
}

template <class SHARE>
zstr::ofstream *ShareLogWriterBase<SHARE>::getFileHandler(uint32_t ts) {
  string filePath;

  try {
    if (fileHandlers_.find(ts) != fileHandlers_.end()) {
      return fileHandlers_[ts];
    }

    filePath = getStatsFilePath(chainType_.c_str(), dataDir_, ts);
    LOG(INFO) << "fopen: " << filePath;

    zstr::ofstream *f = new zstr::ofstream(
        filePath,
        std::ios::app | std::ios::binary,
        compressionLevel_); // append mode, bin file
    if (!*f) {
      LOG(FATAL) << "fopen file fail: " << filePath;
      return nullptr;
    }

    fileHandlers_[ts] = f;
    return f;

  } catch (...) {
    LOG(ERROR) << "open file fail: " << filePath;
    return nullptr;
  }
}

template <class SHARE>
void ShareLogWriterBase<SHARE>::addShare(SHARE &&share) {
  DLOG(INFO) << share.toString();

  if (share.isValid()) {
    shares_.push_back(share);
  } else {
    LOG(ERROR) << "invalid share";
  }
}

template <class SHARE>
size_t ShareLogWriterBase<SHARE>::countShares() {
  return shares_.size();
}

template <class SHARE>
void ShareLogWriterBase<SHARE>::tryCloseOldHanders() {
  while (fileHandlers_.size() > 3) {
    // Maps (and sets) are sorted, so the first element is the smallest,
    // and the last element is the largest.
    auto itr = fileHandlers_.begin();

    LOG(INFO) << "fclose file handler, date: " << date("%F", itr->first);
    delete itr->second;

    fileHandlers_.erase(itr);
  }
}

template <class SHARE>
bool ShareLogWriterBase<SHARE>::flushToDisk() {
  if (shares_.empty()) {
    return true;
  }

  try {
    std::set<zstr::ofstream *> usedHandlers;

    DLOG(INFO) << "flushToDisk shares count: " << shares_.size();
    for (const auto &share : shares_) {
      const uint32_t ts = share.timestamp() - (share.timestamp() % 86400);
      zstr::ofstream *f = getFileHandler(ts);
      if (f == nullptr) {
        return false;
      }

      usedHandlers.insert(f);

      string message;
      if (!share.SerializeToString(&message)) {
        DLOG(INFO) << "share.SerializeToString failed!" << std::endl;
        continue;
      }
      uint32_t size = message.size();
      f->write((char *)&size, sizeof(uint32_t));
      f->write((char *)message.data(), size);
    }

    shares_.clear();

    for (auto &f : usedHandlers) {
      DLOG(INFO) << "fflush() file to disk";
      f->flush();
    }

    // should call this after write data
    tryCloseOldHanders();

    return true;

  } catch (...) {
    LOG(ERROR) << "write file fail";
    return false;
  }
}

//////////////////////////////  ShareLogWriterT  ///////////////////////////////
template <class SHARE>
ShareLogWriterT<SHARE>::ShareLogWriterT(
    const char *chainType,
    const char *kafkaBrokers,
    const string &dataDir,
    const string &kafkaGroupID,
    const char *shareLogTopic,
    const int compressionLevel)
  : ShareLogWriterBase<SHARE>(chainType, dataDir, compressionLevel)
  , running_(true)
  , hlConsumer_(kafkaBrokers, shareLogTopic, 0 /* patition */, kafkaGroupID) {
}

template <class SHARE>
ShareLogWriterT<SHARE>::~ShareLogWriterT() {
}

template <class SHARE>
void ShareLogWriterT<SHARE>::stop() {
  if (!running_)
    return;

  running_ = false;
}

template <class SHARE>
void ShareLogWriterT<SHARE>::consumeShareLog(rd_kafka_message_t *rkmessage) {
  // check error
  if (rkmessage->err) {
    if (rkmessage->err == RD_KAFKA_RESP_ERR__PARTITION_EOF) {
      // Reached the end of the topic+partition queue on the broker.
      // Not really an error.
      //      LOG(INFO) << "consumer reached end of " <<
      //      rd_kafka_topic_name(rkmessage->rkt)
      //      << "[" << rkmessage->partition << "] "
      //      << " message queue at offset " << rkmessage->offset;
      // acturlly
      return;
    }

    LOG(ERROR) << "consume error for topic "
               << rd_kafka_topic_name(rkmessage->rkt) << "["
               << rkmessage->partition << "] offset " << rkmessage->offset
               << ": " << rd_kafka_message_errstr(rkmessage);

    if (rkmessage->err == RD_KAFKA_RESP_ERR__UNKNOWN_PARTITION ||
        rkmessage->err == RD_KAFKA_RESP_ERR__UNKNOWN_TOPIC) {
      LOG(FATAL) << "consume fatal";
    }
    return;
  }

  SHARE share;

  // if (rkmessage->len < sizeof(uint32_t)) {
  //   LOG(ERROR) << "invalid share , share size : "<< rkmessage->len ;
  //   return ;
  // }

  // uint8_t * payload = reinterpret_cast<uint8_t *> (rkmessage->payload);
  // uint32_t headlength  = *((uint32_t*)payload);

  // if (rkmessage->len < sizeof(uint32_t) + headlength) {
  //   LOG(ERROR) << "invalid share , kafka message size : "<< rkmessage->len <<
  //   " <  complete share size " <<
  //              headlength + sizeof(uint32_t);
  //   return;
  // }

  // if (!share.ParseFromArray((const uint8_t *)(payload + sizeof(uint32_t)),
  // headlength)) {
  //   LOG(ERROR) << "parse share from kafka message failed rkmessage->len = "<<
  //   rkmessage->len ; return;
  // }

  if (!share.UnserializeWithVersion(
          (const uint8_t *)(rkmessage->payload), rkmessage->len)) {
    LOG(ERROR) << "parse share from kafka message failed rkmessage->len = "
               << rkmessage->len;
    return;
  }

  this->addShare(std::move(share));
}

template <class SHARE>
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
    if (this->countShares() > 0 &&
        time(nullptr) > kFlushDiskInterval + lastFlushTime) {
      this->flushToDisk();
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
    rd_kafka_message_destroy(rkmessage); /* Return message to rdkafka */
  }

  // flush left shares
  if (this->countShares() > 0) {
    this->flushToDisk();
  }
}

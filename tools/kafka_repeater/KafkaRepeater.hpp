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
#pragma once

#include <string>
#include <map>
#include <atomic>
#include <thread>

#include <glog/logging.h>
#include <libconfig.h++>

#include "Kafka.h"

using namespace std;
using namespace libconfig;

class KafkaRepeater {
public:
  KafkaRepeater(
      string consumeBrokers,
      string consumeTopic,
      string consumeGroupId,
      string produceBrokers,
      string produceTopic)
    : running_(false)
    , messageNumber_(0)
    , consumeBrokers_(consumeBrokers)
    , consumeTopic_(consumeTopic)
    , consumeGroupId_(consumeGroupId)
    , consumer_(
          consumeBrokers_.c_str(),
          consumeTopic_.c_str(),
          0 /* patition */,
          consumeGroupId_.c_str())
    , produceBrokers_(produceBrokers)
    , produceTopic_(produceTopic)
    , producer_(
          produceBrokers_.c_str(),
          produceTopic_.c_str(),
          RD_KAFKA_PARTITION_UA) {}

  static inline void loadOption(
      const libconfig::Setting &cfg,
      const string &key,
      std::map<string, string> &options) {
    if (cfg.exists(key)) {
      options[key] = cfg.lookup(key).c_str();
    }
  }

  bool init(const libconfig::Setting &cfg) {
    LOG(INFO) << "setup kafka consumer...";
    if (!consumer_.setup()) {
      LOG(ERROR) << "setup kafka consumer fail";
      return false;
    }

    LOG(INFO) << "setup kafka producer...";
    std::map<string, string> options;
    // set to 1 (0 is an illegal value here), deliver msg as soon as possible.
    options["queue.buffering.max.ms"] = "1";
    options["sasl.mechanisms"] = "PLAIN";
    loadOption(cfg, "debug", options);
    loadOption(cfg, "security.protocol", options);
    loadOption(cfg, "ssl.ca.location", options);
    loadOption(cfg, "ssl.certificate.location", options);
    loadOption(cfg, "ssl.key.location", options);
    loadOption(cfg, "ssl.key.password", options);
    loadOption(cfg, "sasl.username", options);
    loadOption(cfg, "sasl.password", options);

    if (!producer_.setup(&options)) {
      LOG(ERROR) << "kafka producer setup failure";
      return false;
    }

    // setup kafka and check if it's alive
    if (!producer_.checkAlive()) {
      LOG(ERROR) << "kafka producer is NOT alive";
      return false;
    }

    return true;
  }

  void run() {
    const int32_t kTimeoutMs = 1000;
    running_ = true;

    LOG(INFO) << "waiting kafka messages...";
    while (running_) {
      //
      // consume message
      //
      rd_kafka_message_t *rkmessage;
      rkmessage = consumer_.consumer(kTimeoutMs);

      // timeout, most of time it's not nullptr and set an error:
      //          rkmessage->err == RD_KAFKA_RESP_ERR__PARTITION_EOF
      if (rkmessage == nullptr) {
        continue;
      }

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
          rd_kafka_message_destroy(rkmessage); /* Return message to rdkafka */
          continue;
        }

        LOG(ERROR) << "consume error for topic "
                   << rd_kafka_topic_name(rkmessage->rkt) << "["
                   << rkmessage->partition << "] offset " << rkmessage->offset
                   << ": " << rd_kafka_message_errstr(rkmessage);

        if (rkmessage->err == RD_KAFKA_RESP_ERR__UNKNOWN_PARTITION ||
            rkmessage->err == RD_KAFKA_RESP_ERR__UNKNOWN_TOPIC) {
          LOG(FATAL) << "consume fatal";
          running_ = false;
          rd_kafka_message_destroy(rkmessage); /* Return message to rdkafka */
          continue;
        }

        rd_kafka_message_destroy(rkmessage); /* Return message to rdkafka */
        continue;
      }

      DLOG(INFO) << "a new message, size: " << rkmessage->len;

      // repeat a message
      bool success = repeatMessage(rkmessage);
      if (success) {
        messageNumber_++;
      }

      rd_kafka_message_destroy(rkmessage); /* Return message to rdkafka */
    }

    LOG(INFO) << "kafka repeater stopped";
  }

  void stop() {
    LOG(INFO) << "stopping kafka repeater...";
    running_ = false;
  }

  bool isRunning() { return running_; }

  size_t getMessageNumber() { return messageNumber_; }

  void resetMessageNumber() { messageNumber_ = 0; }

  void runMessageNumberDisplayThread(time_t interval) {
    std::thread t([this, interval]() {
      this->resetMessageNumber();
      while (this->isRunning()) {
        sleep(interval);
        size_t num = this->getMessageNumber();
        this->resetMessageNumber();
        displayMessageNumber(num, interval);
      }
    });
    t.detach();
  }

protected:
  virtual bool repeatMessage(rd_kafka_message_t *rkmessage) {
    sendToKafka(rkmessage->payload, rkmessage->len);
    return true;
  }

  virtual void displayMessageNumber(size_t messageNumber, time_t time) {
    LOG(INFO) << "Repeated " << messageNumber << " messages in " << time
              << " seconds";
  }

  void sendToKafka(const void *data, size_t len) {
    while (!producer_.tryProduce(data, len)) {
      sleep(1);
    }
  }

  template <typename T>
  void sendToKafka(const T &data) {
    sendToKafka(&data, sizeof(T));
  }

  std::atomic<bool> running_;
  size_t messageNumber_; // don't need thread safe (for logs only)

  string consumeBrokers_;
  string consumeTopic_;
  string consumeGroupId_;
  KafkaHighLevelConsumer consumer_;

  string produceBrokers_;
  string produceTopic_;
  KafkaProducer producer_;
};

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
#ifndef KAFKA_H_
#define KAFKA_H_

#include "Common.h"

#include <librdkafka/rdkafka.h>

#define KAFKA_TOPIC_RAWGBT         "RawGbt"
#define KAFKA_TOPIC_STRATUM_JOB    "StratumJob"
#define KAFKA_TOPIC_SOLVED_SHARE   "SolvedShare"
#define KAFKA_TOPIC_SHARE_LOG      "ShareLog"
#define KAFKA_TOPIC_COMMON_EVENTS  "CommonEvents"

#define KAFKA_TOPIC_NMC_AUXBLOCK      "NMCAuxBlock"
#define KAFKA_TOPIC_NMC_SOLVED_SHARE  "NMCSolvedShare"


///////////////////////////////////////////////////////////////////////
// librdkafka options
// https://github.com/edenhill/librdkafka/blob/master/CONFIGURATION.md
///////////////////////////////////////////////////////////////////////

// Maximum transmit message size.
// The RawGbt message may large than 30MB while the block size reach 8MB.
// So allow the message to reach 60MB.
#define RDKAFKA_MESSAGE_MAX_BYTES            "60000000"

// Maximum number of bytes per topic+partition to request when
// fetching messages from the broker
#define RDKAFKA_FETCH_MESSAGE_MAX_BYTES      "60000000"

// Maximum number of kilobytes per topic+partition in the local consumer
// queue. This value may be overshot by fetch.message.max.bytes.
#define RDKAFKA_QUEUED_MAX_MESSAGES_KBYTES   "60000000"

// compression codec to use for compressing message sets
#define RDKAFKA_COMPRESSION_CODEC            "snappy"

// Maximum number of messages allowed on the producer queue.
#define RDKAFKA_QUEUE_BUFFERING_MAX_MESSAGES "100000"

// Maximum time, in milliseconds, for buffering data on the producer queue.
// set to 1 (0 is an illegal value here), deliver msg as soon as possible.
#define RDKAFKA_QUEUE_BUFFERING_MAX_MS       "1000"

// Maximum number of messages batched in one MessageSet.
#define RDKAFKA_BATCH_NUM_MESSAGES           "1000"

// Maximum time the broker may wait to fill the response with fetch.min.bytes
#define RDKAFKA_CONSUMER_FETCH_WAIT_MAX_MS            "10"
#define RDKAFKA_HIGH_LEVEL_CONSUMER_FETCH_WAIT_MAX_MS "50"


///////////////////////////////// KafkaConsumer ////////////////////////////////
// Simple Consumer
class KafkaConsumer {
  string brokers_;
  string topicStr_;
  int    partition_;
  map<string, string> defaultOptions_;

  rd_kafka_conf_t  *conf_;
  rd_kafka_t       *consumer_;
  rd_kafka_topic_t *topic_;

public:
  KafkaConsumer(const char *brokers, const char *topic, int partition);
  ~KafkaConsumer();

  bool checkAlive();

  //
  // offset:
  //     RD_KAFKA_OFFSET_BEGINNING
  //     RD_KAFKA_OFFSET_END
  //     RD_KAFKA_OFFSET_STORED
  //     RD_KAFKA_OFFSET_TAIL(CNT)
  //
  bool setup(int64_t offset, const std::map<string, string> *options=nullptr);
  //
  // don't forget to call rd_kafka_message_destroy() after consumer()
  //
  rd_kafka_message_t *consumer(int timeout_ms);
};


//////////////////////////// KafkaHighLevelConsumer ////////////////////////////
// High Level Consumer
class KafkaHighLevelConsumer {
  string brokers_;
  string topicStr_;
  string groupStr_;
  int    partition_;

  rd_kafka_conf_t  *conf_;
  rd_kafka_t       *consumer_;
  rd_kafka_topic_partition_list_t *topics_;

public:
  KafkaHighLevelConsumer(const char *brokers, const char *topic, int partition,
                         const string &groupStr);
  ~KafkaHighLevelConsumer();

//  bool checkAlive();  // I don't know which function should be used to check
  bool setup();

  //
  // don't forget to call rd_kafka_message_destroy() after consumer()
  //
  rd_kafka_message_t *consumer(int timeout_ms);
};


///////////////////////////////// KafkaProducer ////////////////////////////////
class KafkaProducer {
  string brokers_;
  string topicStr_;
  int    partition_;
  map<string, string> defaultOptions_;

  rd_kafka_conf_t  *conf_;
  rd_kafka_t       *producer_;
  rd_kafka_topic_t *topic_;

public:
  KafkaProducer(const char *brokers, const char *topic, int partition);
  ~KafkaProducer();

  bool setup(const std::map<string, string> *options=nullptr);
  bool checkAlive();
  void produce(const void *payload, size_t len);
};

#endif

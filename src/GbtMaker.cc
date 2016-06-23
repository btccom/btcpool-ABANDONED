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
#include "GbtMaker.h"

#include <glog/logging.h>
#include <librdkafka/rdkafka.h>

#include "bitcoin/util.h"

#include "Utils.h"
#include "utilities_js.hpp"

//
// bitcoind zmq pub msg type: "hashblock", "hashtx", "rawblock", "rawtx"
//
#define BITCOIND_ZMQ_HASHBLOCK      "hashblock"

///////////////////////////////////  GbtMaker  /////////////////////////////////
GbtMaker::GbtMaker(const string &zmqBitcoindAddr,
                   const string &bitcoindRpcAddr, const string &bitcoindRpcUserpass,
                   const string &kafkaBrokers, uint32_t kRpcCallInterval)
: running_(true), zmqContext_(2/*i/o threads*/),
zmqBitcoindAddr_(zmqBitcoindAddr), bitcoindRpcAddr_(bitcoindRpcAddr),
bitcoindRpcUserpass_(bitcoindRpcUserpass), lastGbtMakeTime_(0), kRpcCallInterval_(kRpcCallInterval),
kafkaConf_(nullptr), kafkaProducer_(nullptr), kafkaTopicRawgbt_(nullptr),
kafkaBrokers_(kafkaBrokers)
{
}

GbtMaker::~GbtMaker() {}

bool GbtMaker::init() {
  if (!setupKafka()) {
    return false;
  }

  // check kafka meta, maybe there is better solution to check brokers
  {
    rd_kafka_resp_err_t err;
    const struct rd_kafka_metadata *metadata;
    /* Fetch metadata */
    err = rd_kafka_metadata(kafkaProducer_, kafkaTopicRawgbt_ ? 0 : 1,
                            kafkaTopicRawgbt_, &metadata, 5000);
    if (err != RD_KAFKA_RESP_ERR_NO_ERROR) {
      LOG(FATAL) << "Failed to acquire metadata: " << rd_kafka_err2str(err);
      return false;
    }
    // no need to print out meta data
    rd_kafka_metadata_destroy(metadata);
  }

  // check bitcoind
  {
    string response;
    string request = "{\"jsonrpc\":\"1.0\",\"id\":\"1\",\"method\":\"getinfo\",\"params\":[]}";
    bool res = bitcoindRpcCall(bitcoindRpcAddr_.c_str(), bitcoindRpcUserpass_.c_str(),
                               request.c_str(), response);
    if (!res) {
      LOG(ERROR) << "bitcoind rpc call failure";
      return false;
    }
    LOG(INFO) << "bitcoind getinfo: " << response;

    JsonNode r;
    if (!JsonNode::parse(response.c_str(),
                         response.c_str() + response.length(), r)) {
      LOG(ERROR) << "decode gbt failure";
      return false;
    }
    r["result"]["blocksasdfasd"].type();

    // check fields
    if (r["result"].type() != Utilities::JS::type::Obj ||
        r["result"]["connections"].type() != Utilities::JS::type::Int ||
        r["result"]["blocks"].type()      != Utilities::JS::type::Int) {
      LOG(ERROR) << "getinfo missing some fields";
      return false;
    }
    if (r["result"]["connections"].int32() <= 0) {
      LOG(ERROR) << "bitcoind connections is zero";
      return false;
    }
  }

  return true;
}

void GbtMaker::stop() {
  if (!running_) {
    return;
  }
  running_ = false;
  LOG(INFO) << "stop gbtmaker";
}

bool GbtMaker::setupKafka() {
  kafkaConf_ = rd_kafka_conf_new();
  rd_kafka_conf_set_log_cb(kafkaConf_, kafkaLogger);  // set logger

  char errstr[1024];
  //
  // rdkafka options:
  //
  // queue.buffering.max.ms:
  //         set to 1 (0 is an illegal value here), deliver msg as soon as possible.
  // queue.buffering.max.messages:
  //         100 is enough for gbt
  // message.max.bytes:
  //         Maximum transmit message size. 20000000 = 20,000,000
  //
  // TODO: increase 'message.max.bytes' in the feature
  //
  vector<string> conKeys = {"message.max.bytes", "compression.codec",
    "queue.buffering.max.ms", "queue.buffering.max.messages"};
  vector<string> conVals = {"20000000", "snappy", "1", "100"};
  assert(conKeys.size() == conVals.size());

  for (size_t i = 0; i < conKeys.size(); i++) {
    if (rd_kafka_conf_set(kafkaConf_,
                          conKeys[i].c_str(), conVals[i].c_str(),
                          errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK) {
      LOG(ERROR) << "kafka set conf failure: " << errstr
      << ", key: " << conKeys[i] << ", val: " << conVals[i];
      return false;
    }
  }

  /* create producer */
  if (!(kafkaProducer_ = rd_kafka_new(RD_KAFKA_PRODUCER, kafkaConf_,
                                      errstr, sizeof(errstr)))) {
    LOG(ERROR) << "kafka create producer failure: " << errstr;
    return false;
  }

#ifndef NDEBUG
  rd_kafka_set_log_level(kafkaProducer_, 0);
#else
  rd_kafka_set_log_level(kafkaProducer_, 7 /* LOG_DEBUG */);
#endif

  /* Add brokers */
  LOG(INFO) << "add brokers: " << kafkaBrokers_;
  if (rd_kafka_brokers_add(kafkaProducer_, kafkaBrokers_.c_str()) == 0) {
    LOG(ERROR) << "kafka add brokers failure";
    return false;
  }

  rd_kafka_topic_conf_t *kafkaTopicConf = rd_kafka_topic_conf_new();

  /* Create topic */
  LOG(INFO) << "create topic handle: " << KAFKA_TOPIC_RAWGBT;
  kafkaTopicRawgbt_ = rd_kafka_topic_new(kafkaProducer_, KAFKA_TOPIC_RAWGBT, kafkaTopicConf);
  kafkaTopicConf = NULL; /* Now owned by topic */

  return true;
}

void GbtMaker::kafkaProduceMsg(const void *payload, size_t len) {
  // rd_kafka_produce() is non-blocking
  int res = rd_kafka_produce(kafkaTopicRawgbt_,
                             RD_KAFKA_PARTITION_UA, RD_KAFKA_MSG_F_COPY,
                             (void *)payload, len,
                             NULL, 0,  /* Optional key and its length */
                             /* Message opaque, provided in delivery report 
                              * callback as msg_opaque. */
                             NULL);
  if (res == -1) {
    LOG(ERROR) << "produce to topic [ " << rd_kafka_topic_name(kafkaTopicRawgbt_)
    << "]: " << rd_kafka_err2str(rd_kafka_errno2err(errno));
  }
}

bool GbtMaker::bitcoindRpcGBT(string &response) {
  string request = "{\"jsonrpc\":\"1.0\",\"id\":\"1\",\"method\":\"getblocktemplate\",\"params\":[]}";
  bool res = bitcoindRpcCall(bitcoindRpcAddr_.c_str(), bitcoindRpcUserpass_.c_str(),
                             request.c_str(), response);
//  LOG(INFO) << "bitcoind rpc call, rep: " << rep;

  if (!res) {
    LOG(ERROR) << "bitcoind rpc failure";
    return false;
  }
  return true;
}

string GbtMaker::makeRawGbtMsg() {
  string gbt;
  if (!bitcoindRpcGBT(gbt)) {
    return "";
  }

  JsonNode r;
  if (!JsonNode::parse(gbt.c_str(),
                      gbt.c_str() + gbt.length(), r)) {
    LOG(ERROR) << "decode gbt failure: " << gbt;
    return "";
  }

  // check fields
  if (r["result"].type() != Utilities::JS::type::Obj ||
      r["result"]["previousblockhash"].type() != Utilities::JS::type::Str ||
      r["result"]["height"].type() != Utilities::JS::type::Int ||
      r["result"]["coinbasevalue"].type() != Utilities::JS::type::Int) {
    LOG(ERROR) << "gbt check fields failure";
    return "";
  }

  LOG(INFO) << "gbt height: " << r["result"]["height"].uint32()
  << ", prev_hash: " << r["result"]["previousblockhash"].str()
  << ", coinbase_value: " << r["result"]["coinbasevalue"].uint64()
  << ", bits: " << r["result"]["bits"].str()
  << ", mintime: " << r["result"]["mintime"].uint32()
  << ", version: " << r["result"]["version"].uint32()
  << "|0x" << Strings::Format("%08x", r["result"]["version"].uint32());

  return Strings::Format("{\"created_at_ts\":%u,\"block_template_base64\":\"%s\"}",
                         (uint32_t)time(nullptr), EncodeBase64(gbt).c_str());
}

void GbtMaker::submitRawGbtMsg(bool checkTime) {
  ScopeLock sl(lock_);

  if (checkTime &&
      lastGbtMakeTime_ + kRpcCallInterval_ > time(nullptr)) {
    return;
  }

  const string rawGbtMsg = makeRawGbtMsg();
  if (rawGbtMsg.length() == 0) {
    LOG(ERROR) << "get rawgbt failure";
    return;
  }
  lastGbtMakeTime_ = (uint32_t)time(nullptr);

  // submit to Kafka
  LOG(INFO) << "sumbit to Kafka, msg len: " << rawGbtMsg.length();
  kafkaProduceMsg(rawGbtMsg.c_str(), rawGbtMsg.length());
}

void GbtMaker::threadListenBitcoind() {
  zmq::socket_t subscriber(zmqContext_, ZMQ_SUB);
  subscriber.connect(zmqBitcoindAddr_);
  subscriber.setsockopt(ZMQ_SUBSCRIBE,
                        BITCOIND_ZMQ_HASHBLOCK, strlen(BITCOIND_ZMQ_HASHBLOCK));

  while (running_) {
    zmq::message_t ztype, zcontent;
    try {
      if (subscriber.recv(&ztype, ZMQ_DONTWAIT) == false) {
        if (!running_) { break; }
        usleep(50000);  // so we sleep and try again
        continue;
      }
      subscriber.recv(&zcontent);
    } catch (std::exception & e) {
      LOG(ERROR) << "bitcoind zmq recv exception: " << e.what();
      break;  // break big while
    }
    const string type    = std::string(static_cast<char*>(ztype.data()),    ztype.size());
    const string content = std::string(static_cast<char*>(zcontent.data()), zcontent.size());

    if (type == BITCOIND_ZMQ_HASHBLOCK)
    {
      string hashHex;
      Bin2Hex((const uint8 *)content.data(), content.size(), hashHex);
      LOG(INFO) << "bitcoind recv hashblock: " << hashHex;
      submitRawGbtMsg(false);
    }
    else
    {
      LOG(ERROR) << "unknown message type from bitcoind: " << type;
    }
  } /* /while */

  subscriber.close();
  LOG(INFO) << "stop thread listen to bitcoind";
}

void GbtMaker::run() {
  thread threadListenBitcoind = thread(&GbtMaker::threadListenBitcoind, this);

  while (running_) {
    sleep(1);
    submitRawGbtMsg(true);
  }

  if (threadListenBitcoind.joinable())
    threadListenBitcoind.join();

  /* Wait for messages to be delivered */
  if (kafkaProducer_) {
    LOG(INFO) << "close kafka produer...";

    while (rd_kafka_outq_len(kafkaProducer_) > 0) {
      rd_kafka_poll(kafkaProducer_, 100);
    }
    rd_kafka_topic_destroy(kafkaTopicRawgbt_);  // Destroy topic
    rd_kafka_destroy(kafkaProducer_);           // Destroy the handle
  }
}

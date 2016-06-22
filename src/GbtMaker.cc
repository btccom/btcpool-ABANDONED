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

#include "Utils.h"
#include "bitcoin/util.h"

//
// bitcoind zmq pub msg type: "hashblock", "hashtx", "rawblock", "rawtx"
//
#define BITCOIND_ZMQ_HASHBLOCK "hashblock"



///////////////////////////////////  GbtMaker  /////////////////////////////////
GbtMaker::GbtMaker(const string &zmqBitcoindAddr,
                   const string &bitcoindRpcAddr, const string &bitcoindRpcUserpass)
: zmqContext_(2/*i/o threads*/),
zmqBitcoindAddr_(zmqBitcoindAddr), bitcoindRpcAddr_(bitcoindRpcAddr),
bitcoindRpcUserpass_(bitcoindRpcUserpass), lastGbtMakeTime_(0), kRpcCallInterval_(10)
{
}

GbtMaker::~GbtMaker() {}

void GbtMaker::stop() {
  if (!running_) {
    return;
  }
  running_ = false;
  LOG(INFO) << "stop gbtmaker";
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

  // simple check fields
  if (strstr(gbt.c_str(), "previousblockhash") == nullptr) {
    return "";
  }
  if (strstr(gbt.c_str(), "coinbasevalue") == nullptr) {
    return "";
  }
  if (strstr(gbt.c_str(), "height") == nullptr) {
    return "";
  }

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

  // TODO: submit to Kafka
  LOG(INFO) << "sumbit rawgbt to Kafka, msg len: " << rawGbtMsg.length();
}

void GbtMaker::threadRpcCall() {
  while (running_) {
    sleep(1);
    submitRawGbtMsg(true);
  }
}

void GbtMaker::kafkaProduceMsg() {
  // TODO
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
      LOG(INFO) << "recv hashblock: " << content;
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
}

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

//
// bitcoind zmq pub msg type: "hashblock", "hashtx", "rawblock", "rawtx"
//
#define BITCOIND_ZMQ_HASHBLOCK "hashblock"


void GbtMaker::bitcoindRpcGBT(string &resp) {
  // Rpc: Get Block Template
  string req = "{\"jsonrpc\":\"1.0\",\"id\":\"1\",\"method\":\"getblocktemplate\",\"params\":[]}";
  string rep;
  bool res = bitcoindRpcCall(bitcoindRpcAddr_.c_str(), bitcoindRpcUserpass_.c_str(),
                             req.c_str(), rep);
//  LOG(INFO) << "bitcoind rpc call, rep: " << rep;

  if (!res) {
    LOG(ERROR) << "bitcoind rpc failure";
  }

}

void GbtMaker::threadRpcCall() {

}

void GbtMaker::threadListenBitcoind() {
  zmq::socket_t subscriber(zmqContext_, ZMQ_SUB);
  subscriber.connect(zmqBitcoindAddr_);
  subscriber.setsockopt(ZMQ_SUBSCRIBE, BITCOIND_ZMQ_HASHBLOCK, strlen(BITCOIND_ZMQ_HASHBLOCK));

}
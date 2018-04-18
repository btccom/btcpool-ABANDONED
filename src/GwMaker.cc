/*
 The MIT License (MIT)

 Copyright (C) 2017 RSK Labs Ltd.

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

/**
  File: GwMaker.cc
  Purpose: Poll RSK node to get new work and send it to Kafka "RawGw" topic

  @author Martin Medina
  @copyright RSK Labs Ltd.
  @version 1.0 30/03/17 

  maintained by HaoLi (fatrat1117) and YihaoPeng since Feb 20, 2018
*/

#include "GwMaker.h"

#include <glog/logging.h>
#include <util.h>
#include <utilstrencodings.h>
#include "Utils.h"


///////////////////////////////GwMaker////////////////////////////////////
GwMaker::GwMaker(shared_ptr<GwMakerHandler> handler,
                 const string &kafkaBrokers) : handler_(handler),
                                               running_(true),
                                               kafkaProducer_(kafkaBrokers.c_str(),
                                                              handler->def().rawGwTopic_.c_str(),
                                                              0 /* partition */)
{
}

GwMaker::~GwMaker() {}

bool GwMaker::init() {
  map<string, string> options;
  // set to 1 (0 is an illegal value here), deliver msg as soon as possible.
  options["queue.buffering.max.ms"] = "1";
  if (!kafkaProducer_.setup(&options)) {
    LOG(ERROR) << "kafka producer setup failure";
    return false;
  }

  // setup kafka and check if it's alive
  if (!kafkaProducer_.checkAlive()) {
    LOG(ERROR) << "kafka is NOT alive";
    return false;
  }

  // TODO: check rskd is alive in a similar way as done for btcd

  return true;
}

void GwMaker::stop() {
  if (!running_) {
    return;
  }
  running_ = false;
  LOG(INFO) << "stop GwMaker " << handler_->def().chainType_ << ", topic: " << handler_->def().rawGwTopic_;
}

void GwMaker::kafkaProduceMsg(const void *payload, size_t len) {
  kafkaProducer_.produce(payload, len);
}

string GwMaker::makeRawGwMsg() {
  return handler_->makeRawGwMsg();
}

void GwMaker::submitRawGwMsg() {

  const string rawGwMsg = makeRawGwMsg();
  if (rawGwMsg.length() == 0) {
    LOG(ERROR) << "get rawGw failure";
    return;
  }

  // submit to Kafka
  LOG(INFO) << "submit to Kafka msg len: " << rawGwMsg.length();
  kafkaProduceMsg(rawGwMsg.c_str(), rawGwMsg.length());
}

void GwMaker::run() {

  while (running_) {
    usleep(handler_->def().rpcInterval_ * 1000);
    submitRawGwMsg();
  }

  LOG(INFO) << "GwMaker " << handler_->def().chainType_ << ", topic: " << handler_->def().rawGwTopic_ << " stopped";
}


///////////////////////////////GwMakerHandler////////////////////////////////////
GwMakerHandler::~GwMakerHandler() {
}

string GwMakerHandler::makeRawGwMsg() {
  string gw;
  if (!callRpcGw(gw)) {
    return "";
  }
  LOG(INFO) << "getwork len: " << gw.length();
  return processRawGw(gw);
}

bool GwMakerHandler::callRpcGw(string &response)
{
  string request = getRequestData();
  string userAgent = getUserAgent();

  bool res = rpcCall(def_.rpcAddr_.c_str(),
                     def_.rpcUserPwd_.c_str(),
                     request.empty() ? nullptr : request.c_str(),
                     request.length(),
                     response,
                     userAgent.c_str());

  if (!res)
  {
    LOG(ERROR) << "call RPC failure";
    return false;
  }
  return true;
}


///////////////////////////////GwMakerHandlerRsk////////////////////////////////////
string GwMakerHandlerRsk::processRawGw(const string& msg) 
{
  JsonNode r;
  if (!JsonNode::parse(msg.c_str(), msg.c_str() + msg.length(), r)) {
    LOG(ERROR) << "decode gw failure: " << msg;
    return "";
  }

  // check fields
  if (!checkFields(r)) {
    LOG(ERROR) << "gw check fields failure";
    return "";
  }

  return constructRawMsg(r);
}

bool GwMakerHandlerRsk::checkFields(JsonNode &r) {
  if (r["result"].type()                                != Utilities::JS::type::Obj ||
      r["result"]["parentBlockHash"].type()             != Utilities::JS::type::Str ||
      r["result"]["blockHashForMergedMining"].type()    != Utilities::JS::type::Str ||
      r["result"]["target"].type()                      != Utilities::JS::type::Str ||
      r["result"]["feesPaidToMiner"].type()             != Utilities::JS::type::Str ||
      r["result"]["notify"].type()                      != Utilities::JS::type::Bool)
  {
    return false;
  }

  return true;
}

string GwMakerHandlerRsk::constructRawMsg(JsonNode &r) {

  LOG(INFO) << "chain: " << def_.chainType_ << ", topic: " << def_.rawGwTopic_
  << ", parent block hash: "           << r["result"]["parentBlockHash"].str()
  << ", block hash for merge mining: " << r["result"]["blockHashForMergedMining"].str()
  << ", target: "                      << r["result"]["target"].str()
  << ", fees paid to miner: "          << r["result"]["feesPaidToMiner"].str()
  << ", notify: "                      << r["result"]["notify"].boolean();

  return Strings::Format("{\"created_at_ts\":%u,"
                        "\"chainType\":\"%s\","
                        "\"rpcAddress\":\"%s\","
                        "\"rpcUserPwd\":\"%s\","
                        "\"target\":\"%s\","
                        "\"parentBlockHash\":\"%s\","
                        "\"blockHashForMergedMining\":\"%s\","
                        "\"feesPaidToMiner\":\"%s\","
                        "\"notify\":\"%s\"}",
                        (uint32_t)time(nullptr),
                        def_.chainType_.c_str(),
                        def_.rpcAddr_.c_str(),
                        def_.rpcUserPwd_.c_str(),
                        r["result"]["target"].str().c_str(), 
                        r["result"]["parentBlockHash"].str().c_str(),
                        r["result"]["blockHashForMergedMining"].str().c_str(),
                        r["result"]["feesPaidToMiner"].str().c_str(),
                        r["result"]["notify"].boolean() ? "true" : "false");
}


///////////////////////////////GwMakerHandlerEth////////////////////////////////////
string GwMakerHandlerEth::processRawGw(const string& msg) 
{
  JsonNode r;
  if (!JsonNode::parse(msg.c_str(), msg.c_str() + msg.length(), r)) {
    LOG(ERROR) << "decode gw failure: " << msg;
    return "";
  }

  // check fields
  if (!checkFields(r)) {
    LOG(ERROR) << "gw check fields failure";
    return "";
  }

  return constructRawMsg(r);
}

bool GwMakerHandlerEth::checkFields(JsonNode &r)
{
  // Ethereum's GetWork gives us 3 values:

  // { ... "result":[
  // "0x645cf20198c2f3861e947d4f67e3ab63b7b2e24dcc9095bd9123e7b33371f6cc",
  // "0xabad8f99f3918bf903c6a909d9bbc0fdfa5a2f4b9cb1196175ec825c6610126c",
  // "0x0000000394427b08175efa9a9eb59b9123e2969bf19bf272b20787ed022fbe6c"
  // ]}

  // First value is headerhash, second value is seedhash and third value is
  // target. Seedhash is used to identify DAG file, headerhash and 64 bit
  // nonce value chosen by our miner give us hash, which, if below provided
  // target, yield block/share.

  // error
  // {
  //     "jsonrpc": "2.0",
  //     "id": 73,
  //     "error": {
  //         "code": -32000,
  //         "message": "mining not ready: No work available yet, don't panic."
  //     }
  // }
  if (r.type() != Utilities::JS::type::Obj)
  {
    LOG(ERROR) << "getwork return not jason";
    return false;
  }

  JsonNode result = r["result"];
  if (result["error"].type() == Utilities::JS::type::Obj &&
      result["error"]["message"].type() == Utilities::JS::type::Str)
  {
    LOG(ERROR) << result["error"]["message"].str();
    return false;
  }

  if (r.type() != Utilities::JS::type::Obj ||
      r["result"].type() != Utilities::JS::type::Array ||
      r["result"].array().size() != 3)
  {
    LOG(ERROR) << "getwork retrun unexpected";
    return false;
  }

  return true;
}

string GwMakerHandlerEth::constructRawMsg(JsonNode &r) {
  auto result = r["result"].array();
  
  LOG(INFO) << "chain: "    << def_.chainType_
            << ", topic: "  << def_.rawGwTopic_
            << ", target: " << result[2].str()
            << ", hHash: "  << result[0].str()
            << ", sHash: "  << result[1].str();

  return Strings::Format("{\"created_at_ts\":%u,"
                         "\"chainType\":\"%s\","
                         "\"rpcAddress\":\"%s\","
                         "\"rpcUserPwd\":\"%s\","
                         "\"target\":\"%s\","
                         "\"hHash\":\"%s\","
                         "\"sHash\":\"%s\"}",
                         (uint32_t)time(nullptr),
                         def_.chainType_.c_str(),
                         def_.rpcAddr_.c_str(), 
                         def_.rpcUserPwd_.c_str(),
                         result[2].str().c_str(),
                         result[0].str().c_str(), 
                         result[1].str().c_str());
}


///////////////////////////////GwMakerHandlerSia////////////////////////////////////
string GwMakerHandlerSia::processRawGw(const string &msg)
{
  if (msg.length() != 112)
    return "";

  // target	[0-32)
  // header	[32-112)
  // parent block ID	[32-64)	[0-32)
  // nonce	[64-72)	[32-40)
  // timestamp	[72-80)	[40-48)
  // merkle root	[80-112)	[48-80)
  string targetStr;
  for (int i = 0; i < 32; ++i)
  {
    uint8 val = (uint8)msg[i];
    targetStr += Strings::Format("%02x", val);
  }

  //Claymore purposely reverses the timestamp
  //"00000000000000021f3e8ede65495c4311ef59e5b7a4338542e573819f5979e90000000000000000cd33aa5a00000000486573a66f31f5911959fce210ef557c715f716d0f022e1ba9f396294fc39d42"

  string headerStr;
  for (int i = 32; i < 112; ++i)
  {
    uint8 val = (uint8)msg[i];
    headerStr += Strings::Format("%02x", val);
  }

  //time stamp
  // uint64 timestamp = *((uint64*)&msg[72]);
  // string timestampStr = Strings::Format("%08x%08x", timestamp >> 32, timestamp & 0xFFFFFFFF);
  // DLOG(INFO) << "timestamp string=" <<  timestampStr;

  // headerStr += timestampStr;

  // for (int i = 80; i < 112; ++i)
  // {
  //   uint8 val = (uint8)msg[i];
  //   headerStr += Strings::Format("%02x", val);
  // }

  LOG(INFO) << "chain: "    << def_.chainType_
            << ", topic: "  << def_.rawGwTopic_
            << ", target: " << targetStr
            << ", hHash: "  << headerStr;

  //LOG(INFO) << "Sia work target 0x" << targetStr << ", blkId 0x" << blkIdStr << ;
  return Strings::Format("{\"created_at_ts\":%u,"
                         "\"chainType\":\"%s\","
                         "\"rpcAddress\":\"%s\","
                         "\"rpcUserPwd\":\"%s\","
                         "\"target\":\"%s\","
                         "\"hHash\":\"%s\"}",
                         (uint32_t)time(nullptr),
                         def_.chainType_.c_str(),
                         def_.rpcAddr_.c_str(),
                         def_.rpcUserPwd_.c_str(),
                         targetStr.c_str(),
                         headerStr.c_str());
}

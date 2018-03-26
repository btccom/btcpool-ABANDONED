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
*/

#include "GwMaker.h"

#include <glog/logging.h>
#include <util.h>
#include <utilstrencodings.h>
#include "Utils.h"

// GwMaker::GwMaker(const string &rskdRpcAddr, const string &rskdRpcUserpass,
//                    const string &kafkaBrokers, uint32_t kRpcCallInterval)
// : running_(true), rskdRpcAddr_(rskdRpcAddr),
// rskdRpcUserpass_(rskdRpcUserpass), kRpcCallInterval_(kRpcCallInterval),
// kafkaBrokers_(kafkaBrokers),
// kafkaProducer_(kafkaBrokers_.c_str(), KAFKA_TOPIC_RAWGW, 0/* partition */)
// {
// }

GwMaker::GwMaker(const GwDefinition gwDef,
                 const string &kafkaBrokers) : gwDef_(gwDef),
                                               running_(true),
                                               kafkaProducer_(kafkaBrokers.c_str(), gwDef.topic.c_str(), 0 /* partition */)
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
  LOG(INFO) << "stop GwMaker " << gwDef_.topic;
}

void GwMaker::kafkaProduceMsg(const void *payload, size_t len) {
  kafkaProducer_.produce(payload, len);
}

// string GwMaker::constructRequest() {
//   //return "{\"jsonrpc\": \"2.0\", \"method\": \"mnr_getWork\", \"params\": [], \"id\": 1}";
// }

bool GwMaker::rskdRpcGw(string &response)
{
  //string request = constructRequest();
  bool res = rpcCall(gwDef_.addr.c_str(),
                     gwDef_.userpwd.c_str(),
                     gwDef_.data.empty() ? nullptr : gwDef_.data.c_str(),
                     response,
                     gwDef_.agent.c_str());

  if (!res)
  {
    LOG(ERROR) << "rskd RPC failure";
    return false;
  }
  return true;
}

bool GwMaker::checkFields(JsonNode &r) {
  if (r["result"].type()                                != Utilities::JS::type::Obj ||
      r["result"]["parentBlockHash"].type()             != Utilities::JS::type::Str ||
      r["result"]["blockHashForMergedMining"].type()    != Utilities::JS::type::Str ||
      r["result"]["target"].type()                      != Utilities::JS::type::Str ||
      r["result"]["feesPaidToMiner"].type()             != Utilities::JS::type::Str ||
      r["result"]["notify"].type()                      != Utilities::JS::type::Bool) {
    return false;
  }

  return true;
}

string GwMaker::constructRawMsg(string &gw, JsonNode &r) {
  return "";
  // const uint256 gwHash = Hash(gw.begin(), gw.end());

  // LOG(INFO) << ", parent block hash: "    << r["result"]["parentBlockHash"].str()
  // << ", block hash for merge mining: " << r["result"]["blockHashForMergedMining"].str()
  // << ", target: "               << r["result"]["target"].str()
  // << ", fees paid to miner: "   << r["result"]["feesPaidToMiner"].str()
  // << ", notify: " << r["result"]["notify"].boolean()
  // << ", gwhash: " << gwHash.ToString();

  // return Strings::Format("{\"created_at_ts\":%u,"
  //                        "\"rskdRpcAddress\":\"%s\","
  //                        "\"rskdRpcUserPwd\":\"%s\","
  //                        "\"target\":\"%s\","
  //                        "\"parentBlockHash\":\"%s\","
  //                        "\"blockHashForMergedMining\":\"%s\","
  //                        "\"feesPaidToMiner\":\"%s\","
  //                        "\"notify\":\"%s\"}",
  //                        (uint32_t)time(nullptr), 
  //                        rskdRpcAddr_.c_str(), 
  //                        rskdRpcUserpass_.c_str(),
  //                        r["result"]["target"].str().c_str(), 
  //                        r["result"]["parentBlockHash"].str().c_str(),
  //                        r["result"]["blockHashForMergedMining"].str().c_str(),
  //                        r["result"]["feesPaidToMiner"].str().c_str(),
  //                        r["result"]["notify"].boolean() ? "true" : "false");
}

string GwMaker::makeRawGwMsg() {
  string gw;
  if (!rskdRpcGw(gw)) {
    return "";
  }
  LOG(INFO) << "getwork len: " << gw.length();
  return gwDef_.handler ? gwDef_.handler->processRawMsg(gwDef_, gw) : "";
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
    usleep(gwDef_.interval * 1000);
    submitRawGwMsg();
  }

  LOG(INFO) << "GwMaker " << gwDef_.topic << " stopped";
}

///////////////////////////////GwHandlerEth////////////////////////////////////
string GwHandlerEth::processRawMsg(const GwDefinition& def, const string& msg) 
{
  JsonNode r;
  if (!JsonNode::parse(msg.c_str(), msg.c_str() + msg.length(), r)) {
    LOG(ERROR) << "decode gw failure: " << msg;
    return "";
  }

  //LOG(INFO) << "result type: " << (int)r["result"].type();
  //LOG(INFO) << "parse result: " << r["result"].str();

  // check fields
  if (!checkFields(r)) {
    LOG(ERROR) << "gw check fields failure";
    return "";
  }

  return constructRawMsg(def, r);
}

// GwMakerEth::GwMakerEth(const string &rskdRpcAddr, const string &rskdRpcUserpass,
//                        const string &kafkaBrokers, uint32_t kRpcCallInterval) : GwMaker(rskdRpcAddr, rskdRpcUserpass, kafkaBrokers, kRpcCallInterval)
// {
// }

// string GwMakerEth::constructRequest()
// {
//   return "{\"jsonrpc\": \"2.0\", \"method\": \"eth_getWork\", \"params\": [], \"id\": 1}";
// }

bool GwHandlerEth::checkFields(JsonNode &r)
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

string GwHandlerEth::constructRawMsg(const GwDefinition& def, JsonNode &r) {
  // const uint256 gwHash = Hash(gw.begin(), gw.end());
  // LOG(INFO) << "gwhash: " << gwHash.ToString();
  auto result = r["result"].array();
  return Strings::Format("{\"created_at_ts\":%u,"
                         "\"rskdRpcAddress\":\"%s\","
                         "\"rskdRpcUserPwd\":\"%s\","
                         "\"target\":\"%s\","
                         "\"hHash\":\"%s\","
                         "\"sHash\":\"%s\"}",
                         (uint32_t)time(nullptr), 
                         def.addr.c_str(), 
                         def.userpwd.c_str(),
                         result[2].str().c_str(),
                         result[0].str().c_str(), 
                         result[1].str().c_str());
}


///////////////////////////////GwHandlerSia////////////////////////////////////
string GwHandlerSia::processRawMsg(const GwDefinition &def, const string &msg)
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

  // string blkIdStr;
  // for (int i = 32; i < 64; ++i)
  // {
  //   uint8 val = (uint8)msg[i];
  //   blkIdStr += Strings::Format("%02x", val);
  // }

  // string nonceStr;
  // for (int i = 64; i < 72; ++i)
  // {
  //   uint8 val = (uint8)msg[i];
  //   nonceStr += Strings::Format("%02x", val);
  // }

  // string timestampStr;
  // for (int i = 72; i < 80; ++i)
  // {
  //   uint8 val = (uint8)msg[i];
  //   timestampStr += Strings::Format("%02x", val);
  // }

  // string merkleRootStr;
  // for (int i = 80; i < 112; ++i)
  // {
  //   uint8 val = (uint8)msg[i];
  //   merkleRootStr += Strings::Format("%02x", val);
  // }

  string headerStr;
  for (int i = 32; i < 112; ++i)
  {
    uint8 val = (uint8)msg[i];
    headerStr += Strings::Format("%02x", val);
  }

  //LOG(INFO) << "Sia work target 0x" << targetStr << ", blkId 0x" << blkIdStr << ;
  return Strings::Format("{\"created_at_ts\":%u,"
                         "\"rskdRpcAddress\":\"%s\","
                         "\"rskdRpcUserPwd\":\"%s\","
                         "\"target\":\"%s\","
                         "\"hHash\":\"%s\"}",
                         (uint32_t)time(nullptr),
                         def.addr.c_str(),
                         def.userpwd.c_str(),
                         targetStr.c_str(),
                         headerStr.c_str());
}
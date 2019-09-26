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
#include "GwMakerEth.h"
#include "Utils.h"
#include <glog/logging.h>
#include <limits.h>

///////////////////////////////GwMakerHandlerEth////////////////////////////////////
bool GwMakerHandlerEth::checkFields(JsonNode &r) {
  if (r.type() != Utilities::JS::type::Array || r.array().size() < 2) {
    LOG(ERROR) << "[getBlockByNumber,getWork] returns unexpected";
    return false;
  }

  auto responses = r.array();
  return checkFieldsPendingBlock(responses[0]) &&
      checkFieldsGetwork(responses[1]);
}

bool GwMakerHandlerEth::checkFieldsPendingBlock(JsonNode &r) {
  /*
    response of eth_getBlockByNumber
    {
      "jsonrpc": "2.0",
      "id": 1,
      "result": {
        "difficulty": "0xbab6a8bebb86a",
        "extraData": "0x452f4254432e434f4d2f",
        "gasLimit": "0x7a1200",
        "gasUsed": "0x79f82b",
        "hash": null,
        "logsBloom": "...",
        "miner": null,
        "mixHash":
    "0x0000000000000000000000000000000000000000000000000000000000000000",
        "nonce": null,
        "number": "0x62853d",
        "parentHash":
    "0xd0e3d722db1fa9e0a05330bc0a1b4b3421ca61fcf23224d82869eeb2d77263a2",
        "receiptsRoot":
    "0x823f1bfe3a5f37f9891c528034d6a124752f63bdcc0e88bf3b731375301337f8",
        "sha3Uncles":
    "0x1dcc4de8dec75d7aab85b567b6ccd41ad312451b948a7413f0a142fd40d49347",
        "size": "0x8bfb",
        "stateRoot":
    "0xdd58b5ce8ad79ca02bb9321c3b1a2123b9f627d9164cf4b73371fc39cfc12c5e",
        "timestamp": "0x5bb71091",
        "totalDifficulty": null,
        "transactions": [...],
        "transactionsRoot":
    "0x8bbba93b1b39a96308aac8914b7f407d0ff46bc7456241f9b540874b2e1a4502",
        "uncles": [...]
      }
    }
    */
  if (r.type() != Utilities::JS::type::Obj) {
    LOG(ERROR) << "getBlockByNumber(penging) returns a non-object";
    return false;
  }

  JsonNode result = r["result"];
  if (result["error"].type() == Utilities::JS::type::Obj &&
      result["error"]["message"].type() == Utilities::JS::type::Str) {
    LOG(ERROR) << result["error"]["message"].str();
    return false;
  }

  if (result.type() != Utilities::JS::type::Obj) {
    LOG(ERROR) << "getBlockByNumber(penging) retrun unexpected";
    return false;
  }

  if (result["parentHash"].type() != Utilities::JS::type::Str ||
      result["gasLimit"].type() != Utilities::JS::type::Str ||
      result["gasUsed"].type() != Utilities::JS::type::Str ||
      // number will be null in some versions of Parity
      // result["number"].type() != Utilities::JS::type::Str ||
      result["transactions"].type() != Utilities::JS::type::Array ||
      result["uncles"].type() != Utilities::JS::type::Array) {
    LOG(ERROR) << "result of getBlockByNumber(penging): missing fields";
  }

  return true;
}

bool GwMakerHandlerEth::checkFieldsGetwork(JsonNode &r) {
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

  if (r.type() != Utilities::JS::type::Obj) {
    LOG(ERROR) << "getwork returns a non-object";
    return false;
  }

  JsonNode result = r["result"];
  if (result["error"].type() == Utilities::JS::type::Obj &&
      result["error"]["message"].type() == Utilities::JS::type::Str) {
    LOG(ERROR) << result["error"]["message"].str();
    return false;
  }

  if (result.type() != Utilities::JS::type::Array ||
      result.array().size() < 3) {
    LOG(ERROR) << "getwork retrun unexpected";
    return false;
  }

  return true;
}

string GwMakerHandlerEth::constructRawMsg(JsonNode &r) {
  auto responses = r.array();
  auto block = responses[0]["result"];
  auto work = responses[1]["result"].array();

  string heightStr = "null";

  // number will be null in some versions of Parity
  if (block["number"].type() == Utilities::JS::type::Str) {
    heightStr = block["number"].str();
  }

  // height/block-number in eth_getWork.
  // Parity will response this field.
  if (work.size() >= 4 && work[3].type() == Utilities::JS::type::Str) {
    if (heightStr != "null" && heightStr != work[3].str()) {
      LOG(WARNING)
          << "block height mis-matched between getBlockByNumber(pending) "
          << heightStr << " and getWork() " << work[3].str();
    }
    heightStr = work[3].str();
  }

  long height = strtol(heightStr.c_str(), nullptr, 16);
  if (height < 1 || height == LONG_MAX) {
    LOG(WARNING) << "block height/number wrong: " << heightStr << " (" << height
                 << ")";
    return "";
  }

  float gasLimit = (float)strtoll(block["gasLimit"].str().c_str(), nullptr, 16);
  float gasUsed = (float)strtoll(block["gasUsed"].str().c_str(), nullptr, 16);
  float gasUsedPercent = gasUsed / gasLimit * 100;

  size_t uncles = block["uncles"].array().size();
  size_t transactions = block["transactions"].array().size();
  string parentHash = block["parentHash"].str();

  // eth_getWork extension fields for BTCPool:
  //   work[4], 32 bytes hex encoded parent block header pow-hash
  //   work[5], hex encoded gas limit
  //   work[6], hex encoded gas used
  //   work[7], hex encoded transaction count
  //   work[8], hex encoded uncle count
  bool btcpoolExtensionFields = false;
  if (work.size() >= 9 && work[4].type() == Utilities::JS::type::Str &&
      work[5].type() == Utilities::JS::type::Str &&
      work[6].type() == Utilities::JS::type::Str &&
      work[7].type() == Utilities::JS::type::Str &&
      work[8].type() == Utilities::JS::type::Str) {
    btcpoolExtensionFields = true;

    parentHash = work[4].str();

    gasLimit = (float)strtoll(work[5].str().c_str(), nullptr, 16);
    gasUsed = (float)strtoll(work[6].str().c_str(), nullptr, 16);
    gasUsedPercent = gasUsed / gasLimit * 100;

    transactions = strtoll(work[7].str().c_str(), nullptr, 16);
    uncles = strtoll(work[8].str().c_str(), nullptr, 16);
  }

  // This field is RLP encoded header with additional 4 bytes at the end of
  // extra data to be filled by sserver
  string header;
  if (work.size() >= 10 && work[9].type() == Utilities::JS::type::Str) {
    LOG(INFO) << "header for extra nonce: " << work[9];
    header = Strings::Format(",\"header\":\"%s\"", work[9].str());
  }

  LOG(INFO) << "chain: " << def_.chainType_ << ", topic: " << def_.rawGwTopic_
            << ", parent: " << parentHash << ", target: " << work[2].str()
            << ", hHash: " << work[0].str() << ", sHash: " << work[1].str()
            << ", height: " << height << ", uncles: " << uncles
            << ", transactions: " << transactions
            << ", gasUsedPercent: " << gasUsedPercent
            << ", btcpoolExtensionFields: "
            << (btcpoolExtensionFields ? "true" : "false");

  return Strings::Format(
      "{"
      "\"created_at_ts\":%u,"
      "\"chainType\":\"%s\","
      "\"rpcAddress\":\"%s\","
      "\"rpcUserPwd\":\"%s\","
      "\"parent\":\"%s\","
      "\"target\":\"%s\","
      "\"hHash\":\"%s\","
      "\"sHash\":\"%s\","
      "\"height\":%d,"
      "\"uncles\":%u,"
      "\"transactions\":%u,"
      "\"gasUsedPercent\":%f"
      "%s"
      "}",
      (uint32_t)time(nullptr),
      def_.chainType_,
      def_.rpcAddr_,
      def_.rpcUserPwd_,
      parentHash,
      work[2].str(),
      work[0].str(),
      work[1].str(),
      height,
      uncles,
      transactions,
      gasUsedPercent,
      header);
}

string GwMakerHandlerEth::getBlockHeight() {
  const string request =
      "{\"jsonrpc\":\"2.0\",\"method\":\"eth_getBlockByNumber\",\"params\":["
      "\"pending\", false],\"id\":2}";

  string response;
  bool res = blockchainNodeRpcCall(
      def_.rpcAddr_.c_str(),
      def_.rpcUserPwd_.c_str(),
      request.c_str(),
      response);
  if (!res) {
    LOG(ERROR) << "get pending block failed";
    return "";
  }

  JsonNode j;
  if (!JsonNode::parse(
          response.c_str(), response.c_str() + response.length(), j)) {
    LOG(ERROR) << "deserialize block informaiton failed";
    return "";
  }

  JsonNode result = j["result"];
  if (result.type() != Utilities::JS::type::Obj ||
      result["number"].type() != Utilities::JS::type::Str) {
    LOG(ERROR) << "block informaiton format not expected: " << response;
    return "";
  }

  return result["number"].str();
}

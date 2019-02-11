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
#include <algorithm>

///////////////////////////////GwMakerHandlerEth////////////////////////////////////
bool GwMakerHandlerEth::checkFields(JsonNode &r) {
  // Ethereum's GetWork (btcpool fork) gives us 9 values:

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
      result.array().size() < 9 ||
      std::any_of(
          result.array().begin(),
          result.array().end(),
          [](const JsonNode &element) {
            return element.type() != Utilities::JS::type::Str;
          })) {
    LOG(ERROR) << "getwork retrun unexpected";
    return false;
  }

  return true;
}

string GwMakerHandlerEth::constructRawMsg(JsonNode &r) {
  auto work = r["result"].array();

  try {
    long height = stol(work[3].str(), nullptr, 16);
    float gasLimit = static_cast<float>(stoll(work[5].str(), nullptr, 16));
    float gasUsed = static_cast<float>(stoll(work[6].str(), nullptr, 16));
    float gasUsedPercent = gasUsed / gasLimit * 100;
    size_t transactions = stoull(work[7].str(), nullptr, 16);
    size_t uncles = stoull(work[8].str(), nullptr, 16);

    LOG(INFO) << "chain: " << def_.chainType_ << ", topic: " << def_.rawGwTopic_
              << ", parent: " << work[4].str() << ", target: " << work[2].str()
              << ", hHash: " << work[0].str() << ", sHash: " << work[1].str()
              << ", height: " << height << ", uncles: " << uncles
              << ", transactions: " << transactions
              << ", gasUsedPercent: " << gasUsedPercent;

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
        "\"height\":%ld,"
        "\"uncles\":%lu,"
        "\"transactions\":%lu,"
        "\"gasUsedPercent\":%f"
        "}",
        (uint32_t)time(nullptr),
        def_.chainType_.c_str(),
        def_.rpcAddr_.c_str(),
        def_.rpcUserPwd_.c_str(),
        work[4].str().c_str(),
        work[2].str().c_str(),
        work[0].str().c_str(),
        work[1].str().c_str(),
        height,
        uncles,
        transactions,
        gasUsedPercent);
  } catch (std::invalid_argument &) {
    LOG(ERROR) << "getwork result is invalid: " << r;
  } catch (std::out_of_range &) {
    LOG(ERROR) << "getwork result is out of range: " << r;
  }

  return "";
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

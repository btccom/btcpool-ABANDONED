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

  maintained by YihaoPeng since Feb 20, 2018
*/

#include "GwMakerRsk.h"
#include "Utils.h"
#include <glog/logging.h>
#include <limits.h>

///////////////////////////////GwMakerHandlerRsk////////////////////////////////////
bool GwMakerHandlerRsk::checkFields(JsonNode &r) {
  if (r["result"].type() != Utilities::JS::type::Obj ||
      r["result"]["parentBlockHash"].type() != Utilities::JS::type::Str ||
      r["result"]["blockHashForMergedMining"].type() !=
          Utilities::JS::type::Str ||
      r["result"]["target"].type() != Utilities::JS::type::Str ||
      r["result"]["feesPaidToMiner"].type() != Utilities::JS::type::Str ||
      r["result"]["notify"].type() != Utilities::JS::type::Bool) {
    return false;
  }

  return true;
}

string GwMakerHandlerRsk::constructRawMsg(JsonNode &r) {

  LOG(INFO) << "chain: " << def_.chainType_ << ", topic: " << def_.rawGwTopic_
            << ", parent block hash: " << r["result"]["parentBlockHash"].str()
            << ", block hash for merge mining: "
            << r["result"]["blockHashForMergedMining"].str()
            << ", target: " << r["result"]["target"].str()
            << ", fees paid to miner: " << r["result"]["feesPaidToMiner"].str()
            << ", notify: " << r["result"]["notify"].boolean();

  return Strings::Format(
      "{\"created_at_ts\":%u,"
      "\"chainType\":\"%s\","
      "\"rskdRpcAddress\":\"%s\","
      "\"rskdRpcUserPwd\":\"%s\","
      "\"target\":\"%s\","
      "\"parentBlockHash\":\"%s\","
      "\"blockHashForMergedMining\":\"%s\","
      "\"feesPaidToMiner\":\"%s\","
      "\"notify\":\"%s\"}",
      (uint32_t)time(nullptr),
      def_.chainType_,
      def_.rpcAddr_,
      def_.rpcUserPwd_,
      r["result"]["target"].str(),
      r["result"]["parentBlockHash"].str(),
      r["result"]["blockHashForMergedMining"].str(),
      r["result"]["feesPaidToMiner"].str(),
      r["result"]["notify"].boolean() ? "true" : "false");
}

/*
 The MIT License (MIT)

 Copyright (C) 2017 vcash Labs Ltd.

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

#include "GwMakerVcash.h"
#include "Utils.h"
#include <glog/logging.h>
#include <limits.h>

///////////////////////////////GwMakerHandlerVcash////////////////////////////////////
bool GwMakerHandlerVcash::checkFields(JsonNode &r) {
  if (r["prev_hash"].type() != Utilities::JS::type::Str ||
      r["cur_hash"].type() != Utilities::JS::type::Str ||
      r["bits"].type() != Utilities::JS::type::Int ||
      r["base_rewards"].type() != Utilities::JS::type::Int ||
      r["transactions_fee"].type() !=
          Utilities::JS::type::Int || // maybe double
      r["height"].type() != Utilities::JS::type::Int) {
    return false;
  }

  return true;
}

string GwMakerHandlerVcash::constructRawMsg(JsonNode &r) {

  LOG(INFO) << "chain: " << def_.chainType_ << ", topic: " << def_.rawGwTopic_
            << ", prev hash: " << r["prev_hash"].str()
            << ", block hash for merge mining: " << r["cur_hash"].str()
            << ", bits: " << r["bits"].uint32()
            << ", height: " << r["height"].uint64()
            << ", fees paid to miner: " << r["base_rewards"].uint64()
            << ", transactions_fee: " << r["transactions_fee"].uint64();

  return Strings::Format(
      "{\"created_at_ts\":%u,"
      "\"chainType\":\"%s\","
      "\"vcashdRpcAddress\":\"%s\","
      "\"vcashdRpcUserPwd\":\"%s\","

      "\"bits\":%" PRIu32
      ","
      "\"parentBlockHash\":\"%s\","
      "\"blockHashForMergedMining\":\"%s\","
      "\"baserewards\":%" PRIu64
      ","
      "\"transactionsfee\":%" PRIu64
      ","
      "\"height\": %" PRIu64 "}",
      (uint32_t)time(nullptr),
      def_.chainType_.c_str(),
      def_.rpcAddr_.c_str(),
      def_.rpcUserPwd_.c_str(),
      r["bits"].uint32(),
      r["prev_hash"].str().c_str(),
      r["cur_hash"].str().c_str(),
      r["base_rewards"].uint64(),
      r["transactions_fee"].uint64(),
      r["height"].uint64());
}

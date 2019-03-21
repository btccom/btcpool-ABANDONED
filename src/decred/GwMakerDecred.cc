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
#include "GwMakerDecred.h"
#include "Utils.h"
#include <glog/logging.h>
#include <limits.h>

///////////////////////////////GwMakerHandlerDecred////////////////////////////////////
bool GwMakerHandlerDecred::checkFields(JsonNode &r) {
  if (r.type() != Utilities::JS::type::Array || r.array().size() != 2) {
    return false;
  }

  auto &r0 = r.array().at(0);
  if (r0["result"].type() != Utilities::JS::type::Int) {
    return false;
  }

  auto &r1 = r.array().at(1);
  if (r1["result"].type() != Utilities::JS::type::Obj ||
      r1["result"]["data"].type() != Utilities::JS::type::Str ||
      r1["result"]["data"].size() != 384 ||
      !IsHex(r1["result"]["data"].str()) ||
      r1["result"]["target"].type() != Utilities::JS::type::Str ||
      r1["result"]["target"].size() != 64 ||
      !IsHex(r1["result"]["target"].str())) {
    return false;
  }

  return true;
}

string GwMakerHandlerDecred::constructRawMsg(JsonNode &r) {
  auto &r0 = r.array().at(0);
  auto &r1 = r.array().at(1);
  LOG(INFO) << "chain: " << def_.chainType_ << ", topic: " << def_.rawGwTopic_
            << ", data: " << r1["result"]["data"].str()
            << ", target: " << r1["result"]["target"].str()
            << ", network: " << r0["result"].uint32();

  return Strings::Format(
      "{\"created_at_ts\":%u,"
      "\"chainType\":\"%s\","
      "\"rpcAddress\":\"%s\","
      "\"rpcUserPwd\":\"%s\","
      "\"data\":\"%s\","
      "\"target\":\"%s\","
      "\"network\":%u}",
      (uint32_t)time(nullptr),
      def_.chainType_,
      def_.rpcAddr_,
      def_.rpcUserPwd_,
      r1["result"]["data"].str(),
      r1["result"]["target"].str(),
      r0["result"].uint32());
}

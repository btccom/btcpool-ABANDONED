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
  if (r["result"].type() != Utilities::JS::type::Obj ||
      r["result"]["data"].type() != Utilities::JS::type::Str ||
      r["result"]["data"].size() != 384 || !IsHex(r["result"]["data"].str()) ||
      r["result"]["target"].type() != Utilities::JS::type::Str ||
      r["result"]["target"].size() != 64 ||
      !IsHex(r["result"]["target"].str())) {
    return false;
  }

  return true;
}

string GwMakerHandlerDecred::constructRawMsg(JsonNode &r) {
  LOG(INFO) << "chain: " << def_.chainType_ << ", topic: " << def_.rawGwTopic_
            << ", data: " << r["result"]["data"].str()
            << ", target: " << r["result"]["target"].str();

  return Strings::Format(
      "{\"created_at_ts\":%u,"
      "\"chainType\":\"%s\","
      "\"rpcAddress\":\"%s\","
      "\"rpcUserPwd\":\"%s\","
      "\"data\":\"%s\","
      "\"target\":\"%s\"}",
      (uint32_t)time(nullptr),
      def_.chainType_.c_str(),
      def_.rpcAddr_.c_str(),
      def_.rpcUserPwd_.c_str(),
      r["result"]["data"].str().c_str(),
      r["result"]["target"].str().c_str());
}

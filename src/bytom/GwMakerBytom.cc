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
#include "GwMakerBytom.h"
#include "Utils.h"
#include <glog/logging.h>
#include <limits.h>

///////////////////////////////GwMakerHandlerBytom////////////////////////////////////
bool GwMakerHandlerBytom::checkFields(JsonNode &r) {
  //{"status":"success","data":{"block_header":
  //"0101a612b60a752a07bab9d7495a6861f88fc6f1c6656a29de3afda4747965400762c88cfb8d8ad7054010bb9a9b0622a77f633f47973971a955ca6ae00bad39372c9bf957b11fdae27dc9c377e5192668bc0a367e4a4764f11e7c725ecced1d7b6a492974fab1b6d5bc009ffcfd86808080801d",
  //"seed":"9e6f94f7a8b839b8bfd349fdb794cc125a0711a25c6b4c1dfbdf8d448e0a9a45"}}
  if (r.type() != Utilities::JS::type::Obj) {
    LOG(ERROR) << "Bytom getwork return not jason";
    return false;
  }

  JsonNode status = r["status"];
  if (status.type() != Utilities::JS::type::Str) {
    LOG(ERROR) << "Bytom getwork return not jason";
    return false;
  }

  if (status.str() != "success") {
    LOG(ERROR) << "status " << status.str();
    return false;
  }

  JsonNode data = r["data"];
  if (data.type() != Utilities::JS::type::Obj ||
      data["block_header"].type() != Utilities::JS::type::Str ||
      data["seed"].type() != Utilities::JS::type::Str) {
    LOG(ERROR) << "Bytom getwork retrun unexpected";
    return false;
  }

  return true;
}

string GwMakerHandlerBytom::constructRawMsg(JsonNode &r) {
  auto data = r["data"];
  string header = data["block_header"].str();
  string seed = data["seed"].str();

  LOG(INFO) << "chain: " << def_.chainType_ << ", topic: " << def_.rawGwTopic_
            << ", hHash: " << header << ", sHash: " << seed;

  return Strings::Format(
      "{\"created_at_ts\":%u,"
      "\"chainType\":\"%s\","
      "\"rpcAddress\":\"%s\","
      "\"rpcUserPwd\":\"%s\","
      "\"hHash\":\"%s\","
      "\"sHash\":\"%s\"}",
      (uint32_t)time(nullptr),
      def_.chainType_,
      def_.rpcAddr_,
      def_.rpcUserPwd_,
      header,
      seed);
}

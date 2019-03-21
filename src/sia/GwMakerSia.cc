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
#include "GwMakerSia.h"
#include "Utils.h"
#include <glog/logging.h>
#include <limits.h>

///////////////////////////////GwMakerHandlerSia////////////////////////////////////
string GwMakerHandlerSia::processRawGw(const string &msg) {
  if (msg.length() != 112)
    return "";

  // target	[0-32)
  // header	[32-112)
  // parent block ID	[32-64)	[0-32)
  // nonce	[64-72)	[32-40)
  // timestamp	[72-80)	[40-48)
  // merkle root	[80-112)	[48-80)
  string targetStr;
  for (int i = 0; i < 32; ++i) {
    uint8_t val = (uint8_t)msg[i];
    targetStr += Strings::Format("%02x", val);
  }

  // Claymore purposely reverses the timestamp
  //"00000000000000021f3e8ede65495c4311ef59e5b7a4338542e573819f5979e90000000000000000cd33aa5a00000000486573a66f31f5911959fce210ef557c715f716d0f022e1ba9f396294fc39d42"

  string headerStr;
  for (int i = 32; i < 112; ++i) {
    uint8_t val = (uint8_t)msg[i];
    headerStr += Strings::Format("%02x", val);
  }

  // time stamp
  // uint64_t timestamp = *((uint64*)&msg[72]);
  // string timestampStr = Strings::Format("%08x%08x", timestamp >> 32,
  // timestamp & 0xFFFFFFFF); DLOG(INFO) << "timestamp string=" << timestampStr;

  // headerStr += timestampStr;

  // for (int i = 80; i < 112; ++i)
  // {
  //   uint8_t val = (uint8)msg[i];
  //   headerStr += Strings::Format("%02x", val);
  // }

  LOG(INFO) << "chain: " << def_.chainType_ << ", topic: " << def_.rawGwTopic_
            << ", target: " << targetStr << ", hHash: " << headerStr;

  // LOG(INFO) << "Sia work target 0x" << targetStr << ", blkId 0x" << blkIdStr
  // << ;
  return Strings::Format(
      "{\"created_at_ts\":%u,"
      "\"chainType\":\"%s\","
      "\"rpcAddress\":\"%s\","
      "\"rpcUserPwd\":\"%s\","
      "\"target\":\"%s\","
      "\"hHash\":\"%s\"}",
      (uint32_t)time(nullptr),
      def_.chainType_,
      def_.rpcAddr_,
      def_.rpcUserPwd_,
      targetStr,
      headerStr);
}

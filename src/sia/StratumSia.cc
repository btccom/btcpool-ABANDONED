/*
 The MIT License (MIT)

 Copyright (c) [2016] [BTC.COM]

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
#include "StratumSia.h"

#include "Utils.h"
#include "utilities_js.hpp"

#include <glog/logging.h>

StratumJobSia::StratumJobSia()
  : nTime_(0U) {
}

StratumJobSia::~StratumJobSia() {
}

string StratumJobSia::serializeToJson() const {
  return Strings::Format(
      "{\"created_at_ts\":%u"
      ",\"jobId\":%u"
      ",\"target\":\"%s\""
      ",\"hHash\":\"%s\""
      "}",
      nTime_,
      jobId_,
      networkTarget_.GetHex(),
      blockHashForMergedMining_);
}

///////////////////////////////StratumJobSia///////////////////////////
bool StratumJobSia::unserializeFromJson(const char *s, size_t len) {
  JsonNode j;
  if (!JsonNode::parse(s, s + len, j)) {
    return false;
  }
  if (j["created_at_ts"].type() != Utilities::JS::type::Int ||
      j["jobId"].type() != Utilities::JS::type::Int ||
      j["target"].type() != Utilities::JS::type::Str ||
      j["hHash"].type() != Utilities::JS::type::Str) {
    LOG(ERROR) << "parse sia stratum job failure: " << s;
    return false;
  }

  jobId_ = j["jobId"].uint64();
  nTime_ = j["created_at_ts"].uint32();
  networkTarget_ = uint256S(j["target"].str());
  blockHashForMergedMining_ = j["hHash"].str();

  return true;
}

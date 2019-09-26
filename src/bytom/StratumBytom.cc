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
#include "StratumBytom.h"
#include "Utils.h"
#include "utilities_js.hpp"

#include "bytom/bh_shared.h"

#include <glog/logging.h>

StratumJobBytom::StratumJobBytom()
  : nTime_(0U) {
}

string StratumJobBytom::serializeToJson() const {
  return Strings::Format(
      "{\"created_at_ts\":%u"
      ",\"jobId\":%u"
      ",\"sHash\":\"%s\""
      ",\"hHash\":\"%s\""
      "}",
      nTime_,
      jobId_,
      seed_,
      hHash_);
}

bool StratumJobBytom::unserializeFromJson(const char *s, size_t len) {
  JsonNode j;
  if (!JsonNode::parse(s, s + len, j)) {
    return false;
  }
  if (j["created_at_ts"].type() != Utilities::JS::type::Int ||
      j["jobId"].type() != Utilities::JS::type::Int ||
      j["sHash"].type() != Utilities::JS::type::Str ||
      j["hHash"].type() != Utilities::JS::type::Str) {
    LOG(ERROR) << "parse bytom stratum job failure: " << s;
    return false;
  }

  hHash_ = j["hHash"].str();
  updateBlockHeaderFromHash();

  jobId_ = j["jobId"].uint64();
  nTime_ = j["created_at_ts"].uint32();
  seed_ = j["sHash"].str();
  return true;
}

void StratumJobBytom::updateBlockHeaderFromHash() {
  GoSlice text = {
      (void *)hHash_.data(), (int)hHash_.length(), (int)hHash_.length()};
  DecodeBlockHeader_return bh = DecodeBlockHeader(text);
  DLOG(INFO) << "bytom block height=" << bh.r1 << ", timestamp=" << bh.r3;
  blockHeader_.version = bh.r0;
  blockHeader_.height = bh.r1;
  blockHeader_.previousBlockHash = bh.r2;
  blockHeader_.timestamp = bh.r3;
  blockHeader_.bits = bh.r4;
  blockHeader_.transactionsMerkleRoot = bh.r5;
  blockHeader_.transactionStatusHash = bh.r6;
  free(bh.r2);
  free(bh.r5);
  free(bh.r6);
}

string BlockHeaderBytom::serializeToJson() const {
  return Strings::Format(
      "{\"Version\":%u"
      ",\"Height\":%u"
      ",\"PreviousBlockHash\":\"%s\""
      ",\"Timestamp\":%u"
      ",\"TransactionsMerkleRoot\":\"%s\""
      ",\"TransactionStatusHash\":\"%s\""
      ",\"Bits\":%u"
      "}",
      version,
      height,
      previousBlockHash,
      timestamp,
      transactionsMerkleRoot,
      transactionStatusHash,
      bits);
}

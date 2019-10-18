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
#include "StratumCkb.h"
#include "Utils.h"
#include "utilities_js.hpp"
#include <glog/logging.h>

StratumJobCkb::StratumJobCkb()
  : nTime_(0U) {
}

bool StratumJobCkb::initFromRawJob(const string &msg) {
  JsonNode j;
  if (!JsonNode::parse(msg.c_str(), msg.c_str() + msg.length(), j)) {
    LOG(ERROR) << "deserialize Ckb work failed " << msg;
    return false;
  }
  if (j.type() != Utilities::JS::type::Obj ||
      j["work_id"].type() != Utilities::JS::type::Int ||
      j["parent_hash"].type() != Utilities::JS::type::Str ||
      j["pow_hash"].type() != Utilities::JS::type::Str ||
      j["height"].type() != Utilities::JS::type::Int ||
      j["target"].type() != Utilities::JS::type::Str ||
      j["timestamp"].type() != Utilities::JS::type::Int) {
    LOG(ERROR) << "work format not expected";
    return false;
  }

  nTime_ = time(nullptr);
  work_id_ = j["work_id"].uint64();
  pow_hash_ = j["pow_hash"].str();
  parent_hash_ = j["parent_hash"].str();
  height_ = j["height"].uint64();
  target_ = j["target"].str();
  timestamp_ = j["timestamp"].uint64();

  return true;
}

string StratumJobCkb::serializeToJson() const {

  return Strings::Format(
      "{\"created_at_ts\":%u"
      ",\"work_id\":%" PRIu64
      ""
      ",\"jobid\":%" PRIu64
      ""
      ",\"pow_hash\":\"%s\""
      ",\"parent_hash\":\"%s\""
      ",\"height\":%" PRIu64
      ""
      ",\"target\":\"%s\""
      ""
      ",\"timestamp\":%" PRIu64
      ""
      "}",
      nTime_,
      work_id_,
      jobId_,
      pow_hash_.c_str(),
      parent_hash_.c_str(),
      height_,
      target_.c_str(),
      timestamp_);
}

bool StratumJobCkb::unserializeFromJson(const char *s, size_t len) {
  JsonNode j;
  if (!JsonNode::parse(s, s + len, j)) {
    return false;
  }
  if (j["created_at_ts"].type() != Utilities::JS::type::Int ||
      j["work_id"].type() != Utilities::JS::type::Int ||
      j["jobid"].type() != Utilities::JS::type::Int ||
      j["pow_hash"].type() != Utilities::JS::type::Str ||
      j["parent_hash"].type() != Utilities::JS::type::Str ||
      j["height"].type() != Utilities::JS::type::Int ||
      j["target"].type() != Utilities::JS::type::Str ||
      j["timestamp"].type() != Utilities::JS::type::Int) {
    LOG(ERROR) << "parse bytom stratum job failure: " << s;
    return false;
  }

  jobId_ = j["jobid"].uint64();
  work_id_ = j["work_id"].uint64();
  nTime_ = j["created_at_ts"].uint32();
  pow_hash_ = j["pow_hash"].str();
  parent_hash_ = j["parent_hash"].str();
  height_ = j["height"].int64();
  target_ = j["target"].str();
  timestamp_ = j["timestamp"].uint64();
  return true;
}

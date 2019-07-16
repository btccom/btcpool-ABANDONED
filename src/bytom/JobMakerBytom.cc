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
#include "JobMakerBytom.h"
#include "StratumBytom.h"

#include "Utils.h"
#include "utilities_js.hpp"
///////////////////////////////////JobMakerHandlerBytom//////////////////////////////////
bool JobMakerHandlerBytom::processMsg(const string &msg) {
  JsonNode j;
  if (!JsonNode::parse(msg.c_str(), msg.c_str() + msg.length(), j)) {
    LOG(ERROR) << "deserialize bytom work failed " << msg;
    return false;
  }

  if (!validate(j))
    return false;

  string header = j["hHash"].str();
  if (header == header_)
    return false;

  header_ = move(header);
  time_ = j["created_at_ts"].uint32();

  seed_ = j["sHash"].str();
  return true;
}

bool JobMakerHandlerBytom::validate(JsonNode &j) {
  // check fields are valid
  if (j.type() != Utilities::JS::type::Obj ||
      j["created_at_ts"].type() != Utilities::JS::type::Int ||
      j["rpcAddress"].type() != Utilities::JS::type::Str ||
      j["rpcUserPwd"].type() != Utilities::JS::type::Str ||
      j["hHash"].type() != Utilities::JS::type::Str) {
    LOG(ERROR) << "work format not expected";
    return false;
  }

  // check timestamp
  if (j["created_at_ts"].uint32() + def()->maxJobDelay_ < time(nullptr)) {
    LOG(ERROR) << "too old bytom work: "
               << date("%F %T", j["created_at_ts"].uint32());
    return false;
  }

  return true;
}

string JobMakerHandlerBytom::makeStratumJobMsg() {
  if (0 == header_.size() || 0 == seed_.size())
    return "";

  StratumJobBytom job;
  job.jobId_ = gen_->next();
  job.nTime_ = time_;
  job.seed_ = seed_;
  job.hHash_ = header_;
  return job.serializeToJson();
}

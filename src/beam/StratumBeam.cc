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
#include "StratumBeam.h"
#include "Common.h"
#include "Utils.h"
#include "utilities_js.hpp"
#include <glog/logging.h>
#include <boost/endian/buffers.hpp>

///////////////////////////////StratumJobBeam///////////////////////////
StratumJobBeam::StratumJobBeam() {
}

bool StratumJobBeam::initFromRawJob(
    const string &rawJob, const string &rpcAddr, const string &rpcUserPwd) {
  JsonNode jnode;
  if (!JsonNode::parse(rawJob.data(), rawJob.data() + rawJob.size(), jnode)) {
    LOG(ERROR) << "decode line fail, not a json string";
    return false;
  }
  if (jnode["input"].type() != Utilities::JS::type::Str ||
      jnode["difficulty"].type() != Utilities::JS::type::Int) {
    LOG(ERROR) << "job missing fields";
    return false;
  }

  if (jnode["height"].type() == Utilities::JS::type::Int) {
    height_ = jnode["height"].int32();
  }

  input_ = jnode["input"].str();
  blockBits_ = jnode["difficulty"].uint32();
  rpcAddress_ = rpcAddr;
  rpcUserPwd_ = rpcUserPwd;

  return true;
}

string StratumJobBeam::serializeToJson() const {
  return Strings::Format(
      "{\"jobId\":%u"
      ",\"chain\":\"BEAM\""
      ",\"height\":%u"
      ",\"blockBits\":\"%08x\""
      ",\"input\":\"%s\""
      ",\"rpcAddress\":\"%s\""
      ",\"rpcUserPwd\":\"%s\""
      "}",
      jobId_,
      height_,
      blockBits_,
      input_,
      rpcAddress_,
      rpcUserPwd_);
}

bool StratumJobBeam::unserializeFromJson(const char *s, size_t len) {
  JsonNode j;
  if (!JsonNode::parse(s, s + len, j)) {
    return false;
  }

  if (j["jobId"].type() != Utilities::JS::type::Int ||
      j["chain"].type() != Utilities::JS::type::Str ||
      j["height"].type() != Utilities::JS::type::Int ||
      j["blockBits"].type() != Utilities::JS::type::Str ||
      j["input"].type() != Utilities::JS::type::Str ||
      j["rpcAddress"].type() != Utilities::JS::type::Str ||
      j["rpcUserPwd"].type() != Utilities::JS::type::Str) {
    LOG(ERROR) << "parse beam stratum job failure: " << s;
    return false;
  }

  jobId_ = j["jobId"].uint64();
  height_ = j["height"].uint64();
  blockBits_ = j["blockBits"].uint32_hex();
  input_ = j["input"].str();
  rpcAddress_ = j["rpcAddress"].str();
  rpcUserPwd_ = j["rpcUserPwd"].str();

  return true;
}

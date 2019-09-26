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

#include "StratumGrin.h"

StratumJobGrin::StratumJobGrin() {
}

bool StratumJobGrin::initFromRawJob(JsonNode &jparams) {
  if (jparams.type() != Utilities::JS::type::Obj ||
      jparams["difficulty"].type() != Utilities::JS::type::Int ||
      jparams["height"].type() != Utilities::JS::type::Int ||
      jparams["job_id"].type() != Utilities::JS::type::Int ||
      jparams["pre_pow"].type() != Utilities::JS::type::Str ||
      jparams["pre_pow"].size() != sizeof(PrePowGrin) * 2 ||
      !IsHex(jparams["pre_pow"].str())) {
    LOG(ERROR) << "invalid raw job parameters";
    return false;
  }

  difficulty_ = jparams["difficulty"].uint64();
  height_ = jparams["height"].uint64();
  nodeJobId_ = jparams["job_id"].uint64();
  prePowStr_ = jparams["pre_pow"].str();
  std::vector<char> prePowBin;
  Hex2Bin(prePowStr_.c_str(), prePowStr_.length(), prePowBin);
  memcpy(&prePow_, prePowBin.data(), prePowBin.size());

  return true;
}

string StratumJobGrin::serializeToJson() const {
  return Strings::Format(
      "{\"jobId\":%u,\"height\":%u,\"difficulty\":%u"
      ",\"nodeJobId\":%u"
      ",\"prePow\":\"%s\""
      "}",
      jobId_,
      height_,
      difficulty_,
      nodeJobId_,
      prePowStr_);
}

bool StratumJobGrin::unserializeFromJson(const char *s, size_t len) {
  JsonNode j;
  if (!JsonNode::parse(s, s + len, j)) {
    return false;
  }

  if (j["jobId"].type() != Utilities::JS::type::Int ||
      j["height"].type() != Utilities::JS::type::Int ||
      j["difficulty"].type() != Utilities::JS::type::Int ||
      j["nodeJobId"].type() != Utilities::JS::type::Int ||
      j["prePow"].type() != Utilities::JS::type::Str ||
      j["prePow"].size() != sizeof(PrePowGrin) * 2 ||
      !IsHex(j["prePow"].str())) {
    LOG(ERROR) << "parse grin stratum job failure: " << s;
    return false;
  }

  jobId_ = j["jobId"].uint64();
  height_ = j["height"].uint64();
  difficulty_ = j["difficulty"].uint64();
  nodeJobId_ = j["nodeJobId"].uint64();
  prePowStr_ = j["prePow"].str();
  std::vector<char> prePowBin;
  Hex2Bin(prePowStr_.c_str(), prePowStr_.length(), prePowBin);
  memcpy(&prePow_, prePowBin.data(), prePowBin.size());

  return true;
}

string StratumJobGrin::prePowStr(uint64_t difficulty) const {
  uint64_t shift = DiffToShift(difficulty);
  if (shift > 0) {
    // Shift the timestamp according to difficulty
    PrePowGrin prePow{prePow_};
    prePow.timestamp = prePow.timestamp.value() + shift;
    string prePowStr;
    Bin2Hex(
        reinterpret_cast<const uint8_t *>(&prePow),
        sizeof(PrePowGrin),
        prePowStr);
    return prePowStr;
  } else {
    return prePowStr_;
  }
}

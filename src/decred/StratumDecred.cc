/*
 The MIT License (MIT)

 Copyright (c) [2018] [BTC.COM]

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

#include "StratumDecred.h"
#include "CommonDecred.h"
#include "utilities_js.hpp"
#include <boost/endian/conversion.hpp>

StratumJobDecred::StratumJobDecred() {
  memset(&header_, 0, sizeof(BlockHeaderDecred));
}

string StratumJobDecred::serializeToJson() const {
  return Strings::Format(
      "{\"jobId\":%u"
      ",\"prevHash\":\"%s\""
      ",\"coinBase1\":\"%s\""
      ",\"coinBase2\":\"%s\""
      ",\"version\":\"%s\""
      ",\"target\":\"%s\""
      ",\"network\":%u}",
      jobId_,
      getPrevHash(),
      getCoinBase1(),
      HexStr(BEGIN(header_.stakeVersion), END(header_.stakeVersion)),
      HexStr(BEGIN(header_.version), END(header_.version)),
      target_.ToString(),
      static_cast<uint32_t>(network_));
}

bool StratumJobDecred::unserializeFromJson(const char *s, size_t len) {
  JsonNode j;
  if (!JsonNode::parse(s, s + len, j)) {
    return false;
  }

  if (j["jobId"].type() != Utilities::JS::type::Int ||
      j["prevHash"].type() != Utilities::JS::type::Str ||
      j["prevHash"].size() != 64 ||
      j["coinBase1"].type() != Utilities::JS::type::Str ||
      j["coinBase1"].size() != 216 ||
      j["coinBase2"].type() != Utilities::JS::type::Str ||
      j["coinBase2"].size() != 8 ||
      j["version"].type() != Utilities::JS::type::Str ||
      j["version"].size() != 8 ||
      j["target"].type() != Utilities::JS::type::Str ||
      j["target"].size() != 64 ||
      j["network"].type() != Utilities::JS::type::Int) {
    LOG(ERROR) << "parse stratum job failure: " << s;
    return false;
  }

  memset(&header_, 0, sizeof(BlockHeaderDecred));
  jobId_ = j["jobId"].uint64();
  network_ = static_cast<NetworkDecred>(j["network"].uint32());

#define UNSERIALIZE_SJOB_FIELD(n, d) \
  auto n##Str = j[#n].str(); \
  auto n##bin = ParseHex(n##Str); \
  if (n##bin.size() * 2 != n##Str.length()) { \
    return false; \
  } \
  memcpy(d, n##bin.data(), n##bin.size())

  UNSERIALIZE_SJOB_FIELD(prevHash, &header_.prevBlock);
  UNSERIALIZE_SJOB_FIELD(coinBase1, &header_.merkelRoot);
  UNSERIALIZE_SJOB_FIELD(coinBase2, &header_.stakeVersion);
  UNSERIALIZE_SJOB_FIELD(version, &header_.version);
  UNSERIALIZE_SJOB_FIELD(target, target_.begin());
#undef UNSERIALIZE_SJOB_FIELD

  return true;
}

string StratumJobDecred::getPrevHash() const {
  return HexStr(header_.prevBlock.begin(), header_.prevBlock.end());
}

string StratumJobDecred::getCoinBase1() const {
  return HexStr(header_.merkelRoot.begin(), header_.extraData.begin());
}

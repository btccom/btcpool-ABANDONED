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

#include "JobMakerDecred.h"

#include "CommonDecred.h"
#include "utilities_js.hpp"
#include "Utils.h"

#include <boost/endian/conversion.hpp>

#include <algorithm>
#include <iostream>

#define OFFSET_AND_SIZE_DECRED(f) \
  offsetof(BlockHeaderDecred, f) * 2, \
      sizeof(static_cast<BlockHeaderDecred *>(nullptr)->f) * 2

using std::ostream;
static ostream &operator<<(ostream &os, const GetWorkDecred &work) {
  os << "data = " << work.data << ", target = " << work.target
     << ", created at = " << work.createdAt << ", height = " << work.height
     << ", network = " << static_cast<uint32_t>(work.network)
     << ", voters = " << work.voters << ", size = " << work.size;
  return os;
}

////////////////////////////////JobMakerHandlerDecred//////////////////////////////////
JobMakerHandlerDecred::JobMakerHandlerDecred() {
}

bool JobMakerHandlerDecred::processMsg(const string &msg) {
  clearTimeoutWorks();

  JsonNode j;
  if (!JsonNode::parse(msg.c_str(), msg.c_str() + msg.length(), j)) {
    LOG(ERROR) << "deserialize decred work failed " << msg;
    return false;
  }

  if (!validate(j))
    return false;

  return processMsg(j);
}

string JobMakerHandlerDecred::makeStratumJobMsg() {
  if (works_.empty())
    return string{};

  auto &work = *works_.get<ByBestBlockDecred>().rbegin();
  auto &data = work.data;
  auto jobId = gen_->next();
  auto prevHash = data.substr(OFFSET_AND_SIZE_DECRED(prevBlock));
  auto merkelRootOffset = offsetof(BlockHeaderDecred, merkelRoot);
  auto coinBase1 = data.substr(
      merkelRootOffset * 2,
      (offsetof(BlockHeaderDecred, extraData) - merkelRootOffset) * 2);
  auto coinBase2 = data.substr(OFFSET_AND_SIZE_DECRED(stakeVersion));
  auto version = data.substr(OFFSET_AND_SIZE_DECRED(version));

  return Strings::Format(
      "{\"jobId\":%u"
      ",\"prevHash\":\"%s\""
      ",\"coinBase1\":\"%s\""
      ",\"coinBase2\":\"%s\""
      ",\"version\":\"%s\""
      ",\"target\":\"%s\""
      ",\"network\":%u}",
      jobId,
      prevHash,
      coinBase1,
      coinBase2,
      version,
      work.target,
      static_cast<uint32_t>(work.network));
}

bool JobMakerHandlerDecred::processMsg(JsonNode &j) {
  auto data = j["data"].str();
  auto target = j["target"].str();
  auto createdAt = j["created_at_ts"].uint32();
  auto network = static_cast<NetworkDecred>(j["network"].uint32());
  auto votersString = data.substr(OFFSET_AND_SIZE_DECRED(voters));
  auto voters = boost::endian::big_to_native(
      static_cast<uint16_t>(strtoul(votersString.c_str(), nullptr, 16)));
  auto sizeString = data.substr(OFFSET_AND_SIZE_DECRED(size));
  auto size = boost::endian::big_to_native(
      static_cast<uint32_t>(strtoul(sizeString.c_str(), nullptr, 16)));
  auto heightString = data.substr(OFFSET_AND_SIZE_DECRED(height));
  auto height = boost::endian::big_to_native(
      static_cast<uint32_t>(strtoul(heightString.c_str(), nullptr, 16)));
  if (size == 0 || height == 0) {
    LOG(ERROR) << "current work is invalid: data = " << data
               << ", target = " << target << ", created at = " << createdAt;
    return false;
  }

  // The rightmost element with the equivalent network has the highest
  // height/voters/size due to the nature of composite key
  uint32_t bestHeight = 0;
  uint16_t bestVoters = 0;
  uint32_t bestSize = 0;
  auto &works = works_.get<ByBestBlockDecred>();
  auto r = works.equal_range(network);
  if (r.first != works.end()) {
    auto &bestWork = *(--r.second);
    bestHeight = bestWork.height;
    bestVoters = bestWork.voters;
    bestSize = bestWork.size;
    auto bestTime = bestWork.createdAt;

    // To prevent the job's block height ups and downs
    // when the block height of two bitcoind is not synchronized.
    // The block height downs must past twice the time of stratumJobInterval_
    // without the higher height GW received.
    if (height < bestHeight && createdAt > bestTime &&
        createdAt - bestWork.createdAt < 2 * def()->jobInterval_) {
      LOG(WARNING) << "skip low height work: data = " << data
                   << ", target = " << target << ", created at = " << createdAt
                   << ", best height = " << bestHeight
                   << ", best time = " << bestTime;
      return false;
    }
  }

  auto p =
      works_.emplace(data, target, createdAt, height, network, size, voters);
  auto &work = *p.first;
  if (!p.second) {
    LOG(ERROR) << "current work is duplicated with a previous work: " << work
               << ", current created at = " << createdAt;
    return false;
  }

  if (height < bestHeight) {
    LOG(WARNING) << "current work has a lower height: " << work
                 << ", best height = " << bestHeight;
    return false;
  }

  if (height == bestHeight) {
    if (voters > bestVoters || (voters == bestVoters && size > bestSize)) {
      LOG(INFO) << "current work is triggering a new job: " << work;
      return true;
    }
    return false;
  }

  LOG(INFO) << "current work is triggering a new job: " << work;
  return true;
}

bool JobMakerHandlerDecred::validate(JsonNode &j) {
  // check fields are valid
  if (j.type() != Utilities::JS::type::Obj ||
      j["created_at_ts"].type() != Utilities::JS::type::Int ||
      j["rpcAddress"].type() != Utilities::JS::type::Str ||
      j["rpcUserPwd"].type() != Utilities::JS::type::Str ||
      j["data"].type() != Utilities::JS::type::Str || j["data"].size() != 384 ||
      !IsHex(j["data"].str()) ||
      j["target"].type() != Utilities::JS::type::Str ||
      j["target"].size() != 64 || !IsHex(j["target"].str()) ||
      j["network"].type() != Utilities::JS::type::Int) {
    LOG(ERROR) << "work format not expected";
    return false;
  }

  // check timestamp
  if (j["created_at_ts"].uint32() + def()->maxJobDelay_ < time(nullptr)) {
    LOG(ERROR) << "too old decred work: "
               << date("%F %T", j["created_at_ts"].uint32());
    return false;
  }

  return true;
}

void JobMakerHandlerDecred::clearTimeoutWorks() {
  // Ensure that we has at least one work, even if it expires.
  // So jobmaker can always generate jobs even if blockchain node does not
  // update the response of getwork for a long time when there is no new
  // transaction.
  if (works_.size() <= 1)
    return;

  auto tsNow = static_cast<uint32_t>(time(nullptr));
  auto &works = works_.get<ByCreationTimeDecred>();
  auto iter = works.begin();
  auto iend = works.end();
  while (iter != iend) {
    if (iter->createdAt + def()->workLifeTime_ < tsNow) {
      DLOG(INFO) << "remove an expired work: " << *iter;
      works.erase(iter++);
    } else {
      // There is no need to continue as this index is ordered by creation time
      return;
    }
  }
}

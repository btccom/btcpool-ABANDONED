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
#include "JobMakerEth.h"

#include "StratumEth.h"

#include "Utils.h"

////////////////////////////////JobMakerHandlerEth//////////////////////////////////
bool JobMakerHandlerEth::processMsg(const string &msg) {
  shared_ptr<RskWorkEth> work = make_shared<RskWorkEth>();
  if (!work->initFromGw(msg)) {
    LOG(ERROR) << "eth initFromGw failed " << msg;
    return false;
  }

  const uint64_t key = makeWorkKey(*work);
  if (workMap_.find(key) != workMap_.end()) {
    DLOG(INFO) << "key already exist in workMap: " << key;
  }

  workMap_.insert(std::make_pair(key, work));
  LOG(INFO) << "add work, height: " << work->getHeight()
            << ", header: " << work->getBlockHash()
            << ", from: " << work->getRpcAddress();

  clearTimeoutMsg();

  if (work->getHeight() < lastReceivedHeight_) {
    LOG(WARNING) << "low height work. lastHeight:" << lastReceivedHeight_
                 << ", workHeight: " << work->getHeight();
    return false;
  }

  if (work->getHeight() == lastReceivedHeight_) {
    if (!workOfLastJob_) {
      LOG(WARNING) << "work of last job is empty! lastHeight: "
                   << lastReceivedHeight_;
      return true;
    }

    // job update triggered by more uncles
    if (workOfLastJob_->getUncles() < work->getUncles()) {
      return true;
    }

    // job update triggered by more gas used
    if (workOfLastJob_->getGasUsedPercent() < 10.0 &&
        workOfLastJob_->getGasUsedPercent() < work->getGasUsedPercent()) {
      return true;
    }

    return false;
  }

  // job update triggered by the block height increases.
  lastReceivedHeight_ = work->getHeight();
  return true;
}

void JobMakerHandlerEth::clearTimeoutMsg() {
  // Maps (and sets) are sorted, so the first element is the smallest,
  // and the last element is the largest.

  const uint32_t ts_now = time(nullptr);

  // Ensure that workMap_ has at least one element, even if it expires.
  // So jobmaker can always generate jobs even if blockchain node does not
  // update the response of getwork for a long time when there is no new
  // transaction.
  for (auto itr = workMap_.begin();
       workMap_.size() > 1 && itr != workMap_.end();) {
    const uint32_t ts = itr->second->getCreatedAt();
    const uint32_t height = itr->second->getHeight();

    // gbt expired time
    const uint32_t expiredTime = ts + def()->workLifeTime_;

    if (expiredTime > ts_now) {
      // not expired
      ++itr;
    } else {
      // remove expired gbt
      LOG(INFO) << "remove timeout work: " << date("%F %T", ts) << "|" << ts
                << ", height:" << height
                << ", headerHash:" << itr->second->getBlockHash();

      // c++11: returns an iterator to the next element in the map
      itr = workMap_.erase(itr);
    }
  }
}

string JobMakerHandlerEth::makeStratumJobMsg() {
  if (workMap_.empty()) {
    return "";
  }

  shared_ptr<RskWorkEth> work = workMap_.rbegin()->second;
  StratumJobEth sjob;

  if (!sjob.initFromGw(*work, def()->chain_, def()->serverId_)) {
    LOG(ERROR) << "init stratum job from work fail";
    return "";
  }

  sjob.jobId_ = gen_->next();
  workOfLastJob_ = work;
  return sjob.serializeToJson();
}

uint64_t JobMakerHandlerEth::makeWorkKey(const RskWorkEth &work) {
  const string &blockHash = work.getBlockHash();
  uint64_t blockHashSuffix =
      strtoull(blockHash.substr(blockHash.size() - 4).c_str(), nullptr, 16);

  // key = | 32bits height | 8bits uncles | 8bits gasUsedPercent | 16bit
  // hashSuffix |
  uint64_t key = ((uint64_t)work.getHeight()) << 32;
  key += (((uint64_t)work.getUncles()) & 0xFFu)
      << 24; // No overflow, the largest number of uncles in Ethereum is 2.
  key += (((uint64_t)work.getGasUsedPercent()) & 0xFFu)
      << 16; // No overflow, the largest number should be 100 (100%).
  key += blockHashSuffix;

  return key;
}

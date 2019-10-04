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
#include "JobMakerCkb.h"
#include "StratumCkb.h"
#include "CommonCkb.h"
#include "Utils.h"
#include "utilities_js.hpp"

bool JobMakerHandlerCkb::processMsg(const string &msg) {

  shared_ptr<StratumJobCkb> job = make_shared<StratumJobCkb>();

  if (!job->initFromRawJob(msg)) {
    LOG(ERROR) << "eth initFromGw failed " << msg;
    return false;
  }
  job->jobId_ = gen_->next();

  const uint64_t key = makeWorkKey(*job);
  if (workMap_.find(key) != workMap_.end()) {
    DLOG(INFO) << "key already exist in workMap: " << key;
    return false;
  }

  std::pair<std::map<uint64_t, shared_ptr<StratumJobCkb>>::iterator, bool> ret;
  ret = workMap_.insert(std::make_pair(key, job));
  if (!ret.second) {
    DLOG(INFO) << "insert key into workMap failed: " << key;
    return false;
  }
  jobid2work_.insert(std::make_pair(job->jobId_, job));
  clearTimeoutMsg();

  if (job->height_ < lastReceivedHeight_) {
    LOG(WARNING) << "low height work. lastHeight:" << lastReceivedHeight_
                 << ", workHeight: " << job->height_;
    return false;
  }

  lastReceivedHeight_ = job->height_;
  return true;
}

void JobMakerHandlerCkb::clearTimeoutMsg() {
  // Maps (and sets) are sorted, so the first element is the smallest,
  // and the last element is the largest.

  const uint32_t ts_now = time(nullptr);

  // Ensure that workMap_ has at least one element, even if it expires.
  // So jobmaker can always generate jobs even if blockchain node does not
  // update the response of getwork for a long time when there is no new
  // transaction.
  for (auto itr = workMap_.begin();
       workMap_.size() > 1 && itr != workMap_.end();) {
    const uint32_t ts = itr->second->nTime_;
    const uint32_t height = itr->second->height_;

    // gbt expired time
    const uint32_t expiredTime = ts + def()->workLifeTime_;

    if (expiredTime > ts_now) {
      // not expired
      ++itr;
    } else {
      // remove expired gbt
      LOG(INFO) << "remove timeout work: " << date("%F %T", ts) << "|" << ts
                << ", height:" << height
                << ", headerHash:" << itr->second->pow_hash_;

      jobid2work_.erase(jobid2work_.find(itr->second->jobId_));
      // c++11: returns an iterator to the next element in the map
      itr = workMap_.erase(itr);
    }
  }
}

string JobMakerHandlerCkb::makeStratumJobMsg() {
  if (workMap_.empty()) {
    return "";
  }

  shared_ptr<StratumJobCkb> sjob = jobid2work_.rbegin()->second;
  DLOG(INFO) << "send job : " << sjob->jobId_
             << "job pow_hash :  " << sjob->pow_hash_;
  DLOG(INFO) << "sjob :" << sjob->serializeToJson();
  return sjob->serializeToJson();
}

uint64_t JobMakerHandlerCkb::makeWorkKey(const StratumJobCkb &work) {

  // string blockHash = DecodeHashStrFromBase58(work.hash_);
  DLOG(INFO) << "work.hash_ : " << work.pow_hash_;
  string blockHash = work.pow_hash_;

  uint64_t blockHashSuffix =
      strtoull(blockHash.substr(blockHash.size() - 8).c_str(), nullptr, 16);

  // key = | 32bits height |  32bit hashSuffix |
  uint64_t key = ((uint64_t)work.height_ << 32);
  key += blockHashSuffix;

  return key;
}
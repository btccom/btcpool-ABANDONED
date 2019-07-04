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
#include "StratumMiner.h"

#include "StratumSession.h"
#include "StratumServer.h"
#include "DiffController.h"
#include "StratumMessageDispatcher.h"

#include <boost/algorithm/string.hpp>

#include <event2/buffer.h>

StratumMiner::StratumMiner(
    IStratumSession &session,
    const DiffController &diffController,
    const string &clientAgent,
    const string &workerName,
    int64_t workerId)
  : session_(session)
  , diffController_(new DiffController(diffController))
  , curDiff_(0)
  , clientAgent_(clientAgent)
  , isNiceHashClient_(isNiceHashAgent(clientAgent))
  , workerName_(workerName)
  , workerId_(workerId)
  , invalidSharesCounter_(INVALID_SHARE_SLIDING_WINDOWS_SIZE) {
}

void StratumMiner::setMinDiff(uint64_t minDiff) {
  diffController_->setMinDiff(minDiff);
}

void StratumMiner::resetCurDiff(uint64_t curDiff) {
  diffController_->resetCurDiff(curDiff);
}

uint64_t StratumMiner::calcCurDiff() {
  curDiff_ = diffController_->calcCurDiff();
  return curDiff_;
}

bool StratumMiner::handleShare(
    const std::string &idStr,
    int32_t status,
    uint64_t shareDiff,
    size_t chainId) {
  session_.reportShare(chainId, status, shareDiff);
  auto &dispatcher = session_.getDispatcher();
  bool accepted = StratumStatus::isAccepted(status);
  diffController_->addShare(shareDiff);
  if (accepted && (session_.acceptStale() || !StratumStatus::isStale(status))) {
    if (StratumStatus::isStale(status)) {
      dispatcher.responseShareAcceptedWithStatus(idStr, status);
    } else {
      dispatcher.responseShareAccepted(idStr);
    }
  } else {
    dispatcher.responseShareError(idStr, status);
  }
  return accepted;
}

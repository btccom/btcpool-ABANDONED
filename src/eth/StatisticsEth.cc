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
#include "StatisticsEth.h"

template <typename ShareEthType>
static void processShareEth(
    ShareStatsDay<ShareEthType> &statsDay,
    uint32_t hourIdx,
    const ShareEthType &share) {
  ScopeLock sl(statsDay.lock_);

  if (StratumStatus::isAccepted(share.status())) {
    statsDay.shareAccept1h_[hourIdx] += share.sharediff();
    statsDay.shareAccept1d_ += share.sharediff();

    double score = share.score();
    double reward =
        EthConsensus::getStaticBlockReward(share.height(), share.getChain());
    double earn = score * reward;

    statsDay.score1h_[hourIdx] += score;
    statsDay.score1d_ += score;
    statsDay.earn1h_[hourIdx] += earn;
    statsDay.earn1d_ += earn;
  } else {
    statsDay.shareReject1h_[hourIdx] += share.sharediff();
    statsDay.shareReject1d_ += share.sharediff();
  }
  statsDay.modifyHoursFlag_ |= (0x01u << hourIdx);
}

template <>
void ShareStatsDay<ShareNoStaleEth>::processShare(
    uint32_t hourIdx, const ShareNoStaleEth &share) {
  processShareEth(*this, hourIdx, share);
}

template <>
void ShareStatsDay<ShareWithStaleEth>::processShare(
    uint32_t hourIdx, const ShareWithStaleEth &share) {
  processShareEth(*this, hourIdx, share);
}

template class ShareStatsDay<ShareNoStaleEth>;
template class ShareStatsDay<ShareWithStaleEth>;

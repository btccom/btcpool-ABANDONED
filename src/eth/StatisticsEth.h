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
#ifndef STATISTICS_ETH_H_
#define STATISTICS_ETH_H_

#include "Statistics.h"

#include "CommonEth.h"
#include "StratumEth.h"

///////////////////////////////  GlobalShareEth ////////////////////////////////
// Used to detect duplicate share attacks on ETH mining.
struct GlobalShareEth {
  uint64_t headerHash_;
  uint64_t nonce_;

  GlobalShareEth() = default;

  GlobalShareEth(const ShareEth &share)
    : headerHash_(share.headerhash())
    , nonce_(share.nonce()) {}

  GlobalShareEth &operator=(const GlobalShareEth &r) = default;

  bool operator<(const GlobalShareEth &r) const {
    if (headerHash_ < r.headerHash_ ||
        (headerHash_ == r.headerHash_ && nonce_ < r.nonce_)) {
      return true;
    }
    return false;
  }
};
////////////////////////////  Alias  ////////////////////////////
using DuplicateShareCheckerEth =
    DuplicateShareCheckerT<ShareEth, GlobalShareEth>;

#endif // STATISTICS_ETH_H_

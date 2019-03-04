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
#pragma once

#include "Statistics.h"

#include "CommonBeam.h"
#include "StratumBeam.h"

///////////////////////////////  GlobalShareBeam
///////////////////////////////////
// Used to detect duplicate share attacks on ETH mining.
struct GlobalShareBeam {
  uint64_t inputPrefix_;
  uint64_t nonce_;
  uint32_t outputHash_;

  GlobalShareBeam() = default;

  GlobalShareBeam(const ShareBeam &share)
    : inputPrefix_(share.inputprefix())
    , nonce_(share.nonce())
    , outputHash_(share.outputhash()) {}

  GlobalShareBeam &operator=(const GlobalShareBeam &r) = default;

  bool operator<(const GlobalShareBeam &r) const {
    if (inputPrefix_ < r.inputPrefix_ ||
        (inputPrefix_ == r.inputPrefix_ && nonce_ < r.nonce_) ||
        (inputPrefix_ == r.inputPrefix_ && nonce_ == r.nonce_ &&
         outputHash_ < r.outputHash_)) {
      return true;
    }
    return false;
  }
};

////////////////////////////  Alias  ////////////////////////////
using DuplicateShareCheckerBeam =
    DuplicateShareCheckerT<ShareBeam, GlobalShareBeam>;

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

#include "StratumGrin.h"

#include "ShareLogParser.h"

///////////////////////////////  Alias  ///////////////////////////////
using ShareLogDumperGrin = ShareLogDumperT<ShareGrin>;

class ShareLogParserGrin : public ShareLogParserT<ShareGrin> {
public:
  ShareLogParserGrin(
      const libconfig::Config &config,
      time_t timestamp,
      shared_ptr<DuplicateShareChecker<ShareGrin>> dupShareChecker);

private:
  bool filterShare(const ShareGrin &share) override;

  AlgorithmGrin algorithm_;
};

class ShareLogParserServerGrin : public ShareLogParserServerT<ShareGrin> {
public:
  ShareLogParserServerGrin(
      const libconfig::Config &config,
      shared_ptr<DuplicateShareChecker<ShareGrin>> dupShareChecker);

private:
  shared_ptr<ShareLogParserT<ShareGrin>>
  createShareLogParser(time_t datets) override;

  const libconfig::Config &config_;
};

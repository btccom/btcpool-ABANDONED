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
#ifndef JOB_MAKER_ETH_H_
#define JOB_MAKER_ETH_H_

#include "JobMaker.h"
#include "EthConsensus.h"
#include "rsk/RskWork.h"

struct JobMakerDefinitionEth : public GwJobMakerDefinition {
  virtual ~JobMakerDefinitionEth() {}

  EthConsensus::Chain chain_;
};

class JobMakerHandlerEth : public GwJobMakerHandler {
public:
  virtual ~JobMakerHandlerEth() {}
  bool processMsg(const string &msg) override;
  string makeStratumJobMsg() override;

  // read-only definition
  inline shared_ptr<const JobMakerDefinitionEth> def() {
    return std::dynamic_pointer_cast<const JobMakerDefinitionEth>(def_);
  }

private:
  void clearTimeoutMsg();
  inline uint64_t makeWorkKey(const RskWorkEth &work);

  std::map<uint64_t /* @see makeWorkKey() */, shared_ptr<RskWorkEth>>
      workMap_; // sorting works by height + uncles + gasUsedPercent + hash
  shared_ptr<RskWorkEth> workOfLastJob_; // for quickly updating jobs that with
                                         // low gas used and low uncles
  uint32_t lastReceivedHeight_ = 0; // used for rejecting low height works
};

#endif

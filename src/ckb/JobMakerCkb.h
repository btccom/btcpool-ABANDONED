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
#ifndef JOB_MAKER_CKB_H_
#define JOB_MAKER_CKB_H_

#include "JobMaker.h"
#include "StratumCkb.h"
#include "utilities_js.hpp"

class JobMakerHandlerCkb : public GwJobMakerHandler {
public:
  virtual ~JobMakerHandlerCkb() {}
  bool processMsg(const string &msg) override;
  string makeStratumJobMsg() override;

private:
  void clearTimeoutMsg();
  inline uint64_t makeWorkKey(const StratumJobCkb &job);

  std::map<uint64_t, shared_ptr<StratumJobCkb>> workMap_;
  std::map<uint64_t, shared_ptr<StratumJobCkb>> jobid2work_;
  uint32_t lastReceivedHeight_ = 0;
};

#endif

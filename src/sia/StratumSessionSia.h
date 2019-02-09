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

#ifndef STRATUM_SESSION_SIA_H_
#define STRATUM_SESSION_SIA_H_

#include "StratumSession.h"
#include "StratumServerSia.h"

class StratumSessionSia : public StratumSessionBase<StratumTraitsSia> {
public:
  StratumSessionSia(
      ServerSia &server,
      struct bufferevent *bev,
      struct sockaddr *saddr,
      uint32_t extraNonce1);
  void sendSetDifficulty(LocalJob &localJob, uint64_t difficulty) override;
  void
  sendMiningNotify(shared_ptr<StratumJobEx> exJobPtr, bool isFirstJob) override;

protected:
  void handleRequest(
      const std::string &idStr,
      const std::string &method,
      const JsonNode &jparams,
      const JsonNode &jroot) override;
  void handleRequest_Subscribe(
      const std::string &idStr, const JsonNode &jparams, const JsonNode &jroot);

public:
  std::unique_ptr<StratumMiner> createMiner(
      const std::string &clientAgent,
      const std::string &workerName,
      int64_t workerId) override;

private:
  uint8_t shortJobId_; // Claymore jobId starts from 0
};

#endif // #ifndef STRATUM_SESSION_SIA_H_

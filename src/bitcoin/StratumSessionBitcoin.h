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

#ifndef STRATUM_SESSION_BITCOIN_H_
#define STRATUM_SESSION_BITCOIN_H_

#include "StratumSession.h"
#include "StratumServerBitcoin.h"

class StratumSessionBitcoin : public StratumSessionBase<StratumTraitsBitcoin> {
public:
  StratumSessionBitcoin(ServerBitcoin &server,
                        struct bufferevent *bev,
                        struct sockaddr *saddr,
                        uint32_t extraNonce1);
  uint16_t decodeSessionId(const std::string &exMessage) const override;
  void sendMiningNotify(shared_ptr<StratumJobEx> exJobPtr, bool isFirstJob) override;

protected:
  bool isSubscribe(const std::string &method) const override { return method == "mining.subscribe"; }
  bool isAuthorize(const std::string &method) const override { return method == "mining.authorize"; }
  void handleRequest_Subscribe(const std::string &idStr,
                               const JsonNode &jparams,
                               const JsonNode &jroot) override;
  bool handleRequest_Authorize(const std::string &idStr,
                               const JsonNode &jparams,
                               const JsonNode &jroot,
                               std::string &fullName,
                               std::string &password) override;
  std::unique_ptr<StratumMessageDispatcher> createDispatcher() override;
public:
  std::unique_ptr<StratumMiner> createMiner(const std::string &clientAgent,
                                            const std::string &workerName,
                                            int64_t workerId) override;

private:
  uint8_t allocShortJobId();

  uint8_t shortJobIdIdx_;
};

#endif  // #ifndef STRATUM_SESSION_BITCOIN_H_

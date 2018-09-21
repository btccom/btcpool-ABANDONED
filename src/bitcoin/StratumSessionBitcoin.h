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

#include "StratumServerBitcoin.h"
#include "StratumSession.h"

class AgentSessions;

class StratumSessionBitcoin : public StratumSessionBase<ServerBitcoin>
{
private:
  AgentSessions *agentSessions_;

public:
  StratumSessionBitcoin(evutil_socket_t fd, struct bufferevent *bev,
                 ServerBitcoin *server, struct sockaddr *saddr,
                 const int32_t shareAvgSeconds, const uint32_t extraNonce1);
  ~StratumSessionBitcoin();
protected:
  void sendMiningNotify(shared_ptr<StratumJobEx> exJobPtr, bool isFirstJob=false) override;  
  void handleRequest_Authorize(const string &idStr, const JsonNode &jparams, const JsonNode &jroot) override;
  void handleRequest_Subscribe(const string &idStr, const JsonNode &jparams) override;
  void handleRequest_Submit(const string &idStr, const JsonNode &jparams) override;

  void handleExMessage_RegisterWorker     (const string *exMessage) override;
  void handleExMessage_UnRegisterWorker   (const string *exMessage) override;
  void handleExMessage_SubmitShare        (const string *exMessage) override;
  void handleExMessage_SubmitShareWithTime(const string *exMessage) override;

  bool handleRequest_Specific(const string &idStr, const string &method
                              , const JsonNode &jparams, const JsonNode &jroot) override;
  void handleRequest_SuggestTarget    (const string &idStr, const JsonNode &jparams);

public:
  void handleRequest_Submit(const string &idStr,
                            const uint8_t shortJobId, const uint64_t extraNonce2,
                            const uint32_t nonce, uint32_t nTime,
                            bool isAgentSession,
                            DiffController *sessionDiffController);
  void handleExMessage_AuthorizeAgentWorker(const int64_t workerId,
                                            const string &clientAgent,
                                            const string &workerName);

};

///////////////////////////////// AgentSessions ////////////////////////////////
class AgentSessions {
  //
  // sessionId is vector's index
  //
  // session ID range: [0, 65535], so vector max size is 65536
  vector<int64_t> workerIds_;
  vector<DiffController *> diffControllers_;
  vector<uint8_t> curDiff2ExpVec_;
  int32_t shareAvgSeconds_;
  uint8_t kDefaultDiff2Exp_;

  StratumSessionBitcoin *stratumSession_;

public:
  AgentSessions(const int32_t shareAvgSeconds, StratumSessionBitcoin *stratumSession);
  ~AgentSessions();

  int64_t getWorkerId(const uint16_t sessionId);

  void handleExMessage_SubmitShare     (const string *exMessage, const bool isWithTime);
  void handleExMessage_RegisterWorker  (const string *exMessage);
  void handleExMessage_UnRegisterWorker(const string *exMessage);

  void calcSessionsJobDiff(vector<uint8_t> &sessionsDiff2Exp);
  void getSessionsChangedDiff(const vector<uint8_t> &sessionsDiff2Exp,
                              string &data);
  void getSetDiffCommand(map<uint8_t, vector<uint16_t> > &diffSessionIds,
                         string &data);
};

#endif

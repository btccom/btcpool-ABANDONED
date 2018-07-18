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
#ifndef STRATUM_SESSION_BYTOM_H_
#define STRATUM_SESSION_BYTOM_H_

#include "sserver/common/StratumSession.h"

class StratumSessionBytom : public StratumSession
{
public:
  StratumSessionBytom(evutil_socket_t fd, struct bufferevent *bev,
                    Server *server, struct sockaddr *saddr,
                    const int32_t shareAvgSeconds, const uint32_t extraNonce1);
  void handleRequest_Authorize(const string &idStr, const JsonNode &jparams, const JsonNode &jroot) override;
  void sendMiningNotify(shared_ptr<StratumJobEx> exJobPtr, bool isFirstJob=false) override;  
  void handleRequest_GetWork(const string &idStr, const JsonNode &jparams) override; 
  void handleRequest_Submit   (const string &idStr, const JsonNode &jparams) override;   
  bool validate(const JsonNode &jmethod, const JsonNode &jparams) override;
  bool needToSendLoginResponse() const override {return false;}
  
private:
  void Bytom_rpc2ResponseBoolean(const string &idStr, bool result, const string& failMessage = "");

  uint8 shortJobId_;    //jobId starts from 1
};

#endif  // STRATUM_SESSION_BYTOM_H_

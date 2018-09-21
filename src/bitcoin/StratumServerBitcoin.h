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
#ifndef STRATUM_SERVER_BITCOIN_H_
#define STRATUM_SERVER_BITCOIN_H_

#include "StratumServer.h"
#include "StratumBitcoin.h"

class CBlockHeader;
class JobRepositoryBitcoin;

class ServerBitcoin : public ServerBase<JobRepositoryBitcoin>
{
private:
  string auxPowSolvedShareTopic_;
  string rskSolvedShareTopic_;
  KafkaProducer *kafkaProducerNamecoinSolvedShare_;
  KafkaProducer *kafkaProducerRskSolvedShare_;

public:
  ServerBitcoin(const int32_t shareAvgSeconds, const libconfig::Config &config);
  virtual ~ServerBitcoin();

  bool setupInternal(StratumServer* sserver) override;

  unique_ptr<StratumSession> createConnection(struct bufferevent *bev, struct sockaddr *saddr, uint32_t sessionID) override;
  void sendSolvedShare2Kafka(const FoundBlock *foundBlock,
                             const std::vector<char> &coinbaseBin);

  int checkShare(const ShareBitcoin &share,
                 const uint32 extraNonce1, const string &extraNonce2Hex,
                 const uint32_t nTime, const uint32_t nonce,
                 const uint256 &jobTarget, const string &workFullName,
                 string *userCoinbaseInfo = nullptr);
private:
  JobRepository* createJobRepository(const char *kafkaBrokers,
                                    const char *consumerTopic,
                                     const string &fileLastNotifyTime) override;

};

class JobRepositoryBitcoin : public JobRepositoryBase<ServerBitcoin>
{
private:
  uint256 latestPrevBlockHash_;
public:
  JobRepositoryBitcoin(const char *kafkaBrokers, const char *consumerTopic, const string &fileLastNotifyTime, ServerBitcoin *server);
  virtual ~JobRepositoryBitcoin();
  virtual StratumJob* createStratumJob() override {return new StratumJobBitcoin();}
  StratumJobEx* createStratumJobEx(StratumJob *sjob, bool isClean) override;
  void broadcastStratumJob(StratumJob *sjob) override;

};

class StratumJobExBitcoin : public StratumJobEx
{
  void generateCoinbaseTx(std::vector<char> *coinbaseBin,
                          const uint32_t extraNonce1,
                          const string &extraNonce2Hex,
                          string *userCoinbaseInfo = nullptr);

public:

  string miningNotify1_;
  string miningNotify2_;
  string coinbase1_;
  string miningNotify3_;
  string miningNotify3Clean_;

public:
  StratumJobExBitcoin(StratumJob *sjob, bool isClean);

  void generateBlockHeader(CBlockHeader  *header,
                           std::vector<char> *coinbaseBin,
                           const uint32_t extraNonce1,
                           const string &extraNonce2Hex,
                           const vector<uint256> &merkleBranch,
                           const uint256 &hashPrevBlock,
                           const uint32_t nBits, const int32_t nVersion,
                           const uint32_t nTime, const uint32_t nonce,
                           string *userCoinbaseInfo = nullptr);
  void init();

};

#endif

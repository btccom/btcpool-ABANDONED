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
#include "StratumMiner.h"
#include <uint256.h>

class CBlockHeader;
class FoundBlock;
class JobRepositoryBitcoin;
class ShareBitcoin;
class StratumMinerBitcoin;
class StratumSessionBitcoin;

class ServerBitcoin : public ServerBase<JobRepositoryBitcoin> {
protected:
  struct ChainVarsBitcoin {
    KafkaProducer *kafkaProducerAuxSolvedShare_;
    KafkaProducer *kafkaProducerRskSolvedShare_;
  };

  vector<ChainVarsBitcoin> chainsBitcoin_;
  uint32_t versionMask_ = 0;
  uint32_t extraNonce2Size_ = StratumMiner::kExtraNonce2Size_;
  bool useShareV1_ = false;

  bool subPoolEnabled_ = false;
  string subPoolName_;
  int32_t subPoolExtUserId_;

  bool grandPoolEnabled_ = false;

public:
  ServerBitcoin() = default;
  virtual ~ServerBitcoin();

  inline uint32_t getVersionMask() const { return versionMask_; }
  inline uint32_t extraNonce2Size() const { return extraNonce2Size_; }
  inline bool useShareV1() const { return useShareV1_; }
  inline bool subPoolEnabled() const { return subPoolEnabled_; }
  inline string subPoolName() const { return subPoolName_; }
  inline int32_t subPoolExtUserId() const { return subPoolExtUserId_; }

  bool setupInternal(const libconfig::Config &config) override;

  unique_ptr<StratumSession> createConnection(
      struct bufferevent *bev,
      struct sockaddr *saddr,
      uint32_t sessionID) override;
  void sendSolvedShare2Kafka(
      size_t chainId,
      const FoundBlock *foundBlock,
      const std::vector<char> &coinbaseBin);

  void checkShare(
      size_t chainId,
      const ShareBitcoin &share,
      uint32_t extraNonce1,
      const string &extraNonce2Hex,
      const uint32_t nTime,
      const BitcoinNonceType nonce,
      const uint32_t versionMask,
      const uint256 &jobTarget,
      const string &workFullName,
      const bool isGrandPoolClient,
      const uint32_t extraGrandNonce1,
      std::function<void(int32_t status, uint32_t bitsReached)> returnFn);

protected:
  JobRepository *createJobRepository(
      size_t chainId,
      const char *kafkaBrokers,
      const char *consumerTopic,
      const string &fileLastNotifyTime,
      bool niceHashForced,
      uint64_t niceHashMinDiff,
      const std::string &niceHashMinDiffZookeeperPath) override;

  void sendAuxSolvedShare2Kafka(size_t chainId, const char *data, size_t len);
  void sendRskSolvedShare2Kafka(size_t chainId, const char *data, size_t len);
};

class JobRepositoryBitcoin : public JobRepositoryBase<ServerBitcoin> {
protected:
  uint32_t lastHeight_ = 0;

public:
  using JobRepositoryBase::JobRepositoryBase;
  virtual shared_ptr<StratumJob> createStratumJob() override;
  shared_ptr<StratumJobEx>
  createStratumJobEx(shared_ptr<StratumJob> sjob, bool isClean) override;
  void broadcastStratumJob(shared_ptr<StratumJob> sjob) override;
};

class StratumJobExBitcoin : public StratumJobEx {
  void generateCoinbaseTx(
      std::vector<char> *coinbaseBin,
      const uint32_t extraNonce1,
      const string &extraNonce2Hex,
      const bool isGrandPoolClient,
      const uint32_t extraGrandNonce1);

public:
  string miningNotify1_;
  string miningNotify2_;
  string coinbase1_;
  string grandCoinbase1_;
  string miningNotify3_;
  string miningNotify3Clean_;

public:
  StratumJobExBitcoin(
      size_t chainId,
      shared_ptr<StratumJob> sjob,
      bool isClean,
      uint32_t extraNonce2Size);

  void generateBlockHeader(
      CBlockHeader *header,
      std::vector<char> *coinbaseBin,
      const uint32_t extraNonce1,
      const string &extraNonce2Hex,
      const vector<uint256> &merkleBranch,
      const uint256 &hashPrevBlock,
      const uint32_t nBits,
      const int32_t nVersion,
      const uint32_t nTime,
      const BitcoinNonceType nonce,
      const uint32_t versionMask,
      const bool isGrandPoolClient,
      const uint32_t extraGrandNonce1);
  void init(uint32_t extraNonce2Size);
};

#endif

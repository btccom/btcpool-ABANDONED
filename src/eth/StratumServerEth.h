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
#ifndef STRATUM_SERVER_ETH_H_
#define STRATUM_SERVER_ETH_H_

#include "CommonEth.h"

#include <set>
#include <queue>
#include "StratumServer.h"
#include "StratumEth.h"
#include "Utils.h"

class JobRepositoryEth;

class ServerEth : public ServerBase<JobRepositoryEth> {
public:
  bool setupInternal(const libconfig::Config &config) override;
  void checkShareAndUpdateDiff(
      size_t chainId,
      const ShareEth &share,
      const uint64_t jobId,
      const uint64_t nonce,
      const uint256 &header,
      const boost::optional<uint256> &mixHash,
      const boost::optional<uint32_t> &extraNonce2,
      const std::set<uint64_t> &jobDiffs,
      const string &workFullName,
      std::function<void(int32_t status, uint64_t diff, uint32_t bitsReached)>
          returnFn);
  void sendSolvedShare2Kafka(
      size_t chainId,
      uint64_t nonce,
      const string &headerHash,
      const uint256 &mixHash,
      const uint32_t height,
      const uint64_t networkDiff,
      int32_t userId,
      int64_t workerHashId,
      const string &workerFullName,
      const EthConsensus::Chain chain,
      const string &extraNonce);

  JobRepository *createJobRepository(
      size_t chainId,
      const char *kafkaBrokers,
      const char *consumerTopic,
      const string &fileLastNotifyTime,
      bool niceHashForced,
      uint64_t niceHashMinDiff,
      const std::string &niceHashMinDiffZookeeperPath) override;

  unique_ptr<StratumSession> createConnection(
      struct bufferevent *bev,
      struct sockaddr *saddr,
      const uint32_t sessionID) override;
};

class EthashCalculator {
protected:
  const size_t kMaxCacheSize_ = 3;

  std::mutex lock_;
  SeqMap<uint64_t /*epoch*/, ethash_light_t> lightCaches_;
  std::set<uint64_t /*epoch*/> buildingLightCaches_;
  string cacheFile_;

  // save ethash_light_t to file
  struct LightCacheHeader {
    uint64_t checksum_;
    uint64_t blockNumber_;
    uint64_t cacheSize_;
  };

  // Creating a new ethash_light_t (DAG cache) is so slow (in Debug build),
  // it may need more than 120 seconds for current Ethereum mainnet.
  // So save it to a file before exiting and load it back at next time
  // to reduce the computation time required after a restart.
  //
  // Note: The performance of ethash_light_new() difference between Debug and
  // Release builds is very large. The Release build may complete in 5 seconds,
  // while the Debug build takes more than 60 seconds.
  size_t /*saved num*/ saveCacheToFile(const string &cacheFile);
  void saveCacheToFile(const ethash_light_t &light, std::ofstream &f);
  size_t /*loaded num*/ loadCacheFromFile(const string &cacheFile);
  ethash_light_t loadCacheFromFile(std::ifstream &f);
  uint64_t
  computeCacheChecksum(const LightCacheHeader &header, const uint8_t *data);

  void buildDagCacheWithoutLock(uint64_t height);
  ethash_light_t getDagCacheWithoutLock(uint64_t height);

public:
  EthashCalculator() {}
  EthashCalculator(const string &cacheFile);
  ~EthashCalculator();

  void buildDagCache(uint64_t height);
  void rebuildDagCache(uint64_t height);
  bool compute(
      uint64_t height,
      const ethash_h256_t &header,
      uint64_t nonce,
      ethash_return_value_t &r);
};

class JobRepositoryEth : public JobRepositoryBase<ServerEth> {
public:
  JobRepositoryEth(
      size_t chainId,
      ServerEth *server,
      const char *kafkaBrokers,
      const char *consumerTopic,
      const string &fileLastNotifyTime,
      bool niceHashForced,
      uint64_t niceHashMinDiff,
      const std::string &niceHashMinDiffZookeeperPath);
  virtual ~JobRepositoryEth();

  bool compute(
      uint64_t height,
      const ethash_h256_t &header,
      uint64_t nonce,
      ethash_return_value_t &r);

  shared_ptr<StratumJob> createStratumJob() override {
    return std::make_shared<StratumJobEth>();
  }
  shared_ptr<StratumJobEx>
  createStratumJobEx(shared_ptr<StratumJob> sjob, bool isClean) override;
  void broadcastStratumJob(shared_ptr<StratumJob> sjob) override;

  void rebuildDagCacheNonBlocking(uint64_t height);

protected:
  void buildDagCacheNonBlocking(uint64_t height);

  // TODO: move to configuration file
  const char *kLightCacheFilePathFormat = "./sserver-eth%u-dagcache.dat";

  EthashCalculator ethashCalc_;
  uint32_t lastHeight_ = 0;
};
#endif // STRATUM_SERVER_ETH_H_

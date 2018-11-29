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
#include "StratumServer.h"
#include "StratumEth.h"

class JobRepositoryEth;

class ServerEth : public ServerBase<JobRepositoryEth>
{
public:
  ServerEth(const int32_t shareAvgSeconds) : ServerBase(shareAvgSeconds) {}
  bool setupInternal(StratumServer* sserver) override;
  int checkShareAndUpdateDiff(ShareEth &share,
                              const uint64_t jobId,
                              const uint64_t nonce,
                              const uint256 &header,
                              const std::set<uint64_t> &jobDiffs,
                              uint256 &returnedMixHash,
                              const string &workFullName);
  void sendSolvedShare2Kafka(const string& strNonce, const string& strHeader, const string& strMix,
                             const uint32_t height, const uint64_t networkDiff, const StratumWorker &worker,
                             const EthConsensus::Chain chain);

  JobRepository* createJobRepository(const char *kafkaBrokers,
                                    const char *consumerTopic,
                                     const string &fileLastNotifyTime) override;

  unique_ptr<StratumSession> createConnection(struct bufferevent *bev, struct sockaddr *saddr, const uint32_t sessionID) override;
};

class JobRepositoryEth : public JobRepositoryBase<ServerEth>
{
public:
  JobRepositoryEth(const char *kafkaBrokers, const char *consumerTopic, const string &fileLastNotifyTime, ServerEth *server);
  virtual ~JobRepositoryEth();

  bool compute(ethash_h256_t const header, uint64_t nonce, ethash_return_value_t& r);

  StratumJob *createStratumJob() override {return new StratumJobEth();}
  StratumJobEx* createStratumJobEx(StratumJob *sjob, bool isClean) override;
  void broadcastStratumJob(StratumJob *sjob) override;

  // re-computing light when checking share failed.
  void rebuildLightNonBlocking(StratumJobEth* job);

private:
  // TODO: move to configuration file
  const char *kLightCacheFilePath = "./sserver-eth-dagcache.dat";

  // save ethash_light_t to file
  struct LightCacheHeader {
    uint64_t checkSum_;
	  uint64_t blockNumber_;
	  uint64_t cacheSize_;
  };

  void newLightNonBlocking(StratumJobEth* job);
  void _newLightThread(uint64_t height);
  void deleteLight();
  void deleteLightNoLock();

  // Creating a new ethash_light_t (DAG cache) is so slow (in Debug build),
  // it may need more than 120 seconds for current Ethereum mainnet.
  // So save it to a file before shutdown and load it back at next time 
  // to reduce the computation time required after a reboot.
  // 
  // Note: The performance difference between Debug and Release builds is very large.
  // The Release build may complete in 5 s, while the Debug build takes more than 60 s.
  void saveLightToFile();
  void saveLightToFile(const ethash_light_t &light, std::ofstream &f);
  void loadLightFromFile();
  ethash_light_t loadLightFromFile(std::ifstream &f);
  uint64_t computeLightCacheCheckSum(const LightCacheHeader &header, const uint8_t *data);

  ethash_light_t light_;
  ethash_light_t nextLight_;
  std::atomic<uint64_t> epochs_;
  mutex lightLock_;
  mutex nextLightLock_;

  uint32_t lastHeight_;
};
#endif // STRATUM_SERVER_ETH_H_

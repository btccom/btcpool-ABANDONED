#pragma once
#include "StratumServer.h"

#include "uint256.h"

class JobRepositoryTellor;
class ShareTellor;

class StratumServerTellor : public ServerBase<JobRepositoryTellor> {
public:
  unique_ptr<StratumSession> createConnection(
      struct bufferevent *bev,
      struct sockaddr *saddr,
      uint32_t sessionID) override;

  void checkAndUpdateShare(
      size_t chainId,
      ShareTellor &share,
      shared_ptr<StratumJobEx> exjob,
      const std::set<uint64_t> &jobDiffs,
      const string &workFullName,
      uint256 &blockHash);

  void sendSolvedShare2Kafka(
      size_t chainId,
      const ShareTellor &share,
      shared_ptr<StratumJobEx> exjob,
      const StratumWorker &worker,
      const uint256 &blockHash);

protected:
  JobRepository *createJobRepository(
      size_t chainId,
      const char *kafkaBrokers,
      const char *consumerTopic,
      const string &fileLastNotifyTime,
      bool niceHashForced,
      uint64_t niceHashMinDiff,
      const std::string &niceHashMinDiffZookeeperPath) override;
};

class JobRepositoryTellor : public JobRepositoryBase<StratumServerTellor> {
public:
  JobRepositoryTellor(
      size_t chainId,
      StratumServerTellor *server,
      const char *kafkaBrokers,
      const char *consumerTopic,
      const string &fileLastNotifyTime,
      bool niceHashForced,
      uint64_t niceHashMinDiff,
      const std::string &niceHashMinDiffZookeeperPath);

  shared_ptr<StratumJob> createStratumJob() override;
  void broadcastStratumJob(shared_ptr<StratumJob> sjob) override;

private:
  uint64_t lastHeight_;
  std::string lastchallenge_;
};

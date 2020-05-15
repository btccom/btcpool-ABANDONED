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
#include "StratumServer.h"

#include "StratumServerStats.h"
#include "StratumSession.h"
#include "DiffController.h"
#include "Management.h"

#include <boost/thread.hpp>
#include <event2/thread.h>

#include "ssl/SSLUtils.h"

#include <netinet/tcp.h>

using namespace std;

static const uint32_t MIN_SHARE_WORKER_QUEUE_SIZE = 256;
static const uint32_t MIN_SHARE_WORKER_THREADS = 1;

//////////////////////////////// SessionIDManagerT
/////////////////////////////////
template <uint8_t IBITS>
SessionIDManagerT<IBITS>::SessionIDManagerT(const uint8_t serverId)
  : serverId_(serverId)
  , count_(0)
  , allocIdx_(0)
  , allocInterval_(0) {
  static_assert(IBITS <= 24, "IBITS cannot large than 24");
  sessionIds_.reset();
}

template <uint8_t IBITS>
bool SessionIDManagerT<IBITS>::ifFull() {
  ScopeLock sl(lock_);
  return _ifFull();
}

template <uint8_t IBITS>
bool SessionIDManagerT<IBITS>::_ifFull() {
  if (count_ > kSessionIdMask) {
    return true;
  }
  return false;
}

template <uint8_t IBITS>
void SessionIDManagerT<IBITS>::setAllocInterval(uint32_t interval) {
  allocInterval_ = interval;
}

template <uint8_t IBITS>
bool SessionIDManagerT<IBITS>::allocSessionId(uint32_t *sessionID) {
  ScopeLock sl(lock_);

  if (_ifFull())
    return false;

  // find an empty bit
  while (sessionIds_.test(allocIdx_) == true) {
    allocIdx_ = (allocIdx_ + 1) & kSessionIdMask;
  }

  // set to true
  sessionIds_.set(allocIdx_, true);
  count_++;

  *sessionID = (((uint32_t)serverId_ << IBITS) | allocIdx_);
  allocIdx_ = (allocIdx_ + allocInterval_) & kSessionIdMask;
  return true;
}

template <uint8_t IBITS>
void SessionIDManagerT<IBITS>::freeSessionId(uint32_t sessionId) {
  ScopeLock sl(lock_);

  const uint32_t idx = (sessionId & kSessionIdMask);
  sessionIds_.set(idx, false);
  count_--;
}

// Class template instantiation
template class SessionIDManagerT<8>;
template class SessionIDManagerT<16>;
template class SessionIDManagerT<24>;

////////////////////////////////// JobRepository ///////////////////////////////
JobRepository::JobRepository(
    size_t chainId,
    StratumServer *server,
    const char *kafkaBrokers,
    const char *consumerTopic,
    const string &fileLastNotifyTime,
    bool niceHashForced,
    uint64_t niceHashMinDiff,
    const std::string &niceHashMinDiffZookeeperPath)
  : running_(true)
  , chainId_(chainId)
  , kafkaConsumer_(kafkaBrokers, consumerTopic, 0 /*patition*/)
  , server_(server)
  , fileLastNotifyTime_(fileLastNotifyTime)
  , kMaxJobsLifeTime_(300)
  , kMiningNotifyInterval_(30)
  , lastJobSendTime_(0)
  , lastJobId_(0)
  , lastJobHeight_(0)
  , niceHashForced_(niceHashForced)
  , niceHashMinDiff_(niceHashMinDiff) {
  assert(kMiningNotifyInterval_ < kMaxJobsLifeTime_);
  if (!niceHashMinDiffZookeeperPath.empty()) {
    niceHashMinDiffWatcher_ = std::make_unique<ZookeeperValueWatcher>(
        *server->zk_,
        niceHashMinDiffZookeeperPath,
        20,
        [this](const std::string &value) {
          try {
            niceHashMinDiff_ = formatDifficulty(stoull(value, 0, 10));
            LOG(INFO) << "NiceHash minimal difficulty from Zookeeper is "
                      << niceHashMinDiff_;
          } catch (std::invalid_argument) {
            LOG(ERROR) << "NiceHash minimal difficulty from Zookeeper is "
                          "invalid: "
                       << value;
          } catch (std::out_of_range) {
            LOG(ERROR) << "NiceHash minimal difficulty from Zookeeper is out of"
                          " range: "
                       << value;
          }
        });
  }
}

JobRepository::~JobRepository() {
  if (threadConsume_.joinable())
    threadConsume_.join();
}

void JobRepository::setMaxJobLifeTime(const time_t maxJobLifeTime) {
  LOG(INFO) << "set max job lifetime to " << maxJobLifeTime << "s";
  kMaxJobsLifeTime_ = maxJobLifeTime;
}

void JobRepository::setMiningNotifyInterval(time_t miningNotifyInterval) {
  LOG(INFO) << "set mining notify interval to " << miningNotifyInterval << "s";
  kMiningNotifyInterval_ = miningNotifyInterval;
}

shared_ptr<StratumJobEx> JobRepository::getStratumJobEx(const uint64_t jobId) {
  auto itr = exJobs_.find(jobId);
  if (itr != exJobs_.end()) {
    return itr->second;
  }
  return nullptr;
}

shared_ptr<StratumJobEx> JobRepository::getLatestStratumJobEx() {
  if (exJobs_.size()) {
    return exJobs_.rbegin()->second;
  }
  LOG(WARNING) << "getLatestStratumJobEx fail";
  return nullptr;
}

void JobRepository::stop() {
  if (!running_) {
    return;
  }
  running_ = false;
  LOG(INFO) << "stop job repository";
  if (threadConsume_.joinable()) {
    threadConsume_.join();
  }
}

bool JobRepository::setupThreadConsume() {
  const int32_t kConsumeLatestN = 1;

  // we need to consume the latest one
  map<string, string> consumerOptions;
  consumerOptions["fetch.wait.max.ms"] = "10";
  if (kafkaConsumer_.setup(
          RD_KAFKA_OFFSET_TAIL(kConsumeLatestN), &consumerOptions) == false) {
    LOG(INFO) << "setup consumer fail";
    return false;
  }

  if (!kafkaConsumer_.checkAlive()) {
    LOG(ERROR) << "kafka brokers is not alive";
    return false;
  }

  threadConsume_ = std::thread(&JobRepository::runThreadConsume, this);
  return true;
}

void JobRepository::runThreadConsume() {
  LOG(INFO) << "start job repository consume thread";

  const int32_t kTimeoutMs = 1000;
  while (running_) {
    rd_kafka_message_t *rkmessage;
    rkmessage = kafkaConsumer_.consumer(kTimeoutMs);

    // timeout, most of time it's not nullptr and set an error:
    //          rkmessage->err == RD_KAFKA_RESP_ERR__PARTITION_EOF
    if (rkmessage != nullptr) {
      // consume stratum job
      //
      // It will create a StratumJob and try to broadcast it immediately with
      // broadcastStratumJob(shared_ptr<StratumJob>). A derived class needs to
      // implement the abstract method
      // broadcastStratumJob(shared_ptr<StratumJob>) to decide whether to add
      // the StratumJob to the map exJobs_ and whether to send the job to miners
      // immediately. Derived classes do not need to implement a scheduled
      // sending mechanism, checkAndSendMiningNotify() will provide a default
      // implementation.
      consumeStratumJob(rkmessage);

      // Return message to rdkafka
      rd_kafka_message_destroy(rkmessage);
    }

    server_->dispatch([this]() {
      // check if we need to send mining notify
      // It's a default implementation of scheduled sending / regular updating
      // of stratum jobs. If no job is sent for a long time via
      // broadcastStratumJob(), a job will be sent via this method.
      checkAndSendMiningNotify();

      tryCleanExpiredJobs();
    });
  }

  LOG(INFO) << "stop job repository consume thread";
}

void JobRepository::consumeStratumJob(rd_kafka_message_t *rkmessage) {
  // check error
  if (rkmessage->err) {
    if (rkmessage->err == RD_KAFKA_RESP_ERR__PARTITION_EOF) {
      // Reached the end of the topic+partition queue on the broker.
      // Not really an error.
      //      LOG(INFO) << "consumer reached end of " <<
      //      rd_kafka_topic_name(rkmessage->rkt)
      //      << "[" << rkmessage->partition << "] "
      //      << " message queue at offset " << rkmessage->offset;
      // acturlly
      return;
    }

    LOG(ERROR) << "consume error for topic "
               << rd_kafka_topic_name(rkmessage->rkt) << "["
               << rkmessage->partition << "] offset " << rkmessage->offset
               << ": " << rd_kafka_message_errstr(rkmessage);

    if (rkmessage->err == RD_KAFKA_RESP_ERR__UNKNOWN_PARTITION ||
        rkmessage->err == RD_KAFKA_RESP_ERR__UNKNOWN_TOPIC) {
      LOG(FATAL) << "consume fatal";
    }
    return;
  }

  shared_ptr<StratumJob> sjob = createStratumJob();
  bool res = sjob->unserializeFromJson(
      (const char *)rkmessage->payload, rkmessage->len);
  if (res == false) {
    LOG(ERROR) << "unserialize stratum job fail";
    return;
  }
  // make sure the job is not expired.
  time_t now = time(nullptr);
  if (sjob->jobTime() + kMaxJobsLifeTime_ < now) {
    LOG(ERROR)
        << "too large delay from kafka to receive topic 'StratumJob' job time="
        << sjob->jobTime() << ", max delay=" << kMaxJobsLifeTime_
        << ", now=" << now;
    return;
  }
  DLOG(INFO) << "received jobId : " << sjob->jobId_;
  server_->dispatch([this, sjob]() {
    // here you could use Map.find() without lock, it's sure
    // that everyone is using this Map readonly now
    auto existingJob = getStratumJobEx(sjob->jobId_);
    if (existingJob != nullptr) {
      LOG(ERROR) << "jobId already existed jobId " << sjob->jobId_;
      return;
    }

    broadcastStratumJob(sjob);
  });
}

shared_ptr<StratumJobEx>
JobRepository::createStratumJobEx(shared_ptr<StratumJob> sjob, bool isClean) {
  return std::make_shared<StratumJobEx>(chainId_, sjob, isClean);
}

void JobRepository::markAllJobsAsStale(uint64_t height) {
  for (auto it : exJobs_) {
    auto &exjob = it.second;
    if (exjob->sjob_ && exjob->sjob_->height() <= height) {
      exjob->markStale();
    }
  }
}

void JobRepository::checkAndSendMiningNotify() {
  // last job is 'expried', send a new one
  if (exJobs_.size() &&
      lastJobSendTime_ + kMiningNotifyInterval_ <= time(nullptr)) {
    shared_ptr<StratumJobEx> exJob = exJobs_.rbegin()->second;
    sendMiningNotify(exJob);
  }
}

void JobRepository::sendLatestMiningNotify() {
  if (exJobs_.size()) {
    shared_ptr<StratumJobEx> exJob = exJobs_.rbegin()->second;
    sendMiningNotify(exJob);
  }
}

void JobRepository::sendMiningNotify(shared_ptr<StratumJobEx> exJob) {
  // send job to all clients
  server_->sendMiningNotifyToAll(exJob);
  lastJobSendTime_ = time(nullptr);

  // write last mining notify time to file
  if (lastJobId_ != exJob->sjob_->jobId_ && !fileLastNotifyTime_.empty())
    writeTime2File(fileLastNotifyTime_.c_str(), (uint32_t)lastJobSendTime_);

  lastJobId_ = exJob->sjob_->jobId_;
  lastJobHeight_ = exJob->sjob_->height();
}

void JobRepository::tryCleanExpiredJobs() {
  const uint32_t nowTs = (uint32_t)time(nullptr);
  // Keep at least one job to keep normal mining when the jobmaker fails
  while (exJobs_.size() > 1) {
    // Maps (and sets) are sorted, so the first element is the smallest,
    // and the last element is the largest.
    auto itr = exJobs_.begin();

    const time_t jobTime = (time_t)(itr->first >> 32);
    if (nowTs < jobTime + kMaxJobsLifeTime_) {
      break; // not expired
    }

    LOG(INFO) << "remove expired stratum job, id: " << itr->first
              << ", time: " << date("%F %T", jobTime);

    // remove expired job
    exJobs_.erase(itr);
  }
}

////////////////////////////////// StratumJobEx ////////////////////////////////
StratumJobEx::StratumJobEx(
    size_t chainId, shared_ptr<StratumJob> sjob, bool isClean)
  : state_(0)
  , chainId_(chainId)
  , isClean_(isClean)
  , sjob_(sjob) {
  assert(sjob);
}

StratumJobEx::~StratumJobEx() {
}

void StratumJobEx::markStale() {
  // 0: MINING, 1: STALE
  state_ = 1;
}

bool StratumJobEx::isStale() {
  // 0: MINING, 1: STALE
  return (state_ == 1);
}

///////////////////////////////////// StratumServer
//////////////////////////////////////

SSL_CTX *StratumServer::getSSLCTX(const libconfig::Config &config) {
  return get_server_SSL_CTX(
      config.lookup("sserver.tls_cert_file").c_str(),
      config.lookup("sserver.tls_key_file").c_str());
}

StratumServer::StratumServer()
  : enableTLS_(false)
  , base_(nullptr)
  , listener_(nullptr)
  , tcpReadTimeout_(600)
  , shutdownGracePeriod_(3600)
  , disconnectTimer_(nullptr)
  , acceptStale_(true)
  , isEnableSimulator_(false)
  , isSubmitInvalidBlock_(false)
  , isDevModeEnable_(false)
  , devFixedDifficulty_(1.0)
#ifndef WORK_WITH_STRATUM_SWITCHER
  , sessionIDManager_(nullptr)
#endif
  , userIdManager_(nullptr)
  , userInfo_(nullptr)
  , serverId_(0) {
}

StratumServer::~StratumServer() {
  // Destroy connections before event base
  connections_.clear();

  if (statsExporter_) {
    if (statsExporter_) {
      statsExporter_->unregisterCollector(statsCollector_);
    }

    // Destroy exporter before event base
    statsExporter_.reset();
  }

  if (disconnectTimer_ != nullptr) {
    event_free(disconnectTimer_);
  }

  if (listener_ != nullptr) {
    evconnlistener_free(listener_);
  }
  if (base_ != nullptr) {
    event_base_free(base_);
  }
  if (userInfo_ != nullptr) {
    delete userInfo_;
  }
  for (ChainVars &chain : chains_) {
    if (chain.kafkaProducerShareLog_ != nullptr) {
      delete chain.kafkaProducerShareLog_;
    }
    if (chain.kafkaProducerSolvedShare_ != nullptr) {
      delete chain.kafkaProducerSolvedShare_;
    }
    if (chain.kafkaProducerCommonEvents_ != nullptr) {
      delete chain.kafkaProducerCommonEvents_;
    }
    if (chain.jobRepository_ != nullptr) {
      delete chain.jobRepository_;
    }
  }

  if (nullptr != userIdManager_) {
    delete userIdManager_;
    userIdManager_ = nullptr;
  }

#ifndef WORK_WITH_STRATUM_SWITCHER
  if (sessionIDManager_ != nullptr) {
    delete sessionIDManager_;
  }
#endif
}

void StratumServer::initZookeeper(const libconfig::Config &config) {
  if (!zk_) {
    zk_ = std::make_shared<Zookeeper>(config.lookup("zookeeper.brokers"));
  }
}

bool StratumServer::setup(const libconfig::Config &config) {
  // ------------------- Log Options -------------------
  config.lookupValue("log.hide_ip_prefix", logHideIpPrefix_);

#ifdef WORK_WITH_STRATUM_SWITCHER
  LOG(INFO) << "WORK_WITH_STRATUM_SWITCHER enabled, miners can only connect to "
               "the sserver via a stratum switcher.";
#endif

  config.lookupValue("sserver.accept_stale", acceptStale_);
  LOG_IF(INFO, acceptStale_) << "[Option] stale shares will be accepted.";

  // ------------------- Development Options -------------------

  config.lookupValue("sserver.enable_simulator", isEnableSimulator_);
  if (isEnableSimulator_) {
    LOG(WARNING)
        << "[Dev Option] Simulator is enabled, all share will be accepted. "
        << "This option should not be enabled in a production environment!";
  }

  config.lookupValue(
      "sserver.enable_submit_invalid_block", isSubmitInvalidBlock_);
  if (isSubmitInvalidBlock_) {
    LOG(WARNING)
        << "[Dev Option] Submit invalid block is enabled, all shares will "
           "become solved shares. "
        << "This option should not be enabled in a production environment!";
  }

  config.lookupValue("sserver.enable_dev_mode", isDevModeEnable_);
  if (isDevModeEnable_) {
    config.lookupValue("sserver.dev_fixed_difficulty", devFixedDifficulty_);
    LOG(WARNING)
        << "[Dev Option] Development mode is enabled with fixed difficulty: "
        << devFixedDifficulty_
        << ". This option should not be enabled in a production environment!";
  }

  // grandPool 4+4+8
  config.lookupValue("sserver.grandPoolEnabled", grandPoolEnabled_);

  // ------------------- Diff Controller Options -------------------

  string defDiffStr = config.lookup("sserver.default_difficulty");
  uint64_t defaultDifficulty =
      formatDifficulty(stoull(defDiffStr, nullptr, 16));

  string maxDiffStr = config.lookup("sserver.max_difficulty");
  uint64_t maxDifficulty = formatDifficulty(stoull(maxDiffStr, nullptr, 16));

  string minDiffStr = config.lookup("sserver.min_difficulty");
  uint64_t minDifficulty = formatDifficulty(stoull(minDiffStr, nullptr, 16));

  uint32_t diffAdjustPeriod = 300;
  config.lookupValue("sserver.diff_adjust_period", diffAdjustPeriod);

  uint32_t shareAvgSeconds = 10; // default share interval time 10 seconds
  config.lookupValue("sserver.share_avg_seconds", shareAvgSeconds);

  if (0 == defaultDifficulty || 0 == maxDifficulty || 0 == minDifficulty ||
      0 == diffAdjustPeriod) {
    LOG(ERROR) << "difficulty settings are not expected: def="
               << defaultDifficulty << ", min=" << minDifficulty
               << ", max=" << maxDifficulty
               << ", adjustPeriod=" << diffAdjustPeriod;
    return false;
  }

  if (diffAdjustPeriod < shareAvgSeconds) {
    LOG(ERROR)
        << "`diff_adjust_period` should not less than `share_avg_seconds`";
    return false;
  }

  defaultDifficultyController_ = make_shared<DiffController>(
      defaultDifficulty,
      maxDifficulty,
      minDifficulty,
      shareAvgSeconds,
      diffAdjustPeriod);

  // ------------------- Other Options -------------------

  uint32_t maxJobLifetime = 300;
  config.lookupValue(
      "sserver.max_job_delay", maxJobLifetime); // the old option name
  config.lookupValue(
      "sserver.max_job_lifetime",
      maxJobLifetime); // the new name, overwrite the old if exist
  if (maxJobLifetime < 300) {
    LOG(WARNING)
        << "[Bad Option] sserver.max_job_lifetime (" << maxJobLifetime
        << " seconds) is too short, recommended to be 300 seconds or longer.";
  }

  uint32_t miningNotifyInterval = 30; // optional
  config.lookupValue("sserver.mining_notify_interval", miningNotifyInterval);

  config.lookupValue("sserver.tcp_read_timeout", tcpReadTimeout_); // optional

  string longTimeoutAgent;
  config.lookupValue("sserver.long_timeout_agent", longTimeoutAgent);
  if (!longTimeoutAgent.empty()) {
    longTimeoutPattern_ =
        std::regex{longTimeoutAgent,
                   std::regex_constants::grep | std::regex_constants::icase};
  }

  // ------------------- Listen Options -------------------

  string listenIP = "0.0.0.0";
  config.lookupValue("sserver.ip", listenIP);

  int32_t listenPort = 3333;
  config.lookupValue("sserver.port", listenPort);

  // ------------------- Kafka Options -------------------

  auto addChainVars = [&](const string &chainName,
                          const string &kafkaBrokers,
                          const string &shareTopic,
                          const string &solvedShareTopic,
                          const string &commonEventsTopic,
                          const string &jobTopic,
                          const string &fileLastMiningNotifyTime,
                          const bool niceHashForced,
                          uint64_t niceHashMinDiff,
                          const string &niceHashMinDiffZookeeperPath,
                          int32_t singleUserId) {
    size_t chainId = chains_.size();

    LOG(INFO) << "chain = " << chainName
              << ", niceHashForced = " << niceHashForced
              << ", niceHashMinDiff = " << niceHashMinDiff;

    chains_.push_back(
        {chainName,
         new KafkaProducer(
             kafkaBrokers.c_str(), shareTopic.c_str(), RD_KAFKA_PARTITION_UA),
         new KafkaProducer(
             kafkaBrokers.c_str(),
             solvedShareTopic.c_str(),
             RD_KAFKA_PARTITION_UA),
         new KafkaProducer(
             kafkaBrokers.c_str(),
             commonEventsTopic.c_str(),
             RD_KAFKA_PARTITION_UA),
         createJobRepository(
             chainId,
             kafkaBrokers.c_str(),
             jobTopic.c_str(),
             fileLastMiningNotifyTime,
             niceHashForced,
             niceHashMinDiff,
             niceHashMinDiffZookeeperPath),
         {},
         singleUserId});
  };

  bool multiChains = false;
  config.lookupValue("sserver.multi_chains", multiChains);

  bool niceHashForced = false;
  config.lookupValue("sserver.nicehash.forced", niceHashForced);
  uint64_t niceHashMinDiff = minDifficulty;
  std::string niceHashMinDiffString;
  if (config.lookupValue(
          "sserver.nicehash.min_difficulty", niceHashMinDiffString)) {
    niceHashMinDiff = formatDifficulty(stoull(niceHashMinDiffString, 0, 16));
  }
  std::string niceHashMinDiffZookeeperPath;
  config.lookupValue(
      "sserver.nicehash.min_difficulty_zookeeper_path",
      niceHashMinDiffZookeeperPath);
  if (!niceHashMinDiffZookeeperPath.empty()) {
    initZookeeper(config);
  }

  if (multiChains) {
    const Setting &chains = config.lookup("chains");
    for (int i = 0; i < chains.getLength(); i++) {
      string fileLastMiningNotifyTime; // optional
      chains.lookupValue("file_last_notify_time", fileLastMiningNotifyTime);

      bool chainNiceHashForced = niceHashForced;
      chains[i].lookupValue("nicehash.forced", chainNiceHashForced);
      uint64_t chainNiceHashMinDiff = niceHashMinDiff;
      std::string chainNiceHashMinDiffString;
      if (chains[i].lookupValue(
              "nicehash.min_difficulty", chainNiceHashMinDiffString)) {
        chainNiceHashMinDiff =
            formatDifficulty(stoull(chainNiceHashMinDiffString, 0, 16));
      }
      std::string chainNiceHashMinDiffZookeeperPath =
          niceHashMinDiffZookeeperPath;
      chains[i].lookupValue(
          "nicehash.min_difficulty_zookeeper_path",
          chainNiceHashMinDiffZookeeperPath);
      if (!chainNiceHashMinDiffZookeeperPath.empty()) {
        initZookeeper(config);
      }

      int singleUserId = 0;
      chains[i].lookupValue("single_user_puid", singleUserId);

      addChainVars(
          chains[i].lookup("name"),
          chains[i].lookup("kafka_brokers"),
          chains[i].lookup("share_topic"),
          chains[i].lookup("solved_share_topic"),
          chains[i].lookup("common_events_topic"),
          chains[i].lookup("job_topic"),
          fileLastMiningNotifyTime,
          chainNiceHashForced,
          chainNiceHashMinDiff,
          chainNiceHashMinDiffZookeeperPath,
          singleUserId);
    }
    if (chains_.empty()) {
      LOG(FATAL) << "sserver.multi_chains enabled but chains empty!";
    }
  } else {
    string fileLastMiningNotifyTime; // optional
    config.lookupValue(
        "sserver.file_last_notify_time", fileLastMiningNotifyTime);

    int singleUserId = 0;
    config.lookupValue("users.single_user_puid", singleUserId);

    addChainVars(
        "default",
        config.lookup("kafka.brokers"),
        config.lookup("sserver.share_topic"),
        config.lookup("sserver.solved_share_topic"),
        config.lookup("sserver.common_events_topic"),
        config.lookup("sserver.job_topic"),
        fileLastMiningNotifyTime,
        niceHashForced,
        niceHashMinDiff,
        niceHashMinDiffZookeeperPath,
        singleUserId);
  }

  // single user mode
  config.lookupValue("users.single_user_chain", singleUserChain_);
  config.lookupValue("users.single_user_mode", singleUserMode_);
  config.lookupValue("users.single_user_name", singleUserName_);
  if (singleUserChain_) {
    LOG(INFO) << "[Option] Single User Chain Enabled, chain switching "
                 "reference userName: "
              << singleUserName_;
    if (singleUserName_.empty()) {
      LOG(FATAL)
          << "single_user_name cannot be empty when single_user_chain enabled!";
    }
  }
  if (singleUserMode_) {
    string chainPuids = "";
    for (auto chain : chains_) {
      chainPuids +=
          ", " + chain.name_ + " puid: " + std::to_string(chain.singleUserId_);
    }
    LOG(INFO) << "[Option] Single User Mode Enabled, userName: "
              << singleUserName_ << chainPuids;
  }

  // Proxy protocol support
  config.lookupValue("sserver.proxy_protocol", proxyProtocol_);
  LOG_IF(INFO, proxyProtocol_) << "PROXY protocol support enabled";

  // ------------------- user info -------------------
  // It should at below of addChainVars() or sserver may crashed
  // because of IndexOutOfBoundsException in chains_.
  userInfo_ = new UserInfo(this, config);
  if (!userInfo_->setupThreads()) {
    return false;
  }

#ifndef WORK_WITH_STRATUM_SWITCHER
  // ------------------- server id -------------------
  int serverId;
  config.lookupValue("sserver.id", serverId);
  if (serverId > 0xFF) {
    LOG(ERROR) << "invalid server id, range: [0, 255]";
    return false;
  }

  serverId_ = (uint8_t)serverId;
  if (serverId_ == 0) {
    // assign ID from zookeeper
    initZookeeper(config);
    serverId_ =
        zk_->getUniqIdUint8(config.lookup("sserver.zookeeper_lock_path"));
  }
  string type = config.lookup("sserver.type");
  if ("CKB" == type) {
    sessionIDManager_ = new SessionIDManagerT<16>(serverId_);
  } else {
    sessionIDManager_ = new SessionIDManagerT<24>(serverId_);
  }

#endif

  //-------------------Init local userid manager-------------
  userIdManager_ = new SessionIDManagerT<24>(0);
  miningModel_ = 1;
  config.lookupValue("sserver.mining_model", miningModel_);
  LOG(INFO) << "current mining model : "
            << ((miningModel_ == 1) ? "subaccount" : "anonymous");

  // ------------------- Init JobRepository -------------------
  for (ChainVars &chain : chains_) {
    chain.jobRepository_->setMaxJobLifeTime(maxJobLifetime);
    chain.jobRepository_->setMiningNotifyInterval(miningNotifyInterval);
    if (!chain.jobRepository_->setupThreadConsume()) {
      LOG(ERROR) << "init JobRepository for chain " << chain.name_ << " failed";
      return false;
    }
  }

  // ------------------- Init Kafka -------------------

  // kafkaProducerShareLog_
  {
    map<string, string> options;
    // we could delay 'sharelog' in producer
    // 10000000 * sizeof(ShareBitcoin) ~= 480 MB
    options["queue.buffering.max.messages"] = "10000000";
    // send every second
    options["queue.buffering.max.ms"] = "1000";
    // 10000 * sizeof(ShareBitcoin) ~= 480 KB
    options["batch.num.messages"] = "10000";

    for (ChainVars &chain : chains_) {
      if (!chain.kafkaProducerShareLog_->setup(&options)) {
        LOG(ERROR) << "kafka kafkaProducerShareLog_ for chain " << chain.name_
                   << " setup failure";
        return false;
      }
      if (!chain.kafkaProducerShareLog_->checkAlive()) {
        LOG(ERROR) << "kafka kafkaProducerShareLog_ for chain " << chain.name_
                   << " is NOT alive";
        return false;
      }
    }
  }

  // kafkaProducerSolvedShare_
  {
    map<string, string> options;
    // set to 1 (0 is an illegal value here), deliver msg as soon as possible.
    options["queue.buffering.max.ms"] = "1";

    for (ChainVars &chain : chains_) {
      if (!chain.kafkaProducerSolvedShare_->setup(&options)) {
        LOG(ERROR) << "kafka kafkaProducerSolvedShare_ for chain "
                   << chain.name_ << " setup failure";
        return false;
      }
      if (!chain.kafkaProducerSolvedShare_->checkAlive()) {
        LOG(ERROR) << "kafka kafkaProducerSolvedShare_ for chain "
                   << chain.name_ << " is NOT alive";
        return false;
      }
    }
  }

  // kafkaProducerCommonEvents_
  {
    map<string, string> options;
    options["queue.buffering.max.messages"] = "500000";
    options["queue.buffering.max.ms"] = "1000"; // send every second
    options["batch.num.messages"] = "10000";

    for (ChainVars &chain : chains_) {
      if (!chain.kafkaProducerCommonEvents_->setup(&options)) {
        LOG(ERROR) << "kafka kafkaProducerCommonEvents_ for chain "
                   << chain.name_ << " setup failure";
        return false;
      }
      if (!chain.kafkaProducerCommonEvents_->checkAlive()) {
        LOG(ERROR) << "kafka kafkaProducerCommonEvents_ for chain "
                   << chain.name_ << " is NOT alive";
        return false;
      }
    }
  }

  // ------------------- TCP Listen -------------------

  // Enable multithreading and flag BEV_OPT_THREADSAFE.
  // Without it, bufferevent_socket_new() will return NULL with flag
  // BEV_OPT_THREADSAFE.
  evthread_use_pthreads();

  base_ = event_base_new();
  if (!base_) {
    LOG(ERROR) << "server: cannot create base";
    return false;
  }

  memset(&sin_, 0, sizeof(sin_));
  sin_.sin_family = AF_INET;
  sin_.sin_port = htons(listenPort);
  sin_.sin_addr.s_addr = htonl(INADDR_ANY);
  if (listenIP.empty() ||
      inet_pton(AF_INET, listenIP.c_str(), &sin_.sin_addr) == 0) {
    LOG(ERROR) << "invalid ip: " << listenIP;
    return false;
  }

  listener_ = evconnlistener_new_bind(
      base_,
      StratumServer::listenerCallback,
      (void *)this,
      LEV_OPT_REUSEABLE | LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE_PORT,
      -1,
      (struct sockaddr *)&sin_,
      sizeof(sin_));
  if (!listener_) {
    LOG(ERROR) << "cannot create listener: " << listenIP << ":" << listenPort;
    return false;
  }

  // initialize but don't activate the graceful shutdown disconnect timer event
  config.lookupValue("sserver.shutdown_grace_period", shutdownGracePeriod_);
  disconnectTimer_ = event_new(
      base_, -1, EV_PERSIST, &StratumServer::disconnectCallback, this);

  // check if TLS enabled
  config.lookupValue("sserver.enable_tls", enableTLS_);
  if (enableTLS_) {
    LOG(INFO) << "TLS enabled";
    // try get SSL CTX (load SSL cert and key)
    // any error will abort the process
    sslCTX_ = getSSLCTX(config);
  }

  // setup promethues exporter
  bool statsEnabled = true;
  config.lookupValue("prometheus.enabled", statsEnabled);
  if (statsEnabled) {
    string exporterAddress = "0.0.0.0";
    unsigned int exporterPort = 9100;
    string exporterPath = "/metrics";
    config.lookupValue("prometheus.address", exporterAddress);
    config.lookupValue("prometheus.port", exporterPort);
    config.lookupValue("prometheus.path", exporterPath);
    statsCollector_ = std::make_shared<StratumServerStats>(*this);
    statsExporter_ = prometheus::CreateExporter();
    if (!statsExporter_->setup(exporterAddress, exporterPort, exporterPath)) {
      LOG(WARNING) << "Failed to setup stratum server statistics exporter";
    }
    if (!statsExporter_->registerCollector(statsCollector_)) {
      LOG(WARNING) << "Failed to register stratum server statistics collector";
    }
    if (!statsExporter_->run(base_)) {
      LOG(WARNING) << "Failed to run stratum server statistics exporter";
    }
  }

  uint32_t shareWorkerQueueSize = 0;
  config.lookupValue("sserver.share_worker_queue_size", shareWorkerQueueSize);
  shareWorker_ = std::make_unique<WorkerPool>(
      std::max(shareWorkerQueueSize, MIN_SHARE_WORKER_QUEUE_SIZE));

  uint32_t shareWorkerThreads = 0;
  config.lookupValue("sserver.share_worker_threads", shareWorkerThreads);
  shareWorker_->start(std::max(shareWorkerThreads, MIN_SHARE_WORKER_THREADS));

  bool enableManagement = true;
  config.lookupValue("management.enabled", enableManagement);
  if (enableManagement) {
    management_ = std::make_unique<Management>(config, *this);
    if (!management_->setup()) {
      LOG(ERROR) << "Management setup failed";
      return false;
    }
  }

  // ------------------- Derived Class Setup -------------------
  return setupInternal(config);
}

void StratumServer::run() {
  if (management_) {
    management_->run();
  }
  LOG(INFO) << "stratum server running";
  if (base_ != NULL) {
    //    event_base_loop(base_, EVLOOP_NONBLOCK);
    event_base_dispatch(base_);
  }
}

void StratumServer::stop() {
  LOG(INFO) << "stop stratum server";
  event_base_loopexit(base_, NULL);
  for (ChainVars &chain : chains_) {
    chain.jobRepository_->stop();
  }
  userInfo_->stop();
  shareWorker_->stop();
  if (management_) {
    management_->stop();
  }
}

void StratumServer::stopGracefully() {
  LOG(INFO) << "stop stratum server gracefully";

  // Stop listening & trigger gracefully disconnecting timer
  evconnlistener_disable(listener_);
  if (connections_.empty()) {
    dispatch([this]() { stop(); });
  } else {
    timeval timeout;
    timeout.tv_sec = shutdownGracePeriod_ / connections_.size();
    timeout.tv_usec =
        (shutdownGracePeriod_ - timeout.tv_sec * connections_.size()) *
        1000000 / connections_.size();
    event_add(disconnectTimer_, &timeout);
  }
}

namespace {

class StratumServerTask {
public:
  StratumServerTask(event_base *base, std::function<void()> task)
    : task_{move(task)}
    , event_{event_new(base, -1, 0, &StratumServerTask::execute, this)} {
    event_add(event_, nullptr);
    event_active(event_, EV_TIMEOUT, 0);
  }

  static void execute(evutil_socket_t, short, void *context) {
    delete static_cast<StratumServerTask *>(context);
  }

private:
  ~StratumServerTask() {
    task_();
    event_free(event_);
  }

  std::function<void()> task_;
  struct event *event_;
};

} // namespace

void StratumServer::dispatch(std::function<void()> task) {
  if (!task) {
    return;
  }

  new StratumServerTask{base_, move(task)};
}

void StratumServer::dispatchSafely(
    std::function<void()> task, std::weak_ptr<bool> alive) {
  if (!task) {
    return;
  }

  dispatch([task = std::move(task), alive = std::move(alive)]() {
    if (!alive.expired()) {
      task();
    }
  });
}

void StratumServer::dispatchToShareWorker(std::function<void()> work) {
  shareWorker_->dispatch(std::move(work));
}

size_t StratumServer::switchChain(string userName, size_t newChainId) {
  size_t onlineSessions = 0;
  for (auto &itr : connections_) {
    if (itr->getState() == StratumSession::AUTHENTICATED &&
        itr->getUserName() == userName) {
      onlineSessions++;
      if (itr->getChainId() != newChainId) {
        itr->switchChain(newChainId);
      }
    }
  }
  return onlineSessions;
}

size_t StratumServer::autoSwitchChain(size_t newChainId) {
  size_t switchedSessions = 0;
  for (auto &itr : connections_) {
    if (itr->getState() == StratumSession::AUTHENTICATED &&
        userInfo_->userAutoSwitchChainEnabled(itr->getUserName()) &&
        itr->getChainId() != newChainId) {
      switchedSessions++;
      itr->switchChain(newChainId);
    }
  }
  return switchedSessions;
}

size_t StratumServer::autoRegCallback(const string &userName) {
  size_t sessions = 0;
  for (auto &itr : connections_) {
    if (itr->autoRegCallback(userName)) {
      sessions++;
    }
  }
  return sessions;
}

void StratumServer::sendMiningNotifyToAll(shared_ptr<StratumJobEx> exJobPtr) {
  //
  // http://www.sgi.com/tech/stl/Map.html
  //
  // Map has the important property that inserting a new element into a map
  // does not invalidate iterators that point to existing elements. Erasing
  // an element from a map also does not invalidate any iterators, except,
  // of course, for iterators that actually point to the element that is
  // being erased.
  //

  if (grandPoolEnabled_ && exJobPtr->isClean_) {
    auto itr = connections_.begin();
    while (itr != connections_.end()) {
      auto &conn = *itr;
      if (conn->isGrandPoolClient() && (!conn->isDead())) {
        if (conn->getChainId() == exJobPtr->chainId_) {
          conn->sendMiningNotify(exJobPtr);
        }
      }
      ++itr;
    }
  }

  auto itr = connections_.begin();
  while (itr != connections_.end()) {
    auto &conn = *itr;
    if (conn->isDead()) {
#ifndef WORK_WITH_STRATUM_SWITCHER
      sessionIDManager_->freeSessionId(conn->getSessionId());
#endif
      itr = connections_.erase(itr);
    } else if (
        grandPoolEnabled_ && exJobPtr->isClean_ && conn->isGrandPoolClient()) {
      ++itr;
    } else {
      if (conn->getChainId() == exJobPtr->chainId_) {
        conn->sendMiningNotify(exJobPtr);
      }
      ++itr;
    }
  }
}

void StratumServer::addConnection(unique_ptr<StratumSession> connection) {
  connections_.insert(move(connection));
}

void StratumServer::removeConnection(StratumSession &connection) {
  //
  // if we are here, means the related evbuffer has already been locked.
  // don't lock connsLock_ in this function, it will cause deadlock.
  //
  connection.markAsDead();
}

void StratumServer::listenerCallback(
    struct evconnlistener *listener,
    evutil_socket_t fd,
    struct sockaddr *saddr,
    int socklen,
    void *data) {
  StratumServer *server = static_cast<StratumServer *>(data);
  struct event_base *base = (struct event_base *)server->base_;
  struct bufferevent *bev;
  uint32_t sessionID = 0u;

#ifndef WORK_WITH_STRATUM_SWITCHER
  // can't alloc session Id
  if (server->sessionIDManager_->allocSessionId(&sessionID) == false) {
    close(fd);
    return;
  }
#endif

  // Theoretically we can do it on the listener fd, but this is to make
  // sure we have the same behavior if some distro does not inherit the option.
  int yes = 1;
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(int));

  // When we want to close the connection we want to send RST instead of FIN so
  // that miners will be disconnected immediately.
  linger lingerOn{1, 0};
  setsockopt(fd, SOL_SOCKET, SO_LINGER, &lingerOn, sizeof(struct linger));

  if (server->enableTLS_) {
    SSL *ssl = SSL_new(server->sslCTX_);
    if (ssl == nullptr) {
      LOG(ERROR) << "Error calling SSL_new!";
      server->stop();
      return;
    }

    bev = bufferevent_openssl_socket_new(
        base,
        fd,
        ssl,
        BUFFEREVENT_SSL_ACCEPTING,
        BEV_OPT_CLOSE_ON_FREE | BEV_OPT_THREADSAFE);
  } else {
    bev = bufferevent_socket_new(
        base, fd, BEV_OPT_CLOSE_ON_FREE | BEV_OPT_THREADSAFE);
  }

  // If it was NULL with flag BEV_OPT_THREADSAFE,
  // please call evthread_use_pthreads() before you call event_base_new().
  if (bev == nullptr) {
    LOG(ERROR) << "Error constructing bufferevent! Maybe you forgot call "
                  "evthread_use_pthreads() before event_base_new().";
    server->stop();
    return;
  }

  // create stratum session
  auto conn = server->createConnection(bev, saddr, sessionID);
  if (!conn->initialize()) {
    return;
  }
  // set callback functions
  bufferevent_setcb(
      bev,
      StratumServer::readCallback,
      nullptr,
      StratumServer::eventCallback,
      conn.get());
  // By default, a newly created bufferevent has writing enabled.
  bufferevent_enable(bev, EV_READ | EV_WRITE);

  server->addConnection(move(conn));
}

void StratumServer::disconnectCallback(int, short, void *context) {
  auto server = static_cast<StratumServer *>(context);
  if (server->connections_.empty()) {
    server->stop();
  } else {
    auto iter = server->connections_.begin();
    auto iend = server->connections_.end();
    StratumSession::State state;
    do {
      state = (*iter)->getState();
      iter = server->connections_.erase(iter);
    } while (state < StratumSession::AUTHENTICATED && iter != iend);
  }
}

void StratumServer::readCallback(struct bufferevent *bev, void *connection) {
  auto conn = static_cast<StratumSession *>(connection);
  conn->readBuf(bufferevent_get_input(bev));
}

void StratumServer::eventCallback(
    struct bufferevent *bev, short events, void *connection) {
  auto conn = static_cast<StratumSession *>(connection);
  auto state = conn->getState();

  if (conn->getServer().enableTLS_) {
    if (events & BEV_EVENT_CONNECTED) {
      DLOG(INFO) << "TLS connected";
      return;
    }
  } else {
    // should not be 'BEV_EVENT_CONNECTED'
    assert((events & BEV_EVENT_CONNECTED) != BEV_EVENT_CONNECTED);
  }

  if (events & BEV_EVENT_EOF) {
    LOG_IF(INFO, state != StratumSession::CONNECTED) << "socket closed";
  } else if (events & BEV_EVENT_ERROR) {
    LOG_IF(INFO, state != StratumSession::CONNECTED)
        << "got an error on the socket: "
        << evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR());
  } else if (events & BEV_EVENT_TIMEOUT) {
    LOG_IF(INFO, state != StratumSession::CONNECTED)
        << "socket read/write timeout, events: " << events;
  } else {
    LOG_IF(ERROR, state != StratumSession::CONNECTED)
        << "unhandled socket events: " << events;
  }
  conn->getServer().removeConnection(*conn);
}

void StratumServer::sendShare2Kafka(
    size_t chainId, const char *data, size_t len) {
  chains_[chainId].kafkaProducerShareLog_->produce(data, len);
}

void StratumServer::sendSolvedShare2Kafka(
    size_t chainId, const char *data, size_t len) {
  chains_[chainId].kafkaProducerSolvedShare_->produce(data, len);
}

void StratumServer::sendCommonEvents2Kafka(
    size_t chainId, const string &message) {
  chains_[chainId].kafkaProducerCommonEvents_->produce(
      message.data(), message.size());
}

bool StratumServer::logHideIpPrefix(const string &ip) {
  if (logHideIpPrefix_.empty()) {
    return false;
  }
  if (ip.size() < logHideIpPrefix_.size()) {
    return false;
  }
  return ip.substr(0, logHideIpPrefix_.size()) == logHideIpPrefix_;
}

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
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <err.h>
#include <errno.h>
#include <unistd.h>

#include <iostream>

#include <boost/interprocess/sync/file_lock.hpp>
#include <glog/logging.h>
#include <libconfig.h++>

#include "zmq.hpp"

#include "config/bpool-version.h"
#include "Utils.h"
#include "JobMaker.h"
#include "Zookeeper.h"

#include "bitcoin/BitcoinUtils.h"
#include "bitcoin/JobMakerBitcoin.h"
#include "eth/JobMakerEth.h"
#include "bytom/JobMakerBytom.h"
#include "sia/JobMakerSia.h"
#include "decred/JobMakerDecred.h"
#include "ckb/JobMakerCkb.h"

#include <chainparams.h>

using namespace std;
using namespace libconfig;

static vector<shared_ptr<JobMaker>> gJobMakers;

void handler(int sig) {
  for (auto jobMaker : gJobMakers) {
    if (jobMaker)
      jobMaker->stop();
  }
}

void usage() {
  fprintf(stderr, BIN_VERSION_STRING("jobmaker"));
  fprintf(
      stderr, "Usage:\tjobmaker -c \"jobmaker.cfg\" [-l <log_dir|stderr>]\n");
}

bool isGwChain(const string &chainType) {
  return (
      "ETH" == chainType || "SIA" == chainType || "BTM" == chainType ||
      "DCR" == chainType || "CKB" == chainType);
}

shared_ptr<JobMakerHandler>
createGwJobMakerHandler(shared_ptr<GwJobMakerDefinition> def) {
  shared_ptr<GwJobMakerHandler> handler;

  if (def->chainType_ == "ETH")
    handler = make_shared<JobMakerHandlerEth>();
  else if (def->chainType_ == "SIA")
    handler = make_shared<JobMakerHandlerSia>();
  else if (def->chainType_ == "BTM")
    handler = make_shared<JobMakerHandlerBytom>();
  else if (def->chainType_ == "DCR")
    handler = make_shared<JobMakerHandlerDecred>();
  else if (def->chainType_ == "CKB")
    handler = make_shared<JobMakerHandlerCkb>();
  else
    LOG(FATAL) << "unknown chain type: " << def->chainType_;

  handler->init(def);

  return handler;
}

shared_ptr<JobMakerHandler>
createGbtJobMakerHandler(shared_ptr<GbtJobMakerDefinition> def) {
  shared_ptr<JobMakerHandlerBitcoin> handler;

  if (def->chainType_ == CHAIN_TYPE_STR)
    handler = make_shared<JobMakerHandlerBitcoin>();
  else
    LOG(FATAL) << "unknown chain type: " << def->chainType_;

  handler->init(def);

  return handler;
}

shared_ptr<GwJobMakerDefinition>
createGwJobMakerDefinition(const Setting &setting) {
  shared_ptr<GwJobMakerDefinition> def;

  string chainType;
  readFromSetting(setting, "chain_type", chainType, true);

  if (chainType == "ETH") {
    string chainName;
    readFromSetting(setting, "chain_name", chainName);
    const EthConsensus::Chain chain = EthConsensus::getChain(chainName);

    if (chain == EthConsensus::Chain::UNKNOWN) {
      LOG(FATAL) << "Unknown ETH chain_name: " << chainName;
      return nullptr;
    }

    auto defEth = make_shared<JobMakerDefinitionEth>();
    defEth->chain_ = chain;
    def = defEth;
  } else {
    def = make_shared<GwJobMakerDefinition>();
  }

  def->chainType_ = chainType;

  readFromSetting(setting, "rawgw_topic", def->rawGwTopic_);
  readFromSetting(setting, "job_topic", def->jobTopic_);

  readFromSetting(setting, "job_interval", def->jobInterval_);
  readFromSetting(setting, "max_job_delay", def->maxJobDelay_);
  readFromSetting(setting, "work_life_time", def->workLifeTime_);

  readFromSetting(setting, "zookeeper_lock_path", def->zookeeperLockPath_);
  readFromSetting(setting, "file_last_job_time", def->fileLastJobTime_, true);
  readFromSetting(setting, "id", def->serverId_);

  def->enabled_ = false;
  readFromSetting(setting, "enabled", def->enabled_, true);

  return def;
}

shared_ptr<GbtJobMakerDefinition>
createGbtJobMakerDefinition(const Setting &setting) {
  shared_ptr<GbtJobMakerDefinition> def = make_shared<GbtJobMakerDefinition>();

  readFromSetting(setting, "chain_type", def->chainType_);
  readFromSetting(setting, "testnet", def->testnet_);

  readFromSetting(setting, "payout_address", def->payoutAddr_);
  readFromSetting(setting, "coinbase_info", def->coinbaseInfo_);
  readFromSetting(setting, "block_version", def->blockVersion_);

  // Share jobs with the main pool, but with different coinbase information and
  // addresses.
  if (setting.exists("subpool")) {
    const auto &subpool = setting["subpool"];
    bool subPoolEnabled = false;
    subpool.lookupValue("enabled", subPoolEnabled);

    if (subPoolEnabled) {
      subpool.lookupValue("coinbase_info_max_len", def->subPoolCoinbaseMaxLen_);
      if (def->subPoolCoinbaseMaxLen_ < 1) {
        def->subPoolCoinbaseMaxLen_ = 30;
      }

      // select chain
      if (def->testnet_) {
        SelectParams(CBaseChainParams::TESTNET);
        LOG(WARNING) << "[subpool] using bitcoin testnet3";
      } else {
        SelectParams(CBaseChainParams::MAIN);
      }

      const auto &pools = subpool.lookup("pools");
      for (int i = 0; i < pools.getLength(); i++) {
        const auto &pool = pools[i];

        SubPoolInfo info;

        pool.lookupValue("zk_update_path", info.zkUpdatePath_); // optional

        info.name_ = pool.lookup("name").operator string();
        info.coinbaseInfo_ = pool.lookup("coinbase_info").operator string();

        string payoutAddr = pool.lookup("payout_address").operator string();
        if (!BitcoinUtils::IsValidDestinationString(payoutAddr)) {
          LOG(FATAL) << "[subpool " << info.name_
                     << "] invalid pool payout address";
        } else {
          LOG(INFO) << "[subpool " << info.name_
                    << "] Payout Address: " << payoutAddr;
        }
        info.payoutAddr_ = BitcoinUtils::DecodeDestination(payoutAddr);

        def->subPool_.emplace_back(std::move(info));
      }
    }
  }

  readFromSetting(setting, "rawgbt_topic", def->rawGbtTopic_);
  readFromSetting(setting, "auxpow_gw_topic", def->auxPowGwTopic_);
  readFromSetting(setting, "rsk_rawgw_topic", def->rskRawGwTopic_);
  readFromSetting(setting, "vcash_rawgw_topic", def->vcashRawGwTopic_);
  readFromSetting(setting, "job_topic", def->jobTopic_);

  readFromSetting(setting, "job_interval", def->jobInterval_);
  readFromSetting(setting, "max_job_delay", def->maxJobDelay_);
  readFromSetting(setting, "gbt_life_time", def->gbtLifeTime_);
  readFromSetting(setting, "empty_gbt_life_time", def->emptyGbtLifeTime_);

  def->auxmergedMiningNotifyPolicy_ = 1;
  readFromSetting(
      setting,
      "aux_merged_mining_notify",
      def->auxmergedMiningNotifyPolicy_,
      true);

  def->rskmergedMiningNotifyPolicy_ = 1;
  readFromSetting(
      setting,
      "rsk_merged_mining_notify",
      def->rskmergedMiningNotifyPolicy_,
      true);

  def->vcashmergedMiningNotifyPolicy_ = 1;
  readFromSetting(
      setting,
      "vcash_merged_mining_notify",
      def->vcashmergedMiningNotifyPolicy_,
      true);

  readFromSetting(setting, "zookeeper_lock_path", def->zookeeperLockPath_);
  readFromSetting(setting, "file_last_job_time", def->fileLastJobTime_, true);
  readFromSetting(setting, "id", def->serverId_);

  def->enabled_ = false;
  readFromSetting(setting, "enabled", def->enabled_, true);

  def->grandPoolEnabled_ = false;
  readFromSetting(setting, "grandPoolEnabled", def->grandPoolEnabled_, true);

  return def;
}

void createJobMakers(
    const libconfig::Config &cfg,
    const string &kafkaBrokers,
    const string &zkBrokers,
    vector<shared_ptr<JobMaker>> &makers) {
  const Setting &root = cfg.getRoot();
  const Setting &workerDefs = root["job_workers"];

  for (int i = 0; i < workerDefs.getLength(); i++) {
    string chainType;
    readFromSetting(workerDefs[i], "chain_type", chainType);

    if (isGwChain(chainType)) {
      auto def = createGwJobMakerDefinition(workerDefs[i]);

      if (!def->enabled_) {
        LOG(INFO) << "chain: " << def->chainType_
                  << ", topic: " << def->jobTopic_ << ", disabled.";
        continue;
      }

      LOG(INFO) << "chain: " << def->chainType_ << ", topic: " << def->jobTopic_
                << ", enabled.";

      auto handle = createGwJobMakerHandler(def);
      makers.push_back(
          std::make_shared<JobMaker>(handle, kafkaBrokers, zkBrokers));
    } else {
      auto def = createGbtJobMakerDefinition(workerDefs[i]);

      if (!def->enabled_) {
        LOG(INFO) << "chain: " << def->chainType_
                  << ", topic: " << def->jobTopic_ << ", disabled.";
        continue;
      }

      LOG(INFO) << "chain: " << def->chainType_ << ", topic: " << def->jobTopic_
                << ", enabled.";

      auto handle = createGbtJobMakerHandler(def);
      auto jobmaker =
          std::make_shared<JobMaker>(handle, kafkaBrokers, zkBrokers);
      handle->setParent(jobmaker);
      makers.push_back(jobmaker);
    }
  }
}

void workerThread(shared_ptr<JobMaker> jobmaker) {
  if (!jobmaker->init()) {
    LOG(FATAL) << "jobmaker init failure.";
  }

  jobmaker->run();
}

int main(int argc, char **argv) {
  char *optLogDir = NULL;
  char *optConf = NULL;
  int c;

  if (argc <= 1) {
    usage();
    return 1;
  }
  while ((c = getopt(argc, argv, "c:l:h")) != -1) {
    switch (c) {
    case 'c':
      optConf = optarg;
      break;
    case 'l':
      optLogDir = optarg;
      break;
    case 'h':
    default:
      usage();
      exit(0);
    }
  }

  // Initialize Google's logging library.
  google::InitGoogleLogging(argv[0]);
  if (optLogDir == NULL || strcmp(optLogDir, "stderr") == 0) {
    FLAGS_logtostderr = 1;
  } else {
    FLAGS_log_dir = string(optLogDir);
  }
  // Log messages at a level >= this flag are automatically sent to
  // stderr in addition to log files.
  FLAGS_stderrthreshold = 3; // 3: FATAL
  FLAGS_max_log_size = 100; // max log file size 100 MB
  FLAGS_logbuflevel = -1; // don't buffer logs
  FLAGS_stop_logging_if_full_disk = true;

  LOG(INFO) << BIN_VERSION_STRING("jobmaker");

  // Read the file. If there is an error, report it and exit.
  libconfig::Config cfg;
  try {
    cfg.readFile(optConf);
  } catch (const FileIOException &fioex) {
    std::cerr << "I/O error while reading file." << std::endl;
    return (EXIT_FAILURE);
  } catch (const ParseException &pex) {
    std::cerr << "Parse error at " << pex.getFile() << ":" << pex.getLine()
              << " - " << pex.getError() << std::endl;
    return (EXIT_FAILURE);
  }

  // lock cfg file:
  //    you can't run more than one process with the same config file
  /*boost::interprocess::file_lock pidFileLock(optConf);
  if (pidFileLock.try_lock() == false) {
    LOG(FATAL) << "lock cfg file fail";
    return(EXIT_FAILURE);
  }*/

  signal(SIGTERM, handler);
  signal(SIGINT, handler);

  try {
    vector<shared_ptr<thread>> workers;

    string kafkaBrokers;
    string zkBrokers;
    readFromSetting(cfg, "kafka.brokers", kafkaBrokers);
    readFromSetting(cfg, "zookeeper.brokers", zkBrokers);

    // create JobMaker
    createJobMakers(cfg, kafkaBrokers, zkBrokers, gJobMakers);

    // init & run JobMaker
    for (auto jobmaker : gJobMakers) {
      workers.push_back(std::make_shared<thread>(workerThread, jobmaker));
    }

    // wait threads exiting
    for (auto pWorker : workers) {
      if (pWorker->joinable()) {
        LOG(INFO) << "wait for worker " << pWorker->get_id() << " exiting";
        pWorker->join();
        LOG(INFO) << "worker exited";
      }
    }

  } catch (const SettingException &e) {
    LOG(FATAL) << "config missing: " << e.getPath();
    return 1;
  }

  google::ShutdownGoogleLogging();
  return 0;
}

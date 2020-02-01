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

#include "config/bpool-version.h"
#include "Utils.h"

#include "bitcoin/BlockMakerBitcoin.h"
#include "eth/EthConsensus.h"
#include "eth/BlockMakerEth.h"
#include "bytom/BlockMakerBytom.h"
#include "sia/BlockMakerSia.h"
#include "decred/BlockMakerDecred.h"
#include "ckb/BlockMakerCkb.h"

using namespace std;
using namespace libconfig;

vector<shared_ptr<BlockMaker>> makers; // *gBlockMaker = nullptr;

void handler(int sig) {
  for (auto maker : makers) {
    if (maker)
      maker->stop();
  }
}

void usage() {
  fprintf(stderr, BIN_VERSION_STRING("blkmaker"));
  fprintf(
      stderr, "Usage:\tblkmaker -c \"blkmaker.cfg\" [-l <log_dir|stderr>]\n");
}

// BlockMaker* createBlockMaker(Config& cfg, MysqlConnectInfo* poolDBInfo) {
//   string type = cfg.lookup("blockmaker.type");
//   string broker = cfg.lookup("kafka.brokers");

//   BlockMaker *maker = nullptr;
//   if ("BTC" == type)
//     maker = new BlockMaker(broker.c_str(), *poolDBInfo);
//   else
//     maker = new BlockMakerEth(broker.c_str(), *poolDBInfo);

//   return maker;
// }

BlockMaker *createBlockMaker(
    shared_ptr<BlockMakerDefinition> def,
    const string &broker,
    MysqlConnectInfo *poolDBInfo) {
  BlockMaker *maker = nullptr;
#if defined(CHAIN_TYPE_STR)
  if (CHAIN_TYPE_STR == def->chainType_)
#else
  if (false)
#endif
    maker = new BlockMakerBitcoin(def, broker.c_str(), *poolDBInfo);
  else if ("ETH" == def->chainType_)
    maker = new BlockMakerEth(def, broker.c_str(), *poolDBInfo);
  else if ("SIA" == def->chainType_)
    maker = new BlockMakerSia(def, broker.c_str(), *poolDBInfo);
  else if ("BTM" == def->chainType_)
    maker = new BlockMakerBytom(def, broker.c_str(), *poolDBInfo);
  else if ("DCR" == def->chainType_)
    maker = new BlockMakerDecred(def, broker.c_str(), *poolDBInfo);
  else if ("CKB" == def->chainType_)
    maker = new BlockMakerCkb(def, broker.c_str(), *poolDBInfo);
  return maker;
}

shared_ptr<BlockMakerDefinition> createDefinition(const Setting &setting) {
  string chainType;
  shared_ptr<BlockMakerDefinition> def;

  readFromSetting(setting, "chain_type", chainType);

  // The hard fork Constantinople of Ethereum mainnet
  if (chainType == "ETH") {
    int constantinopleHeight = 7280000;
    setting.lookupValue("constantinople_height", constantinopleHeight);
    EthConsensus::setHardForkConstantinopleHeight(constantinopleHeight);
  }

#if defined(CHAIN_TYPE_STR)
  if (CHAIN_TYPE_STR == chainType)
#else
  if (false)
#endif
  {
    shared_ptr<BlockMakerDefinitionBitcoin> bitcoinDef =
        std::make_shared<BlockMakerDefinitionBitcoin>();

    readFromSetting(setting, "job_topic", bitcoinDef->stratumJobTopic_);
    readFromSetting(setting, "rawgbt_topic", bitcoinDef->rawGbtTopic_);
    readFromSetting(
        setting,
        "auxpow_solved_share_topic",
        bitcoinDef->auxPowSolvedShareTopic_);
    readFromSetting(
        setting, "rsk_solved_share_topic", bitcoinDef->rskSolvedShareTopic_);

    def = bitcoinDef;
  } else {
    def = std::make_shared<BlockMakerDefinition>();
  }

  def->chainType_ = chainType;
  def->enabled_ = false;
  readFromSetting(setting, "enabled", def->enabled_, true);
  readFromSetting(setting, "solved_share_topic", def->solvedShareTopic_);
  readFromSetting(
      setting, "found_aux_block_table", def->foundAuxBlockTable_, true);
  readFromSetting(setting, "aux_chain_name", def->auxChainName_, true);

  const Setting &nodes = setting["nodes"];
  for (int i = 0; i < nodes.getLength(); ++i) {
    const Setting &nodeSetting = nodes[i];
    NodeDefinition nodeDef;
    readFromSetting(nodeSetting, "rpc_addr", nodeDef.rpcAddr_);
    readFromSetting(nodeSetting, "rpc_userpwd", nodeDef.rpcUserPwd_);
    def->nodes.push_back(nodeDef);
  }

  return def;
}

// shared_ptr<BlockMakerHandler> createBlockMakerHandler(const
// BlockMakerDefinition &def)
// {
//   shared_ptr<BlockMakerHandler> handler;

//   if (def->chainType_ == "ETH")
//     handler = make_shared<BlockMakerHandler>();
//   else if (def->chainType_ == "SIA")
//     handler = make_shared<BlockMakerHandler>();
//   else if (def->chainType_ == "RSK")
//     handler = make_shared<BlockMakerHandler>();
//   else
//     LOG(FATAL) << "unknown chain type: " << def->chainType_;

//   handler->init(def);

//   return handler;
// }

void createBlockMakers(
    const libconfig::Config &cfg, MysqlConnectInfo *poolDBInfo) {
  string broker = cfg.lookup("kafka.brokers");
  const Setting &root = cfg.getRoot();
  const Setting &makerDefs = root["blk_makers"];

  for (int i = 0; i < makerDefs.getLength(); ++i) {
    auto def = createDefinition(makerDefs[i]);
    if (!def->enabled_) {
      LOG(INFO) << "chain: " << def->chainType_
                << ", topic: " << def->solvedShareTopic_ << ", disabled.";
      continue;
    }
    LOG(INFO) << "chain: " << def->chainType_
              << ", topic: " << def->solvedShareTopic_ << ", enabled.";
    // auto handler = createBlockMakerHandler(def);
    // makers.push_back(std::make_shared<BlockMaker>(broker.c_str(),
    // *poolDBInfo));
    shared_ptr<BlockMaker> maker(createBlockMaker(def, broker, poolDBInfo));
    makers.push_back(maker);
  }
}

void workerThread(shared_ptr<BlockMaker> maker) {
  if (maker != nullptr)
    maker->run();
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

  LOG(INFO) << BIN_VERSION_STRING("blkmaker");

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

  bool sslVerifyPeer = true;
  cfg.lookupValue("curl.ssl_verify_peer", sslVerifyPeer);
  setSslVerifyPeer(sslVerifyPeer);

  MysqlConnectInfo *poolDBInfo = nullptr;
  {
    int32_t poolDBPort = 3306;
    cfg.lookupValue("pooldb.port", poolDBPort);
    poolDBInfo = new MysqlConnectInfo(
        cfg.lookup("pooldb.host"),
        poolDBPort,
        cfg.lookup("pooldb.username"),
        cfg.lookup("pooldb.password"),
        cfg.lookup("pooldb.dbname"));
  }

  createBlockMakers(cfg, poolDBInfo);

  try {
    vector<shared_ptr<thread>> workers;
    for (auto maker : makers) {
      if (maker->init()) {
        workers.push_back(std::make_shared<thread>(workerThread, maker));
      } else {
        LOG(FATAL) << "blkmaker init failure, chain: "
                   << maker->def()->chainType_;
      }
    }

    // run GwMaker
    for (auto pWorker : workers) {
      if (pWorker->joinable()) {
        LOG(INFO) << "wait for worker " << pWorker->get_id();
        pWorker->join();
        LOG(INFO) << "worker exit";
      }
    }
  } catch (const SettingException &e) {
    LOG(FATAL) << "config missing: " << e.getPath();
    return 1;
  }

  google::ShutdownGoogleLogging();
  return 0;
}

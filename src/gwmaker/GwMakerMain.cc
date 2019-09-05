/*
 The MIT License (MIT)

 Copyright (C) 2017 RSK Labs Ltd.

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

#include "GwMaker.h"
#include "bytom/GwMakerBytom.h"
#include "decred/GwMakerDecred.h"
#include "eth/GwMakerEth.h"
#include "rsk/GwMakerRsk.h"
#include "sia/GwMakerSia.h"
#include "vcash/GwMakerVcash.h"

using namespace std;
using namespace libconfig;

static vector<shared_ptr<GwMaker>> gGwMakers;

void handler(int sig) {
  for (auto gwMaker : gGwMakers) {
    if (gwMaker)
      gwMaker->stop();
  }
}

void usage() {
  fprintf(stderr, BIN_VERSION_STRING("gwmaker"));
  fprintf(stderr, "Usage:\tgwmaker -c \"gwmaker.cfg\" [-l <log_dir|stderr>]\n");
}

shared_ptr<GwMakerHandler> createGwMakerHandler(const GwMakerDefinition &def) {
  shared_ptr<GwMakerHandler> handler;

  if (def.chainType_ == "ETH")
    handler = make_shared<GwMakerHandlerEth>();
  else if (def.chainType_ == "SIA")
    handler = make_shared<GwMakerHandlerSia>();
  else if (def.chainType_ == "RSK")
    handler = make_shared<GwMakerHandlerRsk>();
  else if (def.chainType_ == "BTM")
    handler = make_shared<GwMakerHandlerBytom>();
  else if (def.chainType_ == "DCR")
    handler = make_shared<GwMakerHandlerDecred>();
  else if (def.chainType_ == "VCASH")
    handler = make_shared<GwMakerHandlerVcash>();
  else
    LOG(FATAL) << "unknown chain type: " << def.chainType_;

  handler->init(def);

  return handler;
}

GwMakerDefinition createGwMakerDefinition(const Setting &setting) {
  GwMakerDefinition def;

  readFromSetting(setting, "chain_type", def.chainType_);
  readFromSetting(setting, "rpc_addr", def.rpcAddr_);
  readFromSetting(setting, "rpc_userpwd", def.rpcUserPwd_);
  readFromSetting(setting, "rawgw_topic", def.rawGwTopic_);
  readFromSetting(setting, "rpc_interval", def.rpcInterval_);
  readFromSetting(setting, "parity_notify_host", def.notifyHost_, true);
  readFromSetting(setting, "parity_notify_port", def.notifyPort_, true);

  def.enabled_ = false;
  readFromSetting(setting, "enabled", def.enabled_, true);

  return def;
}

void createGwMakers(
    const libconfig::Config &cfg,
    const string &brokers,
    vector<shared_ptr<GwMaker>> &makers) {
  const Setting &root = cfg.getRoot();
  const Setting &workerDefs = root["gw_workers"];

  for (int i = 0; i < workerDefs.getLength(); i++) {
    GwMakerDefinition def = createGwMakerDefinition(workerDefs[i]);

    if (!def.enabled_) {
      LOG(INFO) << "chain: " << def.chainType_ << ", topic: " << def.rawGwTopic_
                << ", disabled.";
      continue;
    }

    LOG(INFO) << "chain: " << def.chainType_ << ", topic: " << def.rawGwTopic_
              << ", enabled.";

    auto handle = createGwMakerHandler(def);
    makers.push_back(std::make_shared<GwMaker>(handle, brokers));
  }
}

void workerThread(shared_ptr<GwMaker> gwMaker) {
  if (gwMaker)
    gwMaker->run();
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

  LOG(INFO) << BIN_VERSION_STRING("gwmaker");

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
  // you can't run more than one process with the same config file
  /*boost::interprocess::file_lock pidFileLock(optConf);
  if (pidFileLock.try_lock() == false) {
    LOG(FATAL) << "lock cfg file fail";
    return(EXIT_FAILURE);
  }*/

  signal(SIGTERM, handler);
  signal(SIGINT, handler);

  try {

    vector<shared_ptr<thread>> workers;
    string brokers = cfg.lookup("kafka.brokers");

    bool sslVerifyPeer = true;
    cfg.lookupValue("curl.ssl_verify_peer", sslVerifyPeer);
    setSslVerifyPeer(sslVerifyPeer);

    // create GwMaker
    createGwMakers(cfg, brokers, gGwMakers);

    // init & run GwMaker
    for (auto gwMaker : gGwMakers) {
      if (gwMaker->init()) {
        workers.push_back(std::make_shared<thread>(workerThread, gwMaker));
      } else {
        LOG(FATAL) << "gwmaker init failure, chain: " << gwMaker->getChainType()
                   << ", topic: " << gwMaker->getRawGwTopic();
      }
    }

    // wait threads exiting
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

  LOG(INFO) << "gwmaker exit";
  google::ShutdownGoogleLogging();
  return 0;
}

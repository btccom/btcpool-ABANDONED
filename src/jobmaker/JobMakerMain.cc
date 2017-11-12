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

#include "Utils.h"
#include "JobMaker.h"
#include "Zookeeper.h"

using namespace std;
using namespace libconfig;

#define JOBMAKER_LOCK_NODE_PATH "/locks/jobmaker"

JobMaker *gJobMaker = nullptr;
Zookeeper *gZookeeper = nullptr;

void handler(int sig) {
  if (gJobMaker) {
    gJobMaker->stop();
  }

  if (gZookeeper) {
    delete gZookeeper;
    gZookeeper = nullptr;
  }
}

void usage() {
  fprintf(stderr, "Usage:\n\tjobmaker -c \"jobmaker.cfg\" -l \"log_dir\"\n");
}

int main(int argc, char **argv) {
  char *optLogDir = NULL;
  char *optConf   = NULL;
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
      case 'h': default:
        usage();
        exit(0);
    }
  }

  // Initialize Google's logging library.
  google::InitGoogleLogging(argv[0]);
  FLAGS_log_dir         = string(optLogDir);
  // Log messages at a level >= this flag are automatically sent to
  // stderr in addition to log files.
  FLAGS_stderrthreshold = 3;    // 3: FATAL
  FLAGS_max_log_size    = 100;  // max log file size 100 MB
  FLAGS_logbuflevel     = -1;   // don't buffer logs
  FLAGS_stop_logging_if_full_disk = true;

  // Read the file. If there is an error, report it and exit.
  Config cfg;
  try
  {
    cfg.readFile(optConf);
  } catch(const FileIOException &fioex) {
    std::cerr << "I/O error while reading file." << std::endl;
    return(EXIT_FAILURE);
  } catch(const ParseException &pex) {
    std::cerr << "Parse error at " << pex.getFile() << ":" << pex.getLine()
    << " - " << pex.getError() << std::endl;
    return(EXIT_FAILURE);
  }

  // lock cfg file:
  //    you can't run more than one process with the same config file
  boost::interprocess::file_lock pidFileLock(optConf);
  if (pidFileLock.try_lock() == false) {
    LOG(FATAL) << "lock cfg file fail";
    return(EXIT_FAILURE);
  }

  try {
    // get lock from zookeeper
    gZookeeper = new Zookeeper(cfg.lookup("zookeeper.brokers"));
    gZookeeper->getLock(JOBMAKER_LOCK_NODE_PATH);

  } catch(const ZookeeperException &zooex) {
    LOG(FATAL) << zooex.what();
    return(EXIT_FAILURE);
  }

  signal(SIGTERM, handler);
  signal(SIGINT,  handler);

  try {
    // check if we are using testnet3
    bool isTestnet3 = true;
    cfg.lookupValue("testnet", isTestnet3);
    if (isTestnet3) {
      SelectParams(CBaseChainParams::TESTNET);
      LOG(WARNING) << "using bitcoin testnet3";
    } else {
      SelectParams(CBaseChainParams::MAIN);
    }

    string fileLastJobTime;
	string poolCoinbaseInfo;

    // with default values
    uint32_t stratumJobInterval = 20;  // seconds
    uint32_t gbtLifeTime        = 90;
    uint32_t emptyGbtLifeTime   = 15;
    uint32_t blockVersion       = 0u;

    cfg.lookupValue("jobmaker.stratum_job_interval", stratumJobInterval);
    cfg.lookupValue("jobmaker.gbt_life_time",        gbtLifeTime);
    cfg.lookupValue("jobmaker.empty_gbt_life_time",  emptyGbtLifeTime);
    cfg.lookupValue("jobmaker.file_last_job_time",   fileLastJobTime);
    cfg.lookupValue("jobmaker.block_version",        blockVersion);
	cfg.lookupValue("pool.coinbase_info",            poolCoinbaseInfo);

    gJobMaker = new JobMaker(cfg.lookup("kafka.brokers"), stratumJobInterval,
                             cfg.lookup("pool.payout_address"), gbtLifeTime,
                             emptyGbtLifeTime, fileLastJobTime, blockVersion,
							 poolCoinbaseInfo);

    if (!gJobMaker->init()) {
      LOG(FATAL) << "init failure";
    } else {
      gJobMaker->run();
    }
    delete gJobMaker;
  }
  catch (std::exception & e) {
    LOG(FATAL) << "exception: " << e.what();
    return 1;
  }

  google::ShutdownGoogleLogging();
  return 0;
}

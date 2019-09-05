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
#include "bitcoin/GbtMaker.h"

using namespace std;
using namespace libconfig;

GbtMaker *gGbtMaker = nullptr;

void handler(int sig) {
  if (gGbtMaker) {
    gGbtMaker->stop();
  }
}

void usage() {
  fprintf(stderr, BIN_VERSION_STRING("gbtmaker"));
  fprintf(
      stderr, "Usage:\tgbtmaker -c \"gbtmaker.cfg\" [-l <log_dir|stderr>]\n");
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

  LOG(INFO) << BIN_VERSION_STRING("gbtmaker");

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
    bool isCheckZmq = true;
    cfg.lookupValue("gbtmaker.is_check_zmq", isCheckZmq);
    int32_t rpcCallInterval = 5;
    cfg.lookupValue("gbtmaker.rpcinterval", rpcCallInterval);
    gGbtMaker = new GbtMaker(
        cfg.lookup("bitcoind.zmq_addr"),
        cfg.lookup("bitcoind.zmq_timeout"),
        cfg.lookup("bitcoind.rpc_addr"),
        cfg.lookup("bitcoind.rpc_userpwd"),
        cfg.lookup("kafka.brokers"),
        cfg.lookup("gbtmaker.rawgbt_topic"),
        rpcCallInterval,
        isCheckZmq);

    if (!gGbtMaker->init()) {
      LOG(FATAL) << "gbtmaker init failure";
    } else {
#if defined(CHAIN_TYPE_BCH) || defined(CHAIN_TYPE_BSV)
      bool runLightGbt = false;
      cfg.lookupValue("gbtmaker.lightgbt", runLightGbt);
      if (runLightGbt) {
        gGbtMaker->runLightGbt();
      } else {
        gGbtMaker->run();
      }
#else
      gGbtMaker->run();
#endif
    }
    delete gGbtMaker;
  } catch (const SettingException &e) {
    LOG(FATAL) << "config missing: " << e.getPath();
    return 1;
  }

  google::ShutdownGoogleLogging();
  return 0;
}

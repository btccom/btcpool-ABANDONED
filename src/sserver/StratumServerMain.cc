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
#include "StratumServer.h"
#include "bitcoin/StratumServerBitcoin.h"
#include "eth/StratumServerEth.h"
#include "bytom/StratumServerBytom.h"
#include "sia/StratumServerSia.h"
#include "decred/StratumServerDecred.h"
#include "beam/StratumServerBeam.h"
#include "grin/StratumServerGrin.h"
#include "ckb/StratumServerCkb.h"

#include <chainparams.h>

using namespace std;
using namespace libconfig;

StratumServer *gStratumServer = nullptr;

void handler(int sig) {
  if (gStratumServer) {
    if (sig == SIGTERM) {
      gStratumServer->stopGracefully();
    } else {
      gStratumServer->stop();
    }
  }
}

void usage() {
  fprintf(stderr, BIN_VERSION_STRING("sserver"));
  fprintf(stderr, "Usage:\tsserver -c \"sserver.cfg\" [-l <log_dir|stderr>]\n");
}

StratumServer *createStratumServer(const libconfig::Config &config) {
  string type = config.lookup("sserver.type");
  LOG(INFO) << "createServer type: " << type;
#if defined(CHAIN_TYPE_STR)
  if (CHAIN_TYPE_STR == type)
#else
  if (false)
#endif
    return new ServerBitcoin();
  else if ("ETH" == type)
    return new ServerEth();
  else if ("SIA" == type)
    return new ServerSia();
  else if ("BTM" == type)
    return new ServerBytom();
  else if ("DCR" == type)
    return new ServerDecred();
  else if ("BEAM" == type)
    return new ServerBeam();
  else if ("GRIN" == type)
    return new StratumServerGrin();
  else if ("CKB" == type)
    return new StratumServerCkb();

  LOG(FATAL) << "Unknown type: " << type;
  return nullptr;
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

  LOG(INFO) << BIN_VERSION_STRING("sserver");

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
  if (pidFileLock.try_lock() == false)
  {
    LOG(FATAL) << "lock cfg file fail";
    return (EXIT_FAILURE);
  }*/

  signal(SIGTERM, handler);
  signal(SIGINT, handler);
  // ignore SIGPIPE, avoiding process be killed
  signal(SIGPIPE, SIG_IGN);

  try {
    // check if we are using testnet3
    bool isTestnet3 = false;
    cfg.lookupValue("testnet", isTestnet3);
    if (isTestnet3) {
      SelectParams(CBaseChainParams::TESTNET);
      LOG(WARNING) << "using bitcoin testnet3";
    } else {
      SelectParams(CBaseChainParams::MAIN);
    }

    gStratumServer = createStratumServer(cfg);

    if (!gStratumServer->setup(cfg)) {
      LOG(FATAL) << "stratum server setup failure";
      return 1;
    }

    gStratumServer->run();

    delete gStratumServer;
  } catch (const SettingException &e) {
    LOG(FATAL) << "config missing: " << e.getPath();
    return 1;
  }

  google::ShutdownGoogleLogging();
  return 0;
}

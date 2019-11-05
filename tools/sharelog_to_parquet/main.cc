/*
 The MIT License (MIT)

 Copyright (c) [2018] [BTC.COM]

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
#include <fstream>

#include <glog/logging.h>
#include <libconfig.h++>

#include "utils.hpp"
#include "shares.hpp"
#include "ShareLogParser.h"

using namespace std;
using namespace libconfig;

//-------------------------------------------------------------------------------------

std::shared_ptr<ShareLogParserServer> gShareLogParserServer = nullptr;

void handler(int sig) {
  if (gShareLogParserServer) {
    gShareLogParserServer->stop();
  }
}

void usage() {
  fprintf(
      stderr,
      "Usage:\n\tshare_to_parquet -c \"share_to_parquet.cfg\" -l "
      "\"log_dir\"\n");
  fprintf(
      stderr,
      "\tshare_to_parquet -c \"share_to_parquet.cfg\" -l \"log_dir2\" -d "
      "\"20160830\"\n");
}

std::shared_ptr<ShareLogParser> newShareLogParser(
    const string &chainType, time_t timestamp, const libconfig::Config &cfg) {
  if (chainType == "BTC" || chainType == "BCH" || chainType == "BSV" ||
      chainType == "UBTC") {
    BitcoinDifficulty::DiffOneBits = 0x1d00ffff;
    return std::make_shared<ShareLogParserBitcoin>(cfg, timestamp, chainType);
  } else if (chainType == "LTC") {
    BitcoinDifficulty::DiffOneBits = 0x1f00ffff;
    return std::make_shared<ShareLogParserBitcoin>(cfg, timestamp, chainType);
  } else if (chainType == "ZEC") {
    BitcoinDifficulty::DiffOneBits = 0x1f07ffff;
    return std::make_shared<ShareLogParserBitcoin>(cfg, timestamp, chainType);
  } else if (chainType == "BEAM") {
    return std::make_shared<ShareLogParserBeam>(cfg, timestamp, chainType);
  } else if (chainType == "ETH" || chainType == "ETC") {
    return std::make_shared<ShareLogParserEth>(cfg, timestamp, "ETH");
  } else {
    LOG(FATAL) << "newShareLogParser: unknown chain type " << chainType;
    return nullptr;
  }
}

std::shared_ptr<ShareLogParserServer>
newShareLogParserServer(const string &chainType, const libconfig::Config &cfg) {
  if (chainType == "BTC" || chainType == "BCH" || chainType == "BSV" ||
      chainType == "UBTC") {
    BitcoinDifficulty::DiffOneBits = 0x1d00ffff;
    return std::make_shared<ShareLogParserServerBitcoin>(cfg, chainType);
  } else if (chainType == "LTC") {
    BitcoinDifficulty::DiffOneBits = 0x1f00ffff;
    return std::make_shared<ShareLogParserServerBitcoin>(cfg, chainType);
  } else if (chainType == "ZEC") {
    BitcoinDifficulty::DiffOneBits = 0x1f07ffff;
    return std::make_shared<ShareLogParserServerBitcoin>(cfg, chainType);
  } else if (chainType == "BEAM") {
    return std::make_shared<ShareLogParserServerBeam>(cfg, chainType);
  } else if (chainType == "ETH" || chainType == "ETC") {
    return std::make_shared<ShareLogParserServerEth>(cfg, "ETH");
  } else {
    LOG(FATAL) << "newShareLogParserServer: unknown chain type " << chainType;
    return nullptr;
  }
}

int main(int argc, char **argv) {
  char *optLogDir = NULL;
  char *optConf = NULL;
  int32_t optDate = 0;
  int c;

  if (argc <= 1) {
    usage();
    return 1;
  }
  while ((c = getopt(argc, argv, "c:l:d:u:h")) != -1) {
    switch (c) {
    case 'c':
      optConf = optarg;
      break;
    case 'l':
      optLogDir = optarg;
      break;
    case 'd':
      optDate = atoi(optarg);
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

  try {
    // chain type
    string chainType = cfg.lookup("sharelog.chain_type");

    //////////////////////////////////////////////////////////////////////////////
    //  re-run someday's share bin log
    //////////////////////////////////////////////////////////////////////////////
    if (optDate != 0) {
      const string tsStr = StringFormat(
          "%04d-%02d-%02d 00:00:00",
          optDate / 10000,
          optDate / 100 % 100,
          optDate % 100);
      const time_t ts = str2time(tsStr.c_str(), "%F %T");

      std::shared_ptr<ShareLogParser> slparser =
          newShareLogParser(chainType, ts, cfg);
      do {
        if (slparser->init() == false) {
          LOG(ERROR) << "init failure";
          break;
        }
        if (!slparser->processUnchangedShareLog()) {
          LOG(ERROR) << "processUnchangedShareLog fail";
          break;
        }
      } while (0);

      google::ShutdownGoogleLogging();
      return 0;
    }

    //////////////////////////////////////////////////////////////////////////////

    signal(SIGTERM, handler);
    signal(SIGINT, handler);

    gShareLogParserServer = newShareLogParserServer(chainType, cfg);
    gShareLogParserServer->run();
  } catch (const SettingException &e) {
    LOG(FATAL) << "config missing: " << e.getPath();
    return 1;
  }

  google::ShutdownGoogleLogging();
  return 0;
}

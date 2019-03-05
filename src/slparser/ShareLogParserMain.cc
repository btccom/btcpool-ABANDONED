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
#include "ShareLogParser.h"

#ifdef CHAIN_TYPE_ZEC
#include "StatisticsZCash.h"
#include "ShareLogParserZCash.h"
#else
#include "bitcoin/StatisticsBitcoin.h"
#include "bitcoin/ShareLogParserBitcoin.h"

#include "eth/StatisticsEth.h"
#include "eth/ShareLogParserEth.h"

#include "bytom/StatisticsBytom.h"
#include "bytom/ShareLogParserBytom.h"

#include "decred/ShareLogParserDecred.h"

#endif

#include "chainparamsbase.h"
#include "chainparams.h"

using namespace std;
using namespace libconfig;

std::shared_ptr<ShareLogParserServer> gShareLogParserServer = nullptr;

void handler(int sig) {
  if (gShareLogParserServer) {
    gShareLogParserServer->stop();
  }
}

void usage() {
  fprintf(stderr, BIN_VERSION_STRING("slparser"));
  fprintf(stderr, "Usage:\n\tslparser -c \"slparser.cfg\" -l \"log_dir\"\n");
  fprintf(stderr, "\tslparser -c \"slparser.cfg\" -l \"log_dir2\" -d \"20160830\"\n");
  fprintf(stderr, "\tslparser -c \"slparser.cfg\" -l \"log_dir3\" -d \"20160830\" -u \"puid(0: dump all, >0: someone's)\"\n");
}

std::shared_ptr<ShareLogDumper> newShareLogDumper(const string &chainType, const string &dataDir,
                                                  time_t timestamp, const std::set<int32_t> &uids)
{
#ifdef CHAIN_TYPE_ZEC
    return std::make_shared<ShareLogDumperZCash>(chainType.c_str(), dataDir, timestamp, uids);
#elif defined(CHAIN_TYPE_STR)
  if (CHAIN_TYPE_STR == chainType)
  {
    return std::make_shared<ShareLogDumperBitcoin>(chainType.c_str(), dataDir, timestamp, uids);
  }
  else if (chainType == "ETH") {
    return std::make_shared<ShareLogDumperEth>(chainType.c_str(), dataDir, timestamp, uids);
  }
  else if (chainType == "BTM") {
    return std::make_shared<ShareLogDumperBytom>(chainType.c_str(), dataDir, timestamp, uids);    
  }
  else if (chainType == "DCR") {
    return std::make_shared<ShareLogDumperDecred>(chainType.c_str(), dataDir, timestamp, uids);
  }
  else {
      LOG(FATAL) << "newShareLogDumper: unknown chain type " << chainType;
      return nullptr;
    }
#else
    LOG(FATAL) << "newShareLogDumper: unknown chain type " << chainType;
    return nullptr;
#endif
}

std::shared_ptr<ShareLogParser> newShareLogParser(const string &chainType, const string &dataDir,
                                                  time_t timestamp, const MysqlConnectInfo &poolDBInfo,
                                                  const int dupShareTrackingHeight)
{
#ifdef CHAIN_TYPE_ZEC
    return std::make_shared<ShareLogParserZCash>(chainType.c_str(), dataDir, timestamp, poolDBInfo, nullptr);
#elif defined(CHAIN_TYPE_STR)
  if (chainType == "BTC") {
    return std::make_shared<ShareLogParserBitcoin>(chainType.c_str(), dataDir, timestamp, poolDBInfo, nullptr);
  }
  else if (chainType == "ETH") {
    return std::make_shared<ShareLogParserEth>(chainType.c_str(), dataDir, timestamp, poolDBInfo,
                                               std::make_shared<DuplicateShareCheckerEth>(dupShareTrackingHeight));
  }
  else if (chainType == "BTM") {
    return std::make_shared<ShareLogParserBytom>(chainType.c_str(), dataDir, timestamp, poolDBInfo,
                                               std::make_shared<DuplicateShareCheckerBytom>(dupShareTrackingHeight));
  }
  else if (chainType == "DCR") {
    return std::make_shared<ShareLogParserDecred>(chainType.c_str(), dataDir, timestamp, poolDBInfo, nullptr);
  }
  else {
    LOG(FATAL) << "newShareLogParser: unknown chain type " << chainType;
    return nullptr;
  }
#else
    LOG(FATAL) << "newShareLogParser: unknown chain type " << chainType;
    return nullptr;
#endif
}

std::shared_ptr<ShareLogParserServer> newShareLogParserServer(const string &chainType, const string &dataDir,
                                                        const string &httpdHost, unsigned short httpdPort,
                                                        const MysqlConnectInfo &poolDBInfo,
                                                        const uint32_t kFlushDBInterval,
                                                        const int dupShareTrackingHeight)
{

#ifdef CHAIN_TYPE_ZEC
    return std::make_shared<ShareLogParserServerZCash>(chainType.c_str(), dataDir,
                                                         httpdHost, httpdPort,
                                                         poolDBInfo, kFlushDBInterval, nullptr);
#elif defined(CHAIN_TYPE_STR)
  if (chainType == "BTC")
  {
    return std::make_shared<ShareLogParserServerBitcoin>(chainType.c_str(), dataDir,
                                                         httpdHost, httpdPort,
                                                         poolDBInfo, kFlushDBInterval, nullptr);
  }
  else if (chainType == "ETH") {
    return std::make_shared<ShareLogParserServerEth>(chainType.c_str(), dataDir,
                                                     httpdHost, httpdPort,
                                                     poolDBInfo, kFlushDBInterval,
                                                     std::make_shared<DuplicateShareCheckerEth>(dupShareTrackingHeight));
  }
  else if (chainType == "BTM") {
    return std::make_shared<ShareLogParserServerBytom>(chainType.c_str(), dataDir,
                                                     httpdHost, httpdPort,
                                                     poolDBInfo, kFlushDBInterval,
                                                     std::make_shared<DuplicateShareCheckerBytom>(dupShareTrackingHeight));
  }
  else if (chainType == "DCR") {
    return std::make_shared<ShareLogParserServerDecred>(chainType.c_str(), dataDir,
                                                        httpdHost, httpdPort,
                                                        poolDBInfo, kFlushDBInterval, nullptr);
  }
  else {
    LOG(FATAL) << "newShareLogParserServer: unknown chain type " << chainType;
    return nullptr;
  }
#else
    LOG(FATAL) << "newShareLogParserServer: unknown chain type " << chainType;
    return nullptr;
#endif
}

int main(int argc, char **argv) {
  char *optLogDir = NULL;
  char *optConf   = NULL;
  int32_t optDate = 0;
  int32_t optPUID = -1;  // pool user id
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
      case 'u':
        optPUID = atoi(optarg);
        break;
      case 'h': default:
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
  FLAGS_stderrthreshold = 3;    // 3: FATAL
  FLAGS_max_log_size    = 100;  // max log file size 100 MB
  FLAGS_logbuflevel     = -1;   // don't buffer logs
  FLAGS_stop_logging_if_full_disk = true;

  LOG(INFO) << BIN_VERSION_STRING("slparser");

  // Read the file. If there is an error, report it and exit.
  libconfig::Config cfg;
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

    // DB info
    MysqlConnectInfo *poolDBInfo = nullptr;
    {
      int32_t poolDBPort = 3306;
      cfg.lookupValue("pooldb.port", poolDBPort);
      poolDBInfo = new MysqlConnectInfo(cfg.lookup("pooldb.host"), poolDBPort,
                                        cfg.lookup("pooldb.username"),
                                        cfg.lookup("pooldb.password"),
                                        cfg.lookup("pooldb.dbname"));
    }

    // chain type
    string chainType = cfg.lookup("sharelog.chain_type");
    // Track duplicate shares within N blocks.
    int32_t dupShareTrackingHeight = 3;
    cfg.lookupValue("dup_share_checker.tracking_height_number", dupShareTrackingHeight);

    //////////////////////////////////////////////////////////////////////////////
    //  dump shares to stdout
    //////////////////////////////////////////////////////////////////////////////
    if (optDate != 0 && optPUID != -1) {
      const string tsStr = Strings::Format("%04d-%02d-%02d 00:00:00",
                                          optDate/10000,
                                          optDate/100 % 100, optDate % 100);
      const time_t ts = str2time(tsStr.c_str(), "%F %T");
      std::set<int32_t> uids;
      if (optPUID > 0)
      uids.insert(optPUID);

      std::shared_ptr<ShareLogDumper> sldumper = newShareLogDumper(chainType, cfg.lookup("sharelog.data_dir"), ts, uids);
      sldumper->dump2stdout();

      google::ShutdownGoogleLogging();
      return 0;
    }

    //////////////////////////////////////////////////////////////////////////////
    //  re-run someday's share bin log
    //////////////////////////////////////////////////////////////////////////////
    if (optDate != 0) {
      const string tsStr = Strings::Format("%04d-%02d-%02d 00:00:00",
                                          optDate/10000,
                                          optDate/100 % 100, optDate % 100);
      const time_t ts = str2time(tsStr.c_str(), "%F %T");

      std::shared_ptr<ShareLogParser> slparser = newShareLogParser(chainType,
                                                             cfg.lookup("sharelog.data_dir"),
                                                             ts, *poolDBInfo,
                                                             dupShareTrackingHeight);
      do {
        if (slparser->init() == false) {
          LOG(ERROR) << "init failure";
          break;
        }
        if (!slparser->processUnchangedShareLog()) {
          LOG(ERROR) << "processUnchangedShareLog fail";
          break;
        }
        if (!slparser->flushToDB()) {
          LOG(ERROR) << "processUnchangedShareLog fail";
          break;
        }
      } while (0);

      google::ShutdownGoogleLogging();
      return 0;
    }

    //////////////////////////////////////////////////////////////////////////////

    // lock cfg file:
    //    you can't run more than one process with the same config file
    /*boost::interprocess::file_lock pidFileLock(optConf);
    if (pidFileLock.try_lock() == false) {
      LOG(FATAL) << "lock cfg file fail";
      return(EXIT_FAILURE);
    }*/

    signal(SIGTERM, handler);
    signal(SIGINT,  handler);

    int32_t port = 8081;
    cfg.lookupValue("slparserhttpd.port", port);
    uint32_t kFlushDBInterval = 20;
    cfg.lookupValue("slparserhttpd.flush_db_interval", kFlushDBInterval);
    gShareLogParserServer = newShareLogParserServer(chainType,
                                                 cfg.lookup("sharelog.data_dir"),
                                                 cfg.lookup("slparserhttpd.ip"),
                                                 port, *poolDBInfo,
                                                 kFlushDBInterval,
                                                 dupShareTrackingHeight);
    gShareLogParserServer->run();
  }
  catch (std::exception & e) {
    LOG(FATAL) << "exception: " << e.what();
    return 1;
  }

  google::ShutdownGoogleLogging();
  return 0;
}

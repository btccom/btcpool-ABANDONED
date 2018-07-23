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
#include "StatsHttpd.h"
#include "RedisConnection.h"

#include "bitcoin/StatisticsBitcoin.h"
#include "bitcoin/StatsHttpdBitcoin.h"

#include "eth/StatisticsEth.h"
#include "eth/StatsHttpdEth.h"

#include "bytom/StatisticsBytom.h"
#include "bytom/StatsHttpdBytom.h"

using namespace std;
using namespace libconfig;

std::shared_ptr<StatsServer> gStatsServer = nullptr;

void handler(int sig) {
  if (gStatsServer) {
    gStatsServer->stop();
  }
}

void usage() {
  fprintf(stderr, "Usage:\n\tstatshttpd -c \"statshttpd.cfg\" -l \"log_dir\"\n");
}

std::shared_ptr<StatsServer> newStatsServer(const string &chainType, const char *kafkaBrokers,
                                            const char *kafkaShareTopic, const char *kafkaCommonEventsTopic,
                                            const string &httpdHost, unsigned short httpdPort,
                                            const MysqlConnectInfo *poolDBInfo, const RedisConnectInfo *redisInfo,
                                            const uint32_t redisConcurrency, const string &redisKeyPrefix, const int redisKeyExpire,
                                            const int redisPublishPolicy, const int redisIndexPolicy,
                                            const time_t kFlushDBInterval, const string &fileLastFlushTime,
                                            const int dupShareTrackingHeight)
{
  if (chainType == "BTC") {
    return std::make_shared<StatsServerBitcoin>(kafkaBrokers, kafkaShareTopic, kafkaCommonEventsTopic,
                                                httpdHost, httpdPort, poolDBInfo, redisInfo,
                                                redisConcurrency, redisKeyPrefix, redisKeyExpire,
                                                redisPublishPolicy, redisIndexPolicy,
                                                kFlushDBInterval, fileLastFlushTime, nullptr);
  }
  else if (chainType == "ETH") {
    return std::make_shared<StatsServerEth>(kafkaBrokers, kafkaShareTopic, kafkaCommonEventsTopic,
                                            httpdHost, httpdPort, poolDBInfo, redisInfo,
                                            redisConcurrency, redisKeyPrefix, redisKeyExpire,
                                            redisPublishPolicy, redisIndexPolicy,
                                            kFlushDBInterval, fileLastFlushTime,
                                            std::make_shared<DuplicateShareCheckerEth>(dupShareTrackingHeight));
  }
  else if (chainType == "BTM") {
    return std::make_shared<StatsServerBytom>(kafkaBrokers, kafkaShareTopic, kafkaCommonEventsTopic,
                                            httpdHost, httpdPort, poolDBInfo, redisInfo,
                                            redisConcurrency, redisKeyPrefix, redisKeyExpire,
                                            redisPublishPolicy, redisIndexPolicy,
                                            kFlushDBInterval, fileLastFlushTime,
                                            std::make_shared<DuplicateShareCheckerBytom>(dupShareTrackingHeight));
  }
  else {
    LOG(FATAL) << "newStatsServer: unknown chain type " << chainType;
    return nullptr;
  }
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

  // lock cfg file:
  //    you can't run more than one process with the same config file
  boost::interprocess::file_lock pidFileLock(optConf);
  if (pidFileLock.try_lock() == false) {
    LOG(FATAL) << "lock cfg file fail";
    return(EXIT_FAILURE);
  }

  signal(SIGTERM, handler);
  signal(SIGINT,  handler);

  try {
    bool useMysql = true;
    cfg.lookupValue("statshttpd.use_mysql", useMysql);
    bool useRedis = false;
    cfg.lookupValue("statshttpd.use_redis", useRedis);

    MysqlConnectInfo *poolDBInfo = nullptr;
    if (useMysql) {
      int32_t poolDBPort = 3306;
      cfg.lookupValue("pooldb.port", poolDBPort);
      poolDBInfo = new MysqlConnectInfo(cfg.lookup("pooldb.host"), poolDBPort,
                                        cfg.lookup("pooldb.username"),
                                        cfg.lookup("pooldb.password"),
                                        cfg.lookup("pooldb.dbname"));
    }

    RedisConnectInfo *redisInfo = nullptr;
    string redisKeyPrefix;
    int redisKeyExpire = 0;
    int redisPublishPolicy = 0;
    int redisIndexPolicy = 0;
    uint32_t redisConcurrency = 1;

    if (useRedis) {
      int32_t redisPort = 6379;
      cfg.lookupValue("redis.port", redisPort);
      redisInfo = new RedisConnectInfo(cfg.lookup("redis.host"), redisPort, cfg.lookup("redis.password"));

      cfg.lookupValue("redis.key_prefix", redisKeyPrefix);
      cfg.lookupValue("redis.key_expire", redisKeyExpire);
      cfg.lookupValue("redis.publish_policy", redisPublishPolicy);
      cfg.lookupValue("redis.index_policy", redisIndexPolicy);
      cfg.lookupValue("redis.concurrency", redisConcurrency);
    }
    
    string fileLastFlushTime;

    int32_t port = 8080;
    int32_t flushInterval = 20;
    int32_t dupShareTrackingHeight = 3;
    cfg.lookupValue("statshttpd.port", port);
    cfg.lookupValue("statshttpd.flush_db_interval", flushInterval);
    cfg.lookupValue("statshttpd.file_last_flush_time", fileLastFlushTime);
    cfg.lookupValue("dup_share_checker.tracking_height_number", dupShareTrackingHeight);
    gStatsServer = newStatsServer(cfg.lookup("statshttpd.chain_type"),
                                  cfg.lookup("kafka.brokers").c_str(),
                                  cfg.lookup("statshttpd.share_topic").c_str(),
                                  cfg.lookup("statshttpd.common_events_topic").c_str(),
                                  cfg.lookup("statshttpd.ip").c_str(),
                                  (unsigned short)port, poolDBInfo,
                                  redisInfo, redisConcurrency, redisKeyPrefix,
                                  redisKeyExpire, redisPublishPolicy, redisIndexPolicy,
                                  (time_t)flushInterval, fileLastFlushTime,
                                  dupShareTrackingHeight);
    if (gStatsServer->init()) {
    	gStatsServer->run();
    }
  }
  catch (std::exception & e) {
    LOG(FATAL) << "exception: " << e.what();
    return 1;
  }

  google::ShutdownGoogleLogging();
  return 0;
}

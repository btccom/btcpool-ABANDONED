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

#include "Utils.h"
#include "GwMaker.h"

using namespace std;
using namespace libconfig;

//GwMaker *gGwMaker = nullptr;
static vector<shared_ptr<GwMaker>> gGwMakers;

void handler(int sig) {
  for (auto gwMaker: gGwMakers) {
    if (gwMaker)
      gwMaker->stop();
  }
  // if (gGwMaker) {
  //   gGwMaker->stop();
  // }
}

void usage() {
  fprintf(stderr, "Usage:\n\tgwmaker -c \"gwmaker.cfg\" -l \"log_dir\"\n");
}

// GwMaker* createGwMaker (Config &cfg) {
//   GwMaker* gGwMaker = NULL;
//   uint32_t pollPeriod = 100;
//   cfg.lookupValue("gwmaker.poll_period", pollPeriod);
//   string type = cfg.lookup("rskd.type");
//   if ("BTC" == type) {
//     gGwMaker = new GwMaker(cfg.lookup("rskd.rpc_addr"),
//                            cfg.lookup("rskd.rpc_userpwd"),
//                            cfg.lookup("kafka.brokers"),
//                            pollPeriod);
//   } else if ("ETH" == type) {
//     gGwMaker = new GwMakerEth(cfg.lookup("rskd.rpc_addr"),
//                            cfg.lookup("rskd.rpc_userpwd"),
//                            cfg.lookup("kafka.brokers"),
//                            pollPeriod);
//   }

//   return gGwMaker;
// }

GwHandler* createHandler(const string& type) {
  GwHandler* handler = nullptr;
  if ("ETH" == type)
    return new GwHandlerEth();
  else if ("SIA" == type)
    return new GwHandlerSia();

  return handler;
}

void initDefinitions(const Config &cfg)
{
  const Setting &root = cfg.getRoot();
  const Setting &definitions = root["definitions"];

  for (int i = 0; i < definitions.getLength(); i++)
  {
    // string rpcAddr, rpcUserpwd;
    // bitcoinds[i].lookupValue("rpc_addr", rpcAddr);
    // bitcoinds[i].lookupValue("rpc_userpwd", rpcUserpwd);
    // gBlockMaker->addBitcoind(rpcAddr, rpcUserpwd);
    string addr = move(definitions[i].lookup("addr"));
    string userpwd = move(definitions[i].lookup("userpwd"));
    string data = move(definitions[i].lookup("data"));
    string agent = move(definitions[i].lookup("agent"));
    string topic = move(definitions[i].lookup("topic"));
    string handlerType = move(definitions[i].lookup("handler"));
    uint32_t interval = 500;
    definitions[i].lookupValue("interval", interval);
    bool enabled = false;
    definitions[i].lookupValue("enabled", enabled);
    shared_ptr<GwHandler> handler(createHandler(handlerType));
    if (handler != nullptr)
    {
      gGwDefiniitons.push_back(
          {addr,
           userpwd,
           data,
           agent,
           topic,
           interval,
           handler,
           enabled});
    }
    else
      LOG(ERROR) << "created handler failed for type " << handlerType;
  }

  // gGwDefiniitons.push_back(
  //     {"http://127.0.0.1:9980/miner/header",
  //      "xxx",
  //      "",
  //      "Sia-Agent",
  //      "siaraw",
  //      "127.0.0.1:9092",
  //      500,
  //      make_shared<GwHandlerSia>(),
  //      false});

  // gGwDefiniitons.push_back(
  //     {"http://127.0.0.1:8545",
  //      "user:pass",
  //      "{\"jsonrpc\": \"2.0\", \"method\": \"eth_getWork\", \"params\": [], \"id\": 1}",
  //      "curl",
  //      KAFKA_TOPIC_RAWGW,
  //      "127.0.0.1:9092",
  //      500,
  //      make_shared<GwHandlerEth>(),
  //      true});
}

void workerThread(shared_ptr<GwMaker> gwMaker) {
  if (gwMaker)
    gwMaker->run();
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
  // you can't run more than one process with the same config file
  boost::interprocess::file_lock pidFileLock(optConf);
  if (pidFileLock.try_lock() == false) {
    LOG(FATAL) << "lock cfg file fail";
    return(EXIT_FAILURE);
  }

  signal(SIGTERM, handler);
  signal(SIGINT,  handler);

  initDefinitions(cfg);
  vector<shared_ptr<thread>> workers;
  string brokers = std::move(cfg.lookup("kafka.brokers"));
  for (auto gwDef : gGwDefiniitons)
  {
    if (gwDef.enabled)
    {
      shared_ptr<GwMaker> gwMaker = std::make_shared<GwMaker>(gwDef, brokers);
      try
      {
        if (gwMaker->init()) {
          gGwMakers.push_back(gwMaker);
          workers.push_back(std::make_shared<thread>(workerThread, gwMaker));
        }
        else
          LOG(FATAL) << "gwmaker init failure " << gwDef.topic;
      }
      catch (std::exception &e)
      {
        LOG(FATAL) << "exception: " << e.what();
      }
    }
  }

  for (auto pWorker : workers) {
    if (pWorker->joinable()) {
      LOG(INFO) << "wait for worker " << pWorker->get_id();
      pWorker->join();
      LOG(INFO) << "worker exit";
    }
  }

  //TODO: run logic
  //gGwMaker = createGwMaker(cfg);

  // try {
  //   if (!gGwMaker->init()) {
  //     LOG(FATAL) << "gwmaker init failure";
  //   } else {
  //     gGwMaker->run();
  //   }
  //   delete gGwMaker;
  // } catch (std::exception & e) {
  //   LOG(FATAL) << "exception: " << e.what();
  //   return 1;
  // }
  LOG(INFO) << "gwmaker exit";
  google::ShutdownGoogleLogging();
  return 0;
}

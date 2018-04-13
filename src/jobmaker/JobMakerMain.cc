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

#include <zmq.hpp>

#include "Utils.h"
#include "JobMaker.h"
#include "Zookeeper.h"

using namespace std;
using namespace libconfig;

#define JOBMAKER_LOCK_NODE_PATH    "/locks/jobmaker" ZOOKEEPER_NODE_POSTFIX

Zookeeper *gZookeeper = nullptr;
static vector<shared_ptr<JobMaker>> gJobMakers;

void handler(int sig) {
  for (auto jobMaker: gJobMakers) {
    if (jobMaker)
      jobMaker->stop();
  }

  if (gZookeeper) {
    delete gZookeeper;
    gZookeeper = nullptr;
  }
}

void usage() {
  fprintf(stderr, "Usage:\n\tjobmaker -c \"jobmaker.cfg\" -l \"log_dir\"\n");
}

shared_ptr<JobMakerHandler> createJobMakerHandler(const JobMakerDefinition &def) {
  shared_ptr<JobMakerHandler> handler;

  if      (def.chainType_ == "ETH")
    handler = make_shared<JobMakerHandlerEth>();
  else if (def.chainType_ == "SIA")
    handler = make_shared<JobMakerHandlerSia>();
  else
    LOG(FATAL) << "unknown chain type: " << def.chainType_;

  handler->init(def);

  return handler;
}

JobMakerDefinition createJobMakerDefinition(const Setting &setting)
{
  JobMakerDefinition def;

  readFromSetting(setting, "handler",              def.chainType_);
  readFromSetting(setting, "payout_address",       def.payoutAddr);
  readFromSetting(setting, "file_last_job_time",   def.fileLastJobTime);
  readFromSetting(setting, "consumer_topic",       def.consumerTopic);
  readFromSetting(setting, "producer_topic",       def.producerTopic);
  readFromSetting(setting, "stratum_job_interval", def.stratumJobInterval);
  readFromSetting(setting, "max_job_delay",        def.maxJobDelay);

  def.enabled_ = false;
  readFromSetting(setting, "enabled", def.enabled_, true);

  return def;
}

void createJobMakers(const Config &cfg, const string &brokers, vector<shared_ptr<JobMaker>> &makers)
{
  const Setting &root = cfg.getRoot();
  const Setting &workerDefs = root["definitions"];

  for (int i = 0; i < workerDefs.getLength(); i++)
  {
    JobMakerDefinition def = createJobMakerDefinition(workerDefs[i]);

    if (!def.enabled_) {
      LOG(INFO) << "chain: " << def.chainType_ << ", topic: " << def.producerTopic << ", disabled.";
      continue;
    }
    
    LOG(INFO) << "chain: " << def.chainType_ << ", topic: " << def.producerTopic << ", enabled.";

    auto handle = createJobMakerHandler(def);
    makers.push_back(std::make_shared<JobMaker>(handle, brokers));
  }
}

void workerThread(shared_ptr<JobMaker> jobMaker) {
  if (jobMaker)
    jobMaker->run();
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
    vector<shared_ptr<thread>> workers;
    string brokers = cfg.lookup("kafka.brokers");
    
    // create JobMaker
    createJobMakers(cfg, brokers, gJobMakers);

    // init JobMaker
    for (auto JobMaker : gJobMakers)
    {
      if (JobMaker->init()) {
        workers.push_back(std::make_shared<thread>(workerThread, JobMaker));
      }
      else {
        LOG(FATAL) << "jobmaker init failure.";
      }
    }

    // run JobMaker
    for (auto pWorker : workers) {
      if (pWorker->joinable()) {
        LOG(INFO) << "wait for worker " << pWorker->get_id();
        pWorker->join();
        LOG(INFO) << "worker exit";
      }
    }

  }
  catch (std::exception & e) {
    LOG(FATAL) << "exception: " << e.what();
    return 1;
  }

  google::ShutdownGoogleLogging();
  return 0;
}

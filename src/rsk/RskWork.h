/**
  File: RskWork.h
  Purpose: Object that represents RSK data needed to generate a new job

  @author Martin Medina
  @copyright RSK Labs Ltd.
*/

#ifndef RSK_WORK_H_
#define RSK_WORK_H_

#include "utilities_js.hpp"

#include <glog/logging.h>

class RskWork {
  static bool isCleanJob_;

  string blockHash_;
  string target_;
  string fees_;
  string rpcAddress_;
  string rpcUserPwd_;
  bool notifyFlag_;
  bool initialized_;

public:
  RskWork();

  bool initFromGw(const string &rawGetWork);
  bool isInitialized() const;
  string getBlockHash() const;
  string getTarget() const;
  string getFees() const;
  string getRpcAddress() const;
  string getRpcUserPwd() const;
  bool getNotifyFlag() const;
  static void setIsCleanJob(bool cleanJob);
  bool getIsCleanJob() const;
};

#endif
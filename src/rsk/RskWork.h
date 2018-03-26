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
protected:
  u_int32_t created_at;
  string blockHash_;
  string target_;
  string fees_;
  string rpcAddress_;
  string rpcUserPwd_;
  bool notifyFlag_;
  bool initialized_;

public:
  RskWork();
  virtual ~RskWork() {};

  bool initFromGw(const string &rawGetWork);
  bool isInitialized() const;
  u_int32_t getCreatedAt() const;
  string getBlockHash() const;
  string getTarget() const;
  string getFees() const;
  string getRpcAddress() const;
  string getRpcUserPwd() const;
  bool getNotifyFlag() const;
  static void setIsCleanJob(bool cleanJob);
  bool getIsCleanJob() const;
  
private:
  virtual bool validate(JsonNode &work);
  virtual void initialize(JsonNode &work); 
};

class RskWorkEth : public RskWork {
  virtual bool validate(JsonNode &work);
  virtual void initialize(JsonNode &work); 
  //string currBlkHeaderPOWHash_;
  string seedHash_;

public:
  string getSeedHash() const {return seedHash_;}
};

#endif
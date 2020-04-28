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
#ifndef JOB_MAKER_BITCOIN_H_
#define JOB_MAKER_BITCOIN_H_

#include "JobMaker.h"

#include "bitcoin/StratumBitcoin.h"
#include "rsk/RskWork.h"
#include "vcash/VcashWork.h"

#if defined(CHAIN_TYPE_ZEC) && defined(NDEBUG)
// fix "Zcash cannot be compiled without assertions."
#undef NDEBUG
#include <crypto/common.h>
#define NDEBUG
#endif

#include <uint256.h>
#include <base58.h>

#ifdef INCLUDE_BTC_KEY_IO_H
#include <key_io.h>
#endif

struct GbtJobMakerDefinition : public JobMakerDefinition {
  virtual ~GbtJobMakerDefinition() {}

  bool testnet_;

  string payoutAddr_;
  string coinbaseInfo_;
  uint32_t blockVersion_;

  string rawGbtTopic_;
  string auxPowGwTopic_;
  string rskRawGwTopic_;
  string vcashRawGwTopic_;

  uint32_t maxJobDelay_;
  uint32_t gbtLifeTime_;
  uint32_t emptyGbtLifeTime_;

  uint32_t auxmergedMiningNotifyPolicy_;
  uint32_t rskmergedMiningNotifyPolicy_;
  uint32_t vcashmergedMiningNotifyPolicy_;

  vector<SubPoolInfo> subPool_;
  int subPoolCoinbaseMaxLen_ = 30;

  bool grandPoolEnabled_;
};

class JobMakerHandlerBitcoin : public JobMakerHandler {
  // mining bitcoin blocks
  CTxDestination poolPayoutAddr_;
  uint32_t currBestHeight_;
  uint32_t lastJobSendTime_;
  bool isLastJobEmptyBlock_;
  std::map<uint64_t /* @see makeGbtKey() */, string>
      rawgbtMap_; // sorted gbt by timestamp
  deque<uint256> lastestGbtHash_;

  // merged mining for AuxPow blocks (example: Namecoin, ElastOS)
  string latestNmcAuxBlockJson_;
  string latestNmcAuxBlockHash_;
  uint32_t latestNmcAuxBlockHeight_;

  // merged mining for RSK
  RskWork *previousRskWork_;
  RskWork *currentRskWork_;
  bool isMergedMiningUpdate_; // a flag to mark RSK has an update

  // merged mining for RSK
  VcashWork *previousVcashWork_;
  VcashWork *currentVcashWork_;
  // bool isVcashMergedMiningUpdate_; // a flag to mark Vcash has an update

  const size_t SUBPOOL_JSON_MAX_SIZE = 8192;
  std::mutex subPoolLock_;
  std::thread updateSubPoolAddrThread_;

  bool addRawGbt(const string &msg);
  void clearTimeoutGbt();
  bool isReachTimeout();

  void clearTimeoutGw();
  void clearVcashTimeoutGw();
  bool triggerVcashUpdate();
  bool triggerRskUpdate();

  // return false if there is no best rawGbt or
  // doesn't need to send a stratum job at current.
  bool findBestRawGbt(string &bestRawGbt);
  string makeStratumJob(const string &gbt);

  inline uint64_t
  makeGbtKey(uint32_t gbtTime, bool isEmptyBlock, uint32_t height);
  inline uint32_t gbtKeyGetTime(uint64_t gbtKey);
  inline uint32_t gbtKeyGetHeight(uint64_t gbtKey);
  inline bool gbtKeyIsEmptyBlock(uint64_t gbtKey);

  bool updateSubPoolAddr(size_t index);
  void checkSubPoolAddr();
  void watchSubPoolAddr(const string &path);
  static void handleSubPoolUpdateEvent(
      zhandle_t *zh, int type, int state, const char *path, void *pThis);

public:
  JobMakerHandlerBitcoin();
  virtual ~JobMakerHandlerBitcoin() {}

  bool init(shared_ptr<JobMakerDefinition> def) override;
  virtual bool initConsumerHandlers(
      const string &kafkaBrokers,
      vector<JobMakerConsumerHandler> &handlers) override;

  bool processRawGbtMsg(const string &msg);
  bool processAuxPowMsg(const string &msg);
  bool processRskGwMsg(const string &msg);
  bool processVcashGwMsg(const string &msg);

  virtual string makeStratumJobMsg() override;

  // read-only definition
  inline shared_ptr<const GbtJobMakerDefinition> def() {
    return std::dynamic_pointer_cast<const GbtJobMakerDefinition>(def_);
  }

  inline shared_ptr<GbtJobMakerDefinition> defWithoutConst() {
    return std::dynamic_pointer_cast<GbtJobMakerDefinition>(def_);
  }
};

#endif

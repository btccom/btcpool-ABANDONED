#ifndef JOB_MAKER_TELLOR_H_
#define JOB_MAKER_TELLOR_H_

#include "JobMaker.h"
#include "StratumTellor.h"
#include "utilities_js.hpp"

class JobMakerHandlerTellor : public GwJobMakerHandler {
public:
  virtual ~JobMakerHandlerTellor() {}
  bool processMsg(const string &msg) override;
  string makeStratumJobMsg() override;

private:
  void clearTimeoutMsg();
  inline uint64_t makeWorkKey(const StratumJobTellor &job);

  std::map<uint64_t, shared_ptr<StratumJobTellor>> workMap_;
  std::map<uint64_t, shared_ptr<StratumJobTellor>> jobid2work_;
  uint32_t lastReceivedHeight_ = 0;
};

#endif

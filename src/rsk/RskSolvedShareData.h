/**
  File: RskSolvedShareData.h
  Purpose: Object that represents RSK data needed to build a solution block

  @author Martin Medina
  @copyright RSK
*/

#ifndef RSK_SOLVED_SHARE_DATA_H_
#define RSK_SOLVED_SHARE_DATA_H_

class RskSolvedShareData {
public:
  uint64_t jobId_;
  int64_t  workerId_;  // found by who
  int32_t  userId_;
  int32_t  height_;
  uint8_t  header80_[80];
  char     workerFullName_[40];  // <UserName>.<WorkerName>
  char     feesForMiner_[80];
  char     rpcAddress_[80];
  char     rpcUserPwd_[80];

  RskSolvedShareData(): jobId_(0), workerId_(0), userId_(0), height_(0) {
    memset(header80_,       0, sizeof(header80_));
    memset(workerFullName_, 0, sizeof(workerFullName_));
    memset(feesForMiner_, 0, sizeof(feesForMiner_));
    memset(rpcAddress_, 0, sizeof(rpcAddress_));
    memset(rpcUserPwd_, 0, sizeof(rpcUserPwd_));
  }
};

#endif
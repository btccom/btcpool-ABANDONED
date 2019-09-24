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

#include "StratumServerDecred.h"

#include "StratumSessionDecred.h"
#include "StratumDecred.h"
#include "CommonDecred.h"

#include "StratumMiner.h"

#include "arith_uint256.h"

#include <boost/algorithm/string.hpp>
#include <iostream>

using std::ostream;
static ostream &operator<<(ostream &os, const StratumJobDecred &job) {
  os << "jobId = " << job.jobId_ << ", prevHash = " << job.getPrevHash()
     << ", coinBase1 = " << job.getCoinBase1()
     << ", coinBase2 = " << job.header_.stakeVersion
     << ", vesion = " << job.header_.version
     << ", height = " << job.header_.height;
  return os;
}

JobRepositoryDecred::JobRepositoryDecred(
    size_t chainId,
    ServerDecred *server,
    const char *kafkaBrokers,
    const char *consumerTopic,
    const string &fileLastNotifyTime,
    bool niceHashForced,
    uint64_t niceHashMinDiff,
    const std::string &niceHashMinDiffZookeeperPath)
  : JobRepositoryBase<ServerDecred>(
        chainId,
        server,
        kafkaBrokers,
        consumerTopic,
        fileLastNotifyTime,
        niceHashForced,
        niceHashMinDiff,
        niceHashMinDiffZookeeperPath)
  , lastHeight_(0)
  , lastVoters_(0) {
}

shared_ptr<StratumJob> JobRepositoryDecred::createStratumJob() {
  return std::make_shared<StratumJobDecred>();
}

void JobRepositoryDecred::broadcastStratumJob(shared_ptr<StratumJob> sjob) {
  auto jobDecred = std::static_pointer_cast<StratumJobDecred>(sjob);
  if (!jobDecred) {
    LOG(ERROR) << "wrong job type: jobId = " << sjob->jobId_;
    return;
  }

  LOG(INFO) << "broadcasting job: " << *jobDecred;
  bool isClean = false;
  auto height = jobDecred->header_.height.value();
  if (height > lastHeight_) {
    LOG(INFO) << "received job with new height, old height = " << lastHeight_;
    isClean = true;
    lastHeight_ = height;
  }

  auto voters = jobDecred->header_.voters.value();
  bool moreVoters = voters > lastVoters_;

  shared_ptr<StratumJobEx> jobEx(createStratumJobEx(jobDecred, isClean));

  if (isClean) {
    // mark all jobs as stale, should do this before insert new job
    // stale shares will not be rejected, they will be marked as ACCEPT_STALE
    // and have lower rewards.
    for (auto it : exJobs_) {
      it.second->markStale();
    }
  }

  // insert new job
  exJobs_[jobDecred->jobId_] = jobEx;

  // We want to update jobs immediately if there are more voters for the same
  // height block
  if (isClean || (moreVoters && height >= lastHeight_)) {
    lastVoters_ = voters;
    sendMiningNotify(jobEx);
  }
}

// gominer protocol
// mining.notify: extra nonce 2 size is the actual extra nonce 2 size, extra
// nonce 1 is the actual extra nonce 1 mining.submit: extra nonce 2 is the
// actual extra nonce 2
class StratumProtocolDecredGoMiner : public StratumProtocolDecred {
public:
  string getExtraNonce1String(uint32_t extraNonce1) const override {
    return Strings::Format("%08x", boost::endian::endian_reverse(extraNonce1));
  }

  void setExtraNonces(
      BlockHeaderDecred &header,
      uint32_t extraNonce1,
      const vector<uint8_t> &extraNonce2) override {
    *reinterpret_cast<boost::endian::little_uint32_buf_t *>(
        header.extraData.begin()) = extraNonce1;
    std::copy_n(
        extraNonce2.begin(),
        StratumMiner::kExtraNonce2Size_,
        header.extraData.begin() + sizeof(extraNonce1));
  }
};

// tpruvot protocol
// mining.notify: extra nonce 2 size is not used, extra nonce 1 is considered as
// the whole extra nonce, bits higher than 32 to be rolled mining.submit: extra
// nonce 2 is considered as the whole rolled extra nonce but we only take the
// first 8 bytes (last 4 bytes shall be extra nonce1).
class StratumProtocolDecredTPruvot : public StratumProtocolDecred {
public:
  string getExtraNonce1String(uint32_t extraNonce1) const override {
    return Strings::Format("%024x", boost::endian::endian_reverse(extraNonce1));
  }

  void setExtraNonces(
      BlockHeaderDecred &header,
      uint32_t extraNonce1,
      const vector<uint8_t> &extraNonce2) override {
    *reinterpret_cast<boost::endian::little_uint32_buf_t *>(
        header.extraData.begin() + StratumMiner::kExtraNonce2Size_) =
        extraNonce1;
    std::copy_n(
        extraNonce2.begin(),
        StratumMiner::kExtraNonce2Size_,
        header.extraData.begin());
  }
};

bool ServerDecred::setupInternal(const libconfig::Config &config) {
  string protocol;
  config.lookupValue("sserver.protocol", protocol);
  boost::algorithm::to_lower(protocol);

  if (protocol == "gominer") {
    LOG(INFO) << "Using gominer stratum protocol";
    protocol_ = std::make_unique<StratumProtocolDecredGoMiner>();
  } else {
    LOG(INFO) << "Using tpruvot stratum protocol";
    protocol_ = std::make_unique<StratumProtocolDecredTPruvot>();
  }

  return true;
}

unique_ptr<StratumSession> ServerDecred::createConnection(
    bufferevent *bev, sockaddr *saddr, uint32_t sessionID) {
  return std::make_unique<StratumSessionDecred>(
      *this, bev, saddr, sessionID, *protocol_);
}

JobRepository *ServerDecred::createJobRepository(
    size_t chainId,
    const char *kafkaBrokers,
    const char *consumerTopic,
    const string &fileLastNotifyTime,
    bool niceHashForced,
    uint64_t niceHashMinDiff,
    const std::string &niceHashMinDiffZookeeperPath) {
  return new JobRepositoryDecred(
      chainId,
      this,
      kafkaBrokers,
      consumerTopic,
      fileLastNotifyTime,
      niceHashForced,
      niceHashMinDiff,
      niceHashMinDiffZookeeperPath);
}

void ServerDecred::checkAndUpdateShare(
    ShareDecred &share,
    shared_ptr<StratumJobEx> exJobPtr,
    const vector<uint8_t> &extraNonce2,
    uint32_t ntime,
    uint32_t nonce,
    const string &workerFullName) {
  if (!exJobPtr) {
    share.set_status(StratumStatus::JOB_NOT_FOUND);
    return;
  }
  if (exJobPtr->isStale()) {
    share.set_status(StratumStatus::STALE_SHARE);
    return;
  }

  auto sjob = std::static_pointer_cast<StratumJobDecred>(exJobPtr->sjob_);
  share.set_network((uint32_t)sjob->network_);
  share.set_voters(sjob->header_.voters.value());
  if (ntime > sjob->header_.timestamp.value() + 600) {
    share.set_status(StratumStatus::TIME_TOO_NEW);
    return;
  }

  FoundBlockDecred foundBlock(
      share.jobid(),
      share.workerhashid(),
      share.userid(),
      workerFullName,
      sjob->header_,
      sjob->network_);
  auto &header = foundBlock.header_;
  header.timestamp = ntime;
  header.nonce = nonce;
  protocol_->setExtraNonces(header, share.sessionid(), extraNonce2);

  uint256 blkHash = header.getHash();
  auto bnBlockHash = UintToArith256(blkHash);
  auto bnNetworkTarget = UintToArith256(sjob->target_);

  share.set_bitsreached(bnBlockHash.GetCompact());

  //
  // found new block
  //
  if (isSubmitInvalidBlock_ == true || bnBlockHash <= bnNetworkTarget) {
    // send
    sendSolvedShare2Kafka(
        exJobPtr->chainId_, (const char *)&foundBlock, sizeof(foundBlock));

    // mark jobs as stale
    GetJobRepository(exJobPtr->chainId_)->markAllJobsAsStale(sjob->height());

    LOG(INFO) << ">>>> found a new block: " << blkHash.ToString()
              << ", jobId: " << share.jobid() << ", userId: " << share.userid()
              << ", by: " << workerFullName << " <<<<";
  }

  // print out high diff share, 2^10 = 1024
  if ((bnBlockHash >> 10) <= bnNetworkTarget) {
    LOG(INFO) << "high diff share, blkhash: " << blkHash.ToString()
              << ", networkTarget: " << sjob->target_.ToString()
              << ", by: " << workerFullName;
  }

  // check share diff
  auto jobTarget =
      NetworkParamsDecred::get(sjob->network_).powLimit / share.sharediff();

  DLOG(INFO) << "blkHash: " << blkHash.ToString()
             << ", jobTarget: " << jobTarget.ToString()
             << ", networkTarget: " << sjob->target_.ToString();

  if (isEnableSimulator_ == false && bnBlockHash > jobTarget) {
    share.set_status(StratumStatus::LOW_DIFFICULTY);
    return;
  }

  // reach here means an valid share
  share.set_status(StratumStatus::ACCEPT);
}

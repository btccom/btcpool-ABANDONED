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

#include "StratumSessionDecred.h"
#include "StratumDecred.h"
#include "CommonDecred.h"

#include "StratumMiner.h"

#include "arith_uint256.h"

#include <boost/algorithm/string.hpp>
#include <boost/make_unique.hpp>
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

template <typename NetworkTraits>
JobRepositoryDecred<NetworkTraits>::JobRepositoryDecred(
    const char *kafkaBrokers,
    const char *consumerTopic,
    const string &fileLastNotifyTime,
    ServerDecred<NetworkTraits> *server)
  : JobRepositoryBase<ServerDecred<NetworkTraits>>(
        kafkaBrokers, consumerTopic, fileLastNotifyTime, server)
  , lastHeight_(0)
  , lastVoters_(0) {
}

template <typename NetworkTraits>
shared_ptr<StratumJob> JobRepositoryDecred<NetworkTraits>::createStratumJob() {
  return std::make_shared<StratumJobDecred>();
}

template <typename NetworkTraits>
void JobRepositoryDecred<NetworkTraits>::broadcastStratumJob(
    shared_ptr<StratumJob> sjob) {
  auto jobDecred = std::static_pointer_cast<StratumJobDecred>(sjob);
  if (!jobDecred) {
    LOG(ERROR) << "wrong job type: jobId = " << sjob->jobId_;
    return;
  }

  LOG(INFO) << "broadcasting job: " << *jobDecred;
  bool isClean = false;
  auto height = jobDecred->header_.height.value();
  if (height != lastHeight_) {
    LOG(INFO) << "received job with new height, old height = " << lastHeight_;
    isClean = true;
    lastHeight_ = height;
  }

  auto voters = jobDecred->header_.voters.value();
  bool moreVoters = voters > lastVoters_;

  shared_ptr<StratumJobEx> jobEx(this->createStratumJobEx(jobDecred, isClean));
  {
    ScopeLock sl(this->lock_);

    if (isClean) {
      // mark all jobs as stale, should do this before insert new job
      // stale shares will not be rejected, they will be marked as ACCEPT_STALE
      // and have lower rewards.
      for (auto it : this->exJobs_) {
        it.second->markStale();
      }
    }

    // insert new job
    this->exJobs_[jobDecred->jobId_] = jobEx;
  }

  // We want to update jobs immediately if there are more voters for the same
  // height block
  if (isClean || moreVoters) {
    lastVoters_ = voters;
    this->sendMiningNotify(jobEx);
  }
}

// nicehash protocol
// mining.notify: extra nonce 2 size is the actual extra nonce 2 size, extra
// nonce 1 is the actual extra nonce 1 mining.submit: extra nonce 2 is the
// actual extra nonce 2
class StratumProtocolDecredNiceHash : public StratumProtocolDecred {
public:
  string getExtraNonce1String(uint32_t extraNonce1) const override {
    return Strings::Format(
        "%08" PRIx32, boost::endian::endian_reverse(extraNonce1));
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
    return Strings::Format(
        "%024" PRIx32, boost::endian::endian_reverse(extraNonce1));
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

template <typename NetworkTraits>
ServerDecred<NetworkTraits>::ServerDecred(
    int32_t shareAvgSeconds, const libconfig::Config &config)
  : ServerBase<JobRepositoryDecred<NetworkTraits>>(shareAvgSeconds)
  , network_(NetworkDecred::MainNet) {
  string protocol;
  if (config.lookupValue("sserver.protocol", protocol)) {
    boost::algorithm::to_lower(protocol);
  }
  if (protocol == "gominer") {
    LOG(FATAL) << "Gominer is no longer a valid stratum protocol option";
  } else if (protocol == "nicehash") {
    LOG(INFO) << "Using nicehash stratum protocol";
    protocol_ = boost::make_unique<StratumProtocolDecredNiceHash>();
  } else {
    LOG(INFO) << "Using tpruvot stratum protocol";
    protocol_ = boost::make_unique<StratumProtocolDecredTPruvot>();
  }

  string network;
  if (config.lookupValue("sserver.network", network)) {
    boost::algorithm::to_lower(network);
  }
  if (network == "testnet") {
    LOG(INFO) << "Running testnet";
    network_ = NetworkDecred::TestNet;
  } else if (network == "simnet") {
    LOG(INFO) << "Running simnet";
    network_ = NetworkDecred::SimNet;
  } else {
    LOG(INFO) << "Running mainnet";
    network_ = NetworkDecred::MainNet;
  }
}

template <typename NetworkTraits>
unique_ptr<StratumSession> ServerDecred<NetworkTraits>::createConnection(
    bufferevent *bev, sockaddr *saddr, uint32_t sessionID) {
  return boost::make_unique<StratumSessionDecred<NetworkTraits>>(
      *this, bev, saddr, sessionID, *protocol_);
}

template <typename NetworkTraits>
JobRepository *ServerDecred<NetworkTraits>::createJobRepository(
    const char *kafkaBrokers,
    const char *consumerTopic,
    const string &fileLastNotifyTime) {
  return new JobRepositoryDecred<NetworkTraits>(
      kafkaBrokers, consumerTopic, fileLastNotifyTime, this);
}

template <typename NetworkTraits>
int ServerDecred<NetworkTraits>::checkShare(
    ShareDecred<NetworkTraits> &share,
    shared_ptr<StratumJobEx> exJobPtr,
    const vector<uint8_t> &extraNonce2,
    uint32_t ntime,
    uint32_t nonce,
    const string &workerFullName) {
  if (!exJobPtr || exJobPtr->isStale()) {
    return StratumStatus::JOB_NOT_FOUND;
  }

  auto sjob = std::static_pointer_cast<StratumJobDecred>(exJobPtr->sjob_);
  share.set_network(static_cast<uint32_t>(network_));
  share.set_voters(sjob->header_.voters.value());
  if (ntime > sjob->header_.timestamp.value() + 600) {
    return StratumStatus::TIME_TOO_NEW;
  }

  FoundBlockDecred foundBlock(
      share.jobid(),
      share.workerhashid(),
      share.userid(),
      workerFullName,
      sjob->header_,
      network_);
  auto &header = foundBlock.header_;
  header.timestamp = ntime;
  header.nonce = nonce;
  protocol_->setExtraNonces(header, share.sessionid(), extraNonce2);

  uint256 blkHash = header.getHash();
  auto bnBlockHash = UintToArith256(blkHash);
  auto bnNetworkTarget = UintToArith256(sjob->target_);

  //
  // found new block
  //
  if (this->isSubmitInvalidBlock_ == true || bnBlockHash <= bnNetworkTarget) {
    // send
    this->kafkaProducerSolvedShare_->produce(
        &foundBlock, sizeof(FoundBlockDecred));

    // mark jobs as stale
    this->GetJobRepository()->markAllJobsAsStale();

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
  auto jobTarget = NetworkTraits::Diff1Target / share.sharediff();

  DLOG(INFO) << "blkHash: " << blkHash.ToString()
             << ", jobTarget: " << jobTarget.ToString()
             << ", networkTarget: " << sjob->target_.ToString();

  if (this->isEnableSimulator_ == false && bnBlockHash > jobTarget) {
    return StratumStatus::LOW_DIFFICULTY;
  }

  // reach here means an valid share
  return StratumStatus::ACCEPT;
}

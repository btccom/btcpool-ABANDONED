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
#include "StratumServerEth.h"

#include "StratumSessionEth.h"
#include "DiffController.h"

#include <boost/thread.hpp>

#include <fstream>

#include "CommonEth.h"
#include "libethash/ethash.h"
#include "libethash/internal.h"

#include <arith_uint256.h>

using namespace std;

///////////////////////////// EthashCalculator ////////////////////////////////

EthashCalculator::EthashCalculator(const string &cacheFile)
  : cacheFile_(cacheFile) {
  if (!cacheFile_.empty()) {
    loadCacheFromFile(cacheFile_);
  }
}

EthashCalculator::~EthashCalculator() {
  if (!cacheFile_.empty()) {
    saveCacheToFile(cacheFile_);
  }

  ScopeLock sl(lock_);
  for (auto itr : lightCaches_) {
    ethash_light_delete(itr.second);
  }
}

size_t EthashCalculator::saveCacheToFile(const string &cacheFile) {
  ScopeLock sl(lock_);
  size_t loadedNum = 0;

  if (lightCaches_.empty()) {
    LOG(INFO) << "DAG cache was empty";
    return 0;
  }

  std::ofstream f(cacheFile, std::ios::binary | std::ios::trunc);
  if (!f) {
    LOG(ERROR) << "create DAG cache file " << cacheFile << " failed";
    return 0;
  }

  for (auto itr : lightCaches_) {
    saveCacheToFile(itr.second, f);
    loadedNum++;
  }

  f.close();
  LOG(INFO) << "saved " << loadedNum << " DAG caches to file " << cacheFile;
  return loadedNum;
}

void EthashCalculator::saveCacheToFile(
    const ethash_light_t &light, std::ofstream &f) {
  LightCacheHeader header;
  header.blockNumber_ = light->block_number;
  header.cacheSize_ = light->cache_size;
  header.checksum_ =
      computeCacheChecksum(header, (const uint8_t *)light->cache);

  f.write((const char *)&header, sizeof(header));
  f.write((const char *)light->cache, header.cacheSize_);
  f.flush();
}

size_t EthashCalculator::loadCacheFromFile(const string &cacheFile) {
  ScopeLock sl(lock_);
  size_t loadedNum = 0;

  std::ifstream f(cacheFile, std::ios::binary);
  if (!f) {
    LOG(WARNING) << "cannot read DAG cache file " << cacheFile;
    return 0;
  }

  ethash_light_t light;
  while (nullptr != (light = loadCacheFromFile(f))) {
    uint64_t epoch = light->block_number / ETHASH_EPOCH_LENGTH;
    lightCaches_[epoch] = light;
    loadedNum++;
  }

  f.close();
  LOG(INFO) << "loaded " << loadedNum << " DAG caches from file " << cacheFile;
  return loadedNum;
}

ethash_light_t EthashCalculator::loadCacheFromFile(std::ifstream &f) {
  LightCacheHeader header;
  uint64_t checksum;
  struct ethash_light *ret;

  f.read((char *)&header, sizeof(header));
  if (f.gcount() == 0) {
    // file EOF reached
    return nullptr;
  }
  if (f.gcount() != sizeof(header)) {
    LOG(WARNING) << "cannot load DAG cache: header should be " << sizeof(header)
                 << " bytes but read only " << f.gcount() << " bytes";
    return nullptr;
  }

  // ethash_light_delete() will use free() to release the memory.
  // So malloc() and calloc() should used for memory allocation.
  // The basic logic and codes copied from ethash_light_new_internal().
  ret = (ethash_light *)calloc(sizeof(*ret), 1);
  if (!ret) {
    LOG(WARNING) << "cannot load DAG cache: calloc " << sizeof(*ret)
                 << " bytes failed";
    return nullptr;
  }
#if defined(__MIC__)
  ret->cache = _mm_malloc((size_t)header.cacheSize_, 64);
#else
  ret->cache = malloc((size_t)header.cacheSize_);
#endif
  if (!ret->cache) {
    LOG(WARNING) << "cannot load DAG cache: malloc " << header.cacheSize_
                 << " bytes failed";
    goto fail_free_light;
  }

  f.read((char *)ret->cache, header.cacheSize_);
  if (f.gcount() != (std::streamsize)header.cacheSize_) {
    LOG(WARNING) << "cannot load DAG cache: cache should be "
                 << header.cacheSize_ << " bytes but read only " << f.gcount()
                 << " bytes";
    goto fail_free_cache_mem;
  }

  checksum = computeCacheChecksum(header, (const uint8_t *)ret->cache);
  if (checksum != header.checksum_) {
    LOG(WARNING) << "cannot load DAG light: checksum mis-matched, it should be "
                 << header.checksum_ << " but is " << checksum;
    goto fail_free_cache_mem;
  }

  ret->block_number = header.blockNumber_;
  ret->cache_size = header.cacheSize_;
  return ret;

fail_free_cache_mem:
#if defined(__MIC__)
  _mm_free(ret->cache);
#else
  free(ret->cache);
#endif
fail_free_light:
  free(ret);
  return nullptr;
}

uint64_t EthashCalculator::computeCacheChecksum(
    const LightCacheHeader &header, const uint8_t *data) {
  union {
    uint64_t u64;
    uint8_t u8[8];
  } checksum;

  checksum.u64 = 0;
  checksum.u64 += header.blockNumber_;
  checksum.u64 += header.cacheSize_;

  for (size_t i = 0; i < header.cacheSize_; i++) {
    checksum.u8[i % 8] += data[i];
  }

  return checksum.u64;
}

void EthashCalculator::buildDagCacheWithoutLock(uint64_t height) {
  uint64_t epoch = height / ETHASH_EPOCH_LENGTH;
  if (lightCaches_[epoch] != nullptr) {
    return;
  }

  LOG(INFO) << "building DAG cache for block height " << height << " (epoch "
            << epoch << ")";
  time_t beginTime = time(nullptr);

  lightCaches_[epoch] = ethash_light_new(height);

  // Note: The performance of ethash_light_new() difference between Debug and
  // Release builds is very large. The Release build may complete in 5 seconds,
  // while the Debug build takes more than 60 seconds.
  LOG(INFO) << "DAG cache for block height " << height << " (epoch " << epoch
            << ") built within " << (time(nullptr) - beginTime) << " seconds";
}

ethash_light_t EthashCalculator::getDagCacheWithoutLock(uint64_t height) {
  uint64_t epoch = height / ETHASH_EPOCH_LENGTH;
  if (lightCaches_[epoch] == nullptr) {
    buildDagCacheWithoutLock(height);
  }
  return lightCaches_[epoch];
}

void EthashCalculator::buildDagCache(uint64_t height) {
  uint64_t epoch = height / ETHASH_EPOCH_LENGTH;

  bool currentEpochExists = false;
  bool nextEpochExists = false;
  {
    ScopeLock sl(lock_);
    currentEpochExists = lightCaches_[epoch] != nullptr;
    nextEpochExists = lightCaches_[epoch + 1] != nullptr;
  }

  if (currentEpochExists && nextEpochExists) {
    return;
  }

  auto buildLightWithLock = [this](uint64_t height, uint64_t epoch) {
    {
      ScopeLock sl(lock_);
      if (buildingLightCaches_.find(epoch) != buildingLightCaches_.end()) {
        return;
      }
      buildingLightCaches_.insert(epoch);
    }

    LOG(INFO) << "building DAG cache for block height " << height << " (epoch "
              << epoch << ")";
    time_t beginTime = time(nullptr);

    ethash_light_t light = ethash_light_new(height);

    // Note: The performance of ethash_light_new() difference between Debug and
    // Release builds is very large. The Release build may complete in 5
    // seconds, while the Debug build takes more than 60 seconds.
    LOG(INFO) << "DAG cache for block height " << height << " (epoch " << epoch
              << ") built within " << (time(nullptr) - beginTime) << " seconds";

    ScopeLock sl(lock_);
    buildingLightCaches_.erase(epoch);

    if (lightCaches_[epoch] != nullptr) {
      // Other threads have added the same light.
      ethash_light_delete(light);
      return;
    }

    lightCaches_[epoch] = light;
  };

  if (!currentEpochExists) {
    buildLightWithLock(height, epoch);
  }

  if (!nextEpochExists) {
    buildLightWithLock(height + ETHASH_EPOCH_LENGTH, epoch + 1);
  }

  // remove redundant caches
  ScopeLock sl(lock_);
  lightCaches_.clear(kMaxCacheSize_, [](ethash_light_t eth) {
    if (eth) {
      ethash_light_delete(eth);
    }
  });
}

void EthashCalculator::rebuildDagCache(uint64_t height) {
  uint64_t epoch = height / ETHASH_EPOCH_LENGTH;

  LOG(INFO) << "rebuilding DAG cache for block height " << height;
  time_t beginTime = time(nullptr);

  ethash_light_t light = ethash_light_new(height);

  LOG(INFO) << "DAG cache for block height " << height << " rebuilt within "
            << (time(nullptr) - beginTime) << " seconds";

  ScopeLock sl(lock_);
  if (lightCaches_[epoch] != nullptr) {
    ethash_light_delete(lightCaches_[epoch]);
  } else {
    LOG(ERROR) << "EthashCalculator::rebuildDagCache(" << height
               << "): the old DAG cache should not be empty";
  }
  lightCaches_[epoch] = light;
}

bool EthashCalculator::compute(
    uint64_t height,
    const ethash_h256_t &header,
    uint64_t nonce,
    ethash_return_value_t &r) {
  ScopeLock sl(lock_);
  r = ethash_light_compute(getDagCacheWithoutLock(height), header, nonce);
  return r.success;
}

////////////////////////////////// JobRepositoryEth
//////////////////////////////////
JobRepositoryEth::JobRepositoryEth(
    size_t chainId,
    ServerEth *server,
    const char *kafkaBrokers,
    const char *consumerTopic,
    const string &fileLastNotifyTime,
    bool niceHashForced,
    uint64_t niceHashMinDiff,
    const std::string &niceHashMinDiffZookeeperPath)
  : JobRepositoryBase(
        chainId,
        server,
        kafkaBrokers,
        consumerTopic,
        fileLastNotifyTime,
        niceHashForced,
        niceHashMinDiff,
        niceHashMinDiffZookeeperPath)
  , ethashCalc_(Strings::Format(kLightCacheFilePathFormat, (uint32_t)chainId)) {
}

shared_ptr<StratumJobEx> JobRepositoryEth::createStratumJobEx(
    shared_ptr<StratumJob> sjob, bool isClean) {
  return std::make_shared<StratumJobEx>(chainId_, sjob, isClean);
}

void JobRepositoryEth::broadcastStratumJob(shared_ptr<StratumJob> sjob) {
  auto sjobEth = std::static_pointer_cast<StratumJobEth>(sjob);

  bool isClean = false;
  uint32_t height = sjobEth->height_;
  if (height > lastHeight_) {
    isClean = true;
    // lastHeight_ = sjobEth->height_;
    lastHeight_ = height;

    LOG(INFO) << "received new height " << GetServer()->chainName(chainId_)
              << " job " << sjobEth->jobId_ << ", height: " << sjobEth->height_
              << ", headerHash: " << sjobEth->headerHash_;
  } else {
    LOG(INFO) << "received " << GetServer()->chainName(chainId_) << " job "
              << sjobEth->jobId_ << ", height: " << sjobEth->height_
              << ", headerHash: " << sjobEth->headerHash_;
  }

  shared_ptr<StratumJobEx> exJob(createStratumJobEx(sjobEth, isClean));

  if (isClean) {
    // mark all jobs as stale, should do this before insert new job
    // stale shares will not be rejected, they will be marked as ACCEPT_STALE
    // and have lower rewards.
    for (auto it : exJobs_) {
      it.second->markStale();
    }
  }

  // get the last job
  shared_ptr<StratumJobEx> lastExJob = nullptr;
  if (!exJobs_.empty()) {
    lastExJob = exJobs_.rbegin()->second;
  }

  // insert new job
  exJobs_[sjobEth->jobId_] = exJob;

  if (isClean) {
    // Send the job immediately.
    // Sending a job immediately in this function is optional.
    // Jobs can also be sent by checkAndSendMiningNotify().
    sendMiningNotify(exJob);
  } else if (lastExJob != nullptr) {
    auto lastSjob = std::static_pointer_cast<StratumJobEth>(lastExJob->sjob_);

    // job update triggered by more uncles or more gas used
    if (((lastSjob->uncles_ < sjobEth->uncles_) ||
         (lastSjob->gasUsedPercent_ < 10.0 &&
          lastSjob->gasUsedPercent_ < sjobEth->gasUsedPercent_)) &&
        height >= lastHeight_) {
      sendMiningNotify(exJob);
    }
  }

  // Create light for verification.
  // This call should be made at least once and should not be optional.
  buildDagCacheNonBlocking(sjobEth->height_);
}

JobRepositoryEth::~JobRepositoryEth() {
}

void JobRepositoryEth::buildDagCacheNonBlocking(uint64_t height) {
  std::thread t([height, this]() { this->ethashCalc_.buildDagCache(height); });
  t.detach();
}

void JobRepositoryEth::rebuildDagCacheNonBlocking(uint64_t height) {
  std::thread t(
      [height, this]() { this->ethashCalc_.rebuildDagCache(height); });
  t.detach();
}

bool JobRepositoryEth::compute(
    uint64_t height,
    const ethash_h256_t &header,
    uint64_t nonce,
    ethash_return_value_t &r) {
  return ethashCalc_.compute(height, header, nonce, r);
}

////////////////////////////////// ServierEth ///////////////////////////////
bool ServerEth::setupInternal(const libconfig::Config &config) {
// TODO: WORK_WITH_STRATUM_SWITCHER only effects Bitcoin's sserver
#ifndef WORK_WITH_STRATUM_SWITCHER
  // Use 16 bits index of Session ID.
  // The full Session ID (with server id as prefix) is 24 bits.
  // Session ID will be used as starting nonce, so the single
  // searching space of a miner will be 2^40 (= 2^64 - 2^24).
  delete sessionIDManager_;
  sessionIDManager_ = new SessionIDManagerT<16>(serverId_);
  // NiceHash only accepts 2 bytes or shorter of extraNonce (startNonce) in
  // protocol NICEHASH_STRATUM. However we use a 3 bytes of extraNonce. Also,
  // the sessionID is pre-allocated, and we can't allocate more space for a
  // worker after detecting that it is from NiceHash. So we changed the default
  // setting to a large allocation interval. This can minimize the impact of
  // mining space overlap on NiceHash miners.
  sessionIDManager_->setAllocInterval(256);
#endif

  return true;
}

void ServerEth::checkShareAndUpdateDiff(
    size_t chainId,
    const ShareEth &share,
    const uint64_t jobId,
    const uint64_t nonce,
    const uint256 &header,
    const boost::optional<uint256> &mixHash,
    const boost::optional<uint32_t> &extraNonce2,
    const std::set<uint64_t> &jobDiffs,
    const string &workFullName,
    std::function<void(int32_t status, uint64_t diff, uint32_t bitsReached)>
        returnFn) {
  JobRepositoryEth *jobRepo = GetJobRepository(chainId);
  if (nullptr == jobRepo) {
    returnFn(StratumStatus::ILLEGAL_PARARMS, 0, 0);
    return;
  }

  shared_ptr<StratumJobEx> exJobPtr = jobRepo->getStratumJobEx(jobId);
  if (nullptr == exJobPtr) {
    returnFn(StratumStatus::JOB_NOT_FOUND, 0, 0);
    return;
  }

  auto sjob = std::static_pointer_cast<StratumJobEth>(exJobPtr->sjob_);
  bool withExtraNonce = sjob->hasHeader();
  bool preliminarySolution = false;

  if (mixHash) {
    ethash_h256_t ethashHeader, ethashMixHash, ethashTarget;
    Uint256ToEthash256(header, ethashHeader);
    Uint256ToEthash256(*mixHash, ethashMixHash);
    Uint256ToEthash256(sjob->networkTarget_, ethashTarget);
    if (ethash_quick_check_difficulty(
            &ethashHeader, nonce, &ethashMixHash, &ethashTarget)) {
      LOG(INFO) << "[" << chainName(chainId)
                << "] preliminary solution found, header hash: "
                << header.GetHex() << ", nonce: " << nonce
                << ", mix digest: " << mixHash->GetHex()
                << ", network target: " << sjob->networkTarget_.GetHex()
                << ", worker: " << workFullName;

      std::string extraNonce;
      if (withExtraNonce) {
        if (extraNonce2) {
          extraNonce = fmt::format(
              ",\"extraNonce\":\"0x{:08x}{:08x}\"",
              share.sessionid(),
              *extraNonce2);
        } else {
          extraNonce =
              fmt::format(",\"extraNonce\":\"0x{:08x}\"", share.sessionid());
        }
      }
      sendSolvedShare2Kafka(
          chainId,
          nonce,
          sjob->headerHash_,
          *mixHash,
          share.height(),
          share.networkdiff(),
          share.userid(),
          share.workerhashid(),
          workFullName,
          share.getChain(),
          extraNonce);
      preliminarySolution = true;
    }
  }

  dispatchToShareWorker([this,
                         jobRepo,
                         sjob,
                         header,
                         nonce,
                         share,
                         jobDiffs,
                         workFullName,
                         stale = exJobPtr->isStale(),
                         withExtraNonce,
                         extraNonce2,
                         preliminarySolution,
                         chainId,
                         returnFn = std::move(returnFn)]() {
    DLOG(INFO) << "checking share nonce: " << hex << nonce
               << ", header: " << header.GetHex();

    ethash_return_value_t r;
    ethash_h256_t ethashHeader = {0};
    Uint256ToEthash256(header, ethashHeader);

#ifndef NDEBUG
    // Calculate the time required of light verification.
    timeval start, end;
    long mtime, seconds, useconds;
    gettimeofday(&start, NULL);
#endif

    bool ret = jobRepo->compute(share.height(), ethashHeader, nonce, r);

#ifndef NDEBUG
    gettimeofday(&end, NULL);
    seconds = end.tv_sec - start.tv_sec;
    useconds = end.tv_usec - start.tv_usec;
    mtime = ((seconds)*1000 + useconds / 1000.0) + 0.5;
    // Note: The performance difference between Debug and Release builds is
    // very large. The Release build may complete in 4 ms, while the Debug
    // build takes 100 ms.
    DLOG(INFO) << "ethash computing takes " << mtime << " ms";
#endif

    if (!ret || !r.success) {
      LOG(ERROR) << "ethash computing failed, try rebuild the DAG cache";
      jobRepo->rebuildDagCacheNonBlocking(sjob->height_);
      dispatch([returnFn = std::move(returnFn)]() {
        returnFn(StratumStatus::INTERNAL_ERROR, 0, 0);
      });
      return;
    }

    auto returnedMixHash = Ethash256ToUint256(r.mix_hash);

    uint256 shareTarget = Ethash256ToUint256(r.result);

    // can not compare two uint256 directly because uint256 is little endian
    // and uses memcmp
    arith_uint256 bnShareTarget = UintToArith256(shareTarget);
    arith_uint256 bnNetworkTarget = UintToArith256(sjob->networkTarget_);
    uint32_t bitsReached = bnShareTarget.GetCompact();

    DLOG(INFO) << "comapre share target: " << shareTarget.GetHex()
               << ", network target: " << sjob->networkTarget_.GetHex();

    // print out high diff share, 2^10 = 1024
    if ((bnShareTarget >> 10) <= bnNetworkTarget) {
      LOG(INFO) << "high diff share, share target: " << shareTarget.GetHex()
                << ", network target: " << sjob->networkTarget_.GetHex()
                << ", worker: " << workFullName;
    }

    if (isSubmitInvalidBlock_ || bnShareTarget <= bnNetworkTarget) {
      LOG(INFO) << "[" << chainName(chainId)
                << "] solution found, share target: " << shareTarget.GetHex()
                << ", network target: " << sjob->networkTarget_.GetHex()
                << ", worker: " << workFullName;

      if (!preliminarySolution) {
        std::string extraNonce;
        if (withExtraNonce) {
          if (extraNonce2) {
            extraNonce = fmt::format(
                ",\"extraNonce\":\"0x{:08x}{:08x}\"",
                share.sessionid(),
                *extraNonce2);
          } else {
            extraNonce =
                fmt::format(",\"extraNonce\":\"0x{:08x}\"", share.sessionid());
          }
        }
        sendSolvedShare2Kafka(
            chainId,
            nonce,
            sjob->headerHash_,
            returnedMixHash,
            share.height(),
            share.networkdiff(),
            share.userid(),
            share.workerhashid(),
            workFullName,
            share.getChain(),
            extraNonce);
      }

      if (stale) {
        LOG(INFO) << "stale solved share: " << share.toString();
        dispatch([returnFn = std::move(returnFn), bitsReached]() {
          returnFn(StratumStatus::SOLVED_STALE, 0, bitsReached);
        });
        return;
      } else {
        LOG(INFO) << "solved share: " << share.toString();
        dispatch([returnFn = std::move(returnFn), bitsReached]() {
          returnFn(StratumStatus::SOLVED, 0, bitsReached);
        });
        return;
      }
    }

    // higher difficulty is prior
    for (auto itr = jobDiffs.rbegin(); itr != jobDiffs.rend(); itr++) {
      auto jobTarget = uint256S(Eth_DifficultyToTarget(*itr));
      DLOG(INFO) << "comapre share target: " << shareTarget.GetHex()
                 << ", job target: " << jobTarget.GetHex();

      if (isEnableSimulator_ || bnShareTarget <= UintToArith256(jobTarget)) {
        dispatch([returnFn = std::move(returnFn),
                  stale,
                  diff = *itr,
                  bitsReached]() {
          returnFn(
              stale ? StratumStatus::ACCEPT_STALE : StratumStatus::ACCEPT,
              diff,
              bitsReached);
        });
        return;
      }
    }

    dispatch([returnFn = std::move(returnFn), bitsReached]() {
      returnFn(StratumStatus::LOW_DIFFICULTY, 0, bitsReached);
    });
    return;
  });
}

void ServerEth::sendSolvedShare2Kafka(
    size_t chainId,
    uint64_t nonce,
    const string &headerHash,
    const uint256 &mixHash,
    const uint32_t height,
    const uint64_t networkDiff,
    int32_t userId,
    int64_t workerHashId,
    const string &workerFullName,
    const EthConsensus::Chain chain,
    const string &extraNonce) {
  string msg = Strings::Format(
      "{\"nonce\":\"%016x\",\"header\":\"%s\",\"mix\":\"%s\""
      ",\"height\":%u,\"networkDiff\":%u"
      "%s"
      ",\"userId\":%d"
      ",\"workerId\":%d"
      ",\"workerFullName\":\"%s\""
      ",\"chain\":\"%s\"}",
      nonce,
      headerHash,
      mixHash.GetHex().c_str(),
      height,
      networkDiff,
      extraNonce,
      userId,
      workerHashId,
      filterWorkerName(workerFullName),
      EthConsensus::getChainStr(chain));
  LOG(INFO) << "sending solved share: " << msg;
  ServerBase::sendSolvedShare2Kafka(chainId, msg.data(), msg.size());
}

unique_ptr<StratumSession> ServerEth::createConnection(
    struct bufferevent *bev, struct sockaddr *saddr, uint32_t sessionID) {
  return std::make_unique<StratumSessionEth>(*this, bev, saddr, sessionID);
}

JobRepository *ServerEth::createJobRepository(
    size_t chainId,
    const char *kafkaBrokers,
    const char *consumerTopic,
    const string &fileLastNotifyTime,
    bool niceHashForced,
    uint64_t niceHashMinDiff,
    const std::string &niceHashMinDiffZookeeperPath) {
  return new JobRepositoryEth(
      chainId,
      this,
      kafkaBrokers,
      consumerTopic,
      fileLastNotifyTime,
      niceHashForced,
      niceHashMinDiff,
      niceHashMinDiffZookeeperPath);
}

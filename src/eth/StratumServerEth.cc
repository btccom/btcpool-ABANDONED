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
#include <boost/make_unique.hpp>

#include <fstream>

#include "CommonEth.h"
#include "libethash/ethash.h"
#include "libethash/internal.h"

#include <arith_uint256.h>

using namespace std;


////////////////////////////////// JobRepositoryEth ///////////////////////////////
JobRepositoryEth::JobRepositoryEth(const char *kafkaBrokers, const char *consumerTopic, const string &fileLastNotifyTime, ServerEth *server)
  : JobRepositoryBase(kafkaBrokers, consumerTopic, fileLastNotifyTime, server)
  , light_(nullptr)
  , nextLight_(nullptr)
  , epochs_(0xffffffffffffffff)
{
  loadLightFromFile();
}

StratumJobEx* JobRepositoryEth::createStratumJobEx(StratumJob *sjob, bool isClean){
  return new StratumJobEx(sjob, isClean);
}

void JobRepositoryEth::broadcastStratumJob(StratumJob *sjob) {
  StratumJobEth* sjobEth = dynamic_cast<StratumJobEth*>(sjob);

  LOG(INFO) << "broadcast eth stratum job " << std::hex << sjobEth->jobId_;

  bool isClean = false;
  if (sjobEth->height_ != lastHeight_) {
    isClean = true;
    lastHeight_ = sjobEth->height_;

    LOG(INFO) << "received new height stratum job, height: " << sjobEth->height_
              << ", headerHash: " << sjobEth->headerHash_;
  }
  
  shared_ptr<StratumJobEx> exJob(createStratumJobEx(sjobEth, isClean));
  {
    ScopeLock sl(lock_);

    if (isClean) {
      // mark all jobs as stale, should do this before insert new job
      // stale shares will not be rejected, they will be marked as ACCEPT_STALE and have lower rewards.
      for (auto it : exJobs_) {
        it.second->markStale();
      }
    }

    // insert new job
    exJobs_[sjobEth->jobId_] = exJob;
  }

  //send job first
  sendMiningNotify(exJob);
  //then, create light for verification
  newLightNonBlocking(sjobEth);
}

JobRepositoryEth::~JobRepositoryEth() {
  saveLightToFile();
  deleteLight();
}

void JobRepositoryEth::rebuildLightNonBlocking(StratumJobEth* job) {
  epochs_ = 0;
  newLightNonBlocking(job);
}

void JobRepositoryEth::newLightNonBlocking(StratumJobEth* job) {
  if (nullptr == job)
    return;

  boost::thread t(boost::bind(&JobRepositoryEth::_newLightThread, this, job->height_));
  t.detach();
}

void JobRepositoryEth::_newLightThread(uint64_t height)
{
  uint64_t const newEpochs = height / ETHASH_EPOCH_LENGTH;
  //same seed do nothing
  if (newEpochs == epochs_) {
    return;
  }

  {
    ScopeLock slLight(lightLock_);
    ScopeLock slNextLight(nextLightLock_);

    // Update epochs_ immediately to prevent the next thread
    // blocking for waiting nextLightLock_.
    uint64_t oldEpochs = epochs_;
    epochs_ = newEpochs;

    LOG(INFO) << "creating light for blk height... " << height;
    time_t now = time(nullptr);

    if (nullptr == nextLight_) {
      light_ = ethash_light_new(height);
    }
    else if (newEpochs == oldEpochs + 1) {
      //get pre-generated light if exists
      ethash_light_delete(light_);
      light_ =  nextLight_;
      nextLight_ = nullptr;
    }
    else {
      // pre-generated light unavailable because of epochs jumping
      ethash_light_delete(nextLight_);
      nextLight_ = nullptr;

      ethash_light_delete(light_);
      // regenerate light with current epochs
      light_ = ethash_light_new(height);
    }

    if (nullptr == light_) {
      LOG(FATAL) << "create light for blk height: " << height << " failed";
    }

    time_t elapse = time(nullptr) - now;
    // Note: The performance difference between Debug and Release builds is very large.
    // The Release build may complete in 5 s, while the Debug build takes more than 60 s.
    LOG(INFO) << "create light for blk height: " << height << " takes " << elapse << " seconds";
  }

  {
    ScopeLock slNextLight(nextLightLock_);

    time_t now = time(nullptr);
    uint64_t nextBlkNum = height + ETHASH_EPOCH_LENGTH;
    LOG(INFO) << "creating light for blk height... " << nextBlkNum;

    nextLight_ = ethash_light_new(nextBlkNum);

    time_t elapse = time(nullptr) - now;
    // Note: The performance difference between Debug and Release builds is very large.
    // The Release build may complete in 5 s, while the Debug build takes more than 60 s.
    LOG(INFO) << "create light for blk height: " << nextBlkNum << " takes " << elapse << " seconds";
  }
}

void JobRepositoryEth::deleteLight()
{
  ScopeLock slLight(lightLock_);
  ScopeLock slNextLight(nextLightLock_);
  deleteLightNoLock();
}

void JobRepositoryEth::deleteLightNoLock() {
  if (light_ != nullptr) {
    ethash_light_delete(light_);
    light_ = nullptr;
  }

  if (nextLight_ != nullptr) {
    ethash_light_delete(nextLight_);
    nextLight_ = nullptr;
  }
}

void JobRepositoryEth::saveLightToFile() {
  ScopeLock slLight(lightLock_);
  ScopeLock slNextLight(nextLightLock_);

  if (light_ == nullptr && nextLight_ == nullptr) {
    LOG(INFO) << "no DAG light can be cached";
    return;
  }

  std::ofstream f(kLightCacheFilePath, std::ios::binary | std::ios::trunc);
  if (!f) {
    LOG(ERROR) << "create DAG light caching file " << kLightCacheFilePath << " failed";
    return;
  }

  if (light_ != nullptr) {
    LOG(INFO) << "cache DAG light of current epoch to file...";
    saveLightToFile(light_, f);
  }

  if (nextLight_ != nullptr) {
    LOG(INFO) << "cache DAG light of next epoch to file...";
    saveLightToFile(nextLight_, f);
  }

  f.close();
  LOG(INFO) << "DAG light was cached to file " << kLightCacheFilePath;
}

void JobRepositoryEth::saveLightToFile(const ethash_light_t &light, std::ofstream &f) {
  LightCacheHeader header;
  header.blockNumber_ = light->block_number;
  header.cacheSize_ = light->cache_size;
  header.checkSum_ = computeLightCacheCheckSum(header, (const uint8_t *)light->cache);

  f.write((const char *)&header, sizeof(header));
  f.write((const char *)light->cache, header.cacheSize_);
  f.flush();
}

void JobRepositoryEth::loadLightFromFile() {
  ScopeLock slLight(lightLock_);
  ScopeLock slNextLight(nextLightLock_);

  std::ifstream f(kLightCacheFilePath, std::ios::binary);
  if (!f) {
    LOG(WARNING) << "cannot read DAG light caching file " << kLightCacheFilePath;
    return;
  }

  LOG(INFO) << "load DAG light of current epoch from file...";
  light_ = loadLightFromFile(f);

  LOG(INFO) << "load DAG light of next epoch from file...";
  nextLight_ = loadLightFromFile(f);

  if (light_ != nullptr) {
    epochs_ = light_->block_number / ETHASH_EPOCH_LENGTH;
  }

  f.close();
  LOG(INFO) << "loading DAG light from file " << kLightCacheFilePath << " finished";
}

ethash_light_t JobRepositoryEth::loadLightFromFile(std::ifstream &f) {
  if (f.eof()) {
    LOG(WARNING) << "cannot load DAG light: file EOF reached when reading header";
    return NULL;
  }

  uint64_t checkSum;
  LightCacheHeader header;

  f.read((char *)&header, sizeof(header));
  if (f.gcount() != sizeof(header)) {
    LOG(WARNING) << "cannot load DAG light: only " << f.gcount() << " bytes was read"
                    " but header size is " << sizeof(header) << "bytes";
    return NULL;
  }

  // ethash_light_delete() will use free() to release the memory.
  // So malloc() and calloc() should used for memory allocation.
  // The basic logic and codes copied from ethash_light_new_internal().
	struct ethash_light *ret;
	ret = (ethash_light *)calloc(sizeof(*ret), 1);
	if (!ret) {
    LOG(WARNING) << "cannot load DAG light: calloc " << sizeof(*ret) << " bytes failed";
		return NULL;
	}
#if defined(__MIC__)
	ret->cache = _mm_malloc((size_t)header.cacheSize_, 64);
#else
	ret->cache = malloc((size_t)header.cacheSize_);
#endif
	if (!ret->cache) {
    LOG(WARNING) << "cannot load DAG light: malloc " << header.cacheSize_ << " bytes failed (cache maybe broken)";
		goto fail_free_light;
	}

  if (f.eof()) {
    LOG(WARNING) << "cannot load DAG light: file EOF reached when reading cache";
    goto fail_free_cache_mem;
  }

  f.read((char *)ret->cache, header.cacheSize_);
  if (f.gcount() != (std::streamsize)header.cacheSize_) {
    LOG(WARNING) << "cannot load DAG light: only " << f.gcount() << " bytes was read"
                 << " but cache size is " << header.cacheSize_ << " bytes";
    goto fail_free_cache_mem;
  }

  checkSum = computeLightCacheCheckSum(header, (const uint8_t *)ret->cache);
  if (checkSum != header.checkSum_) {
    LOG(WARNING) << "cannot load DAG light: checkSum mis-matched, it should be " << header.checkSum_
                 << " but is " << checkSum << " now";
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
	return NULL;
}

uint64_t JobRepositoryEth::computeLightCacheCheckSum(const LightCacheHeader &header, const uint8_t *data) {
  union {
    uint64_t u64;
    uint8_t  u8[8];
  } checkSum;

  checkSum.u64 = 0;
  checkSum.u64 += header.blockNumber_;
  checkSum.u64 += header.cacheSize_;
  
  for (size_t i=0; i<header.cacheSize_; i++) {
    checkSum.u8[i % 8] += data[i];
  }

  return checkSum.u64;
}

bool JobRepositoryEth::compute(ethash_h256_t const header, uint64_t nonce, ethash_return_value_t &r)
{
  ScopeLock sl(lightLock_);
  if (light_ != nullptr)
  {
    r = ethash_light_compute(light_, header, nonce);
    // LOG(INFO) << "ethash_light_compute: " << r.success << ", result: ";
    // for (int i = 0; i < 32; ++i)
    //   LOG(INFO) << hex << (int)r.result.b[i];

    // LOG(INFO) << "mixed hash: ";
    // for (int i = 0; i < 32; ++i)
    //   LOG(INFO) << hex << (int)r.mix_hash.b[i];

    return r.success;
  }
  LOG(ERROR) << "light_ is nullptr, what's wrong?";
  return false;
}


////////////////////////////////// ServierEth ///////////////////////////////
bool ServerEth::setupInternal(StratumServer* sserver) {
  // TODO: WORK_WITH_STRATUM_SWITCHER only effects Bitcoin's sserver
  #ifndef WORK_WITH_STRATUM_SWITCHER
    // Use 16 bits index of Session ID.
    // The full Session ID (with server id as prefix) is 24 bits.
    // Session ID will be used as starting nonce, so the single
    // searching space of a miner will be 2^40 (= 2^64 - 2^24).
    delete sessionIDManager_;
    sessionIDManager_ = new SessionIDManagerT<16>(serverId_);
    // NiceHash only accepts 2 bytes or shorter of extraNonce (startNonce) in protocol NICEHASH_STRATUM.
    // However we use a 3 bytes of extraNonce. Also, the sessionID is pre-allocated, and we can't allocate
    // more space for a worker after detecting that it is from NiceHash.
    // So we changed the default setting to a large allocation interval.
    // This can minimize the impact of mining space overlap on NiceHash miners.
    sessionIDManager_->setAllocInterval(256);
  #endif

  return true;
}

int ServerEth::checkShareAndUpdateDiff(ShareEth &share,
                                       const uint64_t jobId,
                                       const uint64_t nonce,
                                       const uint256 &header,
                                       const std::set<uint64_t> &jobDiffs,
                                       uint256 &returnedMixHash,
                                       const string &workFullName)
{
  JobRepositoryEth *jobRepo = GetJobRepository();
  if (nullptr == jobRepo) {
    return StratumStatus::ILLEGAL_PARARMS;
  }

  shared_ptr<StratumJobEx> exJobPtr = jobRepo->getStratumJobEx(jobId);
  if (nullptr == exJobPtr)
  {
    return StratumStatus::JOB_NOT_FOUND;
  }

  StratumJobEth *sjob = dynamic_cast<StratumJobEth *>(exJobPtr->sjob_);
  
  DLOG(INFO) << "checking share nonce: " << hex << nonce << ", header: " << header.GetHex();
  
  ethash_return_value_t r;
  ethash_h256_t ethashHeader = {0};
  Uint256ToEthash256(header, ethashHeader);

#ifndef NDEBUG
  // Calculate the time required of light verification.
  timeval start, end;
  long mtime, seconds, useconds;
  gettimeofday(&start, NULL);
#endif

  bool ret = jobRepo->compute(ethashHeader, nonce, r);

#ifndef NDEBUG
  gettimeofday(&end, NULL);
  seconds = end.tv_sec - start.tv_sec;
  useconds = end.tv_usec - start.tv_usec;
  mtime = ((seconds)*1000 + useconds / 1000.0) + 0.5;
  // Note: The performance difference between Debug and Release builds is very large.
  // The Release build may complete in 4 ms, while the Debug build takes 100 ms.
  DLOG(INFO) << "light compute takes " << mtime << " ms";
#endif

  if (!ret || !r.success)
  {
    LOG(ERROR) << "light cache creation error, try re-create it";
    jobRepo->rebuildLightNonBlocking(sjob);
    return StratumStatus::INTERNAL_ERROR;
  }

  returnedMixHash = Ethash256ToUint256(r.mix_hash);

  uint256 shareTarget = Ethash256ToUint256(r.result);
  
  //can not compare two uint256 directly because uint256 is little endian and uses memcmp
  arith_uint256 bnShareTarget = UintToArith256(shareTarget);
  arith_uint256 bnNetworkTarget = UintToArith256(sjob->networkTarget_);
  
  DLOG(INFO) << "comapre share target: " << shareTarget.GetHex()
             << ", network target: " << sjob->networkTarget_.GetHex();
  
  // print out high diff share, 2^10 = 1024
  if ((bnShareTarget >> 10) <= bnNetworkTarget) {
    LOG(INFO) << "high diff share, share target: " << shareTarget.GetHex()
              << ", network target: " << sjob->networkTarget_.GetHex()
              << ", worker: " << workFullName;
  }

  if (isSubmitInvalidBlock_ || bnShareTarget <= bnNetworkTarget) {
    LOG(INFO) << "solution found, share target: " << shareTarget.GetHex()
              << ", network target: " << sjob->networkTarget_.GetHex()
              << ", worker: " << workFullName;

    if (exJobPtr->isStale()) {
      LOG(INFO) << "stale solved share: " << share.toString();
      return StratumStatus::SOLVED_STALE;
    }
    else {
      LOG(INFO) << "solved share: " << share.toString();
      return StratumStatus::SOLVED;
    }
    
  }

  // higher difficulty is prior
  for (auto itr = jobDiffs.rbegin(); itr != jobDiffs.rend(); itr++) {
    auto jobTarget = uint256S(Eth_DifficultyToTarget(*itr));
    DLOG(INFO) << "comapre share target: " << shareTarget.GetHex() << ", job target: " << jobTarget.GetHex();

    if (isEnableSimulator_ || bnShareTarget <= UintToArith256(jobTarget)) {
      share.shareDiff_ = *itr;
      return exJobPtr->isStale() ? StratumStatus::ACCEPT_STALE : StratumStatus::ACCEPT;
    }
  }

  return StratumStatus::LOW_DIFFICULTY;
}

void ServerEth::sendSolvedShare2Kafka(const string &strNonce, const string &strHeader, const string &strMix,
                                      const uint32_t height, const uint64_t networkDiff, const StratumWorker &worker,
                                      const EthConsensus::Chain chain)
{
  string msg = Strings::Format("{\"nonce\":\"%s\",\"header\":\"%s\",\"mix\":\"%s\","
                               "\"height\":%lu,\"networkDiff\":%" PRIu64 ",\"userId\":%ld,"
                               "\"workerId\":%" PRId64 ",\"workerFullName\":\"%s\","
                               "\"chain\":\"%s\"}",
                               strNonce.c_str(), strHeader.c_str(), strMix.c_str(),
                               height, networkDiff, worker.userId_,
                               worker.workerHashId_, filterWorkerName(worker.fullName_).c_str(),
                               EthConsensus::getChainStr(chain).c_str());
  kafkaProducerSolvedShare_->produce(msg.c_str(), msg.length());
}

unique_ptr<StratumSession> ServerEth::createConnection(struct bufferevent *bev, struct sockaddr *saddr, uint32_t sessionID)
{
  return boost::make_unique<StratumSessionEth>(*this, bev, saddr, sessionID);
}

JobRepository *ServerEth::createJobRepository(const char *kafkaBrokers,
                                            const char *consumerTopic,
                                           const string &fileLastNotifyTime)
{
  return new JobRepositoryEth(kafkaBrokers, consumerTopic, fileLastNotifyTime, this);
}

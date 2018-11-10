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
#include "WatcherBitcoin.h"

#include "BitcoinUtils.h"

#include "utilities_js.hpp"

#include <chainparams.h>
#include <hash.h>


//
// input  : 89c2f63dfb970e5638aa66ae3b7404a8a9914ad80328e9fe0000000000000000
// output : 00000000000000000328e9fea9914ad83b7404a838aa66aefb970e5689c2f63d
static
string convertPrevHash(const string &prevHash) {
  assert(prevHash.length() == 64);
  string hash;
  for (int i = 7; i >= 0; i--) {
    uint32_t v = (uint32_t)strtoul(prevHash.substr(i*8, 8).c_str(), nullptr, 16);
    hash.append(Strings::Format("%08x", v));
  }
  return hash;
}



///////////////////////////////// ClientContainer //////////////////////////////
ClientContainerBitcoin::ClientContainerBitcoin(const string &kafkaBrokers, const string &jobTopic, const string &gbtTopic)
  : ClientContainer(kafkaBrokers, jobTopic, gbtTopic)
  , poolStratumJob_(nullptr)
{
}

ClientContainerBitcoin::~ClientContainerBitcoin() 
{
}

boost::shared_lock<boost::shared_mutex> ClientContainerBitcoin::getPoolStratumJobReadLock() {
  return boost::shared_lock<boost::shared_mutex>(stratumJobMutex_);
}

const StratumJobBitcoin * ClientContainerBitcoin::getPoolStratumJob() {
  return poolStratumJob_;
}

PoolWatchClient* ClientContainerBitcoin::createPoolWatchClient( 
                struct event_base *base, const string &poolName, const string &poolHost,
                const int16_t poolPort, const string &workerName)
{
  return new PoolWatchClientBitcoin(base, this,
                                 poolName, poolHost, poolPort, workerName);
}

void ClientContainerBitcoin::consumeStratumJobInternal(const string& str) 
{
    StratumJobBitcoin *sjob = new StratumJobBitcoin();
    bool res = sjob->unserializeFromJson((const char *)str.data(), str.size());
    if (res == false) {
      LOG(ERROR) << "unserialize stratum job fail";
      delete sjob;
      return;
    }

    // make sure the job is not expired.
    if (jobId2Time(sjob->jobId_) + 60 < time(nullptr)) {
      LOG(ERROR) << "too large delay from kafka to receive topic 'StratumJob'";
      delete sjob;
      return;
    }

    LOG(INFO) << "[POOL] stratum job received, height: " << sjob->height_
              << ", prevhash: " << sjob->prevHash_.ToString()
              << ", nBits: " << sjob->nBits_;

    {
      // get a write lock before change this->poolStratumJob_
      // it will unlock by itself in destructor.
      boost::unique_lock<boost::shared_mutex> writeLock(stratumJobMutex_);

      uint256 oldPrevHash;

      if (poolStratumJob_ != nullptr) {
        oldPrevHash = poolStratumJob_->prevHash_;
        delete poolStratumJob_;
      }

      poolStratumJob_ = sjob;

      if (oldPrevHash != sjob->prevHash_) {
        LOG(INFO) << "[POOL] prev block changed, height: " << sjob->height_
                  << ", prevhash: " << sjob->prevHash_.ToString()
                  << ", nBits: " << sjob->nBits_;
      }
    }
}

bool ClientContainerBitcoin::sendEmptyGBT(int32_t blockHeight, uint32_t nBits,
                                   const string &blockPrevHash,
                                   uint32_t blockTime, uint32_t blockVersion) {

  // generate empty GBT
  string gbt;
  gbt += Strings::Format("{\"result\":{");

  gbt += Strings::Format("\"previousblockhash\":\"%s\"", blockPrevHash.c_str());
  gbt += Strings::Format(",\"height\":%d", blockHeight);
  const CChainParams& chainparams = Params();
  gbt += Strings::Format(",\"coinbasevalue\":%" PRId64"",
                         GetBlockReward(blockHeight, chainparams.GetConsensus()));
  gbt += Strings::Format(",\"bits\":\"%08x\"", nBits);
  const uint32_t minTime = blockTime - 60*10;  // just set 10 mins ago
  gbt += Strings::Format(",\"mintime\":%" PRIu32"", minTime);
  gbt += Strings::Format(",\"curtime\":%" PRIu32"", blockTime);
  gbt += Strings::Format(",\"version\":%" PRIu32"", blockVersion);
  gbt += Strings::Format(",\"transactions\":[]");  // empty transactions

  gbt += Strings::Format("}}");

  const uint256 gbtHash = Hash(gbt.begin(), gbt.end());

  string sjob = Strings::Format("{\"created_at_ts\":%u,"
                                "\"block_template_base64\":\"%s\","
                                "\"gbthash\":\"%s\"}",
                                (uint32_t)time(nullptr),
                                EncodeBase64(gbt).c_str(),
                                gbtHash.ToString().c_str());

  // submit to Kafka
  kafkaProducer_.produce(sjob.c_str(), sjob.length());

  LOG(INFO) << "sumbit to Kafka, msg len: " << sjob.length();
  LOG(INFO) << "empty gbt: " << gbt;

  return true;
}

string ClientContainerBitcoin::createOnConnectedReplyString() const
{
  string s = Strings::Format("{\"id\":1,\"method\":\"mining.subscribe\""
                              ",\"params\":[\"%s\"]}\n", BTCCOM_WATCHER_AGENT);
  return s;
}

///////////////////////////////// PoolWatchClient //////////////////////////////
PoolWatchClientBitcoin::PoolWatchClientBitcoin(struct event_base *base, ClientContainerBitcoin *container,
                                 const string &poolName,
                                 const string &poolHost, const int16_t poolPort,
                                 const string &workerName)
  : PoolWatchClient(base, container, poolName, poolHost, poolPort, workerName) 
  , extraNonce1_(0), extraNonce2Size_(0)
{

}

PoolWatchClientBitcoin::~PoolWatchClientBitcoin() 
{
}


void PoolWatchClientBitcoin::handleStratumMessage(const string &line) {
  DLOG(INFO) << "<" << poolName_ << "> UpPoolWatchClient recv(" << line.size() << "): " << line;

  auto containerBitcoin = GetContainerBitcoin();

  JsonNode jnode;
  if (!JsonNode::parse(line.data(), line.data() + line.size(), jnode)) {
    LOG(ERROR) << "decode line fail, not a json string";
    return;
  }
  JsonNode jresult = jnode["result"];
  JsonNode jerror  = jnode["error"];
  JsonNode jmethod = jnode["method"];

  if (jmethod.type() == Utilities::JS::type::Str) {
    JsonNode jparams  = jnode["params"];
    auto jparamsArr = jparams.array();

    if (jmethod.str() == "mining.notify") {
      const string prevHash = convertPrevHash(jparamsArr[1].str());

      if (lastPrevBlockHash_.empty()) {
        lastPrevBlockHash_ = prevHash;  // first set prev block hash
      }

      // stratum job prev block hash changed
      if (lastPrevBlockHash_ != prevHash) {

        // block height in coinbase (BIP34)
        const int32_t  blockHeight = getBlockHeightFromCoinbase(jparamsArr[2].str());
        
        // nBits, the encoded form of network target
        const uint32_t nBits       = jparamsArr[6].uint32_hex();

        // only for display, it will be replaced by current system time
        uint32_t blockTime   = jparamsArr[7].uint32_hex();

        // only for display, it will be replaced by current poolStratumJob's nVersion
        uint32_t nVersion    = jparamsArr[5].uint32_hex();

        lastPrevBlockHash_ = prevHash;
        LOG(INFO) << "<" << poolName_ << "> prev block changed, height: " << blockHeight
                               << ", prev_hash: " << prevHash
                               << ", block_time: " << blockTime
                               << ", nBits: " << nBits
                               << ", nVersion: " << nVersion;

        //////////////////////////////////////////////////////////////////////////
        // To ensure the external job is not deviation from the blockchain.
        // 
        // eg. a Bitcoin pool may receive a Bitcoin Cash job from a external
        // stratum server, because the stratum server is automatic switched
        // between Bitcoin and Bitcoin Cash depending on profit.
        //////////////////////////////////////////////////////////////////////////
        {
          // get a read lock before lookup this->poolStratumJob_
          // it will unlock by itself in destructor.
          auto readLock = containerBitcoin->getPoolStratumJobReadLock();
          const StratumJobBitcoin *poolStratumJob = containerBitcoin->getPoolStratumJob();

          if (poolStratumJob == nullptr) {
            LOG(WARNING) << "<" << poolName_ << "> discard the job: pool stratum job is empty";
            return;
          }

          if (blockHeight == poolStratumJob->height_) {
            LOG(INFO) << "<" << poolName_ << "> discard the job: height is same as pool."
                                      << " pool height: " << poolStratumJob->height_
                                      << ", the job height: " << blockHeight;
            return;
          }

          if (blockHeight != poolStratumJob->height_ + 1) {
            LOG(WARNING) << "<" << poolName_ << "> discard the job: height jumping too much."
                                      << " pool height: " << poolStratumJob->height_
                                      << ", the job height: " << blockHeight;
            return;
          }

#ifdef CHAIN_TYPE_BCH
          // BCH adjusts the difficulty in each block,
          // its DAA algorithm will produce a difficulty change between 0.5 and 2 times.
          // @see <https://www.bitcoinabc.org/2017-11-01-DAA/>
          double poolDiff, jobDiff;
          BitsToDifficulty(poolStratumJob->nBits_, &poolDiff);
          BitsToDifficulty(nBits, &jobDiff);
          double multiple = jobDiff / poolDiff;
          if (multiple < 0.5 || multiple > 2.0) {
            LOG(WARNING) << "<" << poolName_ << "> discard the job: difficulty changes too much."
                         << " pool diff: " << poolDiff << " (" << poolStratumJob->nBits_ << ")"
                         << ", the job diff: " << jobDiff << " (" << nBits
                         << ", = " << multiple << "x pool diff)";
            return;
          }
#else
          // Except for BCH, other blockchains do not adjust the difficulty in each block.
          if (nBits != poolStratumJob->nBits_) {
            LOG(WARNING) << "<" << poolName_ << "> discard the job: nBits different from pool job."
                         << " pool nBits: " << poolStratumJob->nBits_
                         << ", the job nBits: " << nBits;
            return;
          }
#endif

          // the block time from other pool may have a deviation with the current time.
          // so replaced it by current system time.
          blockTime = (uint32_t)time(nullptr);

          // the nVersion from other pool may have some flags that we don't want.
          // so replaced it by current poolStratumJob's.
          nVersion = poolStratumJob->nVersion_;
        }

        containerBitcoin->sendEmptyGBT(blockHeight, nBits, prevHash, blockTime, nVersion);

      }
    }
    else {
      // ignore other messages
    }
    return;
  }

  if (state_ == AUTHENTICATED) {
    //
    // {"error": null, "id": 2, "result": true}
    //
    if (jerror.type()  != Utilities::JS::type::Null ||
        jresult.type() != Utilities::JS::type::Bool ||
        jresult.boolean() != true) {
      LOG(ERROR) << poolName_ <<  " auth fail";
    }
    return;
  }

  if (state_ == CONNECTED) {
    //
    // {"id":1,"result":[[["mining.set_difficulty","01000002"],
    //                    ["mining.notify","01000002"]],"01000002",8],"error":null}
    //
    if (jerror.type() != Utilities::JS::type::Null) {
      LOG(ERROR) << "<" << poolName_ << "> json result is null, err: " << jerror.str();
      return;
    }
    std::vector<JsonNode> resArr = jresult.array();
    if (resArr.size() < 3) {
      LOG(ERROR) << "<" << poolName_ << "> result element's number is less than 3: " << line;
      return;
    }

    extraNonce1_     = resArr[1].uint32_hex();
    extraNonce2Size_ = resArr[2].uint32();
    LOG(INFO) << "<" << poolName_ << "> extraNonce1: " << extraNonce1_
    << ", extraNonce2 Size: " << extraNonce2Size_;

    // subscribe successful
    state_ = SUBSCRIBED;

    // do mining.authorize
    string s = Strings::Format("{\"id\": 1, \"method\": \"mining.authorize\","
                               "\"params\": [\"%s\", \"\"]}\n",
                               workerName_.c_str());
    sendData(s);
    return;
  }

  if (state_ == SUBSCRIBED && jresult.boolean() == true) {
    // authorize successful
    state_ = AUTHENTICATED;
    LOG(INFO) << "<" << poolName_ << "> auth success, name: \"" << workerName_ << "\"";
    return;
  }
}

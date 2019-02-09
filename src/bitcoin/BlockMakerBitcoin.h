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
#ifndef BLOCK_MAKER_BITCOIN_H_
#define BLOCK_MAKER_BITCOIN_H_

#include "BlockMaker.h"
#include "StratumBitcoin.h"

#include <uint256.h>
#include <primitives/transaction.h>

#include <deque>
#include <boost/date_time/posix_time/posix_time.hpp>

class CBlockHeader;

namespace bpt = boost::posix_time;

// class BlockMakerHandler {
//   public:
//     virtual ~BlockMakerHandler() = 0; // mark it's an abstract class
//     virtual void init(const BlockMakerDefinition &def) { def_ = def; }

//     // read-only definition
//     virtual const BlockMakerDefinition& def() { return def_; }
//     virtual void processSolvedShare(rd_kafka_message_t *rkmessage) = 0;
//     // Interface with the GwMaker.
//     // There is a default implementation that use virtual functions below.
//     // If the implementation does not meet the requirements, you can overload
//     it
//     // and ignore all the following virtual functions.
//   //   virtual string makeRawGwMsg();

//   // protected:

//   //   // These virtual functions make it easier to implement the
//   makeRawGwMsg() interface.
//   //   // In most cases, you just need to override getRequestData() and
//   processRawGw().
//   //   // If you have overloaded makeRawGwMsg() above, you can ignore all the
//   following functions.

//   //   // Receive rpc response and generate RawGw message for the pool.
//   //   virtual string processRawGw(const string &gw) { return ""; }

//   //   // Call RPC `getwork` and get the response.
//   //   virtual bool callRpcGw(string &resp);

//   //   // Body of HTTP POST used by callRpcGw().
//   //   // return "" if use HTTP GET.
//   //   virtual string getRequestData() { return ""; }
//   //   // HTTP header `User-Agent` used by callRpcGw().
//   //   virtual string getUserAgent() { return "curl"; }

//     // blockchain and RPC-server definitions
//     BlockMakerDefinition def_;
// };

// class BlockMakerHandlerEth : public BlockMakerHandler{
//   virtual void processSolvedShare(rd_kafka_message_t *rkmessage);
// };

// class BlockMakerHandlerSia : public BlockMakerHandler{
//   virtual void processSolvedShare(rd_kafka_message_t *rkmessage) {

//   }
// };

////////////////////////////////// BlockMaker //////////////////////////////////
class BlockMakerBitcoin : public BlockMaker {
protected:
#ifdef CHAIN_TYPE_BCH
  mutex rawGbtlightLock_;
  std::map<uint256, std::string> rawGbtlightMap_;
#endif // CHAIN_TYPE_BCH

  mutex rawGbtLock_;
  size_t kMaxRawGbtNum_; // how many rawgbt should we keep
  // key: gbthash
  std::deque<uint256> rawGbtQ_;
  // key: gbthash, value: block template json
  std::map<uint256, shared_ptr<vector<CTransactionRef>>> rawGbtMap_;

  mutex jobIdMapLock_;
  size_t kMaxStratumJobNum_;
  // key: jobId, value: gbthash
  std::map<uint64_t, uint256> jobId2GbtHash_;

  bpt::ptime lastSubmittedBlockTime;
  uint32_t submittedRskBlocks;

  KafkaConsumer kafkaConsumerRawGbt_;
  KafkaConsumer kafkaConsumerStratumJob_;
  KafkaConsumer kafkaConsumerNamecoinSolvedShare_;
  KafkaConsumer kafkaConsumerRskSolvedShare_;

  void insertRawGbt(
      const uint256 &gbtHash, shared_ptr<vector<CTransactionRef>> vtxs);

  thread threadConsumeRawGbt_;
  thread threadConsumeStratumJob_;
  thread threadConsumeNamecoinSolvedShare_;
  thread threadConsumeRskSolvedShare_;

  std::map<std::string /*rpcaddr*/, bool /*isSupportSubmitAuxBlock*/>
      isAddrSupportSubmitAux_;

  void runThreadConsumeRawGbt();
  void runThreadConsumeStratumJob();
  void runThreadConsumeNamecoinSolvedShare();
  void runThreadConsumeRskSolvedShare();

  void consumeRawGbt(rd_kafka_message_t *rkmessage);
  void consumeStratumJob(rd_kafka_message_t *rkmessage);
  void consumeSolvedShare(rd_kafka_message_t *rkmessage);
  void processSolvedShare(rd_kafka_message_t *rkmessage) override;
  void consumeNamecoinSolvedShare(rd_kafka_message_t *rkmessage);
  void consumeRskSolvedShare(rd_kafka_message_t *rkmessage);

  void addRawgbt(const char *str, size_t len);

  void saveBlockToDBNonBlocking(
      const FoundBlock &foundBlock,
      const CBlockHeader &header,
      const uint64_t coinbaseValue,
      const int32_t blksize);
  void _saveBlockToDBThread(
      const FoundBlock &foundBlock,
      const CBlockHeader &header,
      const uint64_t coinbaseValue,
      const int32_t blksize);
#ifdef CHAIN_TYPE_BCH
  void
  submitBlockLightNonBlocking(const string &blockHex, const string &job_id);
  void _submitBlockLightThread(
      const string &rpcAddress,
      const string &rpcUserpass,
      const string &job_id,
      const string &blockHex);
#endif // CHAIN_TYPE_BCH

  void submitBlockNonBlocking(const string &blockHex);
  void _submitBlockThread(
      const string &rpcAddress,
      const string &rpcUserpass,
      const string &blockHex);
  bool checkBitcoinds();

  void submitNamecoinBlockNonBlocking(
      const string &auxBlockHash,
      const string &auxPow,
      const string &bitcoinBlockHash,
      const string &rpcAddress,
      const string &rpcUserpass);
  void _submitNamecoinBlockThread(
      const string &auxBlockHash,
      const string &auxPow,
      const string &bitcoinBlockHash,
      const string &rpcAddress,
      const string &rpcUserpass);

  void submitRskBlockPartialMerkleNonBlocking(
      const string &rpcAddress,
      const string &rpcUserPwd,
      const string &blockHashHex,
      const string &blockHeaderHex,
      const string &coinbaseHex,
      const string &merkleHashesHex,
      const string &totalTxCount);
  void _submitRskBlockPartialMerkleThread(
      const string &rpcAddress,
      const string &rpcUserPwd,
      const string &blockHashHex,
      const string &blockHeaderHex,
      const string &coinbaseHex,
      const string &merkleHashesHex,
      const string &totalTxCount);
  bool submitToRskNode();

  // read-only definition
  inline shared_ptr<const BlockMakerDefinitionBitcoin> def() {
    return std::dynamic_pointer_cast<const BlockMakerDefinitionBitcoin>(def_);
  }

public:
  BlockMakerBitcoin(
      shared_ptr<BlockMakerDefinition> def,
      const char *kafkaBrokers,
      const MysqlConnectInfo &poolDB);
  virtual ~BlockMakerBitcoin();

  bool init() override;
  void run() override;
};

#endif

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

#ifdef CHAIN_TYPE_ZEC
using CTransactionRef = std::shared_ptr<const CTransaction>;
#endif

////////////////////////////////// BlockMaker //////////////////////////////////
class BlockMakerBitcoin : public BlockMaker {
protected:
#if defined(CHAIN_TYPE_BCH) || defined(CHAIN_TYPE_BSV)
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
  std::map<uint64_t, std::vector<SubPoolJobBitcoin>> jobId2SubPool_;

  bpt::ptime lastSubmittedBlockTime;
  uint32_t submittedRskBlocks;

  KafkaSimpleConsumer kafkaConsumerRawGbt_;
  KafkaSimpleConsumer kafkaConsumerStratumJob_;
#ifndef CHAIN_TYPE_ZEC
  KafkaSimpleConsumer kafkaConsumerNamecoinSolvedShare_;
  KafkaSimpleConsumer kafkaConsumerRskSolvedShare_;
#endif

  struct AuxBlockInfo {
    uint256 nmcBlockHash_;
    uint256 nmcNetworkTarget_;
    uint256 vcashBlockHash_;
    uint256 vcashNetworkTarget_;
    string vcashRpcAddress_;
    string vcashRpcUserPwd_;
  };

  mutex jobIdAuxBlockInfoLock_;
  std::map<uint64_t, shared_ptr<AuxBlockInfo>> jobId2AuxHash_;
  mutex jobId2RskMMHashLock_;
  std::map<uint64_t, uint256> jobId2RskHashForMergeMining_;

  void insertRawGbt(
      const uint256 &gbtHash, shared_ptr<vector<CTransactionRef>> vtxs);

  thread threadConsumeRawGbt_;
  thread threadConsumeStratumJob_;
#ifndef CHAIN_TYPE_ZEC
  thread threadConsumeNamecoinSolvedShare_;
  thread threadConsumeRskSolvedShare_;

  std::map<std::string /*rpcaddr*/, bool /*isSupportSubmitAuxBlock*/>
      isAddrSupportSubmitAux_;
#endif

  void runThreadConsumeRawGbt();
  void runThreadConsumeStratumJob();
#ifndef CHAIN_TYPE_ZEC
  void runThreadConsumeNamecoinSolvedShare();
  void runThreadConsumeRskSolvedShare();
#endif

  void consumeRawGbt(rd_kafka_message_t *rkmessage);
  void consumeStratumJob(rd_kafka_message_t *rkmessage);
  void consumeSolvedShare(rd_kafka_message_t *rkmessage);
  void processSolvedShare(rd_kafka_message_t *rkmessage) override;
#ifndef CHAIN_TYPE_ZEC
  void consumeNamecoinSolvedShare(rd_kafka_message_t *rkmessage);
  void consumeRskSolvedShare(rd_kafka_message_t *rkmessage);
#endif
  void addRawgbt(const char *str, size_t len);

  void saveBlockToDBNonBlocking(
      const FoundBlock &foundBlock,
      const CBlockHeader &header,
      const string &coinbaseTxBin,
      const uint64_t coinbaseValue,
      const int32_t blksize);
  void _saveBlockToDBThread(
      const FoundBlock &foundBlock,
      const CBlockHeader &header,
      const string &coinbaseTxBin,
      const uint64_t coinbaseValue,
      const int32_t blksize);
#if defined(CHAIN_TYPE_BCH)
  void
  submitBlockLightNonBlocking(const string &blockHex, const string &job_id);
  void _submitBlockLightThread(
      const string &rpcAddress,
      const string &rpcUserpass,
      const string &job_id,
      const string &blockHex);
#elif defined(CHAIN_TYPE_BSV)
  void submitBlockLightNonBlocking(
      const string &job_id,
      const string &coinbaseTx,
      int32_t version,
      uint32_t ntime,
      uint32_t nonce);
  void _submitBlockLightThread(
      const string &rpcAddress,
      const string &rpcUserpass,
      const string &job_id,
      const string &coinbaseTx,
      int32_t version,
      uint32_t ntime,
      uint32_t nonce);
#endif // CHAIN_TYPE_BCH

  void submitBlockNonBlocking(const string &blockHex);
  void _submitBlockThread(
      const string &rpcAddress,
      const string &rpcUserpass,
      const string &blockHex);
  bool checkBitcoinds();

#ifndef CHAIN_TYPE_ZEC
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
      const string &totalTxCount,
      const string &rskHashForMergeMiningHex);
  void _submitRskBlockPartialMerkleThread(
      const string &rpcAddress,
      const string &rpcUserPwd,
      const string &blockHashHex,
      const string &blockHeaderHex,
      const string &coinbaseHex,
      const string &merkleHashesHex,
      const string &totalTxCount,
      const string &rskHashForMergeMiningHex);
  void submitVcashBlockNonBlocking(
      const string &rpcAddress,
      const string &rpcUserPwd,
      const string &blockHashHex,
      const string &blockHeaderHex,
      const string &coinbaseHex,
      const string &merkleHashesHex,
      const string &totalTxCount,
      const string &bitcoinblockhash);
  void _submitVcashBlockThread(
      const string &rpcAddress,
      const string &rpcUserPwd,
      const string &blockHashHex,
      const string &blockHeaderHex,
      const string &coinbaseHex,
      const string &merkleHashesHex,
      const string &totalTxCount,
      const string &bitcoinblockhash);
  void insertAuxBlock2Mysql(
      const string auxtablename,
      const string chainnane,
      const string auxblockhash,
      const string parentblockhask,
      const string submitresponse,
      const string auxpow = "");
  bool submitToRskNode();
#endif

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

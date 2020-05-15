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
#include "BlockMakerBitcoin.h"

#include "StratumBitcoin.h"

#include "BitcoinUtils.h"

#include "rsk/RskSolvedShareData.h"

#ifdef CHAIN_TYPE_ZEC
static inline CTransactionRef MakeTransactionRef() {
  return std::make_shared<const CTransaction>();
}
template <typename Tx>
static inline CTransactionRef MakeTransactionRef(Tx &&txIn) {
  return std::make_shared<const CTransaction>(std::forward<Tx>(txIn));
}
#else
#include <consensus/merkle.h>
#endif

#include <boost/thread.hpp>

#include <streams.h>

////////////////////////////////// BlockMaker //////////////////////////////////
BlockMakerBitcoin::BlockMakerBitcoin(
    shared_ptr<BlockMakerDefinition> blkMakerDef,
    const char *kafkaBrokers,
    const MysqlConnectInfo &poolDB)
  : BlockMaker(blkMakerDef, kafkaBrokers, poolDB)
  , kMaxRawGbtNum_(
        100) /* if 5 seconds a rawgbt, will hold 100*5/60 = 8 mins rawgbt */
  , kMaxStratumJobNum_(
        120) /* if 30 seconds a stratum job, will hold 60 mins stratum job */
  , lastSubmittedBlockTime()
  , submittedRskBlocks(0)
  , kafkaConsumerRawGbt_(
        kafkaBrokers, def()->rawGbtTopic_.c_str(), 0 /* patition */)
  , kafkaConsumerStratumJob_(
        kafkaBrokers, def()->stratumJobTopic_.c_str(), 0 /* patition */)
#ifndef CHAIN_TYPE_ZEC
  , kafkaConsumerNamecoinSolvedShare_(
        kafkaBrokers, def()->auxPowSolvedShareTopic_.c_str(), 0 /* patition */)
  , kafkaConsumerRskSolvedShare_(
        kafkaBrokers, def()->rskSolvedShareTopic_.c_str(), 0 /* patition */)
#endif
{
}

BlockMakerBitcoin::~BlockMakerBitcoin() {
  if (threadConsumeRawGbt_.joinable())
    threadConsumeRawGbt_.join();

  if (threadConsumeStratumJob_.joinable())
    threadConsumeStratumJob_.join();

#ifndef CHAIN_TYPE_ZEC
  if (threadConsumeNamecoinSolvedShare_.joinable())
    threadConsumeNamecoinSolvedShare_.join();

  if (threadConsumeRskSolvedShare_.joinable())
    threadConsumeRskSolvedShare_.join();
#endif
}

bool BlockMakerBitcoin::init() {
  if (!checkBitcoinds())
    return false;

  if (!BlockMaker::init()) {
    return false;
  }
  //
  // Raw Gbt
  //
  // we need to consume the latest N messages
  if (kafkaConsumerRawGbt_.setup(RD_KAFKA_OFFSET_TAIL(kMaxRawGbtNum_)) ==
      false) {
    LOG(INFO) << "setup kafkaConsumerRawGbt_ fail";
    return false;
  }
  if (!kafkaConsumerRawGbt_.checkAlive()) {
    LOG(ERROR) << "kafka brokers is not alive: kafkaConsumerRawGbt_";
    return false;
  }

  //
  // Stratum Job
  //
  // we need to consume the latest 2 messages, just in case
  if (kafkaConsumerStratumJob_.setup(
          RD_KAFKA_OFFSET_TAIL(kMaxStratumJobNum_)) == false) {
    LOG(INFO) << "setup kafkaConsumerStratumJob_ fail";
    return false;
  }
  if (!kafkaConsumerStratumJob_.checkAlive()) {
    LOG(ERROR) << "kafka brokers is not alive: kafkaConsumerStratumJob_";
    return false;
  }

#ifndef CHAIN_TYPE_ZEC
  //
  // Namecoin Sloved Share
  //
  // we need to consume the latest 2 messages, just in case
  if (kafkaConsumerNamecoinSolvedShare_.setup(RD_KAFKA_OFFSET_TAIL(2)) ==
      false) {
    LOG(INFO) << "setup kafkaConsumerNamecoinSolvedShare_ fail";
    return false;
  }
  if (!kafkaConsumerNamecoinSolvedShare_.checkAlive()) {
    LOG(ERROR)
        << "kafka brokers is not alive: kafkaConsumerNamecoinSolvedShare_";
    return false;
  }

  //
  // RSK Solved Share
  //
  // we need to consume the latest 2 messages, just in case
  if (kafkaConsumerRskSolvedShare_.setup(RD_KAFKA_OFFSET_TAIL(2)) == false) {
    LOG(INFO) << "setup kafkaConsumerRskSolvedShare_ fail";
    return false;
  }
  if (!kafkaConsumerRskSolvedShare_.checkAlive()) {
    LOG(ERROR) << "kafka brokers is not alive: kafkaConsumerRskSolvedShare_";
    return false;
  }
#endif

  return true;
}

void BlockMakerBitcoin::consumeRawGbt(rd_kafka_message_t *rkmessage) {
  // check error
  if (rkmessage->err) {
    if (rkmessage->err == RD_KAFKA_RESP_ERR__PARTITION_EOF) {
      // Reached the end of the topic+partition queue on the broker.
      // Not really an error.
      //      LOG(INFO) << "consumer reached end of " <<
      //      rd_kafka_topic_name(rkmessage->rkt)
      //      << "[" << rkmessage->partition << "] "
      //      << " message queue at offset " << rkmessage->offset;
      // acturlly
      return;
    }

    LOG(ERROR) << "consume error for topic "
               << rd_kafka_topic_name(rkmessage->rkt) << "["
               << rkmessage->partition << "] offset " << rkmessage->offset
               << ": " << rd_kafka_message_errstr(rkmessage);

    if (rkmessage->err == RD_KAFKA_RESP_ERR__UNKNOWN_PARTITION ||
        rkmessage->err == RD_KAFKA_RESP_ERR__UNKNOWN_TOPIC) {
      LOG(FATAL) << "consume fatal";
      stop();
    }
    return;
  }

  LOG(INFO) << "received rawgbt message, len: " << rkmessage->len;
  addRawgbt((const char *)rkmessage->payload, rkmessage->len);
}

void BlockMakerBitcoin::addRawgbt(const char *str, size_t len) {
  JsonNode r;
  if (!JsonNode::parse(str, str + len, r)) {
    LOG(ERROR) << "parse rawgbt message to json fail";
    return;
  }
  if (r["created_at_ts"].type() != Utilities::JS::type::Int ||
      r["block_template_base64"].type() != Utilities::JS::type::Str ||
      r["gbthash"].type() != Utilities::JS::type::Str) {
    LOG(ERROR) << "invalid rawgbt: missing fields";
    return;
  }

  const uint256 gbtHash = uint256S(r["gbthash"].str());
  if (rawGbtMap_.find(gbtHash) != rawGbtMap_.end()) {
    LOG(ERROR) << "already exist raw gbt, ignore: " << gbtHash.ToString();
    return;
  }

  const string gbt = DecodeBase64(r["block_template_base64"].str());
  assert(gbt.length() > 64); // valid gbt string's len at least 64 bytes

  JsonNode nodeGbt;
  if (!JsonNode::parse(gbt.c_str(), gbt.c_str() + gbt.length(), nodeGbt)) {
    LOG(ERROR) << "parse gbt message to json fail";
    return;
  }
  JsonNode jgbt = nodeGbt["result"];

#if defined(CHAIN_TYPE_BCH) || defined(CHAIN_TYPE_BSV)
  bool isLightVersion =
      jgbt[LIGHTGBT_JOB_ID].type() == Utilities::JS::type::Str;
  if (isLightVersion) {
    ScopeLock ls(rawGbtlightLock_);
    rawGbtlightMap_[gbtHash] = jgbt[LIGHTGBT_JOB_ID].str();
    LOG(INFO) << "insert rawgbt light: " << gbtHash.ToString()
              << ", job_id: " << jgbt[LIGHTGBT_JOB_ID].str().c_str();
    return;
  }
#endif // CHAIN_TYPE_BCH
  // transaction without coinbase_tx
  shared_ptr<vector<CTransactionRef>> vtxs =
      std::make_shared<vector<CTransactionRef>>();
  for (JsonNode &node : jgbt["transactions"].array()) {
#ifdef CHAIN_TYPE_ZEC
    CTransaction tx;
    DecodeHexTx(tx, node["data"].str());
    vtxs->push_back(MakeTransactionRef(tx));
#else
    CMutableTransaction tx;
    DecodeHexTx(tx, node["data"].str());
    vtxs->push_back(MakeTransactionRef(std::move(tx)));
#endif
  }

  LOG(INFO) << "insert rawgbt: " << gbtHash.ToString()
            << ", txs: " << vtxs->size();
  insertRawGbt(gbtHash, vtxs);
}

void BlockMakerBitcoin::insertRawGbt(
    const uint256 &gbtHash, shared_ptr<vector<CTransactionRef>> vtxs) {
  ScopeLock ls(rawGbtLock_);

  // insert rawgbt
  rawGbtMap_[gbtHash] = vtxs;
  rawGbtQ_.push_back(gbtHash);

  // remove rawgbt if need
  while (rawGbtQ_.size() > kMaxRawGbtNum_) {
    const uint256 h = *rawGbtQ_.begin();

    rawGbtMap_.erase(h); // delete from map
    rawGbtQ_.pop_front(); // delete from Q
  }
}

#ifndef CHAIN_TYPE_ZEC
static string _buildAuxPow(const CBlock *block) {
  //
  // see: https://en.bitcoin.it/wiki/Merged_mining_specification
  //
  string auxPow;

  //
  // build auxpow
  //
  // 1. coinbase hex
  {
    CDataStream ssTx(SER_NETWORK, PROTOCOL_VERSION);
    ssTx << block->vtx[0];
    auxPow += HexStr(ssTx.begin(), ssTx.end());
  }

  // 2. block_hash
#ifdef CHAIN_TYPE_LTC
  auxPow += block->GetPoWHash().GetHex();
#else
  auxPow += block->GetHash().GetHex();
#endif

  // 3. coinbase_branch, Merkle branch
  {
    vector<uint256> merkleBranch = BlockMerkleBranch(*block, 0 /* position */);

    // Number of links in branch
    // should be Variable integer, but can't over than 0xfd, so we just print
    // out 2 hex char
    // https://en.bitcoin.it/wiki/Protocol_specification#Variable_length_integer
    auxPow += Strings::Format("%02x", merkleBranch.size());

    // merkle branch
    for (auto &itr : merkleBranch) {
      // dump 32 bytes from memory
      string hex;
      Bin2Hex(itr.begin(), 32, hex);
      auxPow += hex;
    }

    // branch_side_mask is always going to be all zeroes, because the branch
    // hashes will always be "on the right" of the working hash
    auxPow += "00000000";
  }

  // 4. Aux Blockchain Link
  {
    auxPow += "00"; // Number of links in branch
    auxPow += "00000000"; // Branch sides bitmask
  }

  // 5. Parent Block Header
  {
    CDataStream ssBlock(SER_NETWORK, PROTOCOL_VERSION);
    ssBlock << block->GetBlockHeader();
    auxPow += HexStr(ssBlock.begin(), ssBlock.end());
  }

  return auxPow;
}

void BlockMakerBitcoin::consumeNamecoinSolvedShare(
    rd_kafka_message_t *rkmessage) {
  // check error
  if (rkmessage->err) {
    if (rkmessage->err == RD_KAFKA_RESP_ERR__PARTITION_EOF) {
      // Reached the end of the topic+partition queue on the broker.
      // Not really an error.
      //      LOG(INFO) << "consumer reached end of " <<
      //      rd_kafka_topic_name(rkmessage->rkt)
      //      << "[" << rkmessage->partition << "] "
      //      << " message queue at offset " << rkmessage->offset;
      // acturlly
      return;
    }

    LOG(ERROR) << "consume error for topic "
               << rd_kafka_topic_name(rkmessage->rkt) << "["
               << rkmessage->partition << "] offset " << rkmessage->offset
               << ": " << rd_kafka_message_errstr(rkmessage);

    if (rkmessage->err == RD_KAFKA_RESP_ERR__UNKNOWN_PARTITION ||
        rkmessage->err == RD_KAFKA_RESP_ERR__UNKNOWN_TOPIC) {
      LOG(FATAL) << "consume fatal";
      stop();
    }
    return;
  }

  LOG(INFO) << "received Namecoin SolvedShare message, len: " << rkmessage->len;

  //
  // namecoin solved share message
  //
  JsonNode j;
  if (JsonNode::parse(
          (const char *)rkmessage->payload,
          (const char *)rkmessage->payload + rkmessage->len,
          j) == false) {
    LOG(ERROR) << "decode namecoin solved share message fail: "
               << string((const char *)rkmessage->payload, rkmessage->len);
    return;
  }
  // check fields
  if (j["job_id"].type() != Utilities::JS::type::Int ||
      j["aux_block_hash"].type() != Utilities::JS::type::Str ||
      j["block_header"].type() != Utilities::JS::type::Str ||
      j["coinbase_tx"].type() != Utilities::JS::type::Str ||
      j["rpc_addr"].type() != Utilities::JS::type::Str ||
      j["rpc_userpass"].type() != Utilities::JS::type::Str) {
    LOG(ERROR) << "namecoin solved share message missing some fields";
    return;
  }

  const uint64_t jobId = j["job_id"].uint64();
  const string auxBlockHash = j["aux_block_hash"].str();
  const string blockHeaderHex = j["block_header"].str();
  const string coinbaseTxHex = j["coinbase_tx"].str();
  const string rpcAddr = j["rpc_addr"].str();
  const string rpcUserpass = j["rpc_userpass"].str();

  assert(blockHeaderHex.size() == sizeof(CBlockHeader) * 2);

  CBlockHeader blkHeader;
  vector<char> coinbaseTxBin;

  // block header, hex -> bin
  {
    vector<char> binOut;
    Hex2Bin(blockHeaderHex.c_str(), blockHeaderHex.length(), binOut);
    assert(binOut.size() == sizeof(CBlockHeader));
    memcpy((uint8_t *)&blkHeader, binOut.data(), binOut.size());
  }

  // coinbase tx, hex -> bin
  Hex2Bin(coinbaseTxHex.c_str(), coinbaseTxHex.length(), coinbaseTxBin);

  // get gbtHash and rawgbt (vtxs)
  uint256 gbtHash;
  shared_ptr<vector<CTransactionRef>> vtxs;
  {
    ScopeLock sl(jobIdMapLock_);
    if (jobId2GbtHash_.find(jobId) != jobId2GbtHash_.end()) {
      gbtHash = jobId2GbtHash_[jobId];
    }
  }

  {
    ScopeLock ls(rawGbtLock_);
    if (rawGbtMap_.find(gbtHash) == rawGbtMap_.end()) {
      LOG(ERROR) << "can't find this gbthash in rawGbtMap_: "
                 << gbtHash.ToString();
      return;
    }
    vtxs = rawGbtMap_[gbtHash];
    assert(vtxs.get() != nullptr);
  }

  //
  // build new block
  //
  CBlock newblk(blkHeader);
  vector<uint256> vtxhashes;
  vtxhashes.resize(1 + vtxs->size()); // coinbase + gbt txs

  // put coinbase tx
  {
    CSerializeData sdata;
    sdata.insert(sdata.end(), coinbaseTxBin.begin(), coinbaseTxBin.end());
    newblk.vtx.push_back(MakeTransactionRef());
    CDataStream c(sdata, SER_NETWORK, PROTOCOL_VERSION);
    c >> newblk.vtx[newblk.vtx.size() - 1];
    vtxhashes[0] = newblk.vtx[newblk.vtx.size() - 1]->GetHash();
  }

  // put other txs
  if (vtxs && vtxs->size()) {
    newblk.vtx.insert(newblk.vtx.end(), vtxs->begin(), vtxs->end());
  }

  for (size_t i = 0; i < vtxs->size(); i++) {
    vtxhashes[i + 1] =
        (*vtxs)[i]->GetHash(); // vtxs is a shared_ptr<vector<CTransactionRef>>
  }

  // if the jobid is not exist , need submit to nmc
  std::shared_ptr<AuxBlockInfo> auxblockinfo;
  {
    ScopeLock sl(jobIdAuxBlockInfoLock_);
    if (jobId2AuxHash_.find(jobId) != jobId2AuxHash_.end()) {
      auxblockinfo = jobId2AuxHash_[jobId];
    } else {
      DLOG(INFO) << "cannot find jobid(" << jobId << ") in jobId2AuxHash_";
    }
  }

#ifdef CHAIN_TYPE_LTC
  uint256 bitcoinblockhash = blkHeader.GetPoWHash();
#else
  uint256 bitcoinblockhash = blkHeader.GetHash();
#endif

  if (jobId2AuxHash_.find(jobId) == jobId2AuxHash_.end() ||
      (!auxblockinfo->nmcBlockHash_.IsNull() &&
       UintToArith256(bitcoinblockhash) <=
           UintToArith256(auxblockinfo->nmcNetworkTarget_))) {
    //
    // build aux POW
    //
    const string auxPow = _buildAuxPow(&newblk);

    // submit to namecoind
    submitNamecoinBlockNonBlocking(
        auxBlockHash,
        auxPow,
        newblk.GetHash().ToString(),
        rpcAddr,
        rpcUserpass);
  } else {
    DLOG(INFO) << "nmc block hash : " << auxBlockHash
               << " cannot submit to node.";
  }

  if (auxblockinfo && !auxblockinfo->vcashBlockHash_.IsNull() &&
      UintToArith256(bitcoinblockhash) <=
          UintToArith256(auxblockinfo->vcashNetworkTarget_)) {

    // build coinbase's merkle tree branch
    string merkleHashesHex;
    string hashHex;
    vector<uint256> cbMerkleBranch = ComputeMerkleBranch(vtxhashes, 0);

    Bin2Hex(
        (uint8_t *)(vtxhashes[0].begin()),
        sizeof(uint256),
        hashHex); // coinbase hash
    // merkleHashesHex.append(hashHex);
    for (size_t i = 0; i < cbMerkleBranch.size(); i++) {
      // merkleHashesHex.append("\x20"); // space character
      Bin2HexR((uint8_t *)cbMerkleBranch[i].begin(), sizeof(uint256), hashHex);
      merkleHashesHex.append(hashHex);
    }

    // block tx count
    std::stringstream sstream;
    sstream << std::hex << vtxhashes.size();
    string totalTxCountHex(sstream.str());

    string rpcAddress = auxblockinfo->vcashRpcAddress_;
    if (rpcAddress.find_last_of('/') != string::npos)
      rpcAddress = rpcAddress.substr(0, rpcAddress.find_last_of('/')) +
          "/submitauxblock";

    uint256 hashMerkleRoot = vtxhashes[0];

    for (const uint256 &step : cbMerkleBranch) {
      hashMerkleRoot = Hash(
          BEGIN(hashMerkleRoot), END(hashMerkleRoot), BEGIN(step), END(step));
    }

    submitVcashBlockNonBlocking(
        rpcAddress,
        auxblockinfo->vcashRpcUserPwd_,
        auxblockinfo->vcashBlockHash_.GetHex(),
        blockHeaderHex,
        coinbaseTxHex,
        merkleHashesHex,
        totalTxCountHex,
        bitcoinblockhash.GetHex()); // using thread
  } else {
    DLOG(INFO) << "vcash block hash : "
               << ((!auxblockinfo || auxblockinfo->vcashBlockHash_.IsNull())
                       ? "is null"
                       : auxblockinfo->vcashBlockHash_.GetHex())
               << " cannot submit to node.";
  }

  if (auxblockinfo) {
    DLOG(INFO) << " vcash network target : "
               << auxblockinfo->vcashNetworkTarget_.GetHex()
               << " nmc network target : "
               << auxblockinfo->nmcNetworkTarget_.GetHex()
               << " bitcoin block hash : " << bitcoinblockhash.GetHex();
  }
}

void BlockMakerBitcoin::submitNamecoinBlockNonBlocking(
    const string &auxBlockHash,
    const string &auxPow,
    const string &bitcoinBlockHash,
    const string &rpcAddress,
    const string &rpcUserpass) {
  // use thread to submit
  std::thread t(std::bind(
      &BlockMakerBitcoin::_submitNamecoinBlockThread,
      this,
      auxBlockHash,
      auxPow,
      bitcoinBlockHash,
      rpcAddress,
      rpcUserpass));
  t.detach();
}

void BlockMakerBitcoin::_submitNamecoinBlockThread(
    const string &auxBlockHash,
    const string &auxPow,
    const string &bitcoinBlockHash,
    const string &rpcAddress,
    const string &rpcUserpass) {
  //
  // request : submitauxblock <hash> <auxpow>
  //
  {
    string request = "";

    bool isSupportSubmitAuxBlock = false;
    if (isAddrSupportSubmitAux_.find(rpcAddress) !=
        isAddrSupportSubmitAux_.end()) {
      isSupportSubmitAuxBlock =
          isAddrSupportSubmitAux_.find(rpcAddress)->second;
    } else {
      LOG(INFO) << "can't find " << rpcAddress
                << " in isAddrSupportSubmitAux_ map";
    }

    if (isSupportSubmitAuxBlock) {
      request = Strings::Format(
          "{\"id\":1,\"method\":\"submitauxblock\",\"params\":[\"%s\",\"%s\"]}",
          auxBlockHash,
          auxPow);
    } else {
      request = Strings::Format(
          "{\"id\":1,\"method\":\"getauxblock\",\"params\":[\"%s\",\"%s\"]}",
          auxBlockHash,
          auxPow);
    }

    DLOG(INFO) << "submitauxblock request: " << request;
    // try N times
    string response;
    for (size_t i = 0; i < 3; i++) {
      bool res = blockchainNodeRpcCall(
          rpcAddress.c_str(), rpcUserpass.c_str(), request.c_str(), response);

      // success
      if (res == true) {
        LOG(INFO) << "rpc call success, submit auxblock response: " << response;
        break;
      }

      // failure
      LOG(ERROR) << "rpc call fail: " << response
                 << "\nrpc request : " << request;
    }
    //
    // save to databse
    //
    DLOG(INFO) << "found_aux_block_table : "
               << (def()->foundAuxBlockTable_.empty()
                       ? ""
                       : def()->foundAuxBlockTable_);
    string chainname =
        def()->auxChainName_.empty() ? "aux" : def()->auxChainName_;
    DLOG(INFO) << "aux chain name : " << chainname;

    if (!def()->foundAuxBlockTable_.empty()) {
      insertAuxBlock2Mysql(
          def()->foundAuxBlockTable_.c_str(),
          chainname,
          auxBlockHash,
          bitcoinBlockHash,
          response,
          auxPow);
    }
  }
}
#endif

void BlockMakerBitcoin::processSolvedShare(rd_kafka_message_t *rkmessage) {
  //
  // solved share message:  FoundBlock + coinbase_Tx
  //
  FoundBlock foundBlock;
  CBlockHeader blkHeader;
  vector<char> coinbaseTxBin;

  {
    if (rkmessage->len <= sizeof(FoundBlock)) {
      LOG(ERROR) << "invalid SolvedShare length: " << rkmessage->len;
      return;
    }
    coinbaseTxBin.resize(rkmessage->len - sizeof(FoundBlock));

    // foundBlock
    memcpy(
        (uint8_t *)&foundBlock,
        (const uint8_t *)rkmessage->payload,
        sizeof(FoundBlock));

    // coinbase tx
    memcpy(
        (uint8_t *)coinbaseTxBin.data(),
        (const uint8_t *)rkmessage->payload + sizeof(FoundBlock),
        coinbaseTxBin.size());
    // copy header
    foundBlock.headerData_.get(blkHeader);
  }

  // get gbtHash and rawgbt (vtxs)
  uint256 gbtHash;
  shared_ptr<vector<CTransactionRef>> vtxs;
  {
    ScopeLock sl(jobIdMapLock_);
    if (jobId2GbtHash_.find(foundBlock.jobId_) != jobId2GbtHash_.end()) {
      gbtHash = jobId2GbtHash_[foundBlock.jobId_];
    }
  }

#if defined(CHAIN_TYPE_BCH) || defined(CHAIN_TYPE_BSV)
  std::string gbtlightJobId;
  {
    ScopeLock ls(rawGbtlightLock_);
    const auto iter = rawGbtlightMap_.find(gbtHash);
    if (iter != rawGbtlightMap_.end()) {
      gbtlightJobId = iter->second;
    }
  }
  bool lightVersion = !gbtlightJobId.empty();
  if (!lightVersion)
#endif // CHAIN_TYPE_BCH
  {
    ScopeLock ls(rawGbtLock_);
    if (rawGbtMap_.find(gbtHash) == rawGbtMap_.end()) {
      LOG(ERROR) << "can't find this gbthash in rawGbtMap_: "
                 << gbtHash.ToString();
      return;
    }
    vtxs = rawGbtMap_[gbtHash];
    assert(vtxs.get() != nullptr);
  }

  //
  // build new block
  //
  CBlock newblk(blkHeader);

  // put coinbase tx
  {
    CSerializeData sdata;
    sdata.insert(sdata.end(), coinbaseTxBin.begin(), coinbaseTxBin.end());
#ifdef CHAIN_TYPE_ZEC
    newblk.vtx.push_back(CTransaction());
#else
    newblk.vtx.push_back(MakeTransactionRef());
#endif
    CDataStream c(sdata, SER_NETWORK, PROTOCOL_VERSION);
    c >> newblk.vtx[newblk.vtx.size() - 1];
  }

  // put other txs
  if (vtxs && vtxs->size()) {
#ifdef CHAIN_TYPE_ZEC
    for (size_t i = 0; i < vtxs->size(); ++i) {
      newblk.vtx.push_back(*vtxs->at(i));
    }
#else
    newblk.vtx.insert(newblk.vtx.end(), vtxs->begin(), vtxs->end());
#endif
  }

  // submit to bitcoind
  const string blockHex = EncodeHexBlock(newblk);
#if defined(CHAIN_TYPE_BCH) || defined(CHAIN_TYPE_BSV)
  if (lightVersion) {
    LOG(INFO) << "submit block light: " << newblk.GetHash().ToString()
              << " with job_id: " << gbtlightJobId.c_str();
#ifdef CHAIN_TYPE_BSV
    string coinbaseTxStr;
    Bin2Hex(coinbaseTxBin, coinbaseTxStr);
    submitBlockLightNonBlocking(
        gbtlightJobId,
        coinbaseTxStr,
        blkHeader.nVersion,
        blkHeader.nTime,
        blkHeader.nNonce);
#else
    submitBlockLightNonBlocking(blockHex, gbtlightJobId);
#endif
  } else
#endif // CHAIN_TYPE_BCH
  {
#ifdef CHAIN_TYPE_LTC
    LOG(INFO) << "submit block pow: " << newblk.GetPoWHash().ToString();
#endif
    LOG(INFO) << "submit block: " << newblk.GetHash().ToString();
    submitBlockNonBlocking(blockHex); // using thread
  }

#ifdef CHAIN_TYPE_ZEC
  uint64_t coinbaseValue = AMOUNT_SATOSHIS(newblk.vtx[0].GetValueOut());
#else
  uint64_t coinbaseValue = AMOUNT_SATOSHIS(newblk.vtx[0]->GetValueOut());
#endif

  // save to DB, using thread
  saveBlockToDBNonBlocking(
      foundBlock,
      blkHeader,
      string(coinbaseTxBin.begin(), coinbaseTxBin.end()),
      coinbaseValue, // coinbase value
      blockHex.length() / 2);
}

void BlockMakerBitcoin::saveBlockToDBNonBlocking(
    const FoundBlock &foundBlock,
    const CBlockHeader &header,
    const string &coinbaseTxBin,
    const uint64_t coinbaseValue,
    const int32_t blksize) {
  std::thread t(std::bind(
      &BlockMakerBitcoin::_saveBlockToDBThread,
      this,
      foundBlock,
      header,
      coinbaseTxBin,
      coinbaseValue,
      blksize));
  t.detach();
}

void BlockMakerBitcoin::_saveBlockToDBThread(
    const FoundBlock &foundBlock,
    const CBlockHeader &header,
    const string &coinbaseTxBin,
    const uint64_t coinbaseValue,
    const int32_t blksize) {
  const string nowStr = date("%F %T");
  string sql;

  string subPoolName;
  {
    ScopeLock sl(jobIdMapLock_);
    if (jobId2SubPool_.find(foundBlock.jobId_) != jobId2SubPool_.end()) {
      auto subPool = jobId2SubPool_[foundBlock.jobId_];
      for (const auto &itr : subPool) {
        if ((coinbaseTxBin.find(itr.coinbase1_) != coinbaseTxBin.npos ||
             (itr.grandCoinbase1_.size() > 0 &&
              coinbaseTxBin.find(itr.grandCoinbase1_) != coinbaseTxBin.npos)) &&
            coinbaseTxBin.find(itr.coinbase2_) != coinbaseTxBin.npos) {
          subPoolName = filterTableName("_" + itr.name_);
          break;
        }
      }
    }
  }

  sql = Strings::Format(
      "INSERT INTO `found_blocks%s` "
      " (`puid`, `worker_id`, `worker_full_name`, `job_id`"
      "  ,`height`, `hash`, `rewards`, `size`, `prev_hash`"
      "  ,`bits`, `version`, `created_at`)"
      " VALUES (%d,%d,\"%s\",%u,%d,\"%s\",%d,%d,\"%s\",%u,%d,\"%s\"); ",
      subPoolName,
      foundBlock.userId_,
      foundBlock.workerId_,
      // filter again, just in case
      filterWorkerName(foundBlock.workerFullName_),
      foundBlock.jobId_,
      foundBlock.height_,
      header.GetHash().ToString(),
      coinbaseValue,
      blksize,
      header.hashPrevBlock.ToString(),
      header.nBits,
      header.nVersion,
      nowStr);

  LOG(INFO) << "BlockMakerBitcoin::_saveBlockToDBThread: " << sql;

  // try connect to DB
  MySQLConnection db(poolDB_);
  for (size_t i = 0; i < 3; i++) {
    if (db.ping())
      break;
    else
      std::this_thread::sleep_for(3s);
  }

  if (db.execute(sql) == false) {
    LOG(ERROR) << "insert found block failure: " << sql;
  }
}

bool BlockMakerBitcoin::checkBitcoinds() {
  if (def()->nodes.size() == 0) {
    return false;
  }

  for (const auto &itr : def()->nodes) {
    if (!checkBitcoinRPC(itr.rpcAddr_.c_str(), itr.rpcUserPwd_.c_str())) {
      return false;
    }
  }

  return true;
}

void BlockMakerBitcoin::submitBlockNonBlocking(const string &blockHex) {
  for (const auto &itr : def()->nodes) {
    // use thread to submit
    std::thread t(std::bind(
        &BlockMakerBitcoin::_submitBlockThread,
        this,
        itr.rpcAddr_,
        itr.rpcUserPwd_,
        blockHex));
    t.detach();
  }
}

void BlockMakerBitcoin::_submitBlockThread(
    const string &rpcAddress,
    const string &rpcUserpass,
    const string &blockHex) {
  string request =
      "{\"jsonrpc\":\"1.0\",\"id\":\"1\",\"method\":\"submitblock\",\"params\":"
      "[\"";
  request += blockHex + "\"]}";

  LOG(INFO) << "submit block to: " << rpcAddress;
  DLOG(INFO) << "submitblock request: " << request;
  // try N times
  for (size_t i = 0; i < 3; i++) {
    string response;
    bool res = blockchainNodeRpcCall(
        rpcAddress.c_str(), rpcUserpass.c_str(), request.c_str(), response);

    // success
    if (res == true) {
      LOG(INFO) << "rpc call success, submit block response: " << response;
      break;
    }

    // failure
    LOG(ERROR) << "rpc call fail: " << response
               << "\nrpc request : " << request;
  }
}

#if defined(CHAIN_TYPE_BCH)
void BlockMakerBitcoin::submitBlockLightNonBlocking(
    const string &blockHex, const string &job_id) {
  for (const auto &itr : def()->nodes) {
    // use thread to submit
    std::thread t(std::bind(
        &BlockMakerBitcoin::_submitBlockLightThread,
        this,
        itr.rpcAddr_,
        itr.rpcUserPwd_,
        job_id,
        blockHex));
    t.detach();
  }
}
void BlockMakerBitcoin::_submitBlockLightThread(
    const string &rpcAddress,
    const string &rpcUserpass,
    const string &job_id,
    const string &blockHex) {

  string request =
      "{\"jsonrpc\":\"1.0\",\"id\":\"1\",\"method\":\"submitblocklight\","
      "\"params\":[\"";
  request += blockHex + "\", \"";
  request += job_id + "\"";
  request += "]}";
  LOG(INFO) << "submit block light to: " << rpcAddress;
  DLOG(INFO) << "submitblock request: " << request;
  // try N times
  for (size_t i = 0; i < 3; i++) {
    string response;
    bool res = blockchainNodeRpcCall(
        rpcAddress.c_str(), rpcUserpass.c_str(), request.c_str(), response);
    // success
    if (res == true) {
      LOG(INFO) << "rpc call success, submit block light response: "
                << response;
      break;
    }
    // failure
    LOG(ERROR) << "rpc call fail: " << response
               << "\nrpc request : " << request;
  }
}
#elif defined(CHAIN_TYPE_BSV)
void BlockMakerBitcoin::submitBlockLightNonBlocking(
    const string &job_id,
    const string &coinbaseTx,
    int32_t version,
    uint32_t ntime,
    uint32_t nonce) {
  for (const auto &itr : def()->nodes) {
    // use thread to submit
    boost::thread t(boost::bind(
        &BlockMakerBitcoin::_submitBlockLightThread,
        this,
        itr.rpcAddr_,
        itr.rpcUserPwd_,
        job_id,
        coinbaseTx,
        version,
        ntime,
        nonce));
    t.detach();
  }
}
void BlockMakerBitcoin::_submitBlockLightThread(
    const string &rpcAddress,
    const string &rpcUserpass,
    const string &job_id,
    const string &coinbaseTx,
    int32_t version,
    uint32_t ntime,
    uint32_t nonce) {
  string request = Strings::Format(
      "{\"jsonrpc\":\"1.0\""
      ",\"id\":\"1\""
      ",\"method\":\"submitminingsolution\""
      ",\"params\":[{\"id\":\"%s\""
      ",\"coinbase\":\"%s\""
      ",\"version\":%" PRId32 ",\"time\":%" PRIu32 ",\"nonce\":%" PRIu32 "}]}",
      job_id.c_str(),
      coinbaseTx.c_str(),
      version,
      ntime,
      nonce);
  LOG(INFO) << "submit block light to: " << rpcAddress;
  DLOG(INFO) << "submitblock request: " << request;
  // try N times
  for (size_t i = 0; i < 3; i++) {
    string response;
    bool res = blockchainNodeRpcCall(
        rpcAddress.c_str(), rpcUserpass.c_str(), request.c_str(), response);
    // success
    if (res == true) {
      LOG(INFO) << "rpc call success, submit block light response: "
                << response;
      break;
    }
    // failure
    LOG(ERROR) << "rpc call fail: " << response
               << "\nrpc request : " << request;
  }
}
#endif // CHAIN_TYPE_BCH

void BlockMakerBitcoin::consumeStratumJob(rd_kafka_message_t *rkmessage) {
  // check error
  if (rkmessage->err) {
    if (rkmessage->err == RD_KAFKA_RESP_ERR__PARTITION_EOF) {
      // Reached the end of the topic+partition queue on the broker.
      // Not really an error.
      //      LOG(INFO) << "consumer reached end of " <<
      //      rd_kafka_topic_name(rkmessage->rkt)
      //      << "[" << rkmessage->partition << "] "
      //      << " message queue at offset " << rkmessage->offset;
      // acturlly
      return;
    }

    LOG(ERROR) << "consume error for topic "
               << rd_kafka_topic_name(rkmessage->rkt) << "["
               << rkmessage->partition << "] offset " << rkmessage->offset
               << ": " << rd_kafka_message_errstr(rkmessage);

    if (rkmessage->err == RD_KAFKA_RESP_ERR__UNKNOWN_PARTITION ||
        rkmessage->err == RD_KAFKA_RESP_ERR__UNKNOWN_TOPIC) {
      LOG(FATAL) << "consume fatal";
      stop();
    }
    return;
  }

  LOG(INFO) << "received StratumJob message, len: " << rkmessage->len;

  shared_ptr<StratumJobBitcoin> sjob = std::make_shared<StratumJobBitcoin>();
  bool res = sjob->unserializeFromJson(
      (const char *)rkmessage->payload, rkmessage->len);
  if (res == false) {
    LOG(ERROR) << "unserialize stratum job fail";
    return;
  }

  const uint256 gbtHash = uint256S(sjob->gbtHash_);
  {
    ScopeLock sl(jobIdMapLock_);
    jobId2GbtHash_[sjob->jobId_] = gbtHash;

    jobId2SubPool_[sjob->jobId_].clear();
    for (const auto &itr : sjob->subPool_) {
      vector<char> coinbase1, coinbase2, grandCoinbase1;
      Hex2Bin(
          itr.second.coinbase1_.data(),
          itr.second.coinbase1_.size(),
          coinbase1);
      Hex2Bin(
          itr.second.coinbase2_.data(),
          itr.second.coinbase2_.size(),
          coinbase2);
      Hex2Bin(
          itr.second.grandCoinbase1_.data(),
          itr.second.grandCoinbase1_.size(),
          grandCoinbase1);

      jobId2SubPool_[sjob->jobId_].push_back(
          {itr.first,
           string(coinbase1.begin(), coinbase1.end()),
           string(coinbase2.begin(), coinbase2.end()),
           string(grandCoinbase1.begin(), grandCoinbase1.end())});
    }

    // Maps (and sets) are sorted, so the first element is the smallest,
    // and the last element is the largest.
    while (jobId2GbtHash_.size() > kMaxStratumJobNum_) {
      jobId2GbtHash_.erase(jobId2GbtHash_.begin());
    }
    while (jobId2SubPool_.size() > kMaxStratumJobNum_) {
      jobId2SubPool_.erase(jobId2SubPool_.begin());
    }
  }

  const uint256 rskHashForMergeMining =
      uint256S(sjob->blockHashForMergedMining_);
  {
    ScopeLock sl(jobId2RskMMHashLock_);
    jobId2RskHashForMergeMining_[sjob->jobId_] = rskHashForMergeMining;
    while (jobId2RskHashForMergeMining_.size() > kMaxStratumJobNum_) {
      jobId2RskHashForMergeMining_.erase(jobId2RskHashForMergeMining_.begin());
    }
  }

  std::shared_ptr<AuxBlockInfo> auxblockinfo = std::make_shared<AuxBlockInfo>();
  auxblockinfo->nmcBlockHash_ = sjob->nmcAuxBlockHash_;
  BitsToTarget(sjob->nmcAuxBits_, auxblockinfo->nmcNetworkTarget_);

  auxblockinfo->vcashBlockHash_ =
      uint256S(sjob->vcashBlockHashForMergedMining_);
  auxblockinfo->vcashNetworkTarget_ = sjob->vcashNetworkTarget_;
  auxblockinfo->vcashRpcAddress_ = sjob->vcashdRpcAddress_;
  auxblockinfo->vcashRpcUserPwd_ = sjob->vcashdRpcUserPwd_;

  {
    ScopeLock sl(jobIdAuxBlockInfoLock_);
    jobId2AuxHash_[sjob->jobId_] = auxblockinfo;

    while (jobId2AuxHash_.size() > kMaxStratumJobNum_) {
      jobId2AuxHash_.erase(jobId2AuxHash_.begin());
    }
  }

  LOG(INFO) << "StratumJob, jobId: " << sjob->jobId_
            << ", gbtHash: " << gbtHash.ToString();

#ifndef CHAIN_TYPE_ZEC
  bool isSupportSubmitAuxBlock = false;
  if (!sjob->nmcRpcAddr_.empty() && !sjob->nmcRpcUserpass_.empty() &&
      isAddrSupportSubmitAux_.find(sjob->nmcRpcAddr_) ==
          isAddrSupportSubmitAux_.end()) {

    string response;
    string request =
        "{\"jsonrpc\":\"1.0\",\"id\":\"1\",\"method\":\"help\",\"params\":[]}";
    bool res = blockchainNodeRpcCall(
        sjob->nmcRpcAddr_.c_str(),
        sjob->nmcRpcUserpass_.c_str(),
        request.c_str(),
        response);
    if (!res) {
      LOG(INFO) << "auxcoind rpc call failure";
    } else {
      isSupportSubmitAuxBlock =
          (response.find("createauxblock") == std::string::npos ||
           response.find("submitauxblock") == std::string::npos)
          ? false
          : true;

      LOG(INFO) << "auxcoind " << (isSupportSubmitAuxBlock ? " " : "doesn't ")
                << "support rpc commands: createauxblock and submitauxblock";

      isAddrSupportSubmitAux_[sjob->nmcRpcAddr_] = isSupportSubmitAuxBlock;
    }
  }
#endif
}

void BlockMakerBitcoin::runThreadConsumeRawGbt() {
  const int32_t timeoutMs = 1000;

  while (running_) {
    rd_kafka_message_t *rkmessage;
    rkmessage = kafkaConsumerRawGbt_.consumer(timeoutMs);
    if (rkmessage == nullptr) /* timeout */
      continue;

    consumeRawGbt(rkmessage);

    /* Return message to rdkafka */
    rd_kafka_message_destroy(rkmessage);
  }
}

void BlockMakerBitcoin::runThreadConsumeStratumJob() {
  const int32_t timeoutMs = 1000;

  while (running_) {
    rd_kafka_message_t *rkmessage;
    rkmessage = kafkaConsumerStratumJob_.consumer(timeoutMs);
    if (rkmessage == nullptr) /* timeout */
      continue;

    consumeStratumJob(rkmessage);

    /* Return message to rdkafka */
    rd_kafka_message_destroy(rkmessage);
  }
}

#ifndef CHAIN_TYPE_ZEC
void BlockMakerBitcoin::runThreadConsumeNamecoinSolvedShare() {
  const int32_t timeoutMs = 1000;

  while (running_) {
    rd_kafka_message_t *rkmessage;
    rkmessage = kafkaConsumerNamecoinSolvedShare_.consumer(timeoutMs);
    if (rkmessage == nullptr) /* timeout */
      continue;

    consumeNamecoinSolvedShare(rkmessage);

    /* Return message to rdkafka */
    rd_kafka_message_destroy(rkmessage);
  }
}

/**
  Beginning of methods needed to consume a solved share and submit a block to
  RSK node.

  @author Martin Medina
  @copyright RSK Labs Ltd.
*/
void BlockMakerBitcoin::submitRskBlockPartialMerkleNonBlocking(
    const string &rpcAddress,
    const string &rpcUserPwd,
    const string &blockHashHex,
    const string &blockHeaderHex,
    const string &coinbaseHex,
    const string &merkleHashesHex,
    const string &totalTxCount,
    const string &rskHashForMergeMiningHex) {
  std::thread t(std::bind(
      &BlockMakerBitcoin::_submitRskBlockPartialMerkleThread,
      this,
      rpcAddress,
      rpcUserPwd,
      blockHashHex,
      blockHeaderHex,
      coinbaseHex,
      merkleHashesHex,
      totalTxCount,
      rskHashForMergeMiningHex));
  t.detach();
}

void BlockMakerBitcoin::_submitRskBlockPartialMerkleThread(
    const string &rpcAddress,
    const string &rpcUserPwd,
    const string &blockHashHex,
    const string &blockHeaderHex,
    const string &coinbaseHex,
    const string &merkleHashesHex,
    const string &totalTxCount,
    const string &rskHashForMergeMiningHex) {
  string request =
      "{\"jsonrpc\":\"2.0\",\"id\":\"1\",\"method\":\"mnr_"
      "submitBitcoinBlockPartialMerkle\",\"params\":[";
  request += "\"" + blockHashHex + "\", ";
  request += "\"" + blockHeaderHex + "\", ";
  request += "\"" + coinbaseHex + "\", ";
  request += "\"" + merkleHashesHex + "\", ";
  request += "\"" + totalTxCount + "\"]}";

  LOG(INFO) << "submit block to: " << rpcAddress;
  // try N times
  string response;
  for (size_t i = 0; i < 3; i++) {
    bool res = blockchainNodeRpcCall(
        rpcAddress.c_str(), rpcUserPwd.c_str(), request.c_str(), response);

    // success
    if (res) {
      LOG(INFO) << "rpc call success, submit block response: " << response;
      break;
    }

    // failure
    LOG(ERROR) << "rpc call fail: " << response
               << "\nrpc request : " << request;
  }

  if (!def()->foundAuxBlockTable_.empty()) {
    insertAuxBlock2Mysql(
        def()->foundAuxBlockTable_.c_str(),
        "rsk",
        rskHashForMergeMiningHex,
        blockHashHex,
        response);
  } else {
    LOG(INFO) << "aux block table name is empty, ";
  }
}

void BlockMakerBitcoin::consumeRskSolvedShare(rd_kafka_message_t *rkmessage) {
  // check error
  if (rkmessage->err) {
    if (rkmessage->err == RD_KAFKA_RESP_ERR__PARTITION_EOF) {
      // Reached the end of the topic+partition queue on the broker.
      return;
    }

    LOG(ERROR) << "consume error for topic "
               << rd_kafka_topic_name(rkmessage->rkt) << "["
               << rkmessage->partition << "] offset " << rkmessage->offset
               << ": " << rd_kafka_message_errstr(rkmessage);

    if (rkmessage->err == RD_KAFKA_RESP_ERR__UNKNOWN_PARTITION ||
        rkmessage->err == RD_KAFKA_RESP_ERR__UNKNOWN_TOPIC) {
      LOG(FATAL) << "consume fatal";
      stop();
    }
    return;
  }

  LOG(INFO) << "received RskSolvedShareData message, len: " << rkmessage->len;

  if (!submitToRskNode()) {
    return;
  }

  //
  // solved share message:  RskSolvedShareData + coinbase_Tx
  //
  RskSolvedShareData shareData;
  CBlockHeader blkHeader;
  vector<char> coinbaseTxBin;
  {
    if (rkmessage->len <= sizeof(RskSolvedShareData)) {
      LOG(ERROR) << "invalid RskSolvedShareData length: " << rkmessage->len;
      return;
    }
    coinbaseTxBin.resize(rkmessage->len - sizeof(RskSolvedShareData));

    // shareData
    memcpy(
        (uint8_t *)&shareData,
        (const uint8_t *)rkmessage->payload,
        sizeof(RskSolvedShareData));
    // coinbase tx
    memcpy(
        (uint8_t *)coinbaseTxBin.data(),
        (const uint8_t *)rkmessage->payload + sizeof(RskSolvedShareData),
        coinbaseTxBin.size());
    // copy header
    shareData.headerData_.get(blkHeader);
  }

  LOG(INFO) << "submit RSK block: " << blkHeader.GetHash().ToString();

  // get gbtHash and rawgbt (vtxs)
  uint256 gbtHash;
  shared_ptr<vector<CTransactionRef>> vtxs;
  {
    ScopeLock sl(jobIdMapLock_);
    if (jobId2GbtHash_.find(shareData.jobId_) != jobId2GbtHash_.end()) {
      gbtHash = jobId2GbtHash_[shareData.jobId_];
    }
  }
  {
    ScopeLock ls(rawGbtLock_);
    if (rawGbtMap_.find(gbtHash) == rawGbtMap_.end()) {
      LOG(ERROR) << "can't find this gbthash in rawGbtMap_: "
                 << gbtHash.ToString();
      return;
    }
    vtxs = rawGbtMap_[gbtHash];
  }
  assert(vtxs.get() != nullptr);

  vector<uint256> vtxhashes;
  vtxhashes.resize(1 + vtxs->size()); // coinbase + gbt txs

  // put coinbase tx hash
  {
    CSerializeData sdata;
    sdata.insert(sdata.end(), coinbaseTxBin.begin(), coinbaseTxBin.end());

    CMutableTransaction tx;
    CDataStream c(sdata, SER_NETWORK, PROTOCOL_VERSION);
    c >> tx;

    vtxhashes[0] = tx.GetHash();
  }

  // put other tx hashes
  for (size_t i = 0; i < vtxs->size(); i++) {
    vtxhashes[i + 1] =
        (*vtxs)[i]->GetHash(); // vtxs is a shared_ptr<vector<CTransactionRef>>
  }

  string blockHashHex = blkHeader.GetHash().ToString();
  string blockHeaderHex = EncodeHexBlockHeader(blkHeader);

  // coinbase bin -> hex
  string coinbaseHex;
  Bin2Hex(coinbaseTxBin, coinbaseHex);

  // build coinbase's merkle tree branch
  string merkleHashesHex;
  string hashHex;
  vector<uint256> cbMerkleBranch = ComputeMerkleBranch(vtxhashes, 0);

  Bin2Hex(
      (uint8_t *)(vtxhashes[0].begin()),
      sizeof(uint256),
      hashHex); // coinbase hash
  merkleHashesHex.append(hashHex);
  for (size_t i = 0; i < cbMerkleBranch.size(); i++) {
    merkleHashesHex.append("\x20"); // space character
    Bin2Hex((uint8_t *)cbMerkleBranch[i].begin(), sizeof(uint256), hashHex);
    merkleHashesHex.append(hashHex);
  }

  // block tx count
  std::stringstream sstream;
  sstream << std::hex << vtxhashes.size();
  string totalTxCountHex(sstream.str());

  uint256 rskHashForMergeMining;
  {
    ScopeLock sl(jobId2RskMMHashLock_);
    if (jobId2RskHashForMergeMining_.find(shareData.jobId_) !=
        jobId2RskHashForMergeMining_.end()) {
      rskHashForMergeMining = jobId2RskHashForMergeMining_[shareData.jobId_];
    }
  }

  submitRskBlockPartialMerkleNonBlocking(
      shareData.rpcAddress_,
      shareData.rpcUserPwd_,
      blockHashHex,
      blockHeaderHex,
      coinbaseHex,
      merkleHashesHex,
      totalTxCountHex,
      rskHashForMergeMining.GetHex()); // using thread
}

/**
  Anti flooding mechanism.
  No more than 2 submissions per second can be made to RSK node.

  @returns true if block can be submitted to RSK node. false otherwise.
*/
bool BlockMakerBitcoin::submitToRskNode() {
  uint32_t maxSubmissionsPerSecond = 2;
  int64_t oneSecondWindowInMs = 1000;

  if (lastSubmittedBlockTime.is_not_a_date_time()) {
    lastSubmittedBlockTime = bpt::microsec_clock::universal_time();
  }

  bpt::ptime currentTime(bpt::microsec_clock::universal_time());
  bpt::time_duration elapsed = currentTime - lastSubmittedBlockTime;

  if (elapsed.total_milliseconds() > oneSecondWindowInMs) {
    lastSubmittedBlockTime = currentTime;
    submittedRskBlocks = 0;
    elapsed = currentTime - lastSubmittedBlockTime;
  }

  if (elapsed.total_milliseconds() < oneSecondWindowInMs &&
      submittedRskBlocks < maxSubmissionsPerSecond) {
    submittedRskBlocks++;
    return true;
  }

  return false;
}
void BlockMakerBitcoin::runThreadConsumeRskSolvedShare() {
  const int32_t timeoutMs = 1000;

  while (running_) {
    rd_kafka_message_t *rkmessage;
    rkmessage = kafkaConsumerRskSolvedShare_.consumer(timeoutMs);
    if (rkmessage == nullptr) /* timeout */
      continue;

    consumeRskSolvedShare(rkmessage);

    /* Return message to rdkafka */
    rd_kafka_message_destroy(rkmessage);
  }
}
//// End of methods added to merge mine for RSK

void BlockMakerBitcoin::submitVcashBlockNonBlocking(
    const string &rpcAddress,
    const string &rpcUserPwd,
    const string &blockHashHex,
    const string &blockHeaderHex,
    const string &coinbaseHex,
    const string &merkleHashesHex,
    const string &totalTxCount,
    const string &bitcoinblockhash) {
  std::thread t(std::bind(
      &BlockMakerBitcoin::_submitVcashBlockThread,
      this,
      rpcAddress,
      rpcUserPwd,
      blockHashHex,
      blockHeaderHex,
      coinbaseHex,
      merkleHashesHex,
      totalTxCount,
      bitcoinblockhash));
  t.detach();
}

void BlockMakerBitcoin::_submitVcashBlockThread(
    const string &rpcAddress,
    const string &rpcUserPwd,
    const string &blockHashHex,
    const string &blockHeaderHex,
    const string &coinbaseHex,
    const string &merkleHashesHex,
    const string &totalTxCount,
    const string &bitcoinblockhash) {
  string request = Strings::Format(
      "{"
      "\"header_hash\":\"%s\","
      "\"btc_header\":\"%s\","
      "\"btc_coinbase\":\"%s\","
      "\"btc_merkle_branch\":\"%s\""
      "}",
      blockHashHex.c_str(),
      blockHeaderHex.c_str(),
      coinbaseHex.c_str(),
      merkleHashesHex.c_str());

  DLOG(INFO) << "submit vcash block to: " << rpcAddress
             << "rpc content : " << request;
  // try N times
  string response;
  for (size_t i = 0; i < 3; i++) {
    bool res = blockchainNodeRpcCall(
        rpcAddress.c_str(), rpcUserPwd.c_str(), request.c_str(), response);

    // success
    if (res) {
      LOG(INFO) << "rpc call success, submit vcash block response: "
                << response;
      break;
    }

    // failure
    LOG(ERROR) << "submit vcash block failed: " << response
               << "\nrpc request : " << request;
  }
  // save vcash to databse
  if (!def()->foundAuxBlockTable_.empty()) {
    insertAuxBlock2Mysql(
        def()->foundAuxBlockTable_.c_str(),
        "vcash",
        blockHashHex,
        bitcoinblockhash,
        response);
  }
}

void BlockMakerBitcoin::insertAuxBlock2Mysql(
    const string auxtablename,
    const string chainnane,
    const string auxblockhash,
    const string parentblockhash,
    const string submitresponse,
    const string auxpow) {

  const string nowStr = date("%F %T");
  string sql = Strings::Format(
      "INSERT INTO `%s` "
      " (`bitcoin_block_hash`,`aux_block_hash`,`chain_name`,"
      " `submit_response`, `aux_pow`,`created_at`) "
      " VALUES (\'%s\',\'%s\',\'%s\',\'%s\',\'%s\',\'%s\'); ",
      auxtablename.c_str(),
      parentblockhash.c_str(),
      auxblockhash.c_str(),
      chainnane.c_str(),
      submitresponse.c_str(),
      auxpow.c_str(),
      nowStr.c_str());
  DLOG(INFO) << "insert " << chainnane << " block sql : " << sql;
  // try connect to DB
  MySQLConnection db(poolDB_);
  for (size_t i = 0; i < 3; i++) {
    if (db.ping())
      break;
    else
      std::this_thread::sleep_for(3s);
  }

  if (db.execute(sql) == false) {
    LOG(ERROR) << "insert found block failure: " << sql;
  }
}
#endif

void BlockMakerBitcoin::run() {
  // setup threads
  threadConsumeRawGbt_ =
      std::thread(&BlockMakerBitcoin::runThreadConsumeRawGbt, this);
  threadConsumeStratumJob_ =
      std::thread(&BlockMakerBitcoin::runThreadConsumeStratumJob, this);
#ifndef CHAIN_TYPE_ZEC
  threadConsumeNamecoinSolvedShare_ = std::thread(
      &BlockMakerBitcoin::runThreadConsumeNamecoinSolvedShare, this);
  threadConsumeRskSolvedShare_ =
      std::thread(&BlockMakerBitcoin::runThreadConsumeRskSolvedShare, this);
#endif
  BlockMaker::run();
}

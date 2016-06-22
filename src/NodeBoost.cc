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
#include <glog/logging.h>

#include "NodeBoost.h"

#include "bitcoin/util.h"

uint256 shortHash(const uint256 &hash) {
  uint256 shortHash;
  memcpy(shortHash.begin(), hash.begin(), SHORT_HASH_SIZE);  // copy first N bytes
  return shortHash;
}

static
uint256 getHashFromThinBlock(const string &thinBlock) {
  CBlockHeader header;
  memcpy((uint8_t *)&header, thinBlock.data(), sizeof(CBlockHeader));
  return header.GetHash();
}

static
size_t getTxCountFromThinBlock(const string &thinBlock) {
  return (thinBlock.size() - sizeof(CBlockHeader)) / SHORT_HASH_SIZE;
}

/////////////////////////////////// NodePeer ///////////////////////////////////
NodePeer::NodePeer(const string &subAddr, const string &reqAddr,
                   zmq::context_t *zmqContext, NodeBoost *nodeBoost,
                   TxRepo *txRepo)
:running_(true), subAddr_(subAddr), reqAddr_(reqAddr), nodeBoost_(nodeBoost), txRepo_(txRepo)
{
  zmqSub_ = new zmq::socket_t(*zmqContext, ZMQ_SUB);
  zmqSub_->connect(subAddr_);
  zmqSub_->setsockopt(ZMQ_SUBSCRIBE, MSG_PUB_THIN_BLOCK, strlen(MSG_PUB_THIN_BLOCK));
  zmqSub_->setsockopt(ZMQ_SUBSCRIBE, MSG_PUB_HEARTBEAT,  strlen(MSG_PUB_HEARTBEAT));
  zmqSub_->setsockopt(ZMQ_SUBSCRIBE, MSG_PUB_CLOSEPEER,  strlen(MSG_PUB_CLOSEPEER));

  zmqReq_ = new zmq::socket_t(*zmqContext, ZMQ_REQ);
  zmqReq_->connect(reqAddr_);

  int zmqLinger = 5 * 1000;
  zmqSub_->setsockopt(ZMQ_LINGER, &zmqLinger/*ms*/, sizeof(zmqLinger));
  zmqReq_->setsockopt(ZMQ_LINGER, &zmqLinger/*ms*/, sizeof(zmqLinger));

  lastRecvMsgTime_ = time(nullptr);
}

NodePeer::~NodePeer() {
  LOG(INFO) << "close peer: " << subAddr_ << ", " << reqAddr_;
  zmqSub_ = nullptr;
  zmqReq_ = nullptr;
}

void NodePeer::stop() {
  if (!running_) {
    return;
  }
  running_ = false;

  LOG(INFO) << "stop peer: " << subAddr_ << ", " << reqAddr_;
}

string NodePeer::toString() const {
  return "peer(sub: " + subAddr_ + ", req: " + reqAddr_ + ")";
}

void NodePeer::tellPeerToConnectMyServer(const string &zmqPubAddr,
                                         const string &zmqRepAddr) {
  // 1. send my zmq addr info
  assert(sizeof(uint16_t) == 2);
  zmq::message_t zmsg;
  zmsg.rebuild(MSG_CMD_LEN + 2 + zmqPubAddr.length() + 2 + zmqRepAddr.length());
  uint8_t *p = (uint8_t *)zmsg.data();

  // cmd
  memset((char *)p, 0, MSG_CMD_LEN);
  sprintf((char *)p, "%s", MSG_CMD_CONNECT_PEER);
  p += MSG_CMD_LEN;

  // pub addr
  *(uint16_t *)p = (uint16_t)zmqPubAddr.length();
  p += 2;
  memcpy((char *)p, zmqPubAddr.c_str(), zmqPubAddr.length());
  p += zmqPubAddr.length();

  // req addr
  *(uint16_t *)p = (uint16_t)zmqRepAddr.length();
  p += 2;
  memcpy((char *)p, zmqRepAddr.c_str(), zmqRepAddr.length());
  p += zmqRepAddr.length();
  assert((p - (uint8_t *)zmsg.data()) == zmsg.size());

  zmqReq_->send(zmsg);

  // 2. recv response. req-rep model need to call recv().
  string smsg = s_recv(*zmqReq_);
//  DLOG(INFO) << smsg;

  lastRecvMsgTime_ = time(nullptr);
}

void NodePeer::sendMissingTxs(const vector<uint256> &missingTxs) {
  zmq::message_t zmsg;
  zmsg.rebuild(MSG_CMD_LEN + missingTxs.size() * SHORT_HASH_SIZE);

  memset((char *)zmsg.data(), 0, MSG_CMD_LEN);
  sprintf((char *)zmsg.data(), "%s", MSG_CMD_GET_TXS);

  uint8_t *p = (uint8_t *)zmsg.data() + MSG_CMD_LEN;
  for (auto txhash : missingTxs) {
    memcpy(p, txhash.begin(), SHORT_HASH_SIZE);
    p += SHORT_HASH_SIZE;
  }
  assert(p - (uint8_t *)zmsg.data() == zmsg.size());

  zmqReq_->send(zmsg);
}

void NodePeer::recvMissingTxs() {
  zmq::message_t zmsg;
  zmqReq_->recv(&zmsg);

  unsigned char *p = (unsigned char *)zmsg.data();
  unsigned char *e = (unsigned char *)zmsg.data() + zmsg.size();
  while (p < e) {
    // size
    const int32_t txlen = *(int32_t *)p;
    p += sizeof(int32_t);

    // tx content
    CTransaction tx;
    DecodeBinTx(tx, p, txlen);
    p += txlen;

    txRepo_->AddTx(tx);
  }
}

bool NodePeer::buildBlockFromThin(const string &thinBlock, CBlock &block) {
  const size_t txCnt = getTxCountFromThinBlock(thinBlock);
  unsigned char *p = (unsigned char *)thinBlock.data() + sizeof(CBlockHeader);

  CBlockHeader header;
  memcpy((uint8_t *)&header, thinBlock.data(), sizeof(CBlockHeader));

  block.SetNull();
  block = header;

  // txs
  for (size_t i = 0; i < txCnt; i++) {
    uint256 hash;
    memcpy(hash.begin(), p, SHORT_HASH_SIZE);
    p += SHORT_HASH_SIZE;

    CTransaction tx;
    if (!txRepo_->getTx(hash, tx)) {
      LOG(FATAL) << "missing tx when build block, tx: " << hash.ToString();
      return false;
    }
    block.vtx.push_back(tx);
  }
  return true;
}

bool NodePeer::isAlive() {
  if (running_ && (lastRecvMsgTime_ + 120) > time(nullptr)) {
    return true;
  }
  return false;
}

void NodePeer::handleMsgThinBlock(const string &thinBlock) {
  const uint256 blkhash = getHashFromThinBlock(thinBlock);
  LOG(INFO) << "received thin block, size: " << thinBlock.size()
  << ", hash: " << blkhash.ToString()
  << ", tx count: " << getTxCountFromThinBlock(thinBlock);

  if (nodeBoost_->isExistBlock(blkhash)) {
    LOG(INFO) << "ingore thin block, already exist";
    return;
  }

  vector<uint256> missingTxs;
  nodeBoost_->findMissingTxs(thinBlock, missingTxs);

  if (missingTxs.size() > 0) {
    LOG(INFO) << "request 'get_txs', missing tx count: " << missingTxs.size();
    // send cmd: "get_txs"
    sendMissingTxs(missingTxs);
    recvMissingTxs();
  }

  // submit block
  CBlock block;
  if (!buildBlockFromThin(thinBlock, block)) {
    string hex;
    Bin2Hex((uint8 *)thinBlock.data(), thinBlock.size(), hex);
    LOG(ERROR) << "build block failure, hex: " << hex;
  }

  assert(blkhash == block.GetHash());
  nodeBoost_->foundNewBlock(block, false/* not found by bitcoind*/);
}

void NodePeer::run() {
  while (running_) {

    // sub
    zmq::message_t ztype, zcontent;
    try {
      if (zmqSub_->recv(&ztype, ZMQ_DONTWAIT) == false) {
        if (!running_) { break; }
        usleep(50000);  // so we sleep and try again
        continue;
      }
      zmqSub_->recv(&zcontent);
    } catch (std::exception & e) {
      LOG(ERROR) << "node peer recv exception: " << e.what();
      break;  // break big while
    }
    const string type    = std::string(static_cast<char*>(ztype.data()),    ztype.size());
    const string content = std::string(static_cast<char*>(zcontent.data()), zcontent.size());

    lastRecvMsgTime_ = time(nullptr);

    // MSG_PUB_THIN_BLOCK
    if (type == MSG_PUB_THIN_BLOCK)
    {
      handleMsgThinBlock(content);
    }
    else if (type == MSG_PUB_HEARTBEAT)
    {
      // do nothing
    }
    else if (type == MSG_PUB_CLOSEPEER)
    {
      LOG(INFO) << "received close_peer from: " << subAddr_ << ", content: " << content;
      break;  // break big while
    }
    else
    {
      LOG(ERROR) << "unknown message type: " << type;
    }

  } /* /while */

  zmqSub_->close();
  zmqReq_->close();

  stop();  // set stop flag
}

//////////////////////////////////// TxRepo ////////////////////////////////////
TxRepo::TxRepo(): lock_() {
}
TxRepo::~TxRepo() {
}

bool TxRepo::isExist(const uint256 &hash) {
  ScopeLock sl(lock_);

  const uint256 shash = shortHash(hash);
  if (txsPool_.count(shash)) {
    return true;
  }
  return false;
}

void TxRepo::AddTx(const CTransaction &tx) {
  ScopeLock sl(lock_);
  const uint256 shash = shortHash(tx.GetHash());
  if (txsPool_.count(shash)) {
    return;
  }
  txsPool_.insert(std::make_pair(shash, tx));
//  LOG(INFO) << "tx repo add tx: " << hash.ToString();
}

bool TxRepo::getTx(const uint256 &hash, CTransaction &tx) {
  ScopeLock sl(lock_);

  const uint256 shash = shortHash(hash);
  auto it = txsPool_.find(shash);
  if (it == txsPool_.end()) {
    return false;
  }
  tx = it->second;
  return true;
}

void TxRepo::DelTx(const uint256 &hash) {
  ScopeLock sl(lock_);
  const uint256 shash = shortHash(hash);
  txsPool_.erase(shash);
}

size_t TxRepo::size() {
  ScopeLock sl(lock_);
  return txsPool_.size();
}


//////////////////////////////////// NodeBoost ////////////////////////////////////
NodeBoost::NodeBoost(const string &zmqPubAddr, const string &zmqRepAddr,
                     const string &zmqBitcoind,
                     const string &bitcoindRpcAddr, const string &bitcoindRpcUserpass,
                     TxRepo *txRepo)
: zmqContext_(2/*i/o threads*/),
txRepo_(txRepo), zmqPubAddr_(zmqPubAddr), zmqRepAddr_(zmqRepAddr), zmqBitcoind_(zmqBitcoind),
bitcoindRpcAddr_(bitcoindRpcAddr), bitcoindRpcUserpass_(bitcoindRpcUserpass)
{
  // publisher
  zmqPub_ = new zmq::socket_t(zmqContext_, ZMQ_PUB);
  zmqPub_->bind(zmqPubAddr_);

  // response
  zmqRep_ = new zmq::socket_t(zmqContext_, ZMQ_REP);
  zmqRep_->bind(zmqRepAddr_);
}

NodeBoost::~NodeBoost() {
  zmqRep_ = nullptr;
  zmqPub_ = nullptr;
}

void NodeBoost::stop() {
  if (!running_) {
    return;
  }
  running_ = false;
}

void NodeBoost::findMissingTxs(const string &thinBlock,
                               vector<uint256> &missingTxs) {
  const size_t kHeaderSize = 80;
  assert(kHeaderSize == sizeof(CBlockHeader));
  assert(thinBlock.size() > kHeaderSize);

  uint8_t *p = (uint8_t *)thinBlock.data() + kHeaderSize;
  uint8_t *e = (uint8_t *)thinBlock.data() + thinBlock.size();
  assert((e - p) % SHORT_HASH_SIZE == 0);

  while (p < e) {
    uint256 hash;
    memcpy(hash.begin(), p, SHORT_HASH_SIZE);
    p += SHORT_HASH_SIZE;

    if (!txRepo_->isExist(hash)) {
      missingTxs.push_back(hash);
    }
  }
}

void NodeBoost::handleGetTxs(const zmq::message_t &zin, zmq::message_t &zout) {
  uint8_t *p = (uint8_t *)zin.data() + MSG_CMD_LEN;
  uint8_t *e = (uint8_t *)zin.data() + zin.size();
  assert((e - p) % SHORT_HASH_SIZE == 0);

  zmq::message_t ztmp(4*1024*1024);  // 4MB, TODO: resize in the future
  uint8_t *data = (uint8_t *)ztmp.data();

  while (p < e) {
    uint256 hash;
    memcpy(hash.begin(), p, SHORT_HASH_SIZE);
    p += SHORT_HASH_SIZE;

    CTransaction tx;
    if (!txRepo_->getTx(hash, tx)) {
      LOG(INFO) << "missing tx: " << hash.ToString();
    }

    CDataStream ssTx(SER_NETWORK, BITCOIN_PROTOCOL_VERSION);
    ssTx << tx;
    const size_t txSize = ssTx.end() - ssTx.begin();

    // size
    *(int32_t *)data = (int32_t)txSize;
    data += sizeof(int32_t);

    // tx
    memcpy(data, &(*ssTx.begin()), txSize);
    data += txSize;
  }

  zout.rebuild(ztmp.data(), data - (uint8_t *)ztmp.data());
}

void NodeBoost::handleConnPeer(const zmq::message_t &zin) {
  uint8_t *p = (uint8_t *)zin.data() + MSG_CMD_LEN;
  string zmqPubAddr, zmqRepAddr;
  uint16_t lenp, lenr;

  lenp = *(uint16_t *)p;
  zmqPubAddr.assign((char *)(p + 2), (size_t)lenp);
  p += (2 + lenp);

  lenr = *(uint16_t *)p;
  zmqRepAddr.assign((char *)(p + 2), (size_t)lenr);
  p += (2 + lenr);

  assert((p - (uint8_t *)zin.data()) == zin.size());

  // connect peer
  peerConnect(zmqPubAddr, zmqRepAddr);
}

void NodeBoost::threadZmqResponse() {
  LOG(INFO) << "start thread zmq response";

  while (running_) {
    zmq::message_t zmsg;
    // false meaning non-block, read nothing
    if (zmqRep_->recv(&zmsg, ZMQ_DONTWAIT) == false) {
      if (!running_) { break; }
      usleep(50000);  // so we sleep and try again
      continue;
    }
    assert(zmsg.size() >= MSG_CMD_LEN);

    // cmd
    char cmd[MSG_CMD_LEN];
    snprintf(cmd, sizeof(cmd), "%s", (char *)zmsg.data());
    LOG(INFO) << "receive cmd: " << cmd;

    // handle message
    if (strncmp(cmd, MSG_CMD_GET_TXS, MSG_CMD_LEN) == 0)
    {
      zmq::message_t zout;
      handleGetTxs(zmsg, zout);
      zmqRep_->send(zout);
    }
    else if (strncmp(cmd, MSG_CMD_CONNECT_PEER, MSG_CMD_LEN) == 0)
    {
      handleConnPeer(zmsg);
      s_send(*zmqRep_, "ACK");  // send anything
    }
    else
    {
      s_send(*zmqRep_, "unknown");
      LOG(ERROR) << "unknown cmd: " << cmd;
    }
  }
  LOG(INFO) << "stop thread zmq response";
}

static void _buildMsgThinBlock(const CBlock &block, zmq::message_t &zmsg) {
  const size_t kHeaderSize = 80;
  assert(kHeaderSize == sizeof(CBlockHeader));

  zmsg.rebuild(kHeaderSize + block.vtx.size() * SHORT_HASH_SIZE);
  unsigned char *p = (unsigned char *)zmsg.data();

  CBlockHeader header = block.GetBlockHeader();
  memcpy(p, (uint8 *)&header, sizeof(CBlockHeader));
  p += kHeaderSize;

  for (size_t i = 0; i < block.vtx.size(); i++) {
    uint256 hash = block.vtx[i].GetHash();
    memcpy(p, hash.begin(), SHORT_HASH_SIZE);
    p += SHORT_HASH_SIZE;
  }
}

void NodeBoost::threadListenBitcoind() {
  zmq::socket_t subscriber(zmqContext_, ZMQ_SUB);
  subscriber.connect(zmqBitcoind_);
  subscriber.setsockopt(ZMQ_SUBSCRIBE, "rawblock", 8);
  subscriber.setsockopt(ZMQ_SUBSCRIBE, "rawtx",    5);

  LOG(INFO) << "start thread listen to bitcoind";
  while (running_) {
    zmq::message_t ztype, zcontent;
    try {
      if (subscriber.recv(&ztype, ZMQ_DONTWAIT) == false) {
        if (!running_) { break; }
        usleep(50000);  // so we sleep and try again
        continue;
      }
      subscriber.recv(&zcontent);
    } catch (std::exception & e) {
      LOG(ERROR) << "bitcoind zmq recv exception: " << e.what();
      break;  // break big while
    }
    const string type    = std::string(static_cast<char*>(ztype.data()),    ztype.size());
    const string content = std::string(static_cast<char*>(zcontent.data()), zcontent.size());

    if (type == "rawtx") {
      CTransaction tx;
      DecodeBinTx(tx, (const unsigned char *)content.data(), content.length());
      txRepo_->AddTx(tx);
    }
    else if (type == "rawblock")
    {
      CBlock block;
      DecodeBinBlk(block, (const unsigned char *)content.data(), content.length());
      LOG(INFO) << "received rawblock: " << block.GetHash().ToString()
      << ", tx count: " << block.vtx.size() << ", size: " << content.length();

      foundNewBlock(block, true/* found by bitcoind */);
    }
    else
    {
      LOG(ERROR) << "unknown message type from bitcoind: " << type;
    }
  } /* /while */

  subscriber.close();
  LOG(INFO) << "stop thread listen to bitcoind";
}

void NodeBoost::foundNewBlock(const CBlock &block, bool isFoundByBitcoind) {
  {
    ScopeLock sl(historyLock_);
    const uint256 hash = block.GetHash();
    if (blockHistory_.count(hash)) {
      LOG(INFO) << "block has bend found, ingore it: " << hash.ToString();
      return;  // already has been processed
    }
    blockHistory_.insert(block.GetHash());
  }

  if (!isFoundByBitcoind) {
    submitBlock2Bitcoind(block);  // using thread, none-block
  }
  broadcastBlock(block);

  // clear old block & txs
  blocksQ_.push_back(block);
  while (blocksQ_.size() > 4) {
    auto itr = blocksQ_.begin();
    LOG(INFO) << "clear block & txs, blkhash: " << (*itr).GetHash().ToString();
    size_t txCnt1 = txRepo_->size();

    for (auto tx : (*itr).vtx) {
      txRepo_->DelTx(tx.GetHash());
    }
    blocksQ_.pop_front();

    LOG(INFO) << "txs count: " << txCnt1 << " -> " << txRepo_->size();
  }
}

bool NodeBoost::isExistBlock(const uint256 &hash) {
  ScopeLock sl(historyLock_);
  if (blockHistory_.count(hash)) {
    return true;  // already has been processed
  }
  return false;
}

void NodeBoost::zmqPubMessage(const string &type, zmq::message_t &zmsg) {
  ScopeLock sl(zmqPubLock_);

  zmq::message_t ztype(type.size());
  memcpy(ztype.data(), type.data(), type.size());

  // type
  zmqPub_->send(ztype, ZMQ_SNDMORE);
  // content
  zmqPub_->send(zmsg);
}

void NodeBoost::broadcastBlock(const CBlock &block) {
  // add to txs repo
  for (auto tx : block.vtx) {
    txRepo_->AddTx(tx);
  }

  // broadcast thin block
  LOG(INFO) << "broadcast thin block: " << block.GetHash().ToString();
  zmq::message_t zmsg;
  _buildMsgThinBlock(block, zmsg);
  zmqPubMessage(MSG_PUB_THIN_BLOCK, zmsg);
}

void NodeBoost::broadcastHeartBeat() {
  zmq::message_t zmsg;
  string now = date("%F %T");

  zmsg.rebuild(now.size());
  memcpy(zmsg.data(), now.data(), now.size());
  zmqPubMessage(MSG_PUB_HEARTBEAT, zmsg);
}

void NodeBoost::broadcastClosePeer() {
  zmq::message_t zmsg;
  string now = date("%F %T");

  zmsg.rebuild(now.size());
  memcpy(zmsg.data(), now.data(), now.size());
  zmqPubMessage(MSG_PUB_CLOSEPEER, zmsg);
}

void NodeBoost::run() {
  threadZmqResponse_    = thread(&NodeBoost::threadZmqResponse,    this);
  threadListenBitcoind_ = thread(&NodeBoost::threadListenBitcoind, this);

  time_t lastHeartbeatTime = 0;
  while (running_) {
    if (time(nullptr) >= lastHeartbeatTime + 60) {
      broadcastHeartBeat();
      lastHeartbeatTime = time(nullptr);
    }

    // check peer and delete dead ones
    for (auto itr = peers_.begin(); itr != peers_.end(); ) {
      NodePeer *p = itr->second;
      if (p->isAlive()) {
        ++itr;
        continue;
      }

      // delete dead
      LOG(INFO) << "clear dead peer: " << p->toString();
      p->stop();
      thread *t = peersThreads_[itr->first];
      if (t->joinable()) {
        t->join();
      }
      delete t;
      delete p;
      peersThreads_.erase(itr->first);
      itr = peers_.erase(itr);
    }

    sleep(1);
  } /* /while */

  broadcastClosePeer();
  sleep(1);

  if (threadListenBitcoind_.joinable())
    threadListenBitcoind_.join();

  if (threadZmqResponse_.joinable())
    threadZmqResponse_.join();

  zmqRep_->close();
  zmqPub_->close();

  peerCloseAll();
}

void NodeBoost::peerConnect(const string &subAddr, const string &reqAddr) {
  const string pKey = subAddr + "|" + reqAddr;
  peersThreads_[pKey] = new thread(&NodeBoost::threadPeerConnect, this, subAddr, reqAddr);
}

void NodeBoost::threadPeerConnect(const string subAddr, const string reqAddr) {
  if (running_ == false) {
    LOG(WARNING) << "server has stopped, ignore to connect peer: " << subAddr << ", " << reqAddr;
    return;
  }

  const string pKey = subAddr + "|" + reqAddr;
  if (peers_.count(pKey) != 0) {
    LOG(WARNING) << "already connect to this peer: " << pKey;
    return;
  }

  NodePeer *peer = new NodePeer(subAddr, reqAddr, &zmqContext_, this, txRepo_);
  peer->tellPeerToConnectMyServer(zmqPubAddr_, zmqRepAddr_);
  peers_.insert(make_pair(pKey, peer));

  LOG(INFO) << "connect peer: " << subAddr << ", " << reqAddr;
  peer->run();
}

void NodeBoost::peerCloseAll() {
  for (auto it : peers_) {
    it.second->stop();
  }
  sleep(1);

  for (auto it : peersThreads_) {
    if (it.second->joinable()) {
      it.second->join();
    }
  }

  while (peers_.size()) {
    auto it = peers_.begin();

    delete peersThreads_[it->first];  // thread *
    delete it->second;                // NodePeer *

    peersThreads_.erase(it->first);
    peers_.erase(it);
  }
  assert(peers_.size() == 0);
  assert(peersThreads_.size() == 0);
}

void NodeBoost::threadSubmitBlock2Bitcoind(const string bitcoindRpcAddr,
                                           const string bitcoindRpcUserpass,
                                           const string blockHex) {
  string req;
  req += "{\"jsonrpc\":\"1.0\",\"id\":\"1\",\"method\":\"submitblock\",\"params\":[\"";
  req += blockHex;
  req += "\"]}";

  string rep;
  bool res = bitcoindRpcCall(bitcoindRpcAddr.c_str(), bitcoindRpcUserpass.c_str(),
                             req.c_str(), rep);
  LOG(INFO) << "bitcoind rpc call, rep: " << rep;

  if (!res) {
    LOG(ERROR) << "bitcoind rpc failure";
  }
}

void NodeBoost::submitBlock2Bitcoind(const CBlock &block) {
  // use thread to submit block, none-block
  LOG(INFO) << "submit block to bitcoind, blkhash: " << block.GetHash().ToString();
  const string hex = EncodeHexBlock(block);
  thread t(&NodeBoost::threadSubmitBlock2Bitcoind, this,
           bitcoindRpcAddr_, bitcoindRpcUserpass_, hex);
}


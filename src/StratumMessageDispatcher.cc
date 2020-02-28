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

#include "StratumMessageDispatcher.h"

#include "StratumSession.h"
#include "StratumServer.h"
#include "StratumMiner.h"
#include "DiffController.h"

#include <glog/logging.h>

using namespace std;

#define NULL_DISPATCHER_LOG \
  LOG(ERROR) << "Null message dispatcher shall not be called"

void StratumMessageNullDispatcher::handleRequest(
    const string &idStr,
    const string &method,
    const JsonNode &jparams,
    const JsonNode &jroot) {
  NULL_DISPATCHER_LOG;
}

void StratumMessageNullDispatcher::handleExMessage(const string &exMessage) {
  NULL_DISPATCHER_LOG;
}

void StratumMessageNullDispatcher::responseShareAccepted(const string &idStr) {
  NULL_DISPATCHER_LOG;
}

void StratumMessageNullDispatcher::responseShareAcceptedWithStatus(
    const std::string &idStr, int32_t status) {
  NULL_DISPATCHER_LOG;
}

void StratumMessageNullDispatcher::responseShareError(
    const string &idStr, int32_t status) {
  NULL_DISPATCHER_LOG;
}

void StratumMessageNullDispatcher::setMinDiff(uint64_t minDiff) {
  NULL_DISPATCHER_LOG;
}

void StratumMessageNullDispatcher::resetCurDiff(uint64_t curDiff) {
  NULL_DISPATCHER_LOG;
}

void StratumMessageNullDispatcher::addLocalJob(LocalJob &localJob) {
  NULL_DISPATCHER_LOG;
}

void StratumMessageNullDispatcher::removeLocalJob(LocalJob &localJob) {
  NULL_DISPATCHER_LOG;
}

#undef NULL_DISPATCHER_LOG

StratumMessageMinerDispatcher::StratumMessageMinerDispatcher(
    IStratumSession &session, unique_ptr<StratumMiner> miner)
  : session_(session)
  , miner_(move(miner)) {
}

void StratumMessageMinerDispatcher::handleRequest(
    const string &idStr,
    const string &method,
    const JsonNode &jparams,
    const JsonNode &jroot) {
  miner_->handleRequest(idStr, method, jparams, jroot);
}

void StratumMessageMinerDispatcher::handleExMessage(const string &exMessage) {
  LOG(ERROR) << "Agent message shall not reach here";
}

void StratumMessageMinerDispatcher::responseShareAccepted(const string &idStr) {
  session_.responseTrue(idStr);
}

void StratumMessageMinerDispatcher::responseShareAcceptedWithStatus(
    const std::string &idStr, int32_t status) {
  session_.responseTrueWithCode(idStr, status);
}

void StratumMessageMinerDispatcher::responseShareError(
    const string &idStr, int32_t status) {
  session_.responseError(idStr, status);
}

void StratumMessageMinerDispatcher::setMinDiff(uint64_t minDiff) {
  miner_->setMinDiff(minDiff);
}

void StratumMessageMinerDispatcher::resetCurDiff(uint64_t curDiff) {
  miner_->resetCurDiff(curDiff);
}

void StratumMessageMinerDispatcher::addLocalJob(LocalJob &localJob) {
  auto oldDiff = miner_->getCurDiff();
  auto newDiff = miner_->addLocalJob(localJob);
  if (newDiff != oldDiff) {
    session_.sendSetDifficulty(localJob, newDiff);
  }
}

void StratumMessageMinerDispatcher::removeLocalJob(LocalJob &localJob) {
  miner_->removeLocalJob(localJob);
}

struct StratumMessageExSessionSpecific {
  boost::endian::little_uint8_buf_t magic;
  boost::endian::little_uint8_buf_t command;
  boost::endian::little_uint16_buf_t length;
  boost::endian::little_uint16_buf_t sessionId;
};

struct StratumMessageExMiningSetDiff {
  boost::endian::little_uint8_buf_t magic;
  boost::endian::little_uint8_buf_t command;
  boost::endian::little_uint16_buf_t length;
  boost::endian::little_uint8_buf_t diffExp;
  boost::endian::little_uint16_buf_t count;
};

struct StratumMessageExSubmitResponse {
  boost::endian::little_uint8_buf_t magic;
  boost::endian::little_uint8_buf_t command;
  boost::endian::little_uint16_buf_t length;
  boost::endian::little_uint16_buf_t index;
  boost::endian::little_int32_buf_t status;
};

StratumMessageAgentDispatcher::StratumMessageAgentDispatcher(
    IStratumSession &session, const DiffController &diffController)
  : session_(session)
  , diffController_(new DiffController(diffController)) {
}

StratumMessageAgentDispatcher::~StratumMessageAgentDispatcher() {
  while (!miners_.empty()) {
    unregisterWorker(miners_.begin()->first);
  }
}

void StratumMessageAgentDispatcher::handleRequest(
    const string &idStr,
    const string &method,
    const JsonNode &jparams,
    const JsonNode &jroot) {
  LOG(ERROR) << "Miner message shall not reach here";
}

void StratumMessageAgentDispatcher::handleExMessage(const string &exMessage) {
  auto header = reinterpret_cast<const StratumMessageEx *>(exMessage.data());
  assert(exMessage.size() == header->length.value());
  auto command = static_cast<StratumCommandEx>(header->command.value());
  switch (command) {
  case StratumCommandEx::REGISTER_WORKER:
    handleExMessage_RegisterWorker(exMessage);
    break;
  case StratumCommandEx::UNREGISTER_WORKER:
    handleExMessage_UnregisterWorker(exMessage);
    break;
  case StratumCommandEx::SUBMIT_SHARE:
  case StratumCommandEx::SUBMIT_SHARE_WITH_TIME:
  case StratumCommandEx::SUBMIT_SHARE_WITH_VER:
  case StratumCommandEx::SUBMIT_SHARE_WITH_TIME_VER:
    handleExMessage_SessionSpecific(exMessage);
    break;
  default:
    break;
  }
}

void StratumMessageAgentDispatcher::setMinDiff(uint64_t minDiff) {
  for (auto &p : miners_) {
    p.second->setMinDiff(minDiff);
  }
}

void StratumMessageAgentDispatcher::resetCurDiff(uint64_t curDiff) {
  for (auto &p : miners_) {
    p.second->resetCurDiff(curDiff);
  }
}

void StratumMessageAgentDispatcher::addLocalJob(LocalJob &localJob) {
  uint64_t agentDiff = diffController_->calcCurDiff();
  if (agentDiff != curDiff_) {
    session_.sendSetDifficulty(localJob, agentDiff);
    curDiff_ = agentDiff;
  }

  map<uint8_t, vector<uint16_t>> newDiffs;
  for (auto &p : miners_) {
    uint64_t curDiff = p.second->getCurDiff();
    uint8_t oldDiff = curDiff ? log2(curDiff) : 0;
    uint8_t newDiff = log2(p.second->addLocalJob(localJob));
    if (newDiff != oldDiff) {
      newDiffs[newDiff].push_back(p.first);
    }
  }

  if (!newDiffs.empty()) {
    //
    // CMD_MINING_SET_DIFF:
    // | magic_number(1) | cmd(1) | len (2) | diff_2_exp(1) | count(2) |
    // session_id (2) ... |
    //
    //
    // max session id count is 32,764, each message's max length is UINT16_MAX.
    //     65535 -1-1-2-1-2 = 65,528
    //     65,528 / 2 = 32,764
    //
    string data;
    getSetDiffCommand(newDiffs, data);
    session_.sendData(data);
  }
}

void StratumMessageAgentDispatcher::removeLocalJob(LocalJob &localJob) {
  for (auto &p : miners_) {
    p.second->removeLocalJob(localJob);
  }
}

void StratumMessageAgentDispatcher::beforeSwitchChain() {
  // remove worker from the old chain
  for (auto &itr : miners_) {
    auto &miner = itr.second;
    if (miner) {
      session_.removeWorker(
          miner->clientAgent(), miner->workerName(), miner->workerId());
    }
  }
}

void StratumMessageAgentDispatcher::afterSwitchChain() {
  // add worker to the new chain
  for (auto &itr : miners_) {
    auto &miner = itr.second;
    if (miner) {
      session_.addWorker(
          miner->clientAgent(), miner->workerName(), miner->workerId());
    }
  }
}

void StratumMessageAgentDispatcher::handleExMessage_RegisterWorker(
    const string &exMessage) {
  //
  // REGISTER_WORKER:
  // | magic_number(1) | cmd(1) | len (2) | session_id(2) | clientAgent |
  // worker_name |
  //
  if (exMessage.size() < 8 ||
      exMessage.size() > 100 /* 100 bytes is big enough */)
    return;

  auto header = reinterpret_cast<const StratumMessageExSessionSpecific *>(
      exMessage.data());
  auto sessionId = header->sessionId.value();
  if (sessionId > StratumMessageEx::AGENT_MAX_SESSION_ID)
    return;

  // copy out string and make sure end with zero
  string clientStr;
  clientStr.append(exMessage.begin() + 6, exMessage.end());
  clientStr[clientStr.size() - 1] = '\0';

  // client agent
  auto clientAgentPtr = clientStr.c_str();
  auto clientAgent = filterWorkerName(clientAgentPtr);

  // worker name
  string workerName;
  if (strlen(clientAgentPtr) < clientStr.size() - 2) {
    workerName = filterWorkerName(clientAgentPtr + strlen(clientAgentPtr) + 1);
  }
  if (workerName.empty())
    workerName = DEFAULT_WORKER_NAME;

  // worker Id
  auto workerId = StratumWorker::calcWorkerId(workerName);
  registerWorker(sessionId, clientAgent, workerName, workerId);
}

void StratumMessageAgentDispatcher::handleExMessage_UnregisterWorker(
    const string &exMessage) {
  //
  // UNREGISTER_WORKER:
  // | magic_number(1) | cmd(1) | len (2) | session_id(2) |
  //
  if (exMessage.size() != 6)
    return;
  auto header = reinterpret_cast<const StratumMessageExSessionSpecific *>(
      exMessage.data());
  auto sessionId = header->sessionId.value();
  unregisterWorker(sessionId);
}

void StratumMessageAgentDispatcher::handleExMessage_SessionSpecific(
    const string &exMessage) {
  //
  // Session specific messages
  // | magic_number(1) | cmd(1) | len (2) | ... | session_id(2) | ...
  //
  auto sessionId = session_.decodeSessionId(exMessage);
  auto iter = miners_.find(sessionId);
  if (iter != miners_.end()) {
    iter->second->handleExMessage(exMessage);
  }
}

void StratumMessageAgentDispatcher::registerWorker(
    uint32_t sessionId,
    const std::string &clientAgent,
    const std::string &workerName,
    int64_t workerId) {
  DLOG(INFO) << "[agent] clientAgent: " << clientAgent
             << ", workerName: " << workerName << ", workerId: " << workerId
             << ", session id:" << sessionId;
  miners_.emplace(
      sessionId, session_.createMiner(clientAgent, workerName, workerId));
  session_.addWorker(clientAgent, workerName, workerId);
}

void StratumMessageAgentDispatcher::unregisterWorker(uint32_t sessionId) {
  auto &miner = miners_[sessionId];
  if (miner) {
    session_.removeWorker(
        miner->clientAgent(), miner->workerName(), miner->workerId());
  }
  miners_.erase(sessionId);
}

void StratumMessageAgentDispatcher::getSetDiffCommand(
    std::map<uint8_t, std::vector<uint16_t>> &diffSessionIds,
    std::string &exMessage) {
  //
  // CMD_MINING_SET_DIFF:
  // | magic_number(1) | cmd(1) | len (2) | diff_2_exp(1) | count(2) |
  // session_id (2) ... |
  //
  //
  // max session id count is 32,764, each message's max length is UINT16_MAX.
  //     65535 -1-1-2-1-2 = 65,528
  //     65,528 / 2 = 32,764
  //
  static const size_t kMaxCount = 32764;
  exMessage.clear();

  for (auto &p : diffSessionIds) {
    auto iter = p.second.begin();
    auto iend = p.second.end();
    while (iter != iend) {
      size_t count = distance(iter, iend);
      if (count > kMaxCount)
        count = kMaxCount;

      string buf;
      uint16_t len = 1 + 1 + 2 + 1 + 2 + count * 2;
      buf.resize(len);
      auto start = &buf.front();
      auto header = reinterpret_cast<StratumMessageExMiningSetDiff *>(start);

      // cmd
      header->magic = StratumMessageEx::CMD_MAGIC_NUMBER;
      header->command = static_cast<uint8_t>(StratumCommandEx::MINING_SET_DIFF);

      // len
      header->length = len;

      // diff, 2 exp
      header->diffExp = p.first;

      // count
      header->count = count;
      auto p = reinterpret_cast<boost::endian::little_uint16_buf_t *>(
          start + 1 + 1 + 2 + 1 + 2);

      // session ids
      for (size_t j = 0; j < count; j++) {
        *(p++) = *(iter++);
      }

      exMessage.append(buf);

    } /* /while */
  } /* /for */
}

void StratumMessageAgentDispatcher::responseShareAccepted(
    const std::string &idStr) {
  responseShareError(idStr, StratumStatus::ACCEPT);
}
void StratumMessageAgentDispatcher::responseShareAcceptedWithStatus(
    const std::string &idStr, int32_t status) {
  responseShareError(idStr, status);
}
void StratumMessageAgentDispatcher::responseShareError(
    const std::string &idStr, int32_t status) {
  if (!enableSubmitResponse_) {
    return;
  }

  //
  // CMD_SUBMIT_RESPONSE
  // | magic_number(1) | cmd(1) | len(2) | index(2) | status(4) |
  //
  string buf;
  uint16_t len = sizeof(StratumMessageExSubmitResponse);
  buf.resize(len);
  auto start = &buf.front();
  auto msg = reinterpret_cast<StratumMessageExSubmitResponse *>(start);

  msg->magic = StratumMessageEx::CMD_MAGIC_NUMBER;
  msg->command = static_cast<uint8_t>(StratumCommandEx::SUBMIT_RESPONSE);
  msg->length = len;
  msg->index = strtol(idStr.c_str(), nullptr, 10);
  msg->status = status;

  session_.sendData(buf);
}

void StratumMessageAgentDispatcher::setSubmitResponse(bool enabled) {
  enableSubmitResponse_ = enabled;
}

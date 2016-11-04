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

#include "gtest/gtest.h"
#include "Common.h"
#include "Utils.h"

#include <boost/algorithm/string.hpp>

#include "StratumSession.h"

TEST(StratumSession, LocalShare) {
  StratumSession::LocalShare ls1(0xFFFFFFFFFFFFFFFFULL,
                                 0xFFFFFFFFU, 0xFFFFFFFFU);

  {
    StratumSession::LocalShare ls2(0xFFFFFFFFFFFFFFFEULL,
                                   0xFFFFFFFFU, 0xFFFFFFFFU);
    ASSERT_EQ(ls2 < ls1, true);
  }
  {
    StratumSession::LocalShare ls2(0xFFFFFFFFFFFFFFFFULL,
                                   0xFFFFFFFEU, 0xFFFFFFFFU);
    ASSERT_EQ(ls2 < ls1, true);
  }
  {
    StratumSession::LocalShare ls2(0xFFFFFFFFFFFFFFFFULL,
                                   0xFFFFFFFFU, 0xFFFFFFFEU);
    ASSERT_EQ(ls2 < ls1, true);
  }
  {
    StratumSession::LocalShare ls2(0xFFFFFFFFFFFFFFFFULL,
                                   0xFFFFFFFFU, 0xFFFFFFFFU);
    ASSERT_EQ(ls2 < ls1, false);
    ASSERT_EQ(ls2 < ls2, false);
  }
  {
    StratumSession::LocalShare ls2(0x0ULL, 0x0U, 0x0U);
    ls2 = ls1;
    ASSERT_EQ(ls2 < ls1, false);
    ASSERT_EQ(ls2 < ls2, false);
  }
}


TEST(StratumSession, LocalJob) {
  StratumSession::LocalJob lj;

  {
    StratumSession::LocalShare ls1(0xFFFFFFFFFFFFFFFFULL,
                                   0xFFFFFFFFU, 0xFFFFFFFFU);
    ASSERT_EQ(lj.addLocalShare(ls1), true);
  }
  {
    StratumSession::LocalShare ls1(0xFFFFFFFFFFFFFFFFULL,
                                   0xFFFFFFFFU, 0xFFFFFFFFU);
    ASSERT_EQ(lj.addLocalShare(ls1), false);
  }
  {
    StratumSession::LocalShare ls2(0x0ULL, 0x0U, 0x0U);
    ASSERT_EQ(lj.addLocalShare(ls2), true);
  }
  {
    StratumSession::LocalShare ls2(0x0ULL, 0x0U, 0x0U);
    ASSERT_EQ(lj.addLocalShare(ls2), false);
  }
}

TEST(StratumSession, AgentSessions_RegisterWorker) {
  AgentSessions agent(10, nullptr);

  // | magic_number(1) | cmd(1) | len (2) | session_id(2) | clientAgent | worker_name |
  string exMessage;
  const string clientAgent = "cgminer\"1'";
  const string workerName  = "bitkevin.testcase";
  const uint16_t sessionId = AGENT_MAX_SESSION_ID;
  exMessage.resize(1+1+2+2 + clientAgent.length() + 1 + workerName.length() + 1);

  uint8_t *p = (uint8_t *)exMessage.data();

  // cmd
  *p++ = CMD_MAGIC_NUMBER;
  *p++ = CMD_REGISTER_WORKER;

  // len
  *(uint16_t *)p = (uint16_t)exMessage.size();
  p += 2;

  // session Id
  *(uint16_t *)p = sessionId;
  p += 2;

  // client agent
  strcpy((char *)p, clientAgent.c_str());
  p += strlen(clientAgent.c_str()) + 1;

  // worker name
  strcpy((char *)p, workerName.c_str());
  p += strlen(workerName.c_str()) + 1;

  ASSERT_EQ(p - (uint8_t *)exMessage.data(), exMessage.size());

  agent.handleExMessage_RegisterWorker(&exMessage);
  // please check ouput log
}

TEST(StratumSession, AgentSessions_RegisterWorker2) {
  AgentSessions agent(10, nullptr);

  // | magic_number(1) | cmd(1) | len (2) | session_id(2) | clientAgent | worker_name |
  string exMessage;
  const string clientAgent = "\"'";
  const string workerName = "a.b";
  const uint16_t sessionId = 0;
  exMessage.resize(1+1+2+2 + clientAgent.length() + 1 + workerName.length() + 1, 0);

  uint8_t *p = (uint8_t *)exMessage.data();

  // cmd
  *p++ = CMD_MAGIC_NUMBER;
  *p++ = CMD_REGISTER_WORKER;

  // len
  *(uint16_t *)p = (uint16_t)exMessage.size();
  p += 2;

  // session Id
  *(uint16_t *)p = sessionId;
  p += 2;

  // client agent
  strcpy((char *)p, clientAgent.c_str());
  p += strlen(clientAgent.c_str()) + 1;

  // worker name
  strcpy((char *)p, workerName.c_str());
  p += strlen(workerName.c_str()) + 1;

  ASSERT_EQ(p - (uint8_t *)exMessage.data(), exMessage.size());

  agent.handleExMessage_RegisterWorker(&exMessage);
  // please check ouput log
}

TEST(StratumSession, AgentSessions_RegisterWorker3) {
  AgentSessions agent(10, nullptr);

  // | magic_number(1) | cmd(1) | len (2) | session_id(2) | clientAgent | worker_name |
  string exMessage;
  const string clientAgent;
  const string workerName;
  const uint16_t sessionId = 0;
  exMessage.resize(1+1+2+2 + clientAgent.length() + 1 + workerName.length() + 1, 0);

  uint8_t *p = (uint8_t *)exMessage.data();

  // cmd
  *p++ = CMD_MAGIC_NUMBER;
  *p++ = CMD_REGISTER_WORKER;

  // len
  *(uint16_t *)p = (uint16_t)exMessage.size();
  p += 2;

  // session Id
  *(uint16_t *)p = sessionId;
  p += 2;

  // client agent
  strcpy((char *)p, clientAgent.c_str());
  p += strlen(clientAgent.c_str()) + 1;

  // worker name
  strcpy((char *)p, workerName.c_str());
  p += strlen(workerName.c_str()) + 1;

  ASSERT_EQ(p - (uint8_t *)exMessage.data(), exMessage.size());

  agent.handleExMessage_RegisterWorker(&exMessage);
  // please check ouput log
}

TEST(StratumSession, AgentSessions_RegisterWorker4) {
  AgentSessions agent(10, nullptr);

  // | magic_number(1) | cmd(1) | len (2) | session_id(2) | clientAgent | worker_name |
  string exMessage;
  const uint16_t sessionId = AGENT_MAX_SESSION_ID;
  exMessage.resize(1+1+2+2 + 1 + 1, 0);

  uint8_t *p = (uint8_t *)exMessage.data();

  // cmd
  *p++ = CMD_MAGIC_NUMBER;
  *p++ = CMD_REGISTER_WORKER;

  // len
  *(uint16_t *)p = (uint16_t)exMessage.size();
  p += 2;

  // session Id
  *(uint16_t *)p = sessionId;
  p += 2;

  // client agent
  p++;

  // worker name
  p++;

  ASSERT_EQ(p - (uint8_t *)exMessage.data(), exMessage.size());

  //
  // empty agent and name
  //
  agent.handleExMessage_RegisterWorker(&exMessage);
  // please check ouput log
  //   clientAgent: , workerName: default

  //
  // no zero
  //
  exMessage[exMessage.size() - 1] = 'n';
  exMessage[exMessage.size() - 2] = 'a';
  agent.handleExMessage_RegisterWorker(&exMessage);
  // please check ouput log
  //   clientAgent: a, workerName: default

  //
  //
  //
  exMessage[exMessage.size() - 1] = '\0';
  exMessage[exMessage.size() - 2] = 'a';
  agent.handleExMessage_RegisterWorker(&exMessage);
  // please check ouput log
  //   clientAgent: a, workerName: default

  //
  //
  //
  exMessage[exMessage.size() - 1] = 'n';
  exMessage[exMessage.size() - 2] = '\0';
  agent.handleExMessage_RegisterWorker(&exMessage);
  // please check ouput log
  //   clientAgent: , workerName: default

  //
  //
  //
  exMessage.resize(exMessage.size() + 1);
  (*(uint16_t *)(exMessage.data() + 2))++;  // len++

  exMessage[exMessage.size() - 1] = 'n';
  exMessage[exMessage.size() - 2] = '\0';
  exMessage[exMessage.size() - 3] = '\0';
  agent.handleExMessage_RegisterWorker(&exMessage);
  // please check ouput log
  //   clientAgent: , workerName: default

  //
  //
  //
  exMessage[exMessage.size() - 1] = '\0';
  exMessage[exMessage.size() - 2] = 'n';
  exMessage[exMessage.size() - 3] = '\0';
  agent.handleExMessage_RegisterWorker(&exMessage);
  // please check ouput log
  //   clientAgent: , workerName: n
}

TEST(StratumSession, AgentSessions_SubmitShare) {
  AgentSessions agent(10, nullptr);

  //
  // CMD_SUBMIT_SHARE / CMD_SUBMIT_SHARE_WITH_TIME:
  // | magic_number(1) | cmd(1) | len (2) | jobId (uint8_t) | session_id (uint16_t) |
  // | extra_nonce2 (uint32_t) | nNonce (uint32_t) | [nTime (uint32_t) |]
  //
  const string jobId = "9";
  const uint16_t sessionId = AGENT_MAX_SESSION_ID;

  string exMessage;
  exMessage.resize(1+1+2+1+2+4+4, 0);

  uint8_t *p = (uint8_t *)exMessage.data();

  // cmd
  *p++ = CMD_MAGIC_NUMBER;
  *p++ = CMD_SUBMIT_SHARE;
  // len
  *(uint16_t *)p = (uint16_t)exMessage.size();
  p += 2;
  // jobId
  *p++ = *jobId.c_str();
  // session Id
  *(uint16_t *)p = sessionId;
  p += 2;
  // extra_nonce2
  *(uint32_t *)p = 0x12345678;  // 305419896
  p += 4;
  // nonce
  *(uint32_t *)p = 0x90abcdef;  // 2427178479
  p += 4;

  ASSERT_EQ(p - (uint8_t *)exMessage.data(), exMessage.size());

  agent.handleExMessage_SubmitShare(&exMessage, false);
  // please check ouput log
}

TEST(StratumSession, AgentSessions_SubmitShare_with_time) {
  AgentSessions agent(10, nullptr);

  //
  // CMD_SUBMIT_SHARE / CMD_SUBMIT_SHARE_WITH_TIME:
  // | magic_number(1) | cmd(1) | len (2) | jobId (uint8_t) | session_id (uint16_t) |
  // | extra_nonce2 (uint32_t) | nNonce (uint32_t) | [nTime (uint32_t) |]
  //
  const string jobId = "9";
  const uint16_t sessionId = AGENT_MAX_SESSION_ID;

  string exMessage;
  exMessage.resize(1+1+2+1+2+4+4+4, 0);

  uint8_t *p = (uint8_t *)exMessage.data();

  // cmd
  *p++ = CMD_MAGIC_NUMBER;
  *p++ = CMD_SUBMIT_SHARE_WITH_TIME;
  // len
  *(uint16_t *)p = (uint16_t)exMessage.size();
  p += 2;
  // jobId
  *p++ = *jobId.c_str();
  // session Id
  *(uint16_t *)p = sessionId;
  p += 2;
  // extra_nonce2
  *(uint32_t *)p = 0x12345678u;  // 305419896
  p += 4;
  // nonce
  *(uint32_t *)p = 0xFFabcdefu;  // 4289449455
  p += 4;
  // time
  *(uint32_t *)p = 0xcdef90abu;  // 3455029419
  p += 4;

  ASSERT_EQ(p - (uint8_t *)exMessage.data(), exMessage.size());

  agent.handleExMessage_SubmitShare(&exMessage, true);
  // please check ouput log
}

TEST(StratumSession, AgentSessions_UNREGISTER_WORKER) {
  AgentSessions agent(10, nullptr);
  //
  // CMD_UNREGISTER_WORKER:
  // | magic_number(1) | cmd(1) | len(2) | session_id(2) |
  //
  string exMessage;
  exMessage.resize(6, 0);
  const uint16_t sessionId = AGENT_MAX_SESSION_ID;

  uint8_t *p = (uint8_t *)exMessage.data();

  // cmd
  *p++ = CMD_MAGIC_NUMBER;
  *p++ = CMD_UNREGISTER_WORKER;
  // len
  *(uint16_t *)p = (uint16_t)exMessage.size();
  p += 2;
  // session Id
  *(uint16_t *)p = sessionId;
  p += 2;

  agent.handleExMessage_UnRegisterWorker(&exMessage);
  // please check ouput log
}

TEST(StratumSession, AgentSessions) {
  AgentSessions agent(10, nullptr);

  map<uint8_t, vector<uint16_t> > diffSessionIds;
  string data;

  //
  // CMD_MINING_SET_DIFF:
  // | magic_number(1) | cmd(1) | len (2) | diff_2_exp(1) | count(2) | session_id (2) ... |
  //

  {
    // diff: 1, session_id: 0
    diffSessionIds[1].push_back(0);
  	agent.getSetDiffCommand(diffSessionIds, data);

    uint8_t *p = (uint8_t *)data.data();
    ASSERT_EQ(data.length(), 9);
    ASSERT_EQ(*(uint8_t  *)(p+ 4), 1);  // diff_2exp
    ASSERT_EQ(*(uint16_t *)(p+ 5), 1);  // count
    ASSERT_EQ(*(uint16_t *)(p+ 7), 0);  // first session id
  }

  {
    // size: full
    diffSessionIds.clear();
    for (size_t i = 0; i < UINT16_MAX; i++) {
      // 63 is max diff, 2^63 = UINT64_MAX
      diffSessionIds[63].push_back(i);
    }
    agent.getSetDiffCommand(diffSessionIds, data);

    // 65535 = 32764 + 32764 + 7
    size_t l1 = 1+1+2+1+2+ 32764 * 2;
    size_t l2 = 1+1+2+1+2+ 32764 * 2;
    size_t l3 = 1+1+2+1+2+ 7 * 2;

    ASSERT_EQ(data.length(), l1 + l2 + l3);

    uint8_t *p = (uint8_t *)data.data();
    ASSERT_EQ(*p,         CMD_MAGIC_NUMBER);
    ASSERT_EQ(*(p+l1),    CMD_MAGIC_NUMBER);
    ASSERT_EQ(*(p+l1+l2), CMD_MAGIC_NUMBER);

    // check length
    ASSERT_EQ(*(uint16_t *)(p+2),       l1);
    ASSERT_EQ(*(uint16_t *)(p+2+l1),    l2);
    ASSERT_EQ(*(uint16_t *)(p+2+l1+l2), l3);

    // check diff
    ASSERT_EQ(*(uint8_t *)(p+4),       63);
    ASSERT_EQ(*(uint8_t *)(p+4+l1),    63);
    ASSERT_EQ(*(uint8_t *)(p+4+l1+l2), 63);

    // check count
    ASSERT_EQ(*(uint16_t *)(p+5),       32764);
    ASSERT_EQ(*(uint16_t *)(p+5+l1),    32764);
    ASSERT_EQ(*(uint16_t *)(p+5+l1+l2), 7);

    // check first session id
    ASSERT_EQ(*(uint16_t *)(p+7),       0);
    ASSERT_EQ(*(uint16_t *)(p+7+l1),    32764);
    ASSERT_EQ(*(uint16_t *)(p+7+l1+l2), 32764 + 32764);
    // last session id
    ASSERT_EQ(*(uint16_t *)(p+l1+l2+l3-2), 65535 - 1);
  }
}

TEST(StratumSession, SetDiff) {
  using namespace boost::algorithm;

  {
    string password = "d=1024";
    uint64_t d = 0u, md = 0u;

    vector<string> arr;  // key=value,key=value
    split(arr, password, is_any_of(","));

    for (auto it = arr.begin(); it != arr.end(); it++) {
      vector<string> arr2;  // key,value
      split(arr2, *it, is_any_of("="));
      if (arr2.size() != 2 || arr2[1].empty()) {
        continue;
      }

      if (arr2[0] == "d") {
        // 'd' : start difficulty
        d = strtoull(arr2[1].c_str(), nullptr, 10);
      }
      else if (arr2[0] == "md") {
        // 'md' : minimum difficulty
        md = strtoull(arr2[1].c_str(), nullptr, 10);
      }
    }

    ASSERT_EQ(d, 1024u);
    ASSERT_EQ(md, 0u);
  }

  {
    string password = "md=2048";
    uint64_t d = 0u, md = 0u;

    vector<string> arr;  // key=value,key=value
    split(arr, password, is_any_of(","));

    for (auto it = arr.begin(); it != arr.end(); it++) {
      vector<string> arr2;  // key,value
      split(arr2, *it, is_any_of("="));
      if (arr2.size() != 2 || arr2[1].empty()) {
        continue;
      }

      if (arr2[0] == "d") {
        // 'd' : start difficulty
        d = strtoull(arr2[1].c_str(), nullptr, 10);
      }
      else if (arr2[0] == "md") {
        // 'md' : minimum difficulty
        md = strtoull(arr2[1].c_str(), nullptr, 10);
      }
    }

    ASSERT_EQ(d, 0u);
    ASSERT_EQ(md, 2048u);
  }

  {
    string password = "d=1024,md=2048";
    uint64_t d = 0u, md = 0u;

    vector<string> arr;  // key=value,key=value
    split(arr, password, is_any_of(","));

    for (auto it = arr.begin(); it != arr.end(); it++) {
      vector<string> arr2;  // key,value
      split(arr2, *it, is_any_of("="));
      if (arr2.size() != 2 || arr2[1].empty()) {
        continue;
      }

      if (arr2[0] == "d") {
        // 'd' : start difficulty
        d = strtoull(arr2[1].c_str(), nullptr, 10);
      }
      else if (arr2[0] == "md") {
        // 'md' : minimum difficulty
        md = strtoull(arr2[1].c_str(), nullptr, 10);
      }
    }

    ASSERT_EQ(d,  1024u);
    ASSERT_EQ(md, 2048u);
  }


  {
    string password = "d=1025,md=2500";
    uint64_t d = 0u, md = 0u;

    vector<string> arr;  // key=value,key=value
    split(arr, password, is_any_of(","));

    for (auto it = arr.begin(); it != arr.end(); it++) {
      vector<string> arr2;  // key,value
      split(arr2, *it, is_any_of("="));
      if (arr2.size() != 2 || arr2[1].empty()) {
        continue;
      }

      if (arr2[0] == "d") {
        // 'd' : start difficulty
        d = strtoull(arr2[1].c_str(), nullptr, 10);
      }
      else if (arr2[0] == "md") {
        // 'md' : minimum difficulty
        md = strtoull(arr2[1].c_str(), nullptr, 10);
      }
    }

    // set min diff first
    if (md >= DiffController::kMinDiff_) {
      // diff must be 2^N
      double i = 1;
      while ((uint64_t)exp2(i) < md) {
        i++;
      }
      md = (uint64_t)exp2(i);

      ASSERT_EQ(md, 4096u);
    }

    // than set current diff
    if (d >= DiffController::kMinDiff_) {
      // diff must be 2^N
      double i = 1;
      while ((uint64_t)exp2(i) < d) {
        i++;
      }
      d = (uint64_t)exp2(i);
      
      ASSERT_EQ(d,  2048u);
    }
  }
}

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

#include "StratumServer.h"
#include "bitcoin/BitcoinUtils.h"
#include "bitcoin/StratumBitcoin.h"
#include "bitcoin/StratumServerBitcoin.h"

// #include "Kafka.h"

#ifndef WORK_WITH_STRATUM_SWITCHER

TEST(StratumServer, SessionIDManager24Bits) {
  // KafkaProducer a("", "", 0);
  // a.produce("", 0);


  SessionIDManagerT<24> m(0xFFu);
  uint32_t j, sessionID;

  // fill all session ids
  for (uint32_t i = 0; i <= 0x00FFFFFFu; i++) {
    uint32_t id = (0xFFu << 24) | i;
    ASSERT_EQ(m.allocSessionId(&sessionID), true);
    ASSERT_EQ(sessionID, id);
  }
  ASSERT_EQ(m.ifFull(), true);

  // free the fisrt one
  j = 0xFF000000u;
  m.freeSessionId(j);
  ASSERT_EQ(m.ifFull(), false);
  ASSERT_EQ(m.allocSessionId(&sessionID), true);
  ASSERT_EQ(sessionID, j);
  ASSERT_EQ(m.ifFull(), true);

  // free the last one
  j = 0xFFFFFFFFu;
  m.freeSessionId(j);
  ASSERT_EQ(m.ifFull(), false);
  ASSERT_EQ(m.allocSessionId(&sessionID), true);
  ASSERT_EQ(sessionID, j);
  ASSERT_EQ(m.ifFull(), true);
}

TEST(StratumServer, SessionIDManager16Bits) {
  SessionIDManagerT<16> m(0x99u);
  uint32_t j, sessionID;

  // fill all session ids
  for (uint32_t i = 0; i <= 0x0000FFFFu; i++) {
    uint32_t id = (0x99u << 16) | i;
    ASSERT_EQ(m.allocSessionId(&sessionID), true);
    ASSERT_EQ(sessionID, id);
  }
  ASSERT_EQ(m.ifFull(), true);

  // free the fisrt one
  j = 0x00990000u;
  m.freeSessionId(j);
  ASSERT_EQ(m.ifFull(), false);
  ASSERT_EQ(m.allocSessionId(&sessionID), true);
  ASSERT_EQ(sessionID, j);
  ASSERT_EQ(m.ifFull(), true);

  // free the last one
  j = 0x0099FFFFu;
  m.freeSessionId(j);
  ASSERT_EQ(m.ifFull(), false);
  ASSERT_EQ(m.allocSessionId(&sessionID), true);
  ASSERT_EQ(sessionID, j);
  ASSERT_EQ(m.ifFull(), true);
}

TEST(StratumServer, SessionIDManager16BitsWithInterval) {
  SessionIDManagerT<16> m(0x99u);
  m.setAllocInterval(256);

  uint32_t j, sessionID;

  // fill all session ids
  // Use std::vector<bool> because the compile time of 
  // std::bitset<0x100000000ull> is too long.
  {
    std::vector<bool> ids(0x100000000ull);
    for (uint32_t i = 0; i <= 0x0000FFFFu; i++) {
      ASSERT_EQ(m.allocSessionId(&sessionID), true);
      ASSERT_EQ(ids[sessionID], false);
      ids[sessionID] = true;
    }
    ASSERT_EQ(m.ifFull(), true);
  }

  // free the fisrt one
  {
    j = 0x00990000u;
    m.freeSessionId(j);
    ASSERT_EQ(m.ifFull(), false);
    ASSERT_EQ(m.allocSessionId(&sessionID), true);
    ASSERT_EQ(sessionID, j);
    ASSERT_EQ(m.ifFull(), true);
  }

  // free the last one
  {
    j = 0x0099FFFFu;
    m.freeSessionId(j);
    ASSERT_EQ(m.ifFull(), false);
    ASSERT_EQ(m.allocSessionId(&sessionID), true);
    ASSERT_EQ(sessionID, j);
    ASSERT_EQ(m.ifFull(), true);
  }

  // free all
  for (uint32_t i = 0x00990000u; i <= 0x0099FFFFu; i++) {
    m.freeSessionId(i);
  }

  // fill all again
  {
    std::vector<bool> ids(0x100000000ull);
    for (uint32_t i = 0; i <= 0x0000FFFFu; i++) {
      ASSERT_EQ(m.allocSessionId(&sessionID), true);
      ASSERT_EQ(ids[sessionID], false);
      ids[sessionID] = true;
    }
    ASSERT_EQ(m.ifFull(), true);
  }
}

TEST(StratumServer, SessionIDManager8Bits) {
  SessionIDManagerT<8> m(0x68u);
  uint32_t j, sessionID;

  // fill all session ids
  for (uint32_t i = 0; i <= 0x000000FFu; i++) {
    uint32_t id = (0x68u << 8) | i;
    ASSERT_EQ(m.allocSessionId(&sessionID), true);
    ASSERT_EQ(sessionID, id);
  }
  ASSERT_EQ(m.ifFull(), true);

  // free the fisrt one
  j = 0x00006800u;
  m.freeSessionId(j);
  ASSERT_EQ(m.ifFull(), false);
  ASSERT_EQ(m.allocSessionId(&sessionID), true);
  ASSERT_EQ(sessionID, j);
  ASSERT_EQ(m.ifFull(), true);

  // free the last one
  j = 0x000068FFu;
  m.freeSessionId(j);
  ASSERT_EQ(m.ifFull(), false);
  ASSERT_EQ(m.allocSessionId(&sessionID), true);
  ASSERT_EQ(sessionID, j);
  ASSERT_EQ(m.ifFull(), true);
}

#endif // #ifndef WORK_WITH_STRATUM_SWITCHER

TEST(StratumServerBitcoin, CheckShare) {
  string sjobJson = "{\"jobId\":6645522065066147329,\"gbtHash\":\"d349be274f007c2e1ee773b33bd21ef43d2615c089b7c5460b66584881a10683\","
  "\"prevHash\":\"00000000000000000019d1d9c84df0ecc23e549b86644ad47cb92570a26b12a5\",\"prevHashBeStr\":\"a26b12a57cb9257086644ad4c23e"
  "549bc84df0ec0019d1d90000000000000000\",\"height\":558201,\"coinbase1\":\"020000000100000000000000000000000000000000000000000000000"
  "00000000000000000ffffffff4b03798408041ba3395c612f4254432e434f4d2ffabe6d6dc807e51bd76025d65ccad2ba8ba1e9fba5f09118b6b55a348638cc17b"
  "14e3909080000005fb54ad0\",\"coinbase2\":\"ffffffff036734ec4a0000000016001497cfc76442fe717f2a3f0cc9c175f7561b6619970000000000000000"
  "266a24aa21a9ed40cbdaa98da815640f815b938df95bffe0775d8078771bc47ed4f43ac4e30b0600000000000000002952534b424c4f434b3a9ad45fdcc194d788"
  "895f3ad389b583ea327f826353f7edf6b168db038372cb2700000000\",\"merkleBranch\":\"53146311555e15816f4549a893ff2eb50e60741ecccb2996bafd"
  "dcf4ee008d5ac504967e375b2522af2be8411b1b032dda0e700c2e8913d869533256ff30caccea4ba404b68e625cfd3237e07e8deddb342690b08314d2638b5272"
  "b74ab12fa3b3812908cd6bef999dea979875ba2730615be08b480e4b6f7b878000510a778c557f44bc3f21813d138d25530df85a89a38e2d2827f758ebc68a62e8"
  "225933a5af086e72d9a65fd9be526648e8bcf74271308d9d273425b47bd12db075e841ba703f4c8a20be62d036958278b16f214d7fcd35c46a9f9fb1910618fa9e"
  "029d3f96518aae34efbdabfbfbc055bffe891d93edbc7539ae9c0a22a35e87d5ccb033b89976cbb624af024b53c6a02309cb838eb285ecf675b801f1dd7f2d5c92"
  "4cb1491731c28bea800b12b94bb4f70502a40559c8edb5f73b906ba8e814f10e852ef87365a49346c4b7361b75e38f1d9b96f028880227b7186a0b114e170b170b"
  "47\",\"nVersion\":536870912,\"nBits\":389159077,\"nTime\":1547281171,\"minTime\":1547277926,\"coinbaseValue\":1256993895,\"witness"
  "Commitment\":\"6a24aa21a9ed40cbdaa98da815640f815b938df95bffe0775d8078771bc47ed4f43ac4e30b06\",\"nmcBlockHash\":\"c807e51bd76025d65"
  "ccad2ba8ba1e9fba5f09118b6b55a348638cc17b14e3909\",\"nmcBits\":402868319,\"nmcHeight\":433937,\"nmcRpcAddr\":\"http://127.0.0.1:899"
  "9\",\"nmcRpcUserpass\":\"user:pass\",\"rskBlockHashForMergedMining\":\"0x9ad45fdcc194d788895f3ad389b583ea327f826353f7edf6b168db038"
  "372cb27\",\"rskNetworkTarget\":\"0x00000000000000001386e3444eba74f8a750a71a75ed0b7fecdfd282a8cef091\",\"rskFeesForMiner\":\"0\",\""
  "rskdRpcAddress\":\"http://127.0.0.1:4444\",\"rskdRpcUserPwd\":\"user:pass\",\"isRskCleanJob\":true}";
  
  StratumJobBitcoin *sjob = new StratumJobBitcoin();
  sjob->unserializeFromJson(sjobJson.c_str(), sjobJson.size());

  StratumJobExBitcoin exjob(0, sjob, true);
  
  CBlockHeader header;
  std::vector<char> coinbaseBin;

  exjob.generateBlockHeader(
    &header, &coinbaseBin,
    0xfe0000c3u, "260103fe60004690",
    sjob->merkleBranch_, sjob->prevHash_,
    sjob->nBits_, sjob->nVersion_,
    0x5c39a313u, 0x07ba7929u,
    0x00013f00u
  );
  
  uint256 blkHash = uint256S("1028e53e8145994a9ebe4f39eb6a7e3fd4036f2f21a05a5a696e8ac6d0829ef4");
  ASSERT_EQ(blkHash, header.GetHash());
}

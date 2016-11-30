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
#include "Stratum.h"

#include "bitcoin/chainparams.h"
#include "bitcoin/hash.h"
#include "bitcoin/script/script.h"
#include "bitcoin/uint256.h"
#include "bitcoin/util.h"

#include <stdint.h>

TEST(Stratum, jobId2Time) {
  uint64_t jobId;

  // jobId:   timestamp    +    gbtHash
  //          ----------      ----------
  //           uint32_t        uint32_t
  //  const string jobIdStr = Strings::Format("%08x%s", (uint32_t)time(nullptr),
  //                                          gbtHash.ToString().substr(0, 8).c_str());
  jobId = (1469002809ull << 32) | 0x00000000FFFFFFFFull;
  ASSERT_EQ(jobId2Time(jobId), 1469002809u);

  jobId = (1469002809ull << 32) | 0x0000000000000000ull;
  ASSERT_EQ(jobId2Time(jobId), 1469002809u);
}

TEST(Stratum, Share) {
  Share s;

  ASSERT_EQ(s.isValid(), false);
  ASSERT_EQ(s.score(), 0);
  ASSERT_EQ(s.toString(), "share(jobId: 0, ip: 0.0.0.0, userId: 0, workerId: 0,"
            " timeStamp: 0/1970-01-01 00:00:00, share: 0, blkBits: 00000000, result: 0)");

  s.ip_ = htonl(167772161);  // 167772161 : 10.0.0.1
  ASSERT_EQ(s.toString(), "share(jobId: 0, ip: 10.0.0.1, userId: 0, workerId: 0,"
            " timeStamp: 0/1970-01-01 00:00:00, share: 0, blkBits: 00000000, result: 0)");
}

TEST(Stratum, Share2) {
  Share s;

  s.blkBits_ = 0x1d00ffffu;
  s.share_ = 1ll;
  ASSERT_EQ(s.score(), 1ll);

  s.blkBits_ = 0x18050edcu;
  s.share_ = UINT32_MAX;
  // double will be: 0.0197583
  ASSERT_EQ(score2Str(s.score()), "0.0197582875516673");
}

TEST(Stratum, StratumWorker) {
  StratumWorker w;
  uint64_t u;
  int64_t workerId;

  ASSERT_EQ(w.getUserName("abcd"), "abcd");
  ASSERT_EQ(w.getUserName("abcdabcdabcdabcdabcdabcdabcd"), "abcdabcdabcdabcdabcdabcdabcd");
  ASSERT_EQ(w.getUserName("abcd."), "abcd");
  ASSERT_EQ(w.getUserName("abcd.123"), "abcd");
  ASSERT_EQ(w.getUserName("abcd.123.456"), "abcd");

  //
  // echo -n '123' |openssl dgst -sha256 -binary |openssl dgst -sha256
  //
  w.setUserIDAndNames(INT32_MAX, "abcd.123");
  ASSERT_EQ(w.fullName_,   "abcd.123");
  ASSERT_EQ(w.userId_,     INT32_MAX);
  ASSERT_EQ(w.userName_,   "abcd");
  ASSERT_EQ(w.workerName_, "123");
  // '123' dsha256 : 5a77d1e9612d350b3734f6282259b7ff0a3f87d62cfef5f35e91a5604c0490a3
  //       uint256 : a390044c60a5915ef3f5fe2cd6873f0affb7592228f634370b352d61e9d1775a
  u = strtoull("a390044c60a5915e", nullptr, 16);
  memcpy((uint8_t *)&workerId, (uint8_t *)&u, 8);
  ASSERT_EQ(w.workerHashId_, workerId);


  w.setUserIDAndNames(0, "abcdefg");
  ASSERT_EQ(w.fullName_,   "abcdefg.__default__");
  ASSERT_EQ(w.userId_,     0);
  ASSERT_EQ(w.userName_,   "abcdefg");
  ASSERT_EQ(w.workerName_, "__default__");
  // '__default__' dsha256 : e00f302bc411fde77d954283be6904911742f2ac76c8e79abef5dff4e6a19770
  //               uint256 : 7097a1e6f4dff5be
  u = strtoull("7097a1e6f4dff5be", nullptr, 16);
  memcpy((uint8_t *)&workerId, (uint8_t *)&u, 8);
  ASSERT_EQ(w.workerHashId_, workerId);

  // check allow chars
  w.setUserIDAndNames(0, "abcdefg.azAZ09-._:|^/");
  ASSERT_EQ(w.workerName_, "azAZ09-._:|^/");
  ASSERT_EQ(w.fullName_,   "abcdefg.azAZ09-._:|^/");

  // some of them are bad chars
  w.setUserIDAndNames(0, "abcdefg.~!@#$%^&*()+={}|[]\\<>?,./");
  ASSERT_EQ(w.workerName_, "^|./");
  ASSERT_EQ(w.fullName_,   "abcdefg.^|./");

  // all bad chars
  w.setUserIDAndNames(0, "abcdefg.~!@#$%&*()+={}[]\\<>?,");
  ASSERT_EQ(w.workerName_, "__default__");
  ASSERT_EQ(w.fullName_,   "abcdefg.__default__");
}

TEST(Stratum, StratumJob) {
  StratumJob sjob;
  string poolCoinbaseInfo = "/BTC.COM/";
  uint32_t blockVersion = 0;
  bool res;

  SelectParams(CBaseChainParams::MAIN);
  CBitcoinAddress poolPayoutAddrMainnet("1A1zP1eP5QGefi2DMPTfTL5SLmv7DivfNa");
  ASSERT_EQ(poolPayoutAddrMainnet.IsValid(), true);

  {
    string gbt;
    gbt += "{\"result\":{";
    gbt += "  \"capabilities\": [";
    gbt += "    \"proposal\"";
    gbt += "  ],";
    gbt += "  \"version\": 536870912,";
    gbt += "  \"previousblockhash\": \"000000004f2ea239532b2e77bb46c03b86643caac3fe92959a31fd2d03979c34\",";
    gbt += "  \"transactions\": [";
    gbt += "    {";
    gbt += "      \"data\": \"01000000010291939c5ae8191c2e7d4ce8eba7d6616a66482e3200037cb8b8c2d0af45b445000000006a47304402204df709d9e149804e358de4b082e41d8bb21b3c9d347241b728b1362aafcb153602200d06d9b6f2eca899f43dcd62ec2efb2d9ce2e10adf02738bb908420d7db93ede012103cae98ab925e20dd6ae1f76e767e9e99bc47b3844095c68600af9c775104fb36cffffffff0290f1770b000000001976a91400dc5fd62f6ee48eb8ecda749eaec6824a780fdd88aca08601000000000017a914eb65573e5dd52d3d950396ccbe1a47daf8f400338700000000\",";
    gbt += "      \"hash\": \"bd36bd4fff574b573152e7d4f64adf2bb1c9ab0080a12f8544c351f65aca79ff\",";
    gbt += "      \"depends\": [";
    gbt += "      ],";
    gbt += "      \"fee\": 10000,";
    gbt += "      \"sigops\": 1";
    gbt += "    }";
    gbt += "  ],";
    gbt += "  \"coinbaseaux\": {";
    gbt += "    \"flags\": \"\"";
    gbt += "  },";
    gbt += "  \"coinbasevalue\": 312659655,";
    gbt += "  \"longpollid\": \"000000004f2ea239532b2e77bb46c03b86643caac3fe92959a31fd2d03979c341911\",";
    gbt += "  \"target\": \"000000000000018ae20000000000000000000000000000000000000000000000\",";
    gbt += "  \"mintime\": 1469001544,";
    gbt += "  \"mutable\": [";
    gbt += "    \"time\",";
    gbt += "    \"transactions\",";
    gbt += "    \"prevblock\"";
    gbt += "  ],";
    gbt += "  \"noncerange\": \"00000000ffffffff\",";
    gbt += "  \"sigoplimit\": 20000,";
    gbt += "  \"sizelimit\": 1000000,";
    gbt += "  \"curtime\": 1469006933,";
    gbt += "  \"bits\": \"1a018ae2\",";
    gbt += "  \"height\": 898487";
    gbt += "}}";

    blockVersion = 0;
    SelectParams(CBaseChainParams::TESTNET);
    CBitcoinAddress poolPayoutAddrTestnet("myxopLJB19oFtNBdrAxD5Z34Aw6P8o9P8U");
    ASSERT_EQ(poolPayoutAddrTestnet.IsValid(), true);
    res = sjob.initFromGbt(gbt.c_str(), poolCoinbaseInfo, poolPayoutAddrTestnet, blockVersion, "");
    ASSERT_EQ(res, true);

    const string jsonStr = sjob.serializeToJson();
    StratumJob sjob2;
    res = sjob2.unserializeFromJson(jsonStr.c_str(), jsonStr.length());
    ASSERT_EQ(res, true);

    ASSERT_EQ(sjob2.prevHash_, uint256S("000000004f2ea239532b2e77bb46c03b86643caac3fe92959a31fd2d03979c34"));
    ASSERT_EQ(sjob2.prevHashBeStr_, "03979c349a31fd2dc3fe929586643caabb46c03b532b2e774f2ea23900000000");
    ASSERT_EQ(sjob2.height_, 898487);
    // 46 bytes, 5 bytes (timestamp), 9 bytes (poolCoinbaseInfo)
    // 01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff1e03b7b50d 0402363d58 2f4254432e434f4d2f
    ASSERT_EQ(sjob2.coinbase1_.substr(0, 92),
              "01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff1e03b7b50d");
    ASSERT_EQ(sjob2.coinbase1_.substr(102, 18), "2f4254432e434f4d2f");
    // 0402363d58 -> 0x583d3602 = 1480406530 = 2016-11-29 16:02:10
    uint32_t ts = (uint32_t)strtoull(sjob2.coinbase1_.substr(94, 8).c_str(), nullptr, 16);
    ts = HToBe(ts);
    ASSERT_EQ(ts == time(nullptr) || ts + 1 == time(nullptr), true);

    ASSERT_EQ(sjob2.coinbase2_,
              "ffffffff01c7cea212000000001976a914ca560088c0fb5e6f028faa11085e643e343a8f5c88ac00000000");
    ASSERT_EQ(sjob2.merkleBranch_.size(), 1);
    ASSERT_EQ(sjob2.merkleBranch_[0], uint256S("bd36bd4fff574b573152e7d4f64adf2bb1c9ab0080a12f8544c351f65aca79ff"));
    ASSERT_EQ(sjob2.nVersion_, 536870912);
    ASSERT_EQ(sjob2.nBits_,    436308706);
    ASSERT_EQ(sjob2.nTime_,    1469006933);
    ASSERT_EQ(sjob2.minTime_,  1469001544);
    ASSERT_EQ(sjob2.coinbaseValue_, 312659655);
    ASSERT_GE(time(nullptr), jobId2Time(sjob2.jobId_));
  }
}

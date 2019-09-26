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

#include "grin/CommonGrin.h"

#include "cuckoo/siphash.h"

#include "gtest/gtest.h"

TEST(CommonGrin, VerifyPowGrinSecondary_V1_19_1) {
  siphash_keys hash{
      0x23796193872092ea,
      0xf1017d8a68c4b745,
      0xd312bd53d2cd307b,
      0x840acce5833ddc52,
  };
  std::vector<uint64_t> solution{
      0x45e9,  0x6a59,  0xf1ad,  0x10ef7, 0x129e8, 0x13e58, 0x17936,
      0x19f7f, 0x208df, 0x23704, 0x24564, 0x27e64, 0x2b828, 0x2bb41,
      0x2ffc0, 0x304c5, 0x31f2a, 0x347de, 0x39686, 0x3ab6c, 0x429ad,
      0x45254, 0x49200, 0x4f8f8, 0x5697f, 0x57ad1, 0x5dd47, 0x607f8,
      0x66199, 0x686c7, 0x6d5f3, 0x6da7a, 0x6dbdf, 0x6f6bf, 0x6ffbb,
      0x7580e, 0x78594, 0x785ac, 0x78b1d, 0x7b80d, 0x7c11c, 0x7da35,
  };
  ASSERT_TRUE(VerifyPowGrinSecondary(solution, hash, 19, 1));
}

TEST(CommonGrin, VerifyPowGrinSecondary_V1_19_2) {
  siphash_keys hash{
      0x6a54f2a35ab7e976,
      0x68818717ff5cd30e,
      0x9c14260c1bdbaf7,
      0xea5b4cd5d0de3cf0,
  };
  std::vector<uint64_t> solution{
      0x2b1e,  0x67d3,  0xb041,  0xb289,  0xc6c3,  0xd31e,  0xd75c,
      0x111d7, 0x145aa, 0x1712e, 0x1a3af, 0x1ecc5, 0x206b1, 0x2a55c,
      0x2a9cd, 0x2b67e, 0x321d8, 0x35dde, 0x3721e, 0x37ac0, 0x39edb,
      0x3b80b, 0x3fc79, 0x4148b, 0x42a48, 0x44395, 0x4bbc9, 0x4f775,
      0x515c5, 0x56f97, 0x5aa10, 0x5bc1b, 0x5c56d, 0x5d552, 0x60a2e,
      0x66646, 0x6c3aa, 0x70709, 0x71d13, 0x762a3, 0x79d88, 0x7e3ae,
  };
  ASSERT_TRUE(VerifyPowGrinSecondary(solution, hash, 19, 1));
}

TEST(CommonGrin, VerifyPowGrinSecondary_V2_19) {
  siphash_keys hash{
      0x89f81d7da5e674df,
      0x7586b93105a5fd13,
      0x6fbe212dd4e8c001,
      0x8800c93a8431f938,
  };
  std::vector<uint64_t> solution{
      0xa00,   0x3ffb,  0xa474,  0xdc27,  0x182e6, 0x242cc, 0x24de4,
      0x270a2, 0x28356, 0x2951f, 0x2a6ae, 0x2c889, 0x355c7, 0x3863b,
      0x3bd7e, 0x3cdbc, 0x3ff95, 0x430b6, 0x4ba1a, 0x4bd7e, 0x4c59f,
      0x4f76d, 0x52064, 0x5378c, 0x540a3, 0x5af6b, 0x5b041, 0x5e9d3,
      0x64ec7, 0x6564b, 0x66763, 0x66899, 0x66e80, 0x68e4e, 0x69133,
      0x6b20a, 0x6c2d7, 0x6fd3b, 0x79a8a, 0x79e29, 0x7ae52, 0x7defe,
  };
  ASSERT_TRUE(VerifyPowGrinSecondary(solution, hash, 19, 2));
}

TEST(CommonGrin, VerifyPowGrinSecondary_V2_29) {
  siphash_keys hash{
      0xe2f917b2d79492ed,
      0xf51088eaaa3a07a0,
      0xaf4d4288d36a4fa8,
      0xc8cdfd30a54e0581,
  };
  std::vector<uint64_t> solution{
      0x1a9629,   0x1fb257,   0x5dc22a,   0xf3d0b0,   0x200c474,  0x24bd68f,
      0x48ad104,  0x4a17170,  0x4ca9a41,  0x55f983f,  0x6076c91,  0x6256ffc,
      0x63b60a1,  0x7fd5b16,  0x985bff8,  0xaae71f3,  0xb71f7b4,  0xb989679,
      0xc09b7b8,  0xd7601da,  0xd7ab1b6,  0xef1c727,  0xf1e702b,  0xfd6d961,
      0xfdf0007,  0x10248134, 0x114657f6, 0x11f52612, 0x12887251, 0x13596b4b,
      0x15e8d831, 0x16b4c9e5, 0x17097420, 0x1718afca, 0x187fc40c, 0x19359788,
      0x1b41d3f1, 0x1bea25a7, 0x1d28df0f, 0x1ea6c4a0, 0x1f9bf79f, 0x1fa005c6,
  };
  ASSERT_TRUE(VerifyPowGrinSecondary(solution, hash, 29, 2));
}

TEST(CommonGrin, VerifyPowHashGrin_29) {
  std::vector<uint64_t> solution{
      59138309,  59597355,  71107354,  77008533,  82816831,  101859133,
      114619013, 196503649, 197503487, 197912808, 202490423, 226509852,
      230167322, 239849525, 246639153, 251738180, 292340629, 300501053,
      303094033, 318918118, 324936544, 345338209, 349689584, 353773627,
      354679396, 358400504, 358840569, 360363815, 368109602, 373892354,
      376026158, 379565228, 391226984, 417614022, 420708867, 421164323,
      430924867, 477873828, 480102094, 486250060, 511705732, 523756438};
  auto hash = PowHashGrin(29, solution);
  ASSERT_EQ(
      "05c2415b40d14e645ae061cefaf92eb79aac243ca1edee901bb64efa33000000",
      hash.ToString());
}

TEST(CommonGrin, VerifyPowHashGrin_31) {
  std::vector<uint64_t> solution{
      50391540,   64206759,   67052196,   75608771,   122531694,  152587278,
      185369197,  187760855,  316331143,  364849995,  561317930,  569948632,
      627456486,  648735138,  673803259,  937581581,  977712607,  1037705626,
      1175705337, 1222073277, 1225306713, 1236320668, 1273409118, 1277413023,
      1404097459, 1406705053, 1530956750, 1604606670, 1623540012, 1636597121,
      1642599781, 1649510025, 1808133178, 1858184705, 1904067866, 1918078203,
      1961458609, 2010878811, 2074759526, 2074776605, 2116030026, 2125259404};
  auto hash = PowHashGrin(31, solution);
  ASSERT_EQ(
      "dac9bddea69f066e334cfe06754696e692b80ba1fd252955fb2baba57c130000",
      hash.ToString());
}

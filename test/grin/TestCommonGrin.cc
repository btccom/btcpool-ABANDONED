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

TEST(CommonGrin, VerifyPowGrinSecondary_V3_29) {

  siphash_keys hash{
      0xe4b4a751f2eac47d,
      0x3115d47edfb69267,
      0x87de84146d9d609e,
      0x7deb20eab6d976a1,
  };
  std::vector<uint64_t> solution{
      0x04acd28,  0x29ccf71,  0x2a5572b,  0x2f31c2c,  0x2f60c37,  0x317fe1d,
      0x32f6d4c,  0x3f51227,  0x45ee1dc,  0x535eeb8,  0x5e135d5,  0x6184e3d,
      0x6b1b8e0,  0x6f857a9,  0x8916a0f,  0x9beb5f8,  0xa3c8dc9,  0xa886d94,
      0xaab6a57,  0xd6df8f8,  0xe4d630f,  0xe6ae422,  0xea2d658,  0xf7f369b,
      0x10c465d8, 0x1130471e, 0x12049efb, 0x12f43bc5, 0x15b493a6, 0x16899354,
      0x1915dfca, 0x195c3dac, 0x19b09ab6, 0x1a1a8ed7, 0x1bba748f, 0x1bdbf777,
      0x1c806542, 0x1d201b53, 0x1d9e6af7, 0x1e99885e, 0x1f255834, 0x1f9c383b,
  };
  ASSERT_TRUE(VerifyPowGrinSecondary(solution, hash, 29, 3));
}

TEST(CommonGrin, VerifyPowGrinSecondary_V3_19) {
  siphash_keys hash{
      0xdb7896f799c76dab,
      0x352e8bf25df7a723,
      0xf0aa29cbb1150ea6,
      0x3206c2759f41cbd5,
  };
  std::vector<uint64_t> solution{
      0x0413c, 0x05121, 0x0546e, 0x1293a, 0x1dd27, 0x1e13e, 0x1e1d2,
      0x22870, 0x24642, 0x24833, 0x29190, 0x2a732, 0x2ccf6, 0x302cf,
      0x32d9a, 0x33700, 0x33a20, 0x351d9, 0x3554b, 0x35a70, 0x376c1,
      0x398c6, 0x3f404, 0x3ff0c, 0x48b26, 0x49a03, 0x4c555, 0x4dcda,
      0x4dfcd, 0x4fbb6, 0x50275, 0x584a8, 0x5da0d, 0x5dbf1, 0x6038f,
      0x66540, 0x72bbd, 0x77323, 0x77424, 0x77a14, 0x77dc9, 0x7d9dc,
  };
  ASSERT_TRUE(VerifyPowGrinSecondary(solution, hash, 19, 3));
}

// https://github.com/tromp/grin/blob/38244e504020924b1ef60ec2808f0571d98ef03a/core/src/pow/cuckarooz.rs#L154
TEST(CommonGrin, VerifyPowGrinSecondary_V4_29) {

  siphash_keys hash{
      0x34bb4c75c929a2f5,
      0x21df13263aa81235,
      0x37d00939eae4be06,
      0x473251cbf6941553,
  };

  std::vector<uint64_t> solution{
      0x49733a,   0x1d49107,  0x253d2ca,  0x5ad5e59,  0x5b671bd,  0x5dcae1c,
      0x5f9a589,  0x65e9afc,  0x6a59a45,  0x7d9c6d3,  0x7df96e4,  0x8b26174,
      0xa17b430,  0xa1c8c0d,  0xa8a0327,  0xabd7402,  0xacb7c77,  0xb67524f,
      0xc1c15a6,  0xc7e2c26,  0xc7f5d8d,  0xcae478a,  0xdea9229,  0xe1ab49e,
      0xf57c7db,  0xfb4e8c5,  0xff314aa,  0x110ccc12, 0x143e546f, 0x17007af8,
      0x17140ea2, 0x173d7c5d, 0x175cd13f, 0x178b8880, 0x1801edc5, 0x18c8f56b,
      0x18c8fe6d, 0x19f1a31a, 0x1bb028d1, 0x1caaa65a, 0x1cf29bc2, 0x1dbde27d,
  };

  ASSERT_TRUE(VerifyPowGrinSecondary(solution, hash, 29, 4));
}
// https://github.com/tromp/grin/blob/38244e504020924b1ef60ec2808f0571d98ef03a/core/src/pow/cuckarooz.rs#L139
TEST(CommonGrin, VerifyPowGrinSecondary_V4_19) {

  siphash_keys hash{
      0xd129f63fba4d9a85,
      0x457dcb3666c5e09c,
      0x045247a2e2ee75f7,
      0x1a0f2e1bcb9d93ff,
  };

  std::vector<uint64_t> solution{
      0x33b6,  0x487b,  0x88b7,  0x10bf6, 0x15144, 0x17cb7, 0x22621,
      0x2358e, 0x23775, 0x24fb3, 0x26b8a, 0x2876c, 0x2973e, 0x2f4ba,
      0x30a62, 0x3a36b, 0x3ba5d, 0x3be67, 0x3ec56, 0x43141, 0x4b9c5,
      0x4fa06, 0x51a5c, 0x523e5, 0x53d08, 0x57d34, 0x5c2de, 0x60bba,
      0x62509, 0x64d69, 0x6803f, 0x68af4, 0x6bd52, 0x6f041, 0x6f900,
      0x70051, 0x7097d, 0x735e8, 0x742c2, 0x79ae5, 0x7f64d, 0x7fd49,
  };

  ASSERT_TRUE(VerifyPowGrinSecondary(solution, hash, 19, 4));
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

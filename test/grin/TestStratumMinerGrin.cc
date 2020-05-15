#include "gtest/gtest.h"
#include "grin/CommonGrin.h"
#include "grin/StratumGrin.h"

#include <iostream>

TEST(StratumMinerGrin, LocalShareGrin) {
  static_assert(
      std::is_base_of<LocalShareGrin, StratumTraitsGrin::LocalShareType>::value,
      "StratumTraitsGrin::LocalShareType is not derived from LocalShareGrin");

  // LocalShare localShare(nonce, boost::hash_value(proofs), edgeBits);
  StratumTraitsGrin::LocalShareType ls1(
      0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFU, 0xFFFFFFFFU);
  {
    StratumTraitsGrin::LocalShareType ls2(
        0xFFFFFFFFFFFFFFFEULL, 0xFFFFFFFFU, 0xFFFFFFFFU);
    ASSERT_EQ(ls2 < ls1, true);
  }
  {
    StratumTraitsGrin::LocalShareType ls2(
        0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFEU, 0xFFFFFFFFU);
    ASSERT_EQ(ls2 < ls1, true);
  }
  {
    StratumTraitsGrin::LocalShareType ls2(
        0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFU, 0xFFFFFFFEU);
    ASSERT_EQ(ls2 < ls1, true);
  }

  {
    StratumTraitsGrin::LocalShareType ls2(
        0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFU, 0xFFFFFFFFU);
    ASSERT_EQ(ls2 < ls1, false);
    ASSERT_EQ(ls2 < ls2, false);
  }
  {
    StratumTraitsGrin::LocalShareType ls2(0x0ULL, 0x0U, 0x0U);
    ls2 = ls1;
    ASSERT_EQ(ls2 < ls1, false);
    ASSERT_EQ(ls2 < ls2, false);
  }
}

TEST(StratumMinerGrin, LocalJob) {
  StratumTraitsGrin::LocalJobType lj(0, 0, 0);

  {
    StratumTraitsGrin::LocalShareType ls1(
        0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFU, 0xFFFFFFFFU);
    ASSERT_EQ(lj.addLocalShare(ls1), true);
  }
  {
    StratumTraitsGrin::LocalShareType ls1(
        0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFU, 0xFFFFFFFFU);
    ASSERT_EQ(lj.addLocalShare(ls1), false);
  }
  {
    StratumTraitsGrin::LocalShareType ls2(0x0ULL, 0x0U, 0x0U);
    ASSERT_EQ(lj.addLocalShare(ls2), true);
  }
  {
    StratumTraitsGrin::LocalShareType ls2(0x0ULL, 0x0U, 0x0U);
    ASSERT_EQ(lj.addLocalShare(ls2), false);
  }
}

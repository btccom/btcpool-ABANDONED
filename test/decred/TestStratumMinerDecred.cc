#include "gtest/gtest.h"
#include "decred/CommonDecred.h"
#include "decred/StratumDecred.h"

#include <iostream>

TEST(StratumMinerDecred, LocalShareDecred) {
  static_assert(
      std::is_base_of<LocalShareDecred, StratumTraitsDecred::LocalShareType>::
          value,
      "StratumTraitsDecred::LocalShareType is not derived from "
      "LocalShareDecred");

  // LocalShare localShare(nonce, boost::hash_value(proofs), edgeBits);
  StratumTraitsDecred::LocalShareType ls1(
      0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFU, 0xFFFFFFFFU);
  {
    StratumTraitsDecred::LocalShareType ls2(
        0xFFFFFFFFFFFFFFFEULL, 0xFFFFFFFFU, 0xFFFFFFFFU);
    ASSERT_EQ(ls2 < ls1, true);
  }
  {
    StratumTraitsDecred::LocalShareType ls2(
        0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFEU, 0xFFFFFFFFU);
    ASSERT_EQ(ls2 < ls1, true);
  }
  {
    StratumTraitsDecred::LocalShareType ls2(
        0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFU, 0xFFFFFFFEU);
    ASSERT_EQ(ls2 < ls1, true);
  }

  {
    StratumTraitsDecred::LocalShareType ls2(
        0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFU, 0xFFFFFFFFU);
    ASSERT_EQ(ls2 < ls1, false);
    ASSERT_EQ(ls2 < ls2, false);
  }
  {
    StratumTraitsDecred::LocalShareType ls2(0x0ULL, 0x0U, 0x0U);
    ls2 = ls1;
    ASSERT_EQ(ls2 < ls1, false);
    ASSERT_EQ(ls2 < ls2, false);
  }
}

TEST(StratumMinerDecred, LocalJob) {
  StratumTraitsDecred::LocalJobType lj(0, 0, 0, 0);

  {
    StratumTraitsDecred::LocalShareType ls1(
        0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFU, 0xFFFFFFFFU);
    ASSERT_EQ(lj.addLocalShare(ls1), true);
  }
  {
    StratumTraitsDecred::LocalShareType ls1(
        0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFU, 0xFFFFFFFFU);
    ASSERT_EQ(lj.addLocalShare(ls1), false);
  }
  {
    StratumTraitsDecred::LocalShareType ls2(0x0ULL, 0x0U, 0x0U);
    ASSERT_EQ(lj.addLocalShare(ls2), true);
  }
  {
    StratumTraitsDecred::LocalShareType ls2(0x0ULL, 0x0U, 0x0U);
    ASSERT_EQ(lj.addLocalShare(ls2), false);
  }
}

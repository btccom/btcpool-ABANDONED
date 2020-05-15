#include "gtest/gtest.h"
#include "ckb/CommonCkb.h"
#include "ckb/StratumCkb.h"

#include <iostream>

TEST(StratumMinerCkb, LocalShareCkb) {
  static_assert(
      std::is_base_of<LocalShareCkb, StratumTraitsCkb::LocalShareType>::value,
      "StratumTraitsCkb::LocalShareType is not derived from LocalShareCkb");

  // LocalShare localShare(extraNonce2, sessionId, JobId);
  StratumTraitsCkb::LocalShareType ls1(
      0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFU, 0xFFFFFFFFU);
  {
    StratumTraitsCkb::LocalShareType ls2(
        0xFFFFFFFFFFFFFFFEULL, 0xFFFFFFFFU, 0xFFFFFFFFU);
    ASSERT_EQ(ls2 < ls1, true);
  }
  {
    StratumTraitsCkb::LocalShareType ls2(
        0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFEU, 0xFFFFFFFFU);
    ASSERT_EQ(ls2 < ls1, true);
  }
  {
    StratumTraitsCkb::LocalShareType ls2(
        0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFU, 0xFFFFFFFEU);
    ASSERT_EQ(ls2 < ls1, true);
  }
  {
    StratumTraitsCkb::LocalShareType ls2(
        0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFU, 0xFFFFFFFFU);
    ASSERT_EQ(ls2 < ls1, false);
    ASSERT_EQ(ls2 < ls2, false);
  }
  {
    StratumTraitsCkb::LocalShareType ls2(0x0ULL, 0x0U, 0x0U);
    ls2 = ls1;
    ASSERT_EQ(ls2 < ls1, false);
    ASSERT_EQ(ls2 < ls2, false);
  }
}

// LocalJobType<LocalShareCkb>
TEST(StratumMinerCkb, LocalJob) {
  StratumTraitsCkb::LocalJobType lj(0, 0);

  {
    StratumTraitsCkb::LocalShareType ls1(
        0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFU, 0xFFFFFFFFU);
    ASSERT_EQ(lj.addLocalShare(ls1), true);
  }
  {
    StratumTraitsCkb::LocalShareType ls1(
        0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFU, 0xFFFFFFFFU);
    ASSERT_EQ(lj.addLocalShare(ls1), false);
  }
  {
    StratumTraitsCkb::LocalShareType ls2(0x0ULL, 0x0U, 0x0U);
    ASSERT_EQ(lj.addLocalShare(ls2), true);
  }
  {
    StratumTraitsCkb::LocalShareType ls2(0x0ULL, 0x0U, 0x0U);
    ASSERT_EQ(lj.addLocalShare(ls2), false);
  }
}
#include "gtest/gtest.h"
#include "beam/CommonBeam.h"
#include "beam/StratumBeam.h"

#include <iostream>

TEST(StratumMinerBeam, LocalShareBeam) {
  static_assert(
      std::is_base_of<LocalShareBeam, StratumTraitsBeam::LocalShareType>::value,
      "StratumTraitsBeam::LocalShareType is not derived from LocalShareBeam");

  // LocalShare localShare(nonce, outputHash, 0);
  StratumTraitsBeam::LocalShareType ls1(0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFU);
  {
    StratumTraitsBeam::LocalShareType ls2(0xFFFFFFFFFFFFFFFEULL, 0xFFFFFFFFU);
    ASSERT_EQ(ls2 < ls1, true);
  }
  {
    StratumTraitsBeam::LocalShareType ls2(0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFEU);
    ASSERT_EQ(ls2 < ls1, true);
  }
  {
    StratumTraitsBeam::LocalShareType ls2(0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFU);
    ASSERT_EQ(ls2 < ls1, false);
    ASSERT_EQ(ls2 < ls2, false);
  }
  {
    StratumTraitsBeam::LocalShareType ls2(0x0ULL, 0x0U);
    ls2 = ls1;
    ASSERT_EQ(ls2 < ls1, false);
    ASSERT_EQ(ls2 < ls2, false);
  }
}

TEST(StratumMinerBeam, LocalJob) {
  StratumTraitsBeam::LocalJobType lj(0, 0, 0);

  {
    StratumTraitsBeam::LocalShareType ls1(0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFU);
    ASSERT_EQ(lj.addLocalShare(ls1), true);
  }
  {
    StratumTraitsBeam::LocalShareType ls1(0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFU);
    ASSERT_EQ(lj.addLocalShare(ls1), false);
  }
  {
    StratumTraitsBeam::LocalShareType ls2(0x0ULL, 0x0U);
    ASSERT_EQ(lj.addLocalShare(ls2), true);
  }
  {
    StratumTraitsBeam::LocalShareType ls2(0x0ULL, 0x0U);
    ASSERT_EQ(lj.addLocalShare(ls2), false);
  }
}
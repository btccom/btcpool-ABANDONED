#include "gtest/gtest.h"
#include "eth/CommonEth.h"
#include "eth/StratumEth.h"

#include <iostream>

TEST(StratumMinerEth, LocalShareBytom) {
  static_assert(
      std::is_base_of<LocalShareEth, StratumTraitsEth::LocalShareType>::value,
      "StratumTraitsEth::LocalShareType is not derived from LocalShareEth");

  // LocalShare localShare(nonce, 0, 0);
  StratumTraitsEth::LocalShareType ls1(0xFFFFFFFFFFFFFFFFULL);
  {
    StratumTraitsEth::LocalShareType ls2(0xFFFFFFFFFFFFFFFEULL);
    ASSERT_EQ(ls2 < ls1, true);
  }
  {
    StratumTraitsEth::LocalShareType ls2(0xFFFFFFFFFFFFFFFFULL);
    ASSERT_EQ(ls2 < ls1, false);
    ASSERT_EQ(ls2 < ls2, false);
  }
  {
    StratumTraitsEth::LocalShareType ls2(0x0ULL);
    ls2 = ls1;
    ASSERT_EQ(ls2 < ls1, false);
    ASSERT_EQ(ls2 < ls2, false);
  }
}

TEST(StratumMinerEth, LocalJob) {
  StratumTraitsEth::LocalJobType lj(0, 0, "");

  {
    StratumTraitsEth::LocalShareType ls1(0xFFFFFFFFFFFFFFFFULL);
    ASSERT_EQ(lj.addLocalShare(ls1), true);
  }
  {
    StratumTraitsEth::LocalShareType ls1(0xFFFFFFFFFFFFFFFFULL);
    ASSERT_EQ(lj.addLocalShare(ls1), false);
  }
  {
    StratumTraitsEth::LocalShareType ls2(0x0ULL);
    ASSERT_EQ(lj.addLocalShare(ls2), true);
  }
  {
    StratumTraitsEth::LocalShareType ls2(0x0ULL);
    ASSERT_EQ(lj.addLocalShare(ls2), false);
  }
}

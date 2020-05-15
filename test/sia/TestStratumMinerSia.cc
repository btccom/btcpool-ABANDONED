#include "gtest/gtest.h"
#include "sia/CommonSia.h"
#include "sia/StratumSia.h"

#include <iostream>

TEST(StratumMinerSia, LocalShareSia) {
  static_assert(
      std::is_base_of<LocalShareSia, StratumTraitsSia::LocalShareType>::value,
      "StratumTraitsSia::LocalShareType is not derived from LocalShareSia");

  // LocalShare localShare(nonce, 0, 0);
  StratumTraitsSia::LocalShareType ls1(0xFFFFFFFFFFFFFFFFULL);
  {
    StratumTraitsSia::LocalShareType ls2(0xFFFFFFFFFFFFFFFEULL);
    ASSERT_EQ(ls2 < ls1, true);
  }
  {
    StratumTraitsSia::LocalShareType ls2(0xFFFFFFFFFFFFFFFFULL);
    ASSERT_EQ(ls2 < ls1, false);
    ASSERT_EQ(ls2 < ls2, false);
  }
  {
    StratumTraitsSia::LocalShareType ls2(0x0ULL);
    ls2 = ls1;
    ASSERT_EQ(ls2 < ls1, false);
    ASSERT_EQ(ls2 < ls2, false);
  }
}

TEST(StratumMinerSia, LocalJob) {
  StratumTraitsSia::LocalJobType lj(0, 0, 0);

  {
    StratumTraitsSia::LocalShareType ls1(0xFFFFFFFFFFFFFFFFULL);
    ASSERT_EQ(lj.addLocalShare(ls1), true);
  }
  {
    StratumTraitsSia::LocalShareType ls1(0xFFFFFFFFFFFFFFFFULL);
    ASSERT_EQ(lj.addLocalShare(ls1), false);
  }
  {
    StratumTraitsSia::LocalShareType ls2(0x0ULL);
    ASSERT_EQ(lj.addLocalShare(ls2), true);
  }
  {
    StratumTraitsSia::LocalShareType ls2(0x0ULL);
    ASSERT_EQ(lj.addLocalShare(ls2), false);
  }
}

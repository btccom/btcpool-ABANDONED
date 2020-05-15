#include "gtest/gtest.h"
#include "bytom/CommonBytom.h"
#include "bytom/StratumBytom.h"

#include <iostream>

TEST(StratumMinerBytom, LocalShareBytom) {
  static_assert(
      std::is_base_of<LocalShareBytom, StratumTraitsBytom::LocalShareType>::
          value,
      "StratumTraitsBytom::LocalShareType is not derived from LocalShareBytom");

  // LocalShare localShare(nonce, 0, 0);
  StratumTraitsBytom::LocalShareType ls1(0xFFFFFFFFFFFFFFFFULL);
  {
    StratumTraitsBytom::LocalShareType ls2(0xFFFFFFFFFFFFFFFEULL);
    ASSERT_EQ(ls2 < ls1, true);
  }
  {
    StratumTraitsBytom::LocalShareType ls2(0xFFFFFFFFFFFFFFFFULL);
    ASSERT_EQ(ls2 < ls1, false);
    ASSERT_EQ(ls2 < ls2, false);
  }
  {
    StratumTraitsBytom::LocalShareType ls2(0x0ULL);
    ls2 = ls1;
    ASSERT_EQ(ls2 < ls1, false);
    ASSERT_EQ(ls2 < ls2, false);
  }
}

TEST(StratumMinerBytom, LocalJob) {
  StratumTraitsBytom::LocalJobType lj(0, 0, 0);

  {
    StratumTraitsBytom::LocalShareType ls1(0xFFFFFFFFFFFFFFFFULL);
    ASSERT_EQ(lj.addLocalShare(ls1), true);
  }
  {
    StratumTraitsBytom::LocalShareType ls1(0xFFFFFFFFFFFFFFFFULL);
    ASSERT_EQ(lj.addLocalShare(ls1), false);
  }
  {
    StratumTraitsBytom::LocalShareType ls2(0x0ULL);
    ASSERT_EQ(lj.addLocalShare(ls2), true);
  }
  {
    StratumTraitsBytom::LocalShareType ls2(0x0ULL);
    ASSERT_EQ(lj.addLocalShare(ls2), false);
  }
}

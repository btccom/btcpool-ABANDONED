#include "gtest/gtest.h"
#include "bitcoin/CommonBitcoin.h"
#include "bitcoin/StratumBitcoin.h"

#include <iostream>

TEST(StratumMinerBitcoin, LocalShareBitcoin) {
  StratumTraitsBitcoin::LocalShareType ls1(
      0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFU, 0xFFFFFFFFU, 0xFFFFFFFFU);

  {
    StratumTraitsBitcoin::LocalShareType ls2(
        0xFFFFFFFFFFFFFFFEULL, 0xFFFFFFFFU, 0xFFFFFFFFU, 0xFFFFFFFFU);
    ASSERT_EQ(ls2 < ls1, true);
  }
  {
    StratumTraitsBitcoin::LocalShareType ls2(
        0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFEU, 0xFFFFFFFFU, 0xFFFFFFFFU);
    ASSERT_EQ(ls2 < ls1, true);
  }
  {
    StratumTraitsBitcoin::LocalShareType ls2(
        0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFU, 0xFFFFFFFEU, 0xFFFFFFFFU);
    ASSERT_EQ(ls2 < ls1, true);
  }
  {
    StratumTraitsBitcoin::LocalShareType ls2(
        0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFU, 0xFFFFFFFFU, 0xFFFFFFFEU);
    ASSERT_EQ(ls2 < ls1, true);
  }
  {
    StratumTraitsBitcoin::LocalShareType ls2(
        0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFU, 0xFFFFFFFFU, 0xFFFFFFFFU);
    ASSERT_EQ(ls2 < ls1, false);
    ASSERT_EQ(ls2 < ls2, false);
  }
  {
    StratumTraitsBitcoin::LocalShareType ls2(0x0ULL, 0x0U, 0x0U, 0x0u);
    ls2 = ls1;
    ASSERT_EQ(ls2 < ls1, false);
    ASSERT_EQ(ls2 < ls2, false);
  }
}

TEST(StratumMinerBitcoin, LocalJobBitcoin) {
  StratumTraitsBitcoin::LocalJobType lj(0, 0, 0, 0);

  {
    StratumTraitsBitcoin::LocalShareType ls1(
        0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFU, 0xFFFFFFFFU, 0xFFFFFFFFU);
    ASSERT_EQ(lj.addLocalShare(ls1), true);
  }
  {
    StratumTraitsBitcoin::LocalShareType ls1(
        0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFU, 0xFFFFFFFFU, 0xFFFFFFFFU);
    ASSERT_EQ(lj.addLocalShare(ls1), false);
  }
  {
    StratumTraitsBitcoin::LocalShareType ls2(0x0ULL, 0x0U, 0x0U, 0x0u);
    ASSERT_EQ(lj.addLocalShare(ls2), true);
  }
  {
    StratumTraitsBitcoin::LocalShareType ls2(0x0ULL, 0x0U, 0x0U, 0x0u);
    ASSERT_EQ(lj.addLocalShare(ls2), false);
  }
}

TEST(StratumMinerBitcoin, LocalShareBitcoinGrand) {
  StratumTraitsBitcoin::LocalShareType ls1(
      0xFFFFFFFFFFFFFFFFULL,
      0xFFFFFFFFU,
      0xFFFFFFFFU,
      0xFFFFFFFFU,
      0xFFFFFFFFU);

  {
    StratumTraitsBitcoin::LocalShareType ls2(
        0xFFFFFFFFFFFFFFFEULL,
        0xFFFFFFFFU,
        0xFFFFFFFFU,
        0xFFFFFFFFU,
        0xFFFFFFFFU);
    ASSERT_EQ(ls2 < ls1, true);
  }
  {
    StratumTraitsBitcoin::LocalShareType ls2(
        0xFFFFFFFFFFFFFFFFULL,
        0xFFFFFFFEU,
        0xFFFFFFFFU,
        0xFFFFFFFFU,
        0xFFFFFFFFU);
    ASSERT_EQ(ls2 < ls1, true);
  }
  {
    StratumTraitsBitcoin::LocalShareType ls2(
        0xFFFFFFFFFFFFFFFFULL,
        0xFFFFFFFFU,
        0xFFFFFFFEU,
        0xFFFFFFFFU,
        0xFFFFFFFFU);
    ASSERT_EQ(ls2 < ls1, true);
  }
  {
    StratumTraitsBitcoin::LocalShareType ls2(
        0xFFFFFFFFFFFFFFFFULL,
        0xFFFFFFFFU,
        0xFFFFFFFFU,
        0xFFFFFFFEU,
        0xFFFFFFFFU);
    ASSERT_EQ(ls2 < ls1, true);
  }
  {
    StratumTraitsBitcoin::LocalShareType ls2(
        0xFFFFFFFFFFFFFFFFULL,
        0xFFFFFFFFU,
        0xFFFFFFFFU,
        0xFFFFFFFFU,
        0xFFFFFFFEU);
    ASSERT_EQ(ls2 < ls1, true);
  }
  {
    StratumTraitsBitcoin::LocalShareType ls2(
        0xFFFFFFFFFFFFFFFFULL,
        0xFFFFFFFFU,
        0xFFFFFFFFU,
        0xFFFFFFFFU,
        0xFFFFFFFFU);
    ASSERT_EQ(ls2 < ls1, false);
    ASSERT_EQ(ls2 < ls2, false);
  }
  {
    StratumTraitsBitcoin::LocalShareType ls2(0x0ULL, 0x0U, 0x0U, 0x0u, 0x0u);
    ls2 = ls1;
    ASSERT_EQ(ls2 < ls1, false);
    ASSERT_EQ(ls2 < ls2, false);
  }
}

TEST(StratumMinerBitcoin, LocalJobBitcoinGrand) {
  StratumTraitsBitcoin::LocalJobType lj(0, 0, 0, 0);

  {
    StratumTraitsBitcoin::LocalShareType ls1(
        0xFFFFFFFFFFFFFFFFULL,
        0xFFFFFFFFU,
        0xFFFFFFFFU,
        0xFFFFFFFFU,
        0xFFFFFFFFU);
    ASSERT_EQ(lj.addLocalShare(ls1), true);
  }
  {
    StratumTraitsBitcoin::LocalShareType ls1(
        0xFFFFFFFFFFFFFFFFULL,
        0xFFFFFFFFU,
        0xFFFFFFFFU,
        0xFFFFFFFFU,
        0xFFFFFFFFU);
    ASSERT_EQ(lj.addLocalShare(ls1), false);
  }
  {
    StratumTraitsBitcoin::LocalShareType ls2(0x0ULL, 0x0U, 0x0U, 0x0u, 0x0u);
    ASSERT_EQ(lj.addLocalShare(ls2), true);
  }
  {
    StratumTraitsBitcoin::LocalShareType ls2(0x0ULL, 0x0U, 0x0U, 0x0u, 0x0u);
    ASSERT_EQ(lj.addLocalShare(ls2), false);
  }
}

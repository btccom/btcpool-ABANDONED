#include "BytomUtils.h"

uint64_t GetBlockRewardBytom(uint64_t nHeight) {
  //	based on bytom's mining.go (BlockSubsidy function)
  const uint64_t initialBlockSubsidy = 140700041250000000UL;
  const uint64_t baseSubsidy = 41250000000UL;
  const uint64_t subsidyReductionInterval = 840000UL;
  if (nHeight == 0) {
    return initialBlockSubsidy;
  }
  return baseSubsidy >> (nHeight / subsidyReductionInterval);
}

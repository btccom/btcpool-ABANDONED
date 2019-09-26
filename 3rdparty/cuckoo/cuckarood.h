#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

class siphash_keys;

bool verify_cuckarood(const std::vector<uint64_t> &edges, siphash_keys &keys, uint32_t edgeBits);

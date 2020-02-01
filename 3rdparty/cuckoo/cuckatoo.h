#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

class siphash_keys;

bool verify_cuckatoo(const std::vector<uint64_t> &edges, siphash_keys &keys, uint32_t edge_bits);

bool verify_cuckatoo_ae(const std::vector<uint32_t> &edges, siphash_keys &keys, uint32_t edge_bits);

bool find_pow_ae(std::vector<uint32_t> &pow/*[output]*/, siphash_keys &sip_keys/*[input]*/, uint32_t easiness = 1/*[input]*/);
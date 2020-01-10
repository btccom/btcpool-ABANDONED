#include "cuckaroom.h"
#include "siphash.h"

static const uint64_t EDGE_BLOCK_BITS = 6;
static const uint64_t EDGE_BLOCK_SIZE = 1 << EDGE_BLOCK_BITS;
static const uint64_t EDGE_BLOCK_MASK = EDGE_BLOCK_SIZE - 1;

// fills buffer with EDGE_BLOCK_SIZE siphash outputs for block containing edge in cuckaroo graph
// return siphash output for given edge
static uint64_t sip_block(siphash_keys &keys, uint64_t edge, uint64_t *buf) {
  siphash_state<> shs(keys);
  size_t edge0 = edge & ~EDGE_BLOCK_MASK;
  for (uint64_t i = 0; i < EDGE_BLOCK_SIZE; i++) {
    shs.hash24(edge0 + i);
    buf[i] = shs.xor_lanes();
  }
  for (uint64_t i = EDGE_BLOCK_MASK; i; i--)
    buf[i - 1] ^= buf[i];
  return buf[edge & EDGE_BLOCK_MASK];
}

// verify that edges are ascending and form a cycle in header-generated graph
bool verify_cuckaroom(const std::vector<uint64_t> &edges, siphash_keys &keys, uint32_t edge_bits) {
  size_t xor0 = 0, xor1 = 0;
  uint64_t sips[EDGE_BLOCK_SIZE];
  uint64_t proof_size = edges.size();
  std::vector<size_t> from(proof_size), to(proof_size), visited(proof_size);
  uint64_t edge_size = static_cast<uint64_t>(1) << edge_bits;
  uint64_t edge_mask = edge_size  - 1;

  for (uint64_t n = 0; n < proof_size; n++) {
    if (edges[n] > edge_mask)
      return false;
    if (n && edges[n] <= edges[n-1])
      return false;
    uint64_t edge = sip_block(keys, edges[n], sips);
    xor0 ^= from[n] = edge & edge_mask;
    xor1 ^= to[n] = (edge >> 32) & edge_mask;
    visited[n] = false;
  }
  if (xor0 != xor1)              // optional check for obviously bad proofs
    return false;
  uint64_t n = 0, i = 0;
  do {                        // follow cycle
    if (visited[i])
      return false;
    visited[i] = true;
    uint64_t nexti;
    for (nexti = 0; from[nexti] != to[i];) // find outgoing edge meeting incoming edge i
      if (++nexti == proof_size)
        return false;
    i = nexti;
    n++;
  } while (i != 0);           // must cycle back to start or we would have found branch
  return n == proof_size;
}
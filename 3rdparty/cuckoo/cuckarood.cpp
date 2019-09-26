#include "cuckarood.h"
#include "siphash.h"

static const uint64_t EDGE_BLOCK_BITS = 6;
static const uint64_t EDGE_BLOCK_SIZE = 1 << EDGE_BLOCK_BITS;
static const uint64_t EDGE_BLOCK_MASK = EDGE_BLOCK_SIZE - 1;

// fills buffer with EDGE_BLOCK_SIZE siphash outputs for block containing edge in cuckaroo graph
// return siphash output for given edge
static uint64_t sip_block(siphash_keys &keys, uint64_t edge, uint64_t *buf) {
  siphash_state<25> shs(keys);
  uint64_t edge0 = edge & ~EDGE_BLOCK_MASK;
  for (uint64_t i = 0; i < EDGE_BLOCK_SIZE; i++) {
    shs.hash24(edge0 + i);
    buf[i] = shs.xor_lanes();
  }
  uint64_t last = buf[EDGE_BLOCK_MASK];
  for (uint64_t i = 0; i < EDGE_BLOCK_MASK; i++)
    buf[i] ^= last;
  return buf[edge & EDGE_BLOCK_MASK];
}

// verify that edges are ascending and form a cycle in header-generated graph
bool verify_cuckarood(const std::vector<uint64_t> &edges, siphash_keys &keys, uint32_t edge_bits) {
  uint64_t xor0 = 0, xor1 = 0;
  uint64_t sips[EDGE_BLOCK_SIZE];
  uint64_t proof_size = edges.size();
  std::vector<uint64_t> uvs(2 * proof_size);
  uint64_t edge_size = static_cast<uint64_t>(1) << edge_bits;
  uint64_t edge_mask = edge_size / 2 - 1;
  uint64_t ndir[2] = { 0, 0 };

  for (uint64_t n = 0; n < proof_size; n++) {
    uint64_t dir = edges[n] & 1;
    if (ndir[dir] >= proof_size / 2)
      return false;
    if (edges[n] >= edge_size)
      return false;
    if (n && edges[n] <= edges[n-1])
      return false;
    uint64_t edge = sip_block(keys, edges[n], sips);
    xor0 ^= uvs[4 * ndir[dir] + 2 * dir] = edge & edge_mask;
    xor1 ^= uvs[4 * ndir[dir] + 2 * dir + 1] = (edge >> 32) & edge_mask;
    ndir[dir]++;
  }
  if (xor0 | xor1)              // optional check for obviously bad proofs
    return false;
  uint64_t n = 0, i = 0, j;
  do {                        // follow cycle
    for (uint64_t k = ((j = i) % 4) ^ 2; k < 2 * proof_size; k += 4) {
      if (uvs[k] == uvs[i]) { // find other edge endpoint identical to one at i
        if (j != i)           // already found one before
          return false;
        j = k;
      }
    }
    if (j == i) return false;  // no matching endpoint
    i = j^1;
    n++;
  } while (i != 0);           // must cycle back to start or we would have found branch
  return n == proof_size;
}
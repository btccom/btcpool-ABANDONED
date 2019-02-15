#include "block_crypt.h"
#include "uintBig.h"

namespace beam {

const uint32_t Block::PoW::nSolutionBits;
const uint32_t Block::PoW::nSolutionBytes;

template <uint32_t nBytes_>
const uint32_t uintBig_t<nBytes_>::nBits;
template <uint32_t nBytes_>
const uint32_t uintBig_t<nBytes_>::nBytes;

// Class template instantiation
template struct uintBig_t<8u>;

};

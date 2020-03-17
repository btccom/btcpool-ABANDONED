#pragma once
#include "Utils.h"
#include "uint256.h"
#include <iostream>

namespace tellor {

uint256 GetTellorPowHash(
    std::string challenge, std::string publicaddress, std::string nonce);

uint64_t calcCurDiff(uint64_t networkdiff, uint64_t mindifficulty);
} // namespace tellor

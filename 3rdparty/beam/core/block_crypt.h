// Copyright 2018 The Beam Team
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once
#include "ecc_native.h"
#include "difficulty.h"

namespace beam
{
	struct Block
	{
		// Different parts of the block are split into different structs, so that they can be manipulated (transferred, processed, saved and etc.) independently
		// For instance, there is no need to keep PoW (at least in SPV client) once it has been validated.

		struct PoW
		{
			// equihash parameters. 
			// Parameters recommended by BTG are 144/5, to make it asic-resistant (~1GB average, spikes about 1.5GB). On CPU solve time about 1 minutes
			// The following are the parameters for testnet, to make it of similar size, and much faster solve time, to test concurrency and difficulty adjustment
			static const uint32_t N = 150;
			static const uint32_t K = 5;

			static const uint32_t nNumIndices		= 1 << K; // 32
			static const uint32_t nBitsPerIndex		= N / (K + 1) + 1; // 26

			static const uint32_t nSolutionBits		= nNumIndices * nBitsPerIndex; // 832 bits

			static_assert(!(nSolutionBits & 7), "PoW solution should be byte-aligned");
			static const uint32_t nSolutionBytes	= nSolutionBits >> 3; // 104 bytes

			std::array<uint8_t, nSolutionBytes>	m_Indices;

			typedef uintBig_t<8> NonceType;
			NonceType m_Nonce; // 8 bytes. The overall solution size is 96 bytes.
			Difficulty m_Difficulty;

			bool IsValid(const void* pInput, uint32_t nSizeInput, uint32_t) const;
			bool ComputeHash(const void* pInput, uint32_t nSizeInput, ECC::Hash::Value &hv, uint32_t) const;

		private:
			struct Helper;
		};
	};
}

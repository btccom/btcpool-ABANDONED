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

#include "core/block_crypt.h"
#include "crypto/equihashR.h"
#include "crypto/beamHashIII.h"
#include "uint256.h"
#include "arith_uint256.h"
#include <utility>
#include <glog/logging.h>

namespace beam
{

struct Block::PoW::Helper
{
	blake2b_state m_Blake;

	EquihashR<150,5,0> BeamHashI;
	EquihashR<150,5,3> BeamHashII;
	BeamHash_III       BeamHashIII;
	
	PoWScheme* getCurrentPoW(uint32_t hashVersion) {
		if (hashVersion == 1) {
			return &BeamHashI;
		} else if(hashVersion == 2){
			DLOG(INFO) << "====> using beamhash2...";
			return &BeamHashII;
		} else {
			DLOG(INFO) << "====> using beamhash3...";
			return &BeamHashIII;
		}
	}

	void Reset(const void* pInput, uint32_t nSizeInput, const NonceType& nonce, uint32_t hashVersion)
	{
		getCurrentPoW(hashVersion)->InitialiseState(m_Blake);

		// H(I||...
		blake2b_update(&m_Blake, (uint8_t*) pInput, nSizeInput);
		blake2b_update(&m_Blake, nonce.m_pData, nonce.nBytes);
	}

	bool TestDifficulty(const uint8_t* pSol, uint32_t nSol, Difficulty d) const
	{
		ECC::Hash::Value hv;
		ECC::Hash::Processor() << Blob(pSol, nSol) >> hv;

		return d.IsTargetReached(hv);
	}

	void ComputeHash(const uint8_t* pSol, uint32_t nSol, ECC::Hash::Value &hv) const {
		ECC::Hash::Processor() << Blob(pSol, nSol) >> hv;
	}
};

bool Block::PoW::IsValid(const void* pInput, uint32_t nSizeInput, uint32_t hashVersion) const
{
	Helper hlp;
	hlp.Reset(pInput, nSizeInput, m_Nonce, hashVersion);

	std::vector<uint8_t> v(m_Indices.begin(), m_Indices.end());
    return
		hlp.getCurrentPoW(hashVersion)->IsValidSolution(hlp.m_Blake, v) &&
		hlp.TestDifficulty(&m_Indices.front(), (uint32_t) m_Indices.size(), m_Difficulty);
}

bool Block::PoW::ComputeHash(const void* pInput, uint32_t nSizeInput, ECC::Hash::Value &hv, uint32_t hashVersion) const
{
	Helper hlp;
	hlp.Reset(pInput, nSizeInput, m_Nonce, hashVersion);

	std::vector<uint8_t> v(m_Indices.begin(), m_Indices.end());
	
    if (!hlp.getCurrentPoW(hashVersion)->IsValidSolution(hlp.m_Blake, v)) {
		return false;
	}

	hlp.ComputeHash(&m_Indices.front(), (uint32_t) m_Indices.size(), hv);
	return true;
}

} // namespace beam


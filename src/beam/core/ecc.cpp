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

#include "common.h"
#include "ecc_native.h"
#include "secp256k1-zkp/src/hash_impl.h"

#define ENABLE_MODULE_GENERATOR
#define ENABLE_MODULE_RANGEPROOF

#if defined(__clang__) || defined(__GNUC__) || defined(__GNUG__)
#	pragma GCC diagnostic push
#	pragma GCC diagnostic ignored "-Wunused-function"
#else
#	pragma warning (push, 0) // suppress warnings from secp256k1
#	pragma warning (disable: 4706 4701) // assignment within conditional expression
#endif

#if defined(__clang__) || defined(__GNUC__) || defined(__GNUG__)
#	pragma GCC diagnostic pop
#else
#	pragma warning (default: 4706 4701)
#	pragma warning (pop)
#endif

#ifdef WIN32
#	pragma comment (lib, "Bcrypt.lib")
#else // WIN32
#    include <unistd.h>
#    include <fcntl.h>
#endif // WIN32

//#ifdef __linux__
//#	include <sys/syscall.h>
//#	include <linux/random.h>
//#endif // __linux__


namespace ECC {

	//void* NoErase(void*, size_t) { return NULL; }

	// Pointer to the 'eraser' function. The pointer should be non-const (i.e. variable that can be changed at run-time), so that optimizer won't remove this.
	void (*g_pfnEraseFunc)(void*, size_t) = memset0/*NoErase*/;

	void SecureErase(void* p, uint32_t n)
	{
		g_pfnEraseFunc(p, n);
	}

	template <typename T>
	void data_cmov_as(T* pDst, const T* pSrc, int nWords, int flag)
	{
		const T mask0 = flag + ~((T)0);
		const T mask1 = ~mask0;

		for (int n = 0; n < nWords; n++)
			pDst[n] = (pDst[n] & mask0) | (pSrc[n] & mask1);
	}

	template void data_cmov_as<uint32_t>(uint32_t* pDst, const uint32_t* pSrc, int nWords, int flag);

	/////////////////////
	// Hash
	Hash::Processor::Processor()
	{
		Reset();
	}

	Hash::Processor::~Processor()
	{
		if (m_bInitialized)
			SecureErase(*this);
	}

	void Hash::Processor::Reset()
	{
		secp256k1_sha256_initialize(this);
		m_bInitialized = true;
	}

	void Hash::Processor::Write(const void* p, uint32_t n)
	{
		assert(m_bInitialized);
		secp256k1_sha256_write(this, (const uint8_t*) p, n);
	}

	void Hash::Processor::Finalize(Value& v)
	{
		assert(m_bInitialized);
		secp256k1_sha256_finalize(this, v.m_pData);
		
		m_bInitialized = false;
	}

	void Hash::Processor::Write(const beam::Blob& v)
	{
		Write(v.p, v.n);
	}

} // namespace ECC

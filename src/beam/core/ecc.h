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
#include "common.h"
#include "uintBig.h"

namespace ECC
{
	void SecureErase(void*, uint32_t);
	template <typename T> void SecureErase(T& t) { SecureErase(&t, sizeof(T)); }

	static const uint32_t nBytes = 32;
	static const uint32_t nBits = nBytes << 3;
	typedef beam::uintBig_t<nBytes> uintBig;

	struct Hash
	{
		typedef uintBig Value;
		Value m_Value;

		class Processor;
		class Mac;
	};
}


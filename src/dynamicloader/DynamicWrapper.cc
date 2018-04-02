/*
 The MIT License (MIT)

 Copyright (c) [2016] [BTC.COM]

 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE.
 */
#include "DynamicWrapper.h"

#include "BlockMaker.h"
#include "GbtMaker.h"


//--------------- Wrapper Generator ---------------

// constructor
#define DYWRAP_CONSTRUCTOR_IMPL(className) \
  className##Wrapper::className##Wrapper(DYCLASS_##className##_##className##_FPARAMS) { \
    wrappedObj = new className(DYCLASS_##className##_##className##_APARAMS); \
  }

// destructor
#define DYWRAP_DESTRUCTOR_IMPL(className) \
  className##Wrapper::~className##Wrapper() { \
    delete ((className*)wrappedObj); \
  }

// method
#define DYWRAP_METHOD_IMPL(className, methodName) \
  DYCLASS_##className##_##methodName##_RETURN className##Wrapper::methodName(DYCLASS_##className##_##methodName##_FPARAMS) {\
    return ((className*)wrappedObj)->methodName(DYCLASS_##className##_##methodName##_APARAMS);\
  }

// function NewXXXWrapper(...)
#define DYWRAP_NEW_WRAPPER_FUNC_IMPL(className) \
  className##Wrapper* New##className##Wrapper(DYCLASS_##className##_##className##_FPARAMS) {\
    return new className##Wrapper(DYCLASS_##className##_##className##_APARAMS);\
  }

//--------------- End of Wrapper Generator ---------------


//------------- BlockMaker -------------
DYWRAP_CONSTRUCTOR_IMPL(BlockMaker)
DYWRAP_DESTRUCTOR_IMPL(BlockMaker)

DYWRAP_METHOD_IMPL(BlockMaker, addBitcoind)
DYWRAP_METHOD_IMPL(BlockMaker, init)
DYWRAP_METHOD_IMPL(BlockMaker, stop)
DYWRAP_METHOD_IMPL(BlockMaker, run)

DYWRAP_NEW_WRAPPER_FUNC_IMPL(BlockMaker)


//------------- GbtMaker -------------
DYWRAP_CONSTRUCTOR_IMPL(GbtMaker)
DYWRAP_DESTRUCTOR_IMPL(GbtMaker)

DYWRAP_METHOD_IMPL(GbtMaker, init)
DYWRAP_METHOD_IMPL(GbtMaker, stop)
DYWRAP_METHOD_IMPL(GbtMaker, run)

DYWRAP_NEW_WRAPPER_FUNC_IMPL(GbtMaker)


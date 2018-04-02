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
#ifndef DYNAMIC_WRAPPER_H_
#define DYNAMIC_WRAPPER_H_

#include <string>

#include "ClassDefinition.h"
#include "MysqlConnection.h"

using std::string;


//--------------- Wrapper Generator ---------------

#ifdef DYNAMIC_LOAD_LIBBTCPOOL_BITCOIN
  #define DYWRAP_EXPORT  virtual  // dynamic binding
#else
  #define DYWRAP_EXPORT /*empty*/ // static binding
#endif

//class begin
#define DYWRAP_CLASS_DEF_BEGIN(className) \
  class className##Wrapper { \
  protected: \
    /* The original object that wrapped. \
       Use `void *` so the definition of the original object not needed \
       by the wrapper's user when dynamic loading. */ \
    void *wrappedObj; \
  public:

//class end
#define DYWRAP_CLASS_DEF_END() \
  };

// constructor
#define DYWRAP_CONSTRUCTOR_DEF(className) \
  className##Wrapper(DYCLASS_##className##_##className##_FPARAMS);

// destructor
#define DYWRAP_DESTRUCTOR_DEF(className) \
  DYWRAP_EXPORT ~className##Wrapper();

// method
#define DYWRAP_METHOD_DEF(className, methodName) \
  DYWRAP_EXPORT DYCLASS_##className##_##methodName##_RETURN methodName(DYCLASS_##className##_##methodName##_FPARAMS);

// function NewXXXWrapper(...)
#define DYWRAP_NEW_WRAPPER_FUNC_DEF(className) \
  className##Wrapper* New##className##Wrapper(DYCLASS_##className##_##className##_FPARAMS);

//--------------- End of Wrapper Generator ---------------


DYWRAP_CLASS_DEF_BEGIN(BlockMaker)
  DYWRAP_CONSTRUCTOR_DEF(BlockMaker)
  DYWRAP_DESTRUCTOR_DEF(BlockMaker)
  DYWRAP_METHOD_DEF(BlockMaker, addBitcoind)
  DYWRAP_METHOD_DEF(BlockMaker, init)
  DYWRAP_METHOD_DEF(BlockMaker, stop)
  DYWRAP_METHOD_DEF(BlockMaker, run)
DYWRAP_CLASS_DEF_END()


DYWRAP_CLASS_DEF_BEGIN(GbtMaker)
  DYWRAP_CONSTRUCTOR_DEF(GbtMaker)
  DYWRAP_DESTRUCTOR_DEF(GbtMaker)
  DYWRAP_METHOD_DEF(GbtMaker, init)
  DYWRAP_METHOD_DEF(GbtMaker, stop)
  DYWRAP_METHOD_DEF(GbtMaker, run)
DYWRAP_CLASS_DEF_END()


// skip name managing of C++, use C-style symbols in the shared library.
// so we can find these functions with `dlsym()` calling.
extern "C" {
  DYWRAP_NEW_WRAPPER_FUNC_DEF(BlockMaker)
  DYWRAP_NEW_WRAPPER_FUNC_DEF(GbtMaker)
}

#endif

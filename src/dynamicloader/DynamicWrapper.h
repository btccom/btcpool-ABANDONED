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

#include "ClassDefinition.h"


//====================== Wrapper Generator ======================

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

//================= End of Wrapper Generator ==================


//====================== Generated codes ======================

//------------- BlockMaker -------------
DYWRAP_CLASS_DEF_BEGIN(BlockMaker)
  DYWRAP_CONSTRUCTOR_DEF(BlockMaker)
  DYWRAP_DESTRUCTOR_DEF(BlockMaker)
  DYWRAP_METHOD_DEF(BlockMaker, addBitcoind)
  DYWRAP_METHOD_DEF(BlockMaker, init)
  DYWRAP_METHOD_DEF(BlockMaker, stop)
  DYWRAP_METHOD_DEF(BlockMaker, run)
DYWRAP_CLASS_DEF_END()

//------------- GbtMaker -------------
DYWRAP_CLASS_DEF_BEGIN(GbtMaker)
  DYWRAP_CONSTRUCTOR_DEF(GbtMaker)
  DYWRAP_DESTRUCTOR_DEF(GbtMaker)
  DYWRAP_METHOD_DEF(GbtMaker, init)
  DYWRAP_METHOD_DEF(GbtMaker, stop)
  DYWRAP_METHOD_DEF(GbtMaker, run)
DYWRAP_CLASS_DEF_END()

//------------- GwMaker -------------
DYWRAP_CLASS_DEF_BEGIN(GwMaker)
  DYWRAP_CONSTRUCTOR_DEF(GwMaker)
  DYWRAP_DESTRUCTOR_DEF(GwMaker)
  DYWRAP_METHOD_DEF(GwMaker, init)
  DYWRAP_METHOD_DEF(GwMaker, stop)
  DYWRAP_METHOD_DEF(GwMaker, run)
DYWRAP_CLASS_DEF_END()

//------------- NMCAuxBlockMaker -------------
DYWRAP_CLASS_DEF_BEGIN(NMCAuxBlockMaker)
  DYWRAP_CONSTRUCTOR_DEF(NMCAuxBlockMaker)
  DYWRAP_DESTRUCTOR_DEF(NMCAuxBlockMaker)
  DYWRAP_METHOD_DEF(NMCAuxBlockMaker, init)
  DYWRAP_METHOD_DEF(NMCAuxBlockMaker, stop)
  DYWRAP_METHOD_DEF(NMCAuxBlockMaker, run)
DYWRAP_CLASS_DEF_END()

//------------- JobMaker -------------
DYWRAP_CLASS_DEF_BEGIN(JobMaker)
  DYWRAP_CONSTRUCTOR_DEF(JobMaker)
  DYWRAP_DESTRUCTOR_DEF(JobMaker)
  DYWRAP_METHOD_DEF(JobMaker, init)
  DYWRAP_METHOD_DEF(JobMaker, stop)
  DYWRAP_METHOD_DEF(JobMaker, run)
DYWRAP_CLASS_DEF_END()

//------------- ClientContainer -------------
DYWRAP_CLASS_DEF_BEGIN(ClientContainer)
  DYWRAP_CONSTRUCTOR_DEF(ClientContainer)
  DYWRAP_DESTRUCTOR_DEF(ClientContainer)
  DYWRAP_METHOD_DEF(ClientContainer, addPools)
  DYWRAP_METHOD_DEF(ClientContainer, init)
  DYWRAP_METHOD_DEF(ClientContainer, stop)
  DYWRAP_METHOD_DEF(ClientContainer, run)
DYWRAP_CLASS_DEF_END()

//------------- ShareLogWriter -------------
DYWRAP_CLASS_DEF_BEGIN(ShareLogWriter)
  DYWRAP_CONSTRUCTOR_DEF(ShareLogWriter)
  DYWRAP_DESTRUCTOR_DEF(ShareLogWriter)
  DYWRAP_METHOD_DEF(ShareLogWriter, stop)
  DYWRAP_METHOD_DEF(ShareLogWriter, run)
DYWRAP_CLASS_DEF_END()

//------------- StratumClientWrapper -------------
DYWRAP_CLASS_DEF_BEGIN(StratumClientWrapper)
  DYWRAP_CONSTRUCTOR_DEF(StratumClientWrapper)
  DYWRAP_DESTRUCTOR_DEF(StratumClientWrapper)
  DYWRAP_METHOD_DEF(StratumClientWrapper, stop)
  DYWRAP_METHOD_DEF(StratumClientWrapper, run)
DYWRAP_CLASS_DEF_END()

//------------- ShareLogDumper -------------
DYWRAP_CLASS_DEF_BEGIN(ShareLogDumper)
  DYWRAP_CONSTRUCTOR_DEF(ShareLogDumper)
  DYWRAP_DESTRUCTOR_DEF(ShareLogDumper)
  DYWRAP_METHOD_DEF(ShareLogDumper, dump2stdout)
DYWRAP_CLASS_DEF_END()

//------------- ShareLogParser -------------
DYWRAP_CLASS_DEF_BEGIN(ShareLogParser)
  DYWRAP_CONSTRUCTOR_DEF(ShareLogParser)
  DYWRAP_DESTRUCTOR_DEF(ShareLogParser)
  DYWRAP_METHOD_DEF(ShareLogParser, init)
  DYWRAP_METHOD_DEF(ShareLogParser, flushToDB)
  DYWRAP_METHOD_DEF(ShareLogParser, processUnchangedShareLog)
DYWRAP_CLASS_DEF_END()

//------------- ShareLogParserServer -------------
DYWRAP_CLASS_DEF_BEGIN(ShareLogParserServer)
  DYWRAP_CONSTRUCTOR_DEF(ShareLogParserServer)
  DYWRAP_DESTRUCTOR_DEF(ShareLogParserServer)
  DYWRAP_METHOD_DEF(ShareLogParserServer, stop)
  DYWRAP_METHOD_DEF(ShareLogParserServer, run)
DYWRAP_CLASS_DEF_END()

//------------- StatsServer -------------
DYWRAP_CLASS_DEF_BEGIN(StatsServer)
  DYWRAP_CONSTRUCTOR_DEF(StatsServer)
  DYWRAP_DESTRUCTOR_DEF(StatsServer)
  DYWRAP_METHOD_DEF(StatsServer, init)
  DYWRAP_METHOD_DEF(StatsServer, stop)
  DYWRAP_METHOD_DEF(StatsServer, run)
DYWRAP_CLASS_DEF_END()

//------------- JobMaker -------------
DYWRAP_CLASS_DEF_BEGIN(StratumServer)
  DYWRAP_CONSTRUCTOR_DEF(StratumServer)
  DYWRAP_DESTRUCTOR_DEF(StratumServer)
  DYWRAP_METHOD_DEF(StratumServer, init)
  DYWRAP_METHOD_DEF(StratumServer, stop)
  DYWRAP_METHOD_DEF(StratumServer, run)
DYWRAP_CLASS_DEF_END()


// skip name managing of C++, use C-style symbols in the shared library.
// so we can find these functions with `dlsym()` calling.
extern "C" {
  DYWRAP_NEW_WRAPPER_FUNC_DEF(BlockMaker)
  DYWRAP_NEW_WRAPPER_FUNC_DEF(GbtMaker)
  DYWRAP_NEW_WRAPPER_FUNC_DEF(GwMaker)
  DYWRAP_NEW_WRAPPER_FUNC_DEF(NMCAuxBlockMaker)
  DYWRAP_NEW_WRAPPER_FUNC_DEF(JobMaker)
  DYWRAP_NEW_WRAPPER_FUNC_DEF(ClientContainer)
  DYWRAP_NEW_WRAPPER_FUNC_DEF(ShareLogWriter)
  DYWRAP_NEW_WRAPPER_FUNC_DEF(StratumClientWrapper)
  DYWRAP_NEW_WRAPPER_FUNC_DEF(ShareLogDumper)
  DYWRAP_NEW_WRAPPER_FUNC_DEF(ShareLogParser)
  DYWRAP_NEW_WRAPPER_FUNC_DEF(ShareLogParserServer)
  DYWRAP_NEW_WRAPPER_FUNC_DEF(StatsServer)
  DYWRAP_NEW_WRAPPER_FUNC_DEF(StratumServer)
}

#endif

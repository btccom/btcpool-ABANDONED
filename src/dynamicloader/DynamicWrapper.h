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

#include "MysqlConnection.h"

#ifdef DYNAMIC_LOAD_LIBBTCPOOL_BITCOIN
  #define EXPORT  virtual  // dynamic binding
#else
  #define EXPORT /*empty*/ // static binding
#endif

class BlockMakerWrapper {
protected:
  // The original object that wrapped.
  // Use `void *` so the definition of the original object not needed
  // by the wrapper's user when dynamic loading.
  void *thatObj;

public:
  BlockMakerWrapper(const char *kafkaBrokers, const MysqlConnectInfo &poolDB);
  EXPORT ~BlockMakerWrapper();

  EXPORT void addBitcoind(const string &rpcAddress, const string &rpcUserpass);

  EXPORT bool init();
  EXPORT void stop();
  EXPORT void run();
};

// skip name managing of C++, use C-style symbols in the shared library.
// so we can find these functions with `dlsym()` calling.
extern "C" {
  BlockMakerWrapper *NewBlockMakerWrapper(const char *kafkaBrokers, const MysqlConnectInfo &poolDB);
}

#endif

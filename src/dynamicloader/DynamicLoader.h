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
#ifndef DYNAMIC_LOADER_H_
#define DYNAMIC_LOADER_H_

#include <string>
#include <unordered_map>

#include "ClassDefinition.h"
#include "DynamicWrapper.h"


//--------------- Wrapper Generator ---------------

#define DYLOAD_NEW_WRAPPER_DEF(className) \
  className##Wrapper* new##className(DYCLASS_##className##_##className##_FPARAMS);

#define DYLOAD_NEW_WRAPPER_POINTER(className) \
  typedef className##Wrapper* (*PNew##className##Wrapper)(DYCLASS_##className##_##className##_FPARAMS);

//--------------- End of Wrapper Generator ---------------


class DynamicLoaderException : public std::runtime_error {
public:
  explicit DynamicLoaderException(const std::string &what_arg);
};

class DynamicLoader {
protected:
  void *library; // the value returned by dlopen()

public:
  DynamicLoader(const string &binPath, const string &chainType);

  // definitions of newXXX(...)
  DYLOAD_NEW_WRAPPER_DEF(BlockMaker)
  DYLOAD_NEW_WRAPPER_DEF(GbtMaker)
};

// function pointer of newXXXWrapper(...)
DYLOAD_NEW_WRAPPER_POINTER(BlockMaker)
DYLOAD_NEW_WRAPPER_POINTER(GbtMaker)

#endif

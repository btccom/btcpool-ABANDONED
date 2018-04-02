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
#include "DynamicLoader.h"
#include "DynamicWrapper.h"

#include <stdlib.h>
#include <dlfcn.h>
#include <glog/logging.h>

#include <sstream>
#include <boost/filesystem/path.hpp>

DynamicLoaderException::DynamicLoaderException(const string &what_arg) : std::runtime_error(what_arg) {
  // no more contents than its parent
}

DynamicLoader::DynamicLoader(const string &binPath, const string &chainType) {
  #ifdef DYNAMIC_LOAD_LIBBTCPOOL_BITCOIN
  {
    boost::filesystem::path libPath = binPath;
    libPath = libPath.remove_filename();

    libPath += "/";
    libPath += LIBBTCPOOL_BITCOIN_PREFIX;
    libPath += chainType;
    libPath += LIBBTCPOOL_BITCOIN_POSTFIX;

    LOG(INFO) << "DynamicLoader: loading " << libPath;
    library = dlopen(libPath.string().c_str(), RTLD_LAZY);
    if (!library) {
      std::stringstream errmsg;
      errmsg << "DynamicLoader: dlopen failed: " << dlerror();
      throw DynamicLoaderException(std::string(errmsg.str()));
    }
  }
  #else
  {
    // there is just one chain supported without dynamic loading
    if (chainType != CHAIN_TYPE_STRING) {
      std::stringstream errmsg;
      errmsg << "DynamicLoader: unknown chain type: " << chainType << ", supported chain type: " << CHAIN_TYPE_STRING;
      throw DynamicLoaderException(std::string(errmsg.str()));
    }
  }
  #endif
}

BlockMakerWrapper *DynamicLoader::newBlockMaker(const char *kafkaBrokers, const MysqlConnectInfo &poolDB) {
  #ifdef DYNAMIC_LOAD_LIBBTCPOOL_BITCOIN
  {
    PNewBlockMakerWrapper pNewBlockMakerWrapper = (PNewBlockMakerWrapper)dlsym(library, "NewBlockMakerWrapper");
    const char *dlerrmsg = dlerror();
    if (dlerrmsg != nullptr) {
      std::stringstream errmsg;
      errmsg << "DynamicLoader: dlsym(NewBlockMakerWrapper) failed: " << dlerrmsg;
      throw DynamicLoaderException(std::string(errmsg.str()));
    }

    return pNewBlockMakerWrapper(kafkaBrokers, poolDB);
  }
  #else
  {
    return NewBlockMakerWrapper(kafkaBrokers, poolDB);
  }
  #endif
}

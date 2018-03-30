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

//////////////////////////// BlockMakerWrapper ////////////////////////////
#define that ((BlockMaker*)thatObj)

BlockMakerWrapper::BlockMakerWrapper(const char *kafkaBrokers, const MysqlConnectInfo &poolDB) {
    thatObj = new BlockMaker(kafkaBrokers, poolDB);
}

BlockMakerWrapper::~BlockMakerWrapper() {
    delete that;
}

void BlockMakerWrapper::addBitcoind(const string &rpcAddress, const string &rpcUserpass) {
    that->addBitcoind(rpcAddress, rpcUserpass);
}

bool BlockMakerWrapper::init() {
    return that->init();
}

void BlockMakerWrapper::stop() {
    that->stop();
}

void BlockMakerWrapper::run() {
    that->run();
}

BlockMakerWrapper *NewBlockMakerWrapper(const char *kafkaBrokers, const MysqlConnectInfo &poolDB) {
    return new BlockMakerWrapper(kafkaBrokers, poolDB);
}

#undef that


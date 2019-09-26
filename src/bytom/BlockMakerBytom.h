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
#ifndef BLOCK_MAKER_BYTOM_H_
#define BLOCK_MAKER_BYTOM_H_

#include "BlockMaker.h"
#include "CommonBytom.h"

class BlockMakerBytom : public BlockMaker {
public:
  BlockMakerBytom(
      shared_ptr<BlockMakerDefinition> def,
      const char *kafkaBrokers,
      const MysqlConnectInfo &poolDB);
  void processSolvedShare(rd_kafka_message_t *rkmessage) override;

private:
  void submitBlockNonBlocking(const string &request);
  void _submitBlockThread(
      const string &rpcAddress,
      const string &rpcUserpass,
      const string &request);
  void saveBlockToDBNonBlocking(
      const string &header,
      const uint32_t height,
      const uint64_t networkDiff,
      const StratumWorkerPlain &worker);
  void _saveBlockToDBThread(
      const string &header,
      const uint32_t height,
      const uint64_t networkDiff,
      const StratumWorkerPlain &worker);
};

#endif

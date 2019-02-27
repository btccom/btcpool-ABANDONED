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

#include "ShareLogParserGrin.h"

///////////////  template instantiation ///////////////
// Without this, some linking errors will issued.
// If you add a new derived class of Share, add it at the following.
template class ShareLogDumperT<ShareGrin>;
template class ShareLogParserT<ShareGrin>;
template class ShareLogParserServerT<ShareGrin>;

ShareLogParserGrin::ShareLogParserGrin(
    const char *chainType,
    const string &dataDir,
    time_t timestamp,
    const MysqlConnectInfo &poolDBInfo,
    shared_ptr<DuplicateShareChecker<ShareGrin>> dupShareChecker,
    bool acceptStale,
    const libconfig::Config &config)
  : ShareLogParserT{chainType,
                    dataDir,
                    timestamp,
                    poolDBInfo,
                    std::move(dupShareChecker),
                    acceptStale}
  , algorithm_{AlgorithmGrin::Unknown} {
  string algorithm;
  if (config.lookupValue("sharelog.algorithm", algorithm)) {
    LOG(INFO) << "Grin algorithm: " << algorithm;
    if (algorithm == "cuckaroo") {
      algorithm_ = AlgorithmGrin::Cuckaroo;
    } else if (algorithm == "cuckatoo") {
      algorithm_ = AlgorithmGrin::Cuckatoo;
    }
  }
}

bool ShareLogParserGrin::filterShare(const ShareGrin &share) {
  switch (algorithm_) {
  case AlgorithmGrin::Cuckaroo:
    return share.edgebits() == 29;
  case AlgorithmGrin::Cuckatoo:
    return share.edgebits() >= 31;
  default:
    return true;
  }
}

ShareLogParserServerGrin::ShareLogParserServerGrin(
    const char *chainType,
    const string &dataDir,
    const string &httpdHost,
    unsigned short httpdPort,
    const MysqlConnectInfo &poolDBInfo,
    const uint32_t kFlushDBInterval,
    shared_ptr<DuplicateShareChecker<ShareGrin>> dupShareChecker,
    bool acceptStale,
    const libconfig::Config &config)
  : ShareLogParserServerT{chainType,
                          dataDir,
                          httpdHost,
                          httpdPort,
                          poolDBInfo,
                          kFlushDBInterval,
                          std::move(dupShareChecker),
                          acceptStale}
  , config_{config} {
}

shared_ptr<ShareLogParserT<ShareGrin>>
ShareLogParserServerGrin::createShareLogParser(time_t datets) {
  return std::make_shared<ShareLogParserGrin>(
      chainType_.c_str(),
      dataDir_,
      datets,
      poolDBInfo_,
      dupShareChecker_,
      acceptStale_,
      config_);
}

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
#pragma once

#include <string>
#include <atomic>
#include <zlibstream/zstr.hpp>

#include "ParquetWritter.hpp"
#include "StratumBitcoin.hpp"
#include "StratumBeam.hpp"
#include "StratumEth.hpp"

using namespace std;

///////////////////////////////  ShareLogParser  ///////////////////////////////
// Interface, used as a pointer type.
class ShareLogParser {
public:
  virtual ~ShareLogParser() {}
  virtual bool init() = 0;
  virtual bool processUnchangedShareLog() = 0;
};
///////////////////////////////  ShareLogParserT ///////////////////////////////
//
// 1. read sharelog data files
// 2. calculate share & score
// 3. write stats data to DB
//
template <class SHARE>
class ShareLogParserT : public ShareLogParser {
  time_t date_ = 0; // date_ % 86400 == 0
  time_t hour_ = 0;
  string filePath_; // sharelog data file path
  string outputDir_;
  const string chainType_;
  ParquetWriterT<SHARE> parquetWriter_;

  //
  // for processGrowingShareLog()
  //
  zstr::ifstream *f_; // file handler
  uint8_t *buf_; // fread buffer
  // 48 * 1000000 = 48,000,000 ~ 48 MB
  static const size_t kMaxElementsNum_ = 1000000; // num of shares
  size_t incompleteShareSize_;
  uint32_t bufferlength_;

  static constexpr uint32_t MAX_READING_ERRORS = 5;
  // The number of read errors. If MAX_READING_ERRORS is exceeded,
  // the program will exit
  uint32_t readingErrors_ = 0;

  // single user mode
  bool singleUserMode_ = false;
  int32_t singleUserId_ = 0;

  inline int32_t getHourIdx(uint32_t ts) {
    // %H	Hour in 24h format (00-23)
    return atoi(date("%H", ts).c_str());
  }

  inline void handleReadingError() {
    if (++readingErrors_ > MAX_READING_ERRORS) {
      LOG(FATAL) << "Too many reading errors, the program will exit!";
    }
  }

  inline void resetReadingError() {
    if (readingErrors_) {
      readingErrors_ = 0;
    }
  }

  void parseShareLog(const uint8_t *buf, size_t len);
  void parseShare(SHARE &share);
  bool openParquet();

public:
  ShareLogParserT(
      const libconfig::Config &cfg, time_t timestamp, const string &chainType);
  virtual ~ShareLogParserT();

  bool init();
  void closeParquet();

  // read unchanged share data bin file, for example yestoday's file. it will
  // use mmap() to get high performance. call only once will process
  // the whole bin file
  bool processUnchangedShareLog();

  // today's file is still growing, return processed shares number.
  int64_t processGrowingShareLog();
  bool isReachEOF(); // only for growing file
};

////////////////////////////  ShareLogParserServer  ////////////////////////////
// Interface, used as a pointer type.
class ShareLogParserServer {
public:
  virtual ~ShareLogParserServer(){};
  virtual void stop() = 0;
  virtual void run() = 0;
};

////////////////////////////  ShareLogParserServerT ////////////////////////////
//
// read share binlog, parse shares, calc stats data than save them to database
// table.stats_xxxx. meanwhile hold there stats data in memory so it could
// provide httpd service. web/app could read the latest data from it's http API.
//
template <class SHARE>
class ShareLogParserServerT : public ShareLogParserServer {
protected:
  const libconfig::Config &cfg_;

  atomic<bool> running_;
  // share log daily
  time_t date_; // date_ % 86400 == 0
  shared_ptr<ShareLogParserT<SHARE>> shareLogParser_;
  const string chainType_;
  string dataDir_;

  bool initShareLogParser(time_t datets);
  virtual shared_ptr<ShareLogParserT<SHARE>>
  createShareLogParser(time_t datets);
  void runThreadShareLogParser();
  void trySwitchBinFile(shared_ptr<ShareLogParserT<SHARE>> shareLogParser);

public:
  ShareLogParserServerT(const libconfig::Config &cfg, const string &chainType);
  virtual ~ShareLogParserServerT();

  void stop();
  void run();
};

#include "ShareLogParser.inl"

///////////////////////////////  Alias  ///////////////////////////////

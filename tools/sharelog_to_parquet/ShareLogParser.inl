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

#include <set>
#include <iostream>
#include <thread>

///////////////////////////////  ShareLogParserT ///////////////////////////////
template <class SHARE>
ShareLogParserT<SHARE>::ShareLogParserT(
    const libconfig::Config &cfg, time_t timestamp, const string &chainType)
  : date_(timestamp)
  , outputDir_(cfg.lookup("parquet.data_dir").operator string())
  , chainType_(chainType)
  , f_(nullptr)
  , buf_(nullptr)
  , incompleteShareSize_(0) {

  // single user mode
  cfg.lookupValue("users.single_user_mode", singleUserMode_);
  cfg.lookupValue("users.single_user_puid", singleUserId_);
  if (singleUserMode_) {
    LOG(INFO) << "[Option] Single User Mode Enabled, puid: " << singleUserId_;
  }

  filePath_ = getStatsFilePath(
      chainType_.c_str(), cfg.lookup("sharelog.data_dir"), timestamp);

  // prealloc memory
  // buf_ = (uint8_t *)malloc(kMaxElementsNum_ * sizeof(SHARE));
  bufferlength_ = kMaxElementsNum_ * 48;
  buf_ = (uint8_t *)malloc(bufferlength_);
}

template <class SHARE>
ShareLogParserT<SHARE>::~ShareLogParserT() {
  if (f_)
    delete f_;

  if (buf_)
    free(buf_);
}

template <class SHARE>
bool ShareLogParserT<SHARE>::openParquet() {
  string parquetPath = getParquetFilePath(chainType_, outputDir_, hour_);
  auto stat = parquetWriter_.open(parquetPath, hour_);
  if (!stat.ok()) {
    LOG(ERROR) << "cannot create parquet file " << parquetPath
               << ", message: " << stat.message();
    return false;
  }
  LOG(INFO) << "writing parquet file " << parquetPath;
  return true;
}

template <class SHARE>
bool ShareLogParserT<SHARE>::init() {
  // there is nothing to do
  return true;
}

template <class SHARE>
void ShareLogParserT<SHARE>::closeParquet() {
  parquetWriter_.close();
}

template <class SHARE>
void ShareLogParserT<SHARE>::parseShareLog(const uint8_t *buf, size_t len) {
  SHARE share;
  if (!share.ParseFromArray(buf, len)) {
    LOG(INFO) << "parse share from base message failed! ";
    return;
  }

  if (singleUserMode_) {
    if (!share.has_extuserid() || share.userid() != singleUserId_) {
      // Ignore irrelevant shares
      return;
    }
    // Change the user id for statistical purposes
    share.set_userid(share.extuserid());
  }

  parseShare(share);
}

template <class SHARE>
void ShareLogParserT<SHARE>::parseShare(SHARE &share) {
  time_t shareHour = share.timestamp() / 3600;
  if (shareHour > hour_) {
    hour_ = shareHour;
    if (!openParquet()) {
      LOG(FATAL) << "cannot open parquet file, writer aborted!";
    }
  }

  parquetWriter_.addShare(share);
}

template <class SHARE>
bool ShareLogParserT<SHARE>::processUnchangedShareLog() {
  try {
    // open file
    LOG(INFO) << "open file: " << filePath_;
    zstr::ifstream f(filePath_, std::ios::binary);

    if (!f) {
      LOG(ERROR) << "open file fail: " << filePath_;
      return false;
    }

    // 2000000 * 48 = 96,000,000 Bytes
    string buf;
    buf.resize(96000000);
    uint32_t incompleteShareSize = 0;

    while (f.peek() != EOF) {
      f.read(
          (char *)buf.data() + incompleteShareSize,
          buf.size() - incompleteShareSize);
      uint32_t readNum = f.gcount() + incompleteShareSize;

      uint32_t currentpos = 0;
      while (currentpos + sizeof(uint32_t) < readNum) {
        uint32_t sharelength =
            *(uint32_t *)(buf.data() + currentpos); // get shareLength
        // DLOG(INFO) << "sharelength = " << sharelength << std::endl;
        if (readNum >= currentpos + sizeof(uint32_t) + sharelength) {

          parseShareLog(
              (const uint8_t *)(buf.data() + currentpos + sizeof(uint32_t)),
              sharelength);

          currentpos = currentpos + sizeof(uint32_t) + sharelength;
        } else {
          // LOG(INFO) << "not read enough length " << sharelength << std::endl;
          break;
        }
      }
      incompleteShareSize = readNum - currentpos;
      if (incompleteShareSize > 0) {
        // LOG(INFO) << "incompleteShareSize_ " << incompleteShareSize
        //           << std::endl;
        memcpy(
            (char *)buf.data(),
            (char *)buf.data() + currentpos,
            incompleteShareSize);
      }
    }

    if (incompleteShareSize > 0) {
      LOG(ERROR) << "Sharelog is incomplete, found " << incompleteShareSize
                 << " bytes fragment before EOF" << std::endl;
    }

    closeParquet();

    return true;
  } catch (const zstr::Exception &ex) {
    LOG(ERROR) << "open file fail: " << filePath_
               << ", exception: " << ex.what();
    return false;
  } catch (const strict_fstream::Exception &ex) {
    LOG(ERROR) << "open file fail: " << filePath_
               << ", exception: " << ex.what();
    return false;
  }
}

template <class SHARE>
int64_t ShareLogParserT<SHARE>::processGrowingShareLog() {
  if (f_ == nullptr) {
    bool fileOpened = true;
    try {
      f_ = new zstr::ifstream(filePath_, std::ios::binary);
      if (f_ == nullptr) {
        LOG(WARNING) << "open file fail. Filename: " << filePath_;
        fileOpened = false;
      }
    } catch (...) {
      //  just log warning instead of error because it's a usual scenario the
      //  file not exist and it throw an exception
      LOG(WARNING) << "open file fail with exception. Filename: " << filePath_;
      fileOpened = false;
    }

    if (!fileOpened) {
      delete f_;
      f_ = nullptr;

      return -1;
    }
  }

  uint32_t readNum = 0;
  try {
    assert(f_ != nullptr);

    // clear the eof status so the stream can coninue reading.
    if (f_->eof()) {
      f_->clear();
    }

    //
    // Old Comments:
    // no need to set buffer memory to zero before fread
    // return: the total number of elements successfully read is returned.
    //
    // fread():
    // C11 at 7.21.8.1.2 and 7.21.8.2.2 says: If an error occurs, the resulting
    // value of the file position indicator for the stream is indeterminate.
    //
    // Now zlib/gzip file stream is used.
    //

    // If an incomplete share was found at last read, only reading the rest part
    // of it.

    f_->read(
        (char *)buf_ + incompleteShareSize_,
        bufferlength_ - incompleteShareSize_);

    if (f_->gcount() == 0) {
      return 0;
    }

    readNum = f_->gcount() + incompleteShareSize_;
    uint32_t currentpos = 0, parsedsharenum = 0;
    while (currentpos + sizeof(uint32_t) < readNum) {

      uint32_t sharelength = *(uint32_t *)(buf_ + currentpos); // get
                                                               // shareLength
      if (readNum >= currentpos + sizeof(uint32_t) + sharelength) {

        parseShareLog(buf_ + currentpos + sizeof(uint32_t), sharelength);
        currentpos = currentpos + sizeof(uint32_t) + sharelength;
        parsedsharenum++;
      } else {
        break;
      }
    }
    incompleteShareSize_ = readNum - currentpos;

    if (incompleteShareSize_ > 0) {
      // move the incomplete share to the beginning of buf_
      memcpy((char *)buf_, (char *)buf_ + currentpos, incompleteShareSize_);
    }

    DLOG(INFO) << "processGrowingShareLog share count: " << parsedsharenum;
    resetReadingError();
    return parsedsharenum;

  } catch (const std::exception &ex) {
    LOG(ERROR) << "reading file fail with exception: " << ex.what()
               << ", file: " << filePath_;
    handleReadingError();
    return -1;
  } catch (...) {
    LOG(ERROR) << "reading file fail with unknown exception, file: "
               << filePath_;
    handleReadingError();
    return -1;
  }
}

template <class SHARE>
bool ShareLogParserT<SHARE>::isReachEOF() {
  if (f_ == nullptr || !*f_) {
    // if error we consider as EOF
    return true;
  }

  return f_->eof();
}

////////////////////////////  ShareLogParserServerT<SHARE>
///////////////////////////////
template <class SHARE>
ShareLogParserServerT<SHARE>::ShareLogParserServerT(
    const libconfig::Config &cfg, const string &chainType)
  : cfg_(cfg)
  , running_(true)
  , chainType_(chainType)
  , dataDir_(cfg.lookup("sharelog.data_dir").operator string()) {
  const time_t now = time(nullptr);
  date_ = now - (now % 86400);
}

template <class SHARE>
ShareLogParserServerT<SHARE>::~ShareLogParserServerT<SHARE>() {
  // there is nothing to do
}

template <class SHARE>
void ShareLogParserServerT<SHARE>::stop() {
  if (!running_)
    return;

  LOG(INFO) << "stop ShareLogParserServerT<SHARE>...";

  running_ = false;
}

template <class SHARE>
bool ShareLogParserServerT<SHARE>::initShareLogParser(time_t datets) {
  // reset
  date_ = datets - (datets % 86400);
  shareLogParser_ = nullptr;

  // set new obj
  auto parser = createShareLogParser(datets);

  if (!parser->init()) {
    LOG(ERROR) << "parser check failure, date: " << date("%F", date_);
    return false;
  }

  shareLogParser_ = parser;
  return true;
}

template <class SHARE>
shared_ptr<ShareLogParserT<SHARE>>
ShareLogParserServerT<SHARE>::createShareLogParser(time_t datets) {
  return std::make_shared<ShareLogParserT<SHARE>>(cfg_, datets, chainType_);
}

template <class SHARE>
void ShareLogParserServerT<SHARE>::runThreadShareLogParser() {
  LOG(INFO) << "thread sharelog parser start";

  static size_t nonShareCounter = 0;

  while (running_) {
    // get ShareLogParserT
    shared_ptr<ShareLogParserT<SHARE>> shareLogParser = shareLogParser_;

    // maybe last switch has been fail, we need to check and try again
    if (shareLogParser == nullptr) {
      if (initShareLogParser(time(nullptr)) == false) {
        LOG(ERROR) << "initShareLogParser fail";
        std::this_thread::sleep_for(3s);
        continue;
      }
    }
    assert(shareLogParser != nullptr);

    int64_t shareNum = 0;

    while (running_) {
      shareNum = shareLogParser->processGrowingShareLog();
      if (shareNum <= 0) {
        nonShareCounter++;
        break;
      }
      nonShareCounter = 0;
      DLOG(INFO) << "process share: " << shareNum;
    }
    // shareNum < 0 means that the file read error. So wait longer.
    std::this_thread::sleep_for(shareNum < 0 ? 5s : 1s);

    // No new share has been read in the last five times.
    // Maybe sharelog has switched to a new file.
    if (nonShareCounter > 5) {
      // check if need to switch bin file
      DLOG(INFO) << "no new shares, try switch bin file";
      trySwitchBinFile(shareLogParser);
    }

  } /* while */

  if (shareLogParser_) {
    shareLogParser_->closeParquet();
  }

  LOG(INFO) << "thread sharelog parser stop";
}

template <class SHARE>
void ShareLogParserServerT<SHARE>::trySwitchBinFile(
    shared_ptr<ShareLogParserT<SHARE>> shareLogParser) {
  assert(shareLogParser != nullptr);

  const time_t now = time(nullptr);
  const time_t beginTs = now - (now % 86400);

  if (beginTs == date_)
    return; // still today

  //
  // switch file when:
  //   1. today has been pasted at least 5 seconds
  //   2. last bin file has reached EOF
  //   3. new file exists
  //
  const string filePath = getStatsFilePath(chainType_.c_str(), dataDir_, now);
  if (now > beginTs + 5 && shareLogParser->isReachEOF() &&
      fileNonEmpty(filePath.c_str())) {
    bool res = initShareLogParser(now);
    if (!res) {
      LOG(ERROR) << "trySwitchBinFile fail";
    }
  }
}

template <class SHARE>
void ShareLogParserServerT<SHARE>::run() {
  // use current timestamp when first setup
  if (initShareLogParser(time(nullptr)) == false) {
    return;
  }

  runThreadShareLogParser();
}

//--------------------------------------------------------------------
// Alias and template instantiation
using ShareLogParserBitcoin = ShareLogParserT<ShareBitcoin>;
using ShareLogParserServerBitcoin = ShareLogParserServerT<ShareBitcoin>;
template class ShareLogParserT<ShareBitcoin>;
template class ShareLogParserServerT<ShareBitcoin>;

using ShareLogParserBeam = ShareLogParserT<ShareBeam>;
using ShareLogParserServerBeam = ShareLogParserServerT<ShareBeam>;
template class ShareLogParserT<ShareBeam>;
template class ShareLogParserServerT<ShareBeam>;

using ShareLogParserEth = ShareLogParserT<ShareEth>;
using ShareLogParserServerEth = ShareLogParserServerT<ShareEth>;
template class ShareLogParserT<ShareEth>;
template class ShareLogParserServerT<ShareEth>;

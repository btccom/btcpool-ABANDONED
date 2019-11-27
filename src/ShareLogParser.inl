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

#include <boost/algorithm/string.hpp>
#include <set>
#include <iostream>

///////////////////////////////  ShareLogDumperT ///////////////////////////////
template <class SHARE>
ShareLogDumperT<SHARE>::ShareLogDumperT(
    const libconfig::Config &cfg,
    time_t timestamp,
    const std::set<int32_t> &uids)
  : uids_(uids)
  , isDumpAll_(false) {

  // single user mode
  cfg.lookupValue("users.single_user_mode", singleUserMode_);
  cfg.lookupValue("users.single_user_puid", singleUserId_);
  if (singleUserMode_) {
    LOG(INFO) << "[Option] Single User Mode Enabled, puid: " << singleUserId_;
  }

  filePath_ = getStatsFilePath(
      cfg.lookup("sharelog.chain_type"),
      cfg.lookup("sharelog.data_dir"),
      timestamp);

  if (uids_.empty())
    isDumpAll_ = true;
}

template <class SHARE>
ShareLogDumperT<SHARE>::~ShareLogDumperT() {
}

template <class SHARE>
void ShareLogDumperT<SHARE>::dump2stdout() {
  try {
    // open file (auto-detecting compression format or non-compression)
    LOG(INFO) << "open file: " << filePath_;
    zstr::ifstream f(filePath_, std::ios::binary);

    if (!f) {
      LOG(ERROR) << "open file fail: " << filePath_;
      return;
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
        uint32_t sharelength = *(uint32_t *)(buf.data() + currentpos);

        if (readNum >= currentpos + sizeof(uint32_t) + sharelength) {

          parseShareLog(
              (const uint8_t *)(buf.data() + currentpos + sizeof(uint32_t)),
              sharelength);
          currentpos = currentpos + sizeof(uint32_t) + sharelength;
        } else {
          DLOG(INFO) << "not read enough length " << sharelength << std::endl;
          break;
        }
      }
      incompleteShareSize = readNum - currentpos;
      if (incompleteShareSize > 0) {
        DLOG(INFO) << "incomplete share size: " << incompleteShareSize
                   << std::endl;
        memcpy(
            (char *)buf.data(),
            (char *)buf.data() + currentpos,
            incompleteShareSize);
      }
    }

  } catch (...) {
    LOG(ERROR) << "open file fail: " << filePath_;
  }
}

template <class SHARE>
void ShareLogDumperT<SHARE>::parseShareLog(const uint8_t *buf, size_t len) {
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

  parseShare(&share);
}

template <class SHARE>
void ShareLogDumperT<SHARE>::parseShare(const SHARE *share) {
  if (!share->isValid()) {
    LOG(ERROR) << "invalid share: " << share->toString();
    return;
  }

  if (isDumpAll_ || uids_.find(share->userid()) != uids_.end()) {
    // print to stdout
    std::cout << share->toString() << std::endl;
  }
}

///////////////////////////////  ShareLogParserT ///////////////////////////////
template <class SHARE>
ShareLogParserT<SHARE>::ShareLogParserT(
    const libconfig::Config &cfg,
    time_t timestamp,
    shared_ptr<DuplicateShareChecker<SHARE>> dupShareChecker)
  : date_(timestamp)
  , chainType_(cfg.lookup("sharelog.chain_type").operator string())
  , f_(nullptr)
  , buf_(nullptr)
  , kMaxElementsNum_(1000000)
  , incompleteShareSize_(0)
  , poolDB_({cfg.lookup("pooldb.host").operator string(),
             configLookup<int32_t>(cfg, "pooldb.port", 3306),
             cfg.lookup("pooldb.username").operator string(),
             cfg.lookup("pooldb.password").operator string(),
             cfg.lookup("pooldb.dbname").operator string()})
  , dupShareChecker_(dupShareChecker)
  , acceptStale_(configLookup(cfg, "sharelog.accept_stale", false)) {

  // single user mode
  cfg.lookupValue("users.single_user_mode", singleUserMode_);
  cfg.lookupValue("users.single_user_puid", singleUserId_);
  if (singleUserMode_) {
    LOG(INFO) << "[Option] Single User Mode Enabled, puid: " << singleUserId_;
  }

  pthread_rwlock_init(&rwlock_, nullptr);

  {
    // for the pool
    WorkerKey pkey(0, 0);
    if ("CKB" == chainType_) {
      rpcUrl_ = cfg.lookup("sharelog.rpcurl").operator string();
      cfg.lookupValue("sharelog.max_elements_num", kMaxElementsNum_);
      LOG(INFO) << "chaintype : " << chainType_ << " RPCURL : " << rpcUrl_
                << "max_elements_num : " << kMaxElementsNum_;
      workersStats_[pkey] = std::make_shared<ShareStatsDay<SHARE>>(rpcUrl_);
    } else {
      workersStats_[pkey] = std::make_shared<ShareStatsDay<SHARE>>();
    }
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
bool ShareLogParserT<SHARE>::init() {
  // check db
  if (!poolDB_.ping()) {
    LOG(ERROR) << "connect to db fail";
    return false;
  }

  return true;
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
  if (!share.isValid()) {
    LOG(ERROR) << "invalid share: " << share.toString();
    return;
  }
  if (dupShareChecker_ && !dupShareChecker_->addShare(share)) {
    LOG(INFO) << "duplicate share attack: " << share.toString();
    share.set_status(StratumStatus::DUPLICATE_SHARE);
  }
  if (!filterShare(share)) {
    DLOG(INFO) << "filtered share: " << share.toString();
    return;
  }

  WorkerKey wkey(share.userid(), share.workerhashid());
  WorkerKey ukey(share.userid(), 0);
  WorkerKey pkey(0, 0);

  pthread_rwlock_wrlock(&rwlock_);
  if ("CKB" == chainType_) {
    if (workersStats_.find(wkey) == workersStats_.end()) {
      workersStats_[wkey] =
          std::make_shared<ShareStatsDayNormalized<SHARE>>(rpcUrl_);
    }
    if (workersStats_.find(ukey) == workersStats_.end()) {
      workersStats_[ukey] = std::make_shared<ShareStatsDay<SHARE>>(rpcUrl_);
    }
  } else {
    if (workersStats_.find(wkey) == workersStats_.end()) {
      workersStats_[wkey] = std::make_shared<ShareStatsDayNormalized<SHARE>>();
    }
    if (workersStats_.find(ukey) == workersStats_.end()) {
      workersStats_[ukey] = std::make_shared<ShareStatsDay<SHARE>>();
    }
  }
  pthread_rwlock_unlock(&rwlock_);

  const uint32_t hourIdx = getHourIdx(share.timestamp());
  workersStats_[wkey]->processShare(hourIdx, share, acceptStale_);
  workersStats_[ukey]->processShare(hourIdx, share, acceptStale_);
  workersStats_[pkey]->processShare(hourIdx, share, acceptStale_);
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
    return true;
  } catch (...) {
    LOG(ERROR) << "open file fail: " << filePath_;
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

template <class SHARE>
void ShareLogParserT<SHARE>::generateHoursData(
    shared_ptr<ShareStatsDay<SHARE>> stats,
    const int32_t userId,
    const int64_t workerId,
    vector<string> *valuesWorkersHour,
    vector<string> *valuesUsersHour,
    vector<string> *valuesPoolHour) {
  static_assert(
      sizeof(stats->shareAccept1h_) / sizeof(stats->shareAccept1h_[0]) == 24,
      "shareAccept1h_ should have 24 members");
  static_assert(
      sizeof(stats->shareStale1h_) / sizeof(stats->shareStale1h_[0]) == 24,
      "shareStale1h_ should have 24 members");
  static_assert(
      sizeof(stats->shareRejects1h_) / sizeof(stats->shareRejects1h_[0]) == 24,
      "shareRejects1h_ should have 24 members");
  static_assert(
      sizeof(stats->score1h_) / sizeof(stats->score1h_[0]) == 24,
      "score1h_ should have 24 members");

  string table, extraValues;
  // worker
  if (userId != 0 && workerId != 0) {
    extraValues = Strings::Format("%d,%d,", workerId, userId);
    table = "stats_workers_hour";
  }
  // user
  else if (userId != 0 && workerId == 0) {
    extraValues = Strings::Format("%d,", userId);
    table = "stats_users_hour";
  }
  // pool
  else if (userId == 0 && workerId == 0) {
    table = "stats_pool_hour";
  } else {
    LOG(ERROR) << "unknown stats type";
    return;
  }

  // loop hours from 00 -> 03
  for (size_t i = 0; i < 24; i++) {
    string valuesStr;
    {
      ScopeLock sl(stats->lock_);
      const uint32_t flag = (0x01U << i);
      if ((stats->modifyHoursFlag_ & flag) == 0x0u) {
        continue;
      }
      const string hourStr =
          Strings::Format("%s%02d", date("%Y%m%d", date_), i);
      const int32_t hour = atoi(hourStr.c_str());

      const uint64_t accept = stats->shareAccept1h_[i]; // alias
      const uint64_t stale = stats->shareStale1h_[i]; // alias
      const uint64_t reject = sumRejectShares(stats->shareRejects1h_[i]);
      const string rejectDetail =
          generateRejectDetail(stats->shareRejects1h_[i]);
      double rejectRate = 0.0;
      if (reject)
        rejectRate = (double)reject / (accept + reject);
      const string nowStr = date("%F %T");
      const string scoreStr = score2Str(stats->score1h_[i]);
      const double earn = stats->earn1h_[i];

      valuesStr = Strings::Format(
          "%s%d,%u,%u,%u,'%s',%f,'%s',%0.0lf,'%s','%s'",
          extraValues,
          hour,
          accept,
          stale,
          reject,
          rejectDetail,
          rejectRate,
          scoreStr,
          earn,
          nowStr,
          nowStr);
    } // for scope lock

    if (table == "stats_workers_hour") {
      valuesWorkersHour->push_back(valuesStr);
    } else if (table == "stats_users_hour") {
      valuesUsersHour->push_back(valuesStr);
    } else if (table == "stats_pool_hour") {
      valuesPoolHour->push_back(valuesStr);
    }
  } /* /for */
}

template <class SHARE>
void ShareLogParserT<SHARE>::flushHourOrDailyData(
    const vector<string> values,
    const string &tableName,
    const string &extraFields) {
  string mergeSQL;
  string fields;

  // in case two process use the same tmp table name, we add process id into
  // tmp table name.
  const string tmpTableName = Strings::Format("%s_tmp_%d", tableName, getpid());

  if (!poolDB_.ping()) {
    LOG(ERROR) << "can't connect to pool DB";
    return;
  }

  if (values.size() == 0) {
    LOG(INFO) << "no active workers";
    return;
  }

  // drop tmp table
  const string sqlDropTmpTable =
      Strings::Format("DROP TEMPORARY TABLE IF EXISTS `%s`;", tmpTableName);
  // create tmp table
  const string createTmpTable = Strings::Format(
      "CREATE TEMPORARY TABLE `%s` like `%s`;", tmpTableName, tableName);

  if (!poolDB_.execute(sqlDropTmpTable)) {
    LOG(ERROR) << "DROP TEMPORARY TABLE `" << tmpTableName << "` failure";
    return;
  }
  if (!poolDB_.execute(createTmpTable)) {
    LOG(ERROR) << "CREATE TEMPORARY TABLE `" << tmpTableName << "` failure";
    // something went wrong with the current mysql connection, try to reconnect.
    poolDB_.reconnect();
    return;
  }

  // fields for table.stats_xxxxx_hour
  fields = Strings::Format(
      "%s `share_accept`,`share_stale`,"
      "`share_reject`,`reject_detail`,`reject_rate`,"
      "`score`,`earn`,`created_at`,`updated_at`",
      extraFields);

  if (!multiInsert(poolDB_, tmpTableName, fields, values)) {
    LOG(ERROR) << "multi-insert table." << tmpTableName << " failure";
    return;
  }

  // merge two table items
  mergeSQL = Strings::Format(
      "INSERT INTO `%s` "
      " SELECT * FROM `%s` AS `t2` "
      " ON DUPLICATE KEY "
      " UPDATE "
      "  `share_accept` = `t2`.`share_accept`, "
      "  `share_stale`  = `t2`.`share_stale`, "
      "  `share_reject` = `t2`.`share_reject`, "
      "  `reject_detail`= `t2`.`reject_detail`, "
      "  `reject_rate`  = `t2`.`reject_rate`, "
      "  `score`        = `t2`.`score`, "
      "  `earn`         = `t2`.`earn`, "
      "  `updated_at`   = `t2`.`updated_at` ",
      tableName,
      tmpTableName);
  if (!poolDB_.update(mergeSQL)) {
    LOG(ERROR) << "merge mining_workers failure";
    return;
  }

  if (!poolDB_.execute(sqlDropTmpTable)) {
    LOG(ERROR) << "DROP TEMPORARY TABLE `" << tmpTableName << "` failure";
    return;
  }
}

template <class SHARE>
void ShareLogParserT<SHARE>::generateDailyData(
    shared_ptr<ShareStatsDay<SHARE>> stats,
    const int32_t userId,
    const int64_t workerId,
    vector<string> *valuesWorkersDay,
    vector<string> *valuesUsersDay,
    vector<string> *valuesPoolDay) {
  string table, extraValues;
  // worker
  if (userId != 0 && workerId != 0) {
    extraValues = Strings::Format("%d,%d,", workerId, userId);
    table = "stats_workers_day";
  }
  // user
  else if (userId != 0 && workerId == 0) {
    extraValues = Strings::Format("%d,", userId);
    table = "stats_users_day";
  }
  // pool
  else if (userId == 0 && workerId == 0) {
    table = "stats_pool_day";
  } else {
    LOG(ERROR) << "unknown stats type";
    return;
  }

  string valuesStr;
  {
    ScopeLock sl(stats->lock_);
    const int32_t day = atoi(date("%Y%m%d", date_).c_str());

    const uint64_t accept = stats->shareAccept1d_; // alias
    const uint64_t stale = stats->shareStale1d_; // alias
    const uint64_t reject = sumRejectShares(stats->shareRejects1d_);
    const string rejectDetail = generateRejectDetail(stats->shareRejects1d_);
    double rejectRate = 0.0;
    if (reject)
      rejectRate = (double)reject / (accept + reject);
    const string nowStr = date("%F %T");
    const string scoreStr = score2Str(stats->score1d_);
    const double earn = stats->earn1d_;

    valuesStr = Strings::Format(
        "%s%d,%u,%u,%u,'%s',%f,'%s',%0.0lf,'%s','%s'",
        extraValues,
        day,
        accept,
        stale,
        reject,
        rejectDetail,
        rejectRate,
        scoreStr,
        earn,
        nowStr,
        nowStr);
  } // for scope lock

  if (table == "stats_workers_day") {
    valuesWorkersDay->push_back(valuesStr);
  } else if (table == "stats_users_day") {
    valuesUsersDay->push_back(valuesStr);
  } else if (table == "stats_pool_day") {
    valuesPoolDay->push_back(valuesStr);
  }
}

template <class SHARE>
shared_ptr<ShareStatsDay<SHARE>>
ShareLogParserT<SHARE>::getShareStatsDayHandler(const WorkerKey &key) {
  pthread_rwlock_rdlock(&rwlock_);
  auto itr = workersStats_.find(key);
  pthread_rwlock_unlock(&rwlock_);

  if (itr != workersStats_.end()) {
    return itr->second;
  }
  return nullptr;
}

template <class SHARE>
void ShareLogParserT<SHARE>::removeExpiredDataFromDB() {
  static time_t lastRemoveTime = 0u;
  string sql;

  // check if we need to remove, 3600 = 1 hour
  if (lastRemoveTime + 3600 > time(nullptr)) {
    return;
  }

  // set the last remove timestamp
  lastRemoveTime = time(nullptr);

  //
  // table.stats_workers_day
  //
  {
    const int32_t kDailyDataKeepDays_workers = 90; // 3 months
    const string dayStr =
        date("%Y%m%d", time(nullptr) - 86400 * kDailyDataKeepDays_workers);
    sql = Strings::Format(
        "DELETE FROM `stats_workers_day` WHERE `day` < '%s'", dayStr);
    if (poolDB_.execute(sql)) {
      LOG(INFO) << "delete expired workers daily data before '" << dayStr
                << "', count: " << poolDB_.affectedRows();
    }
  }

  //
  // table.stats_workers_hour
  //
  {
    const int32_t kHourDataKeepDays_workers = 24 * 3; // 3 days
    const string hourStr =
        date("%Y%m%d%H", time(nullptr) - 3600 * kHourDataKeepDays_workers);
    sql = Strings::Format(
        "DELETE FROM `stats_workers_hour` WHERE `hour` < '%s'", hourStr);
    if (poolDB_.execute(sql)) {
      LOG(INFO) << "delete expired workers hour data before '" << hourStr
                << "', count: " << poolDB_.affectedRows();
    }
  }

  //
  // table.stats_users_hour
  //
  {
    const int32_t kHourDataKeepDays_users = 24 * 30; // 30 days
    const string hourStr =
        date("%Y%m%d%H", time(nullptr) - 3600 * kHourDataKeepDays_users);
    sql = Strings::Format(
        "DELETE FROM `stats_users_hour` WHERE `hour` < '%s'", hourStr);
    if (poolDB_.execute(sql)) {
      LOG(INFO) << "delete expired users hour data before '" << hourStr
                << "', count: " << poolDB_.affectedRows();
    }
  }
}

template <class SHARE>
bool ShareLogParserT<SHARE>::flushToDB(bool removeExpiredData) {
  if (!poolDB_.ping()) {
    LOG(ERROR) << "connect db fail";
    return false;
  }

  LOG(INFO) << "start flush to DB...";

  //
  // we must finish the workersStats_ loop asap
  //
  vector<WorkerKey> keys;
  vector<shared_ptr<ShareStatsDay<SHARE>>> stats;

  pthread_rwlock_rdlock(&rwlock_);
  for (const auto &itr : workersStats_) {
    if (itr.second->modifyHoursFlag_ == 0x0u) {
      continue; // no new data, ignore
    }
    keys.push_back(itr.first);
    stats.push_back(itr.second); // shared_ptr increase ref here
  }
  pthread_rwlock_unlock(&rwlock_);

  LOG(INFO) << "dumped workers stats";

  vector<string> valuesWorkersHour;
  vector<string> valuesUsersHour;
  vector<string> valuesPoolHour;

  vector<string> valuesWorkersDay;
  vector<string> valuesUsersDay;
  vector<string> valuesPoolDay;

  for (size_t i = 0; i < keys.size(); i++) {
    //
    // the lock is in flushDailyData() & flushHoursData(), so maybe we lost
    // some data between func gaps, but it's not important. we will exec
    // processUnchangedShareLog() after the day has been past, no data will lost
    // by than.
    //
    generateHoursData(
        stats[i],
        keys[i].userId_,
        keys[i].workerId_,
        &valuesWorkersHour,
        &valuesUsersHour,
        &valuesPoolHour);
    generateDailyData(
        stats[i],
        keys[i].userId_,
        keys[i].workerId_,
        &valuesWorkersDay,
        &valuesUsersDay,
        &valuesPoolDay);

    stats[i]->modifyHoursFlag_ = 0x0u; // reset flag
  }

  LOG(INFO) << "generated sql values";
  size_t counter = 0;

  // flush hours data
  flushHourOrDailyData(
      valuesWorkersHour, "stats_workers_hour", "`worker_id`,`puid`,`hour`,");
  flushHourOrDailyData(valuesUsersHour, "stats_users_hour", "`puid`,`hour`,");
  flushHourOrDailyData(valuesPoolHour, "stats_pool_hour", "`hour`,");
  counter +=
      valuesWorkersHour.size() + valuesUsersHour.size() + valuesPoolHour.size();

  // flush daily data
  flushHourOrDailyData(
      valuesWorkersDay, "stats_workers_day", "`worker_id`,`puid`,`day`,");
  flushHourOrDailyData(valuesUsersDay, "stats_users_day", "`puid`,`day`,");
  flushHourOrDailyData(valuesPoolDay, "stats_pool_day", "`day`,");
  counter +=
      valuesWorkersDay.size() + valuesUsersDay.size() + valuesPoolDay.size();

  // done: daily data and hour data
  LOG(INFO) << "flush to DB... done, items: " << counter;

  // clean expired data
  if (removeExpiredData) {
    removeExpiredDataFromDB();
  }

  return true;
}

////////////////////////////  ShareLogParserServerT<SHARE>
///////////////////////////////
template <class SHARE>
ShareLogParserServerT<SHARE>::ShareLogParserServerT(
    const libconfig::Config &cfg,
    shared_ptr<DuplicateShareChecker<SHARE>> dupShareChecker)
  : cfg_(cfg)
  , running_(true)
  , chainType_(cfg.lookup("sharelog.chain_type").operator string())
  , dataDir_(cfg.lookup("sharelog.data_dir").operator string())
  , kFlushDBInterval_(configLookup(cfg, "slparserhttpd.flush_db_interval", 20))
  , dupShareChecker_(dupShareChecker)
  , base_(nullptr)
  , httpdHost_(cfg.lookup("slparserhttpd.ip").operator string())
  , httpdPort_(configLookup(cfg, "slparserhttpd.port", 8081))
  , requestCount_(0)
  , responseBytes_(0) {
  const time_t now = time(nullptr);

  uptime_ = now;
  date_ = now - (now % 86400);

  pthread_rwlock_init(&rwlock_, nullptr);
}

template <class SHARE>
ShareLogParserServerT<SHARE>::~ShareLogParserServerT<SHARE>() {
  stop();

  if (threadShareLogParser_.joinable())
    threadShareLogParser_.join();

  pthread_rwlock_destroy(&rwlock_);
}

template <class SHARE>
void ShareLogParserServerT<SHARE>::stop() {
  if (!running_)
    return;

  LOG(INFO) << "stop ShareLogParserServerT<SHARE>...";

  running_ = false;
  event_base_loopexit(base_, NULL);
}

template <class SHARE>
bool ShareLogParserServerT<SHARE>::initShareLogParser(time_t datets) {
  pthread_rwlock_wrlock(&rwlock_);

  // reset
  date_ = datets - (datets % 86400);
  shareLogParser_ = nullptr;

  // set new obj
  auto parser = createShareLogParser(datets);

  if (!parser->init()) {
    LOG(ERROR) << "parser check failure, date: " << date("%F", date_);
    pthread_rwlock_unlock(&rwlock_);
    return false;
  }

  shareLogParser_ = parser;
  pthread_rwlock_unlock(&rwlock_);
  return true;
}

template <class SHARE>
shared_ptr<ShareLogParserT<SHARE>>
ShareLogParserServerT<SHARE>::createShareLogParser(time_t datets) {
  return std::make_shared<ShareLogParserT<SHARE>>(
      cfg_, datets, dupShareChecker_);
}

template <class SHARE>
void ShareLogParserServerT<SHARE>::getShareStats(
    struct evbuffer *evb,
    const char *pUserId,
    const char *pWorkerId,
    const char *pHour) {
  vector<string> vHoursStr;
  vector<string> vWorkerIdsStr;
  vector<WorkerKey> keys;
  vector<int32_t> hours; // range: -23, -22, ..., 0, 24
  const int32_t userId = atoi(pUserId);

  // split by ','
  {
    string pHourStr = pHour;
    boost::split(vHoursStr, pHourStr, boost::is_any_of(","));

    string pWorkerIdStr = pWorkerId;
    boost::split(vWorkerIdsStr, pWorkerIdStr, boost::is_any_of(","));
  }

  // get worker keys
  keys.reserve(vWorkerIdsStr.size());
  for (size_t i = 0; i < vWorkerIdsStr.size(); i++) {
    const int64_t workerId = strtoll(vWorkerIdsStr[i].c_str(), nullptr, 10);
    keys.push_back(WorkerKey(userId, workerId));
  }

  // get hours
  hours.reserve(vHoursStr.size());
  for (const auto itr : vHoursStr) {
    hours.push_back(atoi(itr.c_str()));
  }

  vector<ShareStats> shareStats;
  shareStats.resize(keys.size() * hours.size());
  _getShareStats(keys, hours, shareStats);

  // output json string
  for (size_t i = 0; i < keys.size(); i++) {
    Strings::EvBufferAdd(
        evb, "%s\"%d\":[", (i == 0 ? "" : ","), keys[i].workerId_);

    for (size_t j = 0; j < hours.size(); j++) {
      ShareStats *s = &shareStats[i * hours.size() + j];
      const int32_t hour = hours[j];

      Strings::EvBufferAdd(
          evb,
          "%s{\"hour\":%d,\"accept\":%u,\"stale\":%u,\"reject\":%u"
          ",\"reject_detail\":%s,\"reject_rate\":%f,\"earn\":%0.0lf}",
          (j == 0 ? "" : ","),
          hour,
          s->shareAccept_,
          s->shareStale_,
          s->shareReject_,
          s->rejectDetail_,
          s->rejectRate_,
          s->earn_);
    }
    Strings::EvBufferAdd(evb, "]");
  }
}

template <class SHARE>
void ShareLogParserServerT<SHARE>::_getShareStats(
    const vector<WorkerKey> &keys,
    const vector<int32_t> &hours,
    vector<ShareStats> &shareStats) {
  pthread_rwlock_rdlock(&rwlock_);
  shared_ptr<ShareLogParserT<SHARE>> shareLogParser = shareLogParser_;
  pthread_rwlock_unlock(&rwlock_);

  if (shareLogParser == nullptr)
    return;

  for (size_t i = 0; i < keys.size(); i++) {
    shared_ptr<ShareStatsDay<SHARE>> statsDay =
        shareLogParser->getShareStatsDayHandler(keys[i]);
    if (statsDay == nullptr)
      continue;

    for (size_t j = 0; j < hours.size(); j++) {
      ShareStats *stats = &shareStats[i * hours.size() + j];
      const int32_t hour = hours[j];

      if (hour == 24) {
        statsDay->getShareStatsDay(stats);
      } else if (hour <= 0 && hour >= -23) {
        const uint32_t hourIdx = atoi(date("%H").c_str()) + hour;
        statsDay->getShareStatsHour(hourIdx, stats);
      }
    }
  }
}

template <class SHARE>
void ShareLogParserServerT<SHARE>::httpdShareStats(
    struct evhttp_request *req, void *arg) {
  evhttp_add_header(
      evhttp_request_get_output_headers(req), "Content-Type", "text/json");
  ShareLogParserServerT<SHARE> *server = (ShareLogParserServerT<SHARE> *)arg;
  server->requestCount_++;

  evhttp_cmd_type rMethod = evhttp_request_get_command(req);
  char *query = nullptr; // remember free it

  if (rMethod == EVHTTP_REQ_GET) {
    // GET
    struct evhttp_uri *uri = evhttp_uri_parse(evhttp_request_get_uri(req));
    const char *uriQuery = nullptr;
    if (uri != nullptr && (uriQuery = evhttp_uri_get_query(uri)) != nullptr) {
      query = strdup(uriQuery);
      evhttp_uri_free(uri);
    }
  } else if (rMethod == EVHTTP_REQ_POST) {
    // POST
    struct evbuffer *evbIn = evhttp_request_get_input_buffer(req);
    size_t len = 0;
    if (evbIn != nullptr && (len = evbuffer_get_length(evbIn)) > 0) {
      query = (char *)malloc(len + 1);
      evbuffer_copyout(evbIn, query, len);
      query[len] = '\0'; // evbuffer is not include '\0'
    }
  }

  // evbuffer for output
  struct evbuffer *evb = evbuffer_new();

  // query is empty, return
  if (query == nullptr) {
    Strings::EvBufferAdd(evb, "{\"err_no\":1,\"err_msg\":\"invalid args\"}");
    evhttp_send_reply(req, HTTP_OK, "OK", evb);
    evbuffer_free(evb);

    return;
  }

  // parse query
  struct evkeyvalq params;
  evhttp_parse_query_str(query, &params);
  const char *pUserId = evhttp_find_header(&params, "user_id");
  const char *pWorkerId = evhttp_find_header(&params, "worker_id");
  const char *pHour = evhttp_find_header(&params, "hour");

  if (pUserId == nullptr || pWorkerId == nullptr || pHour == nullptr) {
    Strings::EvBufferAdd(evb, "{\"err_no\":1,\"err_msg\":\"invalid args\"}");
    evhttp_send_reply(req, HTTP_OK, "OK", evb);
    goto finish;
  }

  Strings::EvBufferAdd(evb, "{\"err_no\":0,\"err_msg\":\"\",\"data\":{");
  server->getShareStats(evb, pUserId, pWorkerId, pHour);
  Strings::EvBufferAdd(evb, "}}");

  server->responseBytes_ += evbuffer_get_length(evb);
  evhttp_send_reply(req, HTTP_OK, "OK", evb);

finish:
  evhttp_clear_headers(&params);
  evbuffer_free(evb);
  if (query)
    free(query);
}

template <class SHARE>
void ShareLogParserServerT<SHARE>::getServerStatus(
    ShareLogParserServerT<SHARE>::ServerStatus &s) {
  s.date_ = date_;
  s.uptime_ = (uint32_t)(time(nullptr) - uptime_);
  s.requestCount_ = requestCount_;
  s.responseBytes_ = responseBytes_;

  pthread_rwlock_rdlock(&rwlock_);
  shared_ptr<ShareLogParserT<SHARE>> shareLogParser = shareLogParser_;
  pthread_rwlock_unlock(&rwlock_);

  WorkerKey pkey(0, 0);
  shared_ptr<ShareStatsDay<SHARE>> statsDayPtr =
      shareLogParser->getShareStatsDayHandler(pkey);

  s.stats.resize(2);
  statsDayPtr->getShareStatsDay(&(s.stats[0]));
  statsDayPtr->getShareStatsHour(atoi(date("%H").c_str()), &(s.stats[1]));
}

template <class SHARE>
void ShareLogParserServerT<SHARE>::httpdServerStatus(
    struct evhttp_request *req, void *arg) {
  evhttp_add_header(
      evhttp_request_get_output_headers(req), "Content-Type", "text/json");
  ShareLogParserServerT<SHARE> *server = (ShareLogParserServerT<SHARE> *)arg;
  server->requestCount_++;

  struct evbuffer *evb = evbuffer_new();

  ShareLogParserServerT<SHARE>::ServerStatus s;
  server->getServerStatus(s);

  time_t now = time(nullptr);
  if (now % 3600 == 0)
    now += 2; // just in case the denominator is zero

  Strings::EvBufferAdd(
      evb,
      "{\"err_no\":0,\"err_msg\":\"\","
      "\"data\":{\"uptime\":\"%04u d %02u h %02u m %02u s\","
      "\"request\":%u,\"repbytes\":%u"
      ",\"pool\":{\"today\":{"
      "\"hashrate_t\":%f,\"accept\":%u"
      ",\"stale\":%u,\"reject\":%u,\"reject_detail\":%s"
      ",\"reject_rate\":%f,\"earn\":%0.0lf},"
      "\"curr_hour\":{\"hashrate_t\":%f,\"accept\":%u"
      ",\"stale\":%u,\"reject\":%u,\"reject_detail\":%s"
      ",\"reject_rate\":%f,\"earn\":%0.0lf}}"
      "}}",
      s.uptime_ / 86400,
      (s.uptime_ % 86400) / 3600,
      (s.uptime_ % 3600) / 60,
      s.uptime_ % 60,
      s.requestCount_,
      s.responseBytes_,
      // pool today
      share2HashrateT(s.stats[0].shareAccept_, now % 86400),
      s.stats[0].shareAccept_,
      s.stats[0].shareStale_,
      s.stats[0].shareReject_,
      s.stats[0].rejectDetail_,
      s.stats[0].rejectRate_,
      s.stats[0].earn_,
      // pool current hour
      share2HashrateT(s.stats[1].shareAccept_, now % 3600),
      s.stats[1].shareAccept_,
      s.stats[1].shareStale_,
      s.stats[1].shareReject_,
      s.stats[0].rejectDetail_,
      s.stats[1].rejectRate_,
      s.stats[1].earn_);

  server->responseBytes_ += evbuffer_get_length(evb);
  evhttp_send_reply(req, HTTP_OK, "OK", evb);
  evbuffer_free(evb);
}

template <class SHARE>
void ShareLogParserServerT<SHARE>::runHttpd() {
  struct evhttp_bound_socket *handle;
  struct evhttp *httpd;

  base_ = event_base_new();
  httpd = evhttp_new(base_);

  evhttp_set_allowed_methods(
      httpd, EVHTTP_REQ_GET | EVHTTP_REQ_POST | EVHTTP_REQ_HEAD);
  evhttp_set_timeout(httpd, 5 /* timeout in seconds */);

  evhttp_set_cb(
      httpd, "/", ShareLogParserServerT<SHARE>::httpdServerStatus, this);
  evhttp_set_cb(
      httpd,
      "/share_stats",
      ShareLogParserServerT<SHARE>::httpdShareStats,
      this);
  evhttp_set_cb(
      httpd,
      "/share_stats/",
      ShareLogParserServerT<SHARE>::httpdShareStats,
      this);

  handle =
      evhttp_bind_socket_with_handle(httpd, httpdHost_.c_str(), httpdPort_);
  if (!handle) {
    LOG(ERROR) << "couldn't bind to port: " << httpdPort_
               << ", host: " << httpdHost_ << ", exiting.";
    return;
  }
  event_base_dispatch(base_);
}

template <class SHARE>
bool ShareLogParserServerT<SHARE>::setupThreadShareLogParser() {
  threadShareLogParser_ =
      std::thread(&ShareLogParserServerT<SHARE>::runThreadShareLogParser, this);
  return true;
}

template <class SHARE>
void ShareLogParserServerT<SHARE>::runThreadShareLogParser() {
  LOG(INFO) << "thread sharelog parser start";

  static size_t nonShareCounter = 0;
  time_t lastFlushDBTime = 0;

  while (running_) {
    // get ShareLogParserT
    pthread_rwlock_rdlock(&rwlock_);
    shared_ptr<ShareLogParserT<SHARE>> shareLogParser = shareLogParser_;
    pthread_rwlock_unlock(&rwlock_);

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
      if ("CKB" == chainType_ &&
          time(nullptr) > lastFlushDBTime + kFlushDBInterval_) {
        DLOG(INFO) << "flush sharelog to DB";
        shareLogParser->flushToDB(); // will wait util all data flush to DB
        lastFlushDBTime = time(nullptr);
      }
      nonShareCounter = 0;
      DLOG(INFO) << "process share: " << shareNum;
    }
    // shareNum < 0 means that the file read error. So wait longer.
    std::this_thread::sleep_for(shareNum < 0 ? 5s : 1s);

    // flush data to db
    if (time(nullptr) > lastFlushDBTime + kFlushDBInterval_) {
      shareLogParser->flushToDB(); // will wait util all data flush to DB
      lastFlushDBTime = time(nullptr);
    }

    // No new share has been read in the last five times.
    // Maybe sharelog has switched to a new file.
    if (nonShareCounter > 5) {
      // check if need to switch bin file
      DLOG(INFO) << "no new shares, try switch bin file";
      trySwitchBinFile(shareLogParser);
    }

  } /* while */

  LOG(INFO) << "thread sharelog parser stop";

  stop(); // if thread exit, we must call server to stop
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
    shareLogParser->flushToDB(); // flush data

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

  if (setupThreadShareLogParser() == false) {
    return;
  }

  runHttpd();
}

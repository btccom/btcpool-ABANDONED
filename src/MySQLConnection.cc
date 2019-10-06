/**
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "MySQLConnection.h"
#include "Utils.h"

#include <mysql.h>
#include <glog/logging.h>

MySQLResult::MySQLResult()
  : result(nullptr) {
}

MySQLResult::MySQLResult(MYSQL_RES *result)
  : result(result) {
}

void MySQLResult::reset(MYSQL_RES *result) {
  if (this->result) {
    mysql_free_result(this->result);
  }
  this->result = result;
}

MySQLResult::~MySQLResult() {
  if (result) {
    mysql_free_result(result);
  }
}

uint64_t MySQLResult::numRows() {
  if (result) {
    return mysql_num_rows(result);
  }
  return 0;
}

uint32_t MySQLResult::fields() {
  return mysql_num_fields(result);
}

char **MySQLResult::nextRow() {
  return mysql_fetch_row(result);
}

MySQLConnection::MySQLConnection(const MysqlConnectInfo &connectInfo)
  : host_(connectInfo.host_.c_str())
  , port_(connectInfo.port_)
  , username_(connectInfo.username_.c_str())
  , password_(connectInfo.password_.c_str())
  , dbName_(connectInfo.dbName_.c_str())
  , conn(nullptr) {
}

MySQLConnection::~MySQLConnection() {
  close();
}

bool MySQLConnection::open() {
  close();
  conn = mysql_init(NULL);
  if (!conn) {
    LOG(ERROR) << "create MYSQL failed";
    return false;
  }
  if (mysql_real_connect(
          conn,
          host_.c_str(),
          username_.c_str(),
          password_.c_str(),
          dbName_.c_str(),
          port_,
          nullptr,
          0) == nullptr) {
    LOG(ERROR) << "mysql_real_connect failed: " << mysql_error(conn);
    close();
    return false;
  }

  // set charaseter
  mysql_set_character_set(conn, "utf8");

  // set timezone
  {
    const string sql = "SET time_zone = \"+00:00\"";
    mysql_query(conn, sql.c_str());
  }

  return true;
}

void MySQLConnection::close() {
  if (conn) {
    mysql_close(conn);
    conn = nullptr;
  }
}

bool MySQLConnection::ping() {
  //
  // mysql_ping():
  //  Checks whether the connection to the server is working. If the connection
  //  has gone down and auto-reconnect is enabled an attempt to reconnect is
  //  made. If the connection is down and auto-reconnect is disabled,
  //  mysql_ping() returns an error.
  // Zero if the connection to the server is active. Nonzero if an error
  // occurred.
  //

  if (!conn) {
    open();
  }

  // ping
  if (mysql_ping(conn) == 0) {
    return true;
  }
  LOG(ERROR) << "mysql_ping() failure, error_no: " << mysql_errno(conn)
             << ", error_info: " << mysql_error(conn);

  // re-connect
  LOG(INFO) << "reconnect to mysql DB";
  close();
  open();

  // ping again
  if (mysql_ping(conn) == 0) {
    return true;
  }
  LOG(ERROR) << "mysql_ping() failure, error_no: " << mysql_errno(conn)
             << ", error_info: " << mysql_error(conn);

  return false;
}

bool MySQLConnection::reconnect() {
  LOG(INFO) << "reconnect to mysql DB";
  close();
  open();
  return ping();
}

bool MySQLConnection::execute(const char *sql) {
  uint32_t error_no;
  int queryTimes = 0;

  DLOG(INFO) << "[MySQLConnection::execute] SQL: " << sql;

query:
  if (!conn) {
    open();
  }
  queryTimes++;
  if (mysql_query(conn, sql) == 0) {
    return true; // exec sql success
  }

  // get mysql error
  error_no = mysql_errno(conn);
  LOG(ERROR) << "exec sql failure, error_no: " << error_no
             << ", error_info: " << mysql_error(conn) << " , sql: " << sql;

  // 2006: MySQL server has gone away
  // 2013: Lost connection to MySQL server
  if (!(error_no == 2006 || error_no == 2013)) {
    return false; // not a network error
  }

  // use mysql_ping() to reconnnect
  if (queryTimes <= 3 && (error_no == 2006 || error_no == 2013)) {
    // rds switch master-slave usually take about 20 seconds
    std::this_thread::sleep_for(10s);
    if (mysql_ping(conn) == 0) {
      LOG(ERROR) << "reconnect success";
    } else {
      LOG(ERROR) << "reconnect failure, close conn and try open conn again";
      close();
    }
    goto query;
  }

  return false;
}

bool MySQLConnection::query(const char *sql, MySQLResult &result) {
  bool res = execute(sql);
  if (res)
    result.reset(mysql_store_result(conn));
  return res;
}

int64_t MySQLConnection::update(const char *sql) {
  if (execute(sql))
    return mysql_affected_rows(conn);
  else
    return -1;
}

uint64_t MySQLConnection::affectedRows() {
  return mysql_affected_rows(conn);
}

uint64_t MySQLConnection::getInsertId() {
  return mysql_insert_id(conn);
}

//
// SQL: show variables like "max_allowed_packet"
//
//   |    Variable_name    |   Value  |
//   | max_allowed_packet  | 16777216 |
//
string MySQLConnection::getVariable(const char *name) {
  string sql = Strings::Format("SHOW VARIABLES LIKE \"%s\";", name);
  MySQLResult result;
  if (!query(sql, result) || result.numRows() == 0) {
    return "";
  }
  char **row = result.nextRow();
  DLOG(INFO) << "msyql get variable: \"" << row[0] << "\" = \"" << row[1]
             << "\"";
  return string(row[1]);
}

bool multiInsert(
    MySQLConnection &db,
    const string &table,
    const string &fields,
    const vector<string> &values) {
  string sqlPrefix =
      Strings::Format("INSERT INTO `%s`(%s) VALUES ", table, fields);

  if (values.size() == 0 || fields.length() == 0 || table.length() == 0) {
    return false;
  }

  string sql = sqlPrefix;
  for (auto &it : values) {
    sql += Strings::Format("(%s),", it);
    // overthan 16MB
    // notice: you need to make sure mysql.max_allowed_packet is over than 16MB
    if (sql.length() >= 16 * 1024 * 1024) {
      sql.resize(sql.length() - 1);
      if (!db.execute(sql.c_str())) {
        return false;
      }
      sql = sqlPrefix;
    }
  }

  if (sql.length() > sqlPrefix.length()) {
    sql.resize(sql.length() - 1);
    if (!db.execute(sql.c_str())) {
      return false;
    }
  }

  return true;
}

MySQLExecQueue::MySQLExecQueue(const MysqlConnectInfo &dbInfo)
  : dbInfo_(dbInfo) {
  run();
}

MySQLExecQueue::~MySQLExecQueue() {
  stop();
}

void MySQLExecQueue::addSQL(const string &sql) {
  std::unique_lock<std::mutex> lock(sqlQueueLock_);
  sqlQueue_.push(sql);
  lock.unlock();
  notify_.notify_one();
}

void MySQLExecQueue::run() {
  running_ = true;

  thread_ = std::thread([this]() {
    LOG(INFO) << "MySQLExecQueue running...";

    for (;;) {
      std::unique_lock<std::mutex> lock(sqlQueueLock_);
      while (sqlQueue_.empty() && running_) {
        notify_.wait(lock);
      }
      if (!running_) {
        break;
      }
      string sql = sqlQueue_.front();
      sqlQueue_.pop();
      lock.unlock();
      execSQL(sql);
    }

    // Execute the remaining SQL and then stop
    std::unique_lock<std::mutex> lock(sqlQueueLock_);
    while (!sqlQueue_.empty()) {
      execSQL(sqlQueue_.front());
      sqlQueue_.pop();
    }
    lock.unlock();

    LOG(INFO) << "MySQLExecQueue stopped";
  });
}

void MySQLExecQueue::execSQL(const string &sql) {
  // try connect to DB
  MySQLConnection db(dbInfo_);
  for (size_t i = 0; i < 3; i++) {
    if (db.ping())
      break;
    else
      std::this_thread::sleep_for(3s);
  }

  if (db.execute(sql) == false) {
    LOG(ERROR) << "executing sql failure: " << sql;
  }
}

void MySQLExecQueue::stop() {
  running_ = false;
  notify_.notify_one();
  if (thread_.joinable()) {
    thread_.join();
  }
}

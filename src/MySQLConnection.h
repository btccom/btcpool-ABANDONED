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
#ifndef MYSQL_CONNECTION_H_
#define MYSQL_CONNECTION_H_

#include "Common.h"

extern "C" struct st_mysql;
typedef struct st_mysql MYSQL;
extern "C" struct st_mysql_res;
typedef struct st_mysql_res MYSQL_RES;

/**
 * Simple wrapper for MYSQL_RES
 * auto free
 */
struct MySQLResult {
  struct st_mysql_res * result;
  MySQLResult();
  MySQLResult(MYSQL_RES * result);
  void reset(MYSQL_RES * result);
  ~MySQLResult();
  uint64 numRows();
  uint32 fields();
  char ** nextRow();
};

class MysqlConnectInfo {
public:
  string  host_;
  int32_t port_;
  string  username_;
  string  password_;
  string  dbName_;

  MysqlConnectInfo(const string &host, int32_t port, const string &userName,
                   const string &password, const string &dbName):
  host_(host), port_(port), username_(userName), password_(password), dbName_(dbName)
  {
  }

  MysqlConnectInfo(const MysqlConnectInfo &r) {
    host_     = r.host_;
    port_     = r.port_;
    username_ = r.username_;
    password_ = r.password_;
    dbName_   = r.dbName_;
  }

  MysqlConnectInfo& operator=(const MysqlConnectInfo &r) {
    host_     = r.host_;
    port_     = r.port_;
    username_ = r.username_;
    password_ = r.password_;
    dbName_   = r.dbName_;
    return *this;
  }
};

/**
 * Simple wrapper for MYSQL connection
 * open/close auto managed by class
 * with support for auto-reconnect
 * @see test/TestMySQLConnection.cc for demo usage
 */
class MySQLConnection {
protected:
  string host_;
  int32_t port_;
  string username_;
  string password_;
  string dbName_;

  struct st_mysql * conn;

public:
  MySQLConnection(const MysqlConnectInfo &connectInfo);
  ~MySQLConnection();

  bool open();
  void close();
  bool ping();

  bool execute(const char * sql);
  bool execute(const string &sql) {
    return execute(sql.c_str());
  }

  bool query(const char * sql, MySQLResult & result);
  bool query(const string & sql, MySQLResult & result) {
    return query(sql.c_str(), result);
  }

  // return -1 on failure
  int64_t update(const char * sql);
  int64_t update(const string & sql) {
    return update(sql.c_str());
  }
  uint64 affectedRows();
  uint64 getInsertId();

  string getVariable(const char *name);
};

bool multiInsert(MySQLConnection &db, const string &table,
                 const string &fields, const vector<string> &values);

#endif

#ifndef _MYSQL_OPERATIONS_H_
#define _MYSQL_OPERATIONS_H_

#include "dataoperation_base.h"
#include <mysql.h>

struct StatementCloser {
  StatementCloser(MYSQL_STMT *s)
    : statement(s) {}

  ~StatementCloser() {
    if (statement) {
      mysql_stmt_close(statement);
    }
  }

  MYSQL_STMT *statement;
};

class DataHandlerLoadOperationMysql : public DataHandlerLoadOperationBase {
public:
  ~DataHandlerLoadOperationMysql();
  bool DoLoad(std::vector<char> &outData) override;
  std::string Id() const override { return m_ID; }

private:
  friend class MysqlDataOperationManager;
  DataHandlerLoadOperationMysql(
      MYSQL *const *connection, std::string id, std::string tableName);

  MYSQL *const *m_Connection;
  std::string m_ID;
  std::string m_TableName;
  MYSQL_BIND m_BindParam[1];
};

class MysqlDataOperationManager : public DataOperationManagerBase {
public:
  MysqlDataOperationManager(
      std::string server,
      std::string username,
      std::string password,
      std::string dbname,
      std::string tablename,
      int port = 0);
  ~MysqlDataOperationManager();

  std::unique_ptr<DataHandler> GetDataHandler(std::string id) const override;
  bool GetDataList(
      std::vector<std::string> &out,
      std::regex regex = std::regex(".*"),
      bool checkNotation = false) override;
  std::unique_ptr<DataHandler> StoreData(
      std::string id,
      std::vector<char> &&data,
      bool forceOverwrite = false) override;
  bool DeleteData(const std::string &id) override;

  bool IsExists(const std::string &id) const;

private:
  bool InitConnection();
  bool ValidateConnection();

  std::string m_Server;
  std::string m_Username;
  std::string m_Password;
  std::string m_Database;
  std::string m_TableName;
  int m_Port;

  MYSQL *m_Connection;
};

#endif // _MYSQL_OPERATIONS_H_
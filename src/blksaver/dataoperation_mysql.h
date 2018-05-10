#ifndef _MYSQL_OPERATIONS_H_
#define _MYSQL_OPERATIONS_H_

#include "dataoperation_base.h"
#include <mysql/mysql.h>

struct StatementCloser
{
    StatementCloser()
        : statement(nullptr)
    {
    }

    ~StatementCloser()
    {
        if(statement)
        {
            mysql_stmt_close(statement);
        }
    }

    MYSQL_STMT* statement;
};


class DataHandlerLoadOperationMysql : public DataHandlerLoadOperationBase
{
public:
    ~DataHandlerLoadOperationMysql();
    bool DoLoad(std::vector<char>& outData) override;
    std::string Id() const override
    {
        return m_ID;
    }

private:
    friend class MysqlDataOperationManager;
    DataHandlerLoadOperationMysql(MYSQL* connection, std::string id, std::string tableName);

    MYSQL* m_Connection;
    std::string m_ID;
    std::string m_TableName;
    MYSQL_STMT* m_Statement;
    MYSQL_BIND m_BindParam[1];
    StatementCloser m_StatementCloser;
};

class MysqlDataOperationManager : public DataOperationManagerBase
{
public:

    MysqlDataOperationManager(const std::string& server, const std::string& username, const std::string& password
                    , const std::string& dbname, std::string tablename, int port = 0);
    MysqlDataOperationManager(MYSQL* mysqlConnection, std::string tablename);
    ~MysqlDataOperationManager();

    std::unique_ptr<DataHandler> GetDataHandler(std::string id) const override;
    std::vector<std::string> GetDataList(std::regex regex = std::regex(".*"), bool checkNotation = false) const override;
    std::unique_ptr<DataHandler> StoreData(std::string id, std::vector<char>&& data, bool forceOverwrite = false) override;
    bool DeleteData(const std::string& id) override;

    bool IsExists(const std::string& id) const;
private:
    MYSQL* m_Connection;
    bool m_ownConnection;
    std::string m_TableName;
};

#endif // _MYSQL_OPERATIONS_H_
#include "dataoperation_mysql.h"
#include <iostream>
#include <glog/logging.h>

using namespace std;

DataHandlerLoadOperationMysql::DataHandlerLoadOperationMysql(MYSQL* connection, std::string id, std::string tableName)
    : m_Connection(connection)
    , m_ID(id)
    , m_TableName(tableName)
{
    m_Statement = mysql_stmt_init(m_Connection);
    m_StatementCloser.statement = m_Statement;

    stringstream ss;
    ss << "SELECT data FROM " << m_TableName << " WHERE id=?";
    string selectStatement(ss.str());
    mysql_stmt_prepare(m_Statement, selectStatement.c_str(), selectStatement.length());

    memset(m_BindParam, 0, sizeof(m_BindParam));

    m_BindParam[0].buffer_type = MYSQL_TYPE_STRING;
    m_BindParam[0].buffer = (void*)m_ID.c_str();
    m_BindParam[0].buffer_length = m_ID.length();
    mysql_stmt_bind_param(m_Statement, m_BindParam);

}

DataHandlerLoadOperationMysql::~DataHandlerLoadOperationMysql()
{
}


bool DataHandlerLoadOperationMysql::DoLoad(std::vector<char>& outData)
{    
    if(mysql_stmt_execute(m_Statement))
        return false;

    my_bool updateMaxLenFlag = true;
    mysql_stmt_attr_set(m_Statement, STMT_ATTR_UPDATE_MAX_LENGTH, &updateMaxLenFlag);
    mysql_stmt_store_result(m_Statement);

    unsigned long dataLen = 0;
    MYSQL_BIND bindResult[1];
    memset(bindResult, 0, sizeof(bindResult));
    bindResult[0].buffer_type = MYSQL_TYPE_LONG_BLOB;
    bindResult[0].length = &dataLen;    
    if(mysql_stmt_bind_result(m_Statement, bindResult))
    {
        return false;
    }

    auto fetchRes = mysql_stmt_fetch(m_Statement);
    if(fetchRes && fetchRes != MYSQL_DATA_TRUNCATED)
    {
        return false;
    }

    outData.resize(dataLen);
    bindResult[0].buffer = outData.data();
    bindResult[0].buffer_length = outData.size();
    auto fetchColRes = mysql_stmt_fetch_column(m_Statement, bindResult, 0, 0);
    return fetchColRes == 0;
}

MysqlDataOperationManager::MysqlDataOperationManager(const std::string& server, const std::string& username, const std::string& password, const std::string& dbname, std::string tablename, int port)
    : m_Connection(nullptr)
    , m_ownConnection(true)
    , m_TableName(std::move(tablename))
{
    m_Connection = mysql_init(NULL);
    if(!mysql_real_connect(m_Connection, server.c_str(), username.c_str()
        , password.c_str(), dbname.c_str(), port, NULL, 0))
    {
        LOG(FATAL) << "Connecting MySQL failed: " << mysql_error(m_Connection);
 
        mysql_close(m_Connection);
        m_Connection = nullptr;
    }

}

MysqlDataOperationManager::MysqlDataOperationManager(MYSQL* mysqlConnection, std::string tablename)
    : m_Connection(mysqlConnection)
    , m_ownConnection(false)
    , m_TableName(std::move(tablename))
 {

}

MysqlDataOperationManager::~MysqlDataOperationManager()
{
    if(m_ownConnection && m_Connection)
    {
        mysql_close(m_Connection);
        m_Connection = nullptr;
    }
}

bool MysqlDataOperationManager::IsExists(const std::string& id) const
{
    MYSQL_STMT* selectStatement = mysql_stmt_init(m_Connection);

    StatementCloser closer;
    closer.statement = selectStatement;

    stringstream ss;
    ss << "SELECT id FROM " << m_TableName << " WHERE id=?";

    std::string statementStr(ss.str());
    if(mysql_stmt_prepare(selectStatement, statementStr.c_str(), statementStr.length()))
    {
        return false;
    }
    
    MYSQL_BIND bindParam[1];
    memset(bindParam, 0, sizeof(bindParam));

    bindParam[0].buffer_type = MYSQL_TYPE_STRING;
    bindParam[0].buffer = (void*)id.c_str();
    bindParam[0].buffer_length = id.length();
    if(mysql_stmt_bind_param(selectStatement, bindParam))
    {
        return false;
    }
    if(mysql_stmt_execute(selectStatement))
    {
        return false;
    }
    mysql_stmt_store_result(selectStatement);
    auto rowCount = mysql_stmt_num_rows(selectStatement);
    return rowCount > 0;
}


std::unique_ptr<DataHandler> MysqlDataOperationManager::GetDataHandler(std::string id) const
{
    std::unique_ptr<DataHandler> result;

    if(IsExists(id))
    {
        auto mysqlOperation = new DataHandlerLoadOperationMysql(m_Connection, id, m_TableName);
        result = std::unique_ptr<DataHandler>(new DataHandler(mysqlOperation));
    }

    return result;
}

std::vector<std::string> MysqlDataOperationManager::GetDataList(std::regex regex, bool checkNotation) const
{
    std::vector<std::string> result;

    MYSQL_STMT* selectStatement = mysql_stmt_init(m_Connection);

    StatementCloser closer;
    closer.statement = selectStatement;

    stringstream ss;
    ss << "SELECT id FROM " << m_TableName;
    std::string statementStr(ss.str());
    if(mysql_stmt_prepare(selectStatement, statementStr.c_str(), statementStr.length()))
    {
        return result;
    }

    MYSQL_BIND bindResult[1];
    memset(bindResult, 0, sizeof(bindResult));

    char idBuffer[256];

    bindResult[0].buffer_type = MYSQL_TYPE_STRING;
    bindResult[0].buffer = idBuffer;
    bindResult[0].buffer_length = sizeof(idBuffer);
    if (mysql_stmt_bind_result(selectStatement, bindResult))
    {
        return result;
    }
    
    if(mysql_stmt_execute(selectStatement))
    {
        return result;
    }
    mysql_stmt_store_result(selectStatement);

    while (!mysql_stmt_fetch(selectStatement))
    {
        result.emplace_back(idBuffer);
    }

    return result;
}

std::unique_ptr<DataHandler> MysqlDataOperationManager::StoreData(std::string id, std::vector<char>&& data, bool forceOverwrite)
{
    std::unique_ptr<DataHandler> result;

    bool rowExisted = IsExists(id);
    if(rowExisted && !forceOverwrite)
    {
        //  TODO: warning file existed and not force overwrite
        return result;
    }
    MYSQL_STMT* insertStatement = mysql_stmt_init(m_Connection);
    StatementCloser closer;
    closer.statement = insertStatement;
    
    stringstream ss;
    ss << "INSERT INTO " << m_TableName << "(id, data) VALUES (?, ?)";
    std::string insertStatementStr(ss.str());
    if(mysql_stmt_prepare(insertStatement, insertStatementStr.c_str(), insertStatementStr.length()))
    {
        return result;
    }

    MYSQL_BIND bindParam[2];
    memset(bindParam, 0, sizeof(bindParam));

    auto idLen = id.length();
    bindParam[0].buffer_type = MYSQL_TYPE_STRING;
    bindParam[0].buffer = (void*)id.c_str();
    bindParam[0].buffer_length = idLen;
    bindParam[0].length = &idLen;    

    auto dataSize = data.size();
    bindParam[1].buffer_type = MYSQL_TYPE_LONG_BLOB;
    bindParam[1].buffer = data.data();
    bindParam[1].buffer_length = dataSize;
    bindParam[1].length = &dataSize;
    
    if (mysql_stmt_bind_param(insertStatement, bindParam))
    {
        return result;
    }

    // const unsigned long _maxChuckSize = 1000000;
    // unsigned long dataLen = data.size();
    // unsigned long offset = 0;
    // while(offset < dataLen)
    // {
    //     if (mysql_stmt_send_long_data(insertStatement, 1, data.data() + offset, std::min(dataLen - offset, _maxChuckSize)))
    //     {
    //         // fprintf(stderr, "\n send_long_data failed");
    //         // fprintf(stderr, "\n %s", mysql_stmt_error(insertStatement));
    //         return result;
    //     }
    //     offset += _maxChuckSize;
    // }

    if (mysql_stmt_execute(insertStatement))
    {
        LOG(ERROR) << "execute err: " << mysql_stmt_error(insertStatement) << "\n";
        return result;
    }

    auto mysqlOperation = new DataHandlerLoadOperationMysql(m_Connection, id, m_TableName);
    result = std::unique_ptr<DataHandler>(new DataHandler(mysqlOperation, std::move(data)));

    return result;
}

bool MysqlDataOperationManager::DeleteData(const std::string& id)
{
    MYSQL_STMT* deleteStatement = mysql_stmt_init(m_Connection);
    StatementCloser closer;
    closer.statement = deleteStatement;
    
    stringstream ss;
    ss << "DELETE FROM " << m_TableName << " WHERE id=?";
    std::string deleteStatementStr(ss.str());
    if(mysql_stmt_prepare(deleteStatement, deleteStatementStr.c_str(), deleteStatementStr.length()))
    {
        return false;
    }

    MYSQL_BIND bindParam[1];
    memset(bindParam, 0, sizeof(bindParam));

    auto idLen = id.length();
    bindParam[0].buffer_type = MYSQL_TYPE_STRING;
    bindParam[0].buffer = (void*)id.c_str();
    bindParam[0].buffer_length = idLen;
    bindParam[0].length = &idLen;    
    if (mysql_stmt_bind_param(deleteStatement, bindParam))
    {
        return false;
    }

    if (mysql_stmt_execute(deleteStatement))
    {
        return false;
    }
    auto affectedRow = mysql_stmt_affected_rows(deleteStatement);
    
    return affectedRow > 0;
}

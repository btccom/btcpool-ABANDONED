#include "dataoperation_mysql.h"
#include <iostream>
#include <glog/logging.h>

using namespace std;

bool IsConnectionAlive(MYSQL* connection)
{
    if(!connection)
        return false;
    return mysql_query(connection, "DO 1") == 0;
}

DataHandlerLoadOperationMysql::DataHandlerLoadOperationMysql(MYSQL* const * connection, std::string id, std::string tableName)
    : m_Connection(connection)
    , m_ID(id)
    , m_TableName(tableName)
{

}

DataHandlerLoadOperationMysql::~DataHandlerLoadOperationMysql()
{
}


bool DataHandlerLoadOperationMysql::DoLoad(std::vector<char>& outData)
{    
    if(!IsConnectionAlive(*m_Connection))
        return false;
    MYSQL_STMT* statement = mysql_stmt_init(*m_Connection);
    StatementCloser statementCloser(statement);

    stringstream ss;
    ss << "SELECT data FROM " << m_TableName << " WHERE id=?";
    string selectStatement(ss.str());
    mysql_stmt_prepare(statement, selectStatement.c_str(), selectStatement.length());

    memset(m_BindParam, 0, sizeof(m_BindParam));

    m_BindParam[0].buffer_type = MYSQL_TYPE_STRING;
    m_BindParam[0].buffer = (void*)m_ID.c_str();
    m_BindParam[0].buffer_length = m_ID.length();
    mysql_stmt_bind_param(statement, m_BindParam);

    if(mysql_stmt_execute(statement))
        return false;

    my_bool updateMaxLenFlag = true;
    mysql_stmt_attr_set(statement, STMT_ATTR_UPDATE_MAX_LENGTH, &updateMaxLenFlag);
    mysql_stmt_store_result(statement);

    unsigned long dataLen = 0;
    MYSQL_BIND bindResult[1];
    memset(bindResult, 0, sizeof(bindResult));
    bindResult[0].buffer_type = MYSQL_TYPE_LONG_BLOB;
    bindResult[0].length = &dataLen;    
    if(mysql_stmt_bind_result(statement, bindResult))
    {
        return false;
    }

    auto fetchRes = mysql_stmt_fetch(statement);
    if(fetchRes && fetchRes != MYSQL_DATA_TRUNCATED)
    {
        return false;
    }

    outData.resize(dataLen);
    bindResult[0].buffer = outData.data();
    bindResult[0].buffer_length = outData.size();
    auto fetchColRes = mysql_stmt_fetch_column(statement, bindResult, 0, 0);
    return fetchColRes == 0;
}

MysqlDataOperationManager::MysqlDataOperationManager(std::string server, std::string username, std::string password, std::string dbname, std::string tablename, int port)
    : m_Server(std::move(server))
    , m_Username(std::move(username))
    , m_Password(std::move(password))
    , m_Database(std::move(dbname))
    , m_TableName(std::move(tablename))
    , m_Port(port)
    , m_Connection(nullptr)
{
    InitConnection();
}

MysqlDataOperationManager::~MysqlDataOperationManager()
{
    if(m_Connection)
    {
        mysql_close(m_Connection);
        m_Connection = nullptr;
    }
}

bool MysqlDataOperationManager::InitConnection()
{
    m_Connection = mysql_init(NULL);
    if(!mysql_real_connect(m_Connection, m_Server.c_str(), m_Username.c_str()
        , m_Password.c_str(), m_Database.c_str(), m_Port, NULL, 0))
    {
        LOG(FATAL) << "Connecting MySQL failed: " << mysql_error(m_Connection);
 
        mysql_close(m_Connection);
        m_Connection = nullptr;
        return false;
    }
    return true;
}

bool MysqlDataOperationManager::ValidateConnection()
{
    if(!IsConnectionAlive(m_Connection))
    {
        LOG(WARNING) << "Lost mysql connection. Retry connection\n";
        if(!InitConnection())
        {
            LOG(ERROR) << "Retry mysql connection failed\n";
            return false;
        }
    }
    return true;
}

bool MysqlDataOperationManager::IsExists(const std::string& id) const
{
    MYSQL_STMT* selectStatement = mysql_stmt_init(m_Connection);
    StatementCloser closer(selectStatement);

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

    if(IsConnectionAlive(m_Connection) && IsExists(id))
    {
        auto mysqlOperation = new DataHandlerLoadOperationMysql(&m_Connection, id, m_TableName);
        result = std::unique_ptr<DataHandler>(new DataHandler(mysqlOperation));
    }

    return result;
}

bool MysqlDataOperationManager::GetDataList(std::vector<std::string>& out, std::regex regex, bool checkNotation)
{
    if(!ValidateConnection())
        return false;
    std::vector<std::string> result;

    MYSQL_STMT* selectStatement = mysql_stmt_init(m_Connection);
    if(!selectStatement)
    {
        LOG(ERROR) << "Init prepare statement error: " << mysql_stmt_error(selectStatement) << "\n";
        return false;
    }

    StatementCloser closer(selectStatement);

    stringstream ss;
    ss << "SELECT id FROM " << m_TableName;
    std::string statementStr(ss.str());
    if(mysql_stmt_prepare(selectStatement, statementStr.c_str(), statementStr.length()))
    {
        LOG(ERROR) << "mysql prepare statement err: " << mysql_stmt_error(selectStatement) << ". SQL: " << statementStr.c_str() << "\n";
        return false;
    }

    MYSQL_BIND bindResult[1];
    memset(bindResult, 0, sizeof(bindResult));

    char idBuffer[256];

    bindResult[0].buffer_type = MYSQL_TYPE_STRING;
    bindResult[0].buffer = idBuffer;
    bindResult[0].buffer_length = sizeof(idBuffer);
    if (mysql_stmt_bind_result(selectStatement, bindResult))
    {
        LOG(ERROR) << "mysql bind statement err: " << mysql_stmt_error(selectStatement) << ". SQL: " << statementStr.c_str() << "\n";
        return false;
    }
    
    if(mysql_stmt_execute(selectStatement))
    {
        LOG(ERROR) << "mysql execute err: " << mysql_stmt_error(selectStatement) << ". SQL: " << statementStr.c_str() << "\n";
        return false;
    }
    mysql_stmt_store_result(selectStatement);

    while (!mysql_stmt_fetch(selectStatement))
    {
        result.emplace_back(idBuffer);
    }
    out = result;
    return true;
}

std::unique_ptr<DataHandler> MysqlDataOperationManager::StoreData(std::string id, std::vector<char>&& data, bool forceOverwrite)
{
    std::unique_ptr<DataHandler> result;
    if(!ValidateConnection())
        return result;

    bool rowExisted = IsExists(id);
    if(rowExisted && !forceOverwrite)
    {
        //  TODO: warning file existed and not force overwrite
        LOG(WARNING) << "data id " << id.c_str() << " already exist. mysql store data failed\n";
        return result;
    }
    MYSQL_STMT* insertStatement = mysql_stmt_init(m_Connection);
    StatementCloser closer(insertStatement);
    
    stringstream ss;
    ss << "INSERT INTO " << m_TableName << "(id, data) VALUES (?, ?)";
    std::string insertStatementStr(ss.str());
    if(mysql_stmt_prepare(insertStatement, insertStatementStr.c_str(), insertStatementStr.length()))
    {
        LOG(ERROR) << "mysql prepare statement err: " << mysql_stmt_error(insertStatement) << ". SQL: " << insertStatementStr.c_str() << "\n";
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
        LOG(ERROR) << "mysql bind statement err: " << mysql_stmt_error(insertStatement) << ". SQL: " << insertStatementStr.c_str() << "\n";
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
        LOG(ERROR) << "execute err: " << mysql_stmt_error(insertStatement) << ". SQL: " << insertStatementStr.c_str() << "\n";
        return result;
    }

    auto mysqlOperation = new DataHandlerLoadOperationMysql(&m_Connection, id, m_TableName);
    result = std::unique_ptr<DataHandler>(new DataHandler(mysqlOperation, std::move(data)));

    return result;
}

bool MysqlDataOperationManager::DeleteData(const std::string& id)
{
    if(!ValidateConnection())
        return false;
    
    MYSQL_STMT* deleteStatement = mysql_stmt_init(m_Connection);
    StatementCloser closer(deleteStatement);
    
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

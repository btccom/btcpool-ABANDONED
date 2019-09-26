#if 0   //  set this code to a valid mysql server connection first before activating it.

#include "gtest/gtest.h"
#include "gbtsync/gbtsync.h"
#include "testdataoperationmanager_common.h"

const char* server = "localhost";
const char* user = "root";
const char* password = "Hello@123";
const char* database = "testing";


TEST(MySQLDataOperationManager, GetList)
{
    MysqlDataOperationManager manager(server, user, password, database, "filedatatest");

    manager.DeleteData("someid");
    std::string valueStr = "some string to insert";
    std::vector<char> value(valueStr.begin(), valueStr.end());
    auto loader = manager.StoreData("someid", std::move(value));
    ASSERT_TRUE(loader != nullptr);
    EXPECT_TRUE(loader->Load());
    std::vector<char> expectedValue(valueStr.begin(), valueStr.end());
    EXPECT_TRUE(loader->Data() == expectedValue);
    expectedValue.push_back('a');
    EXPECT_FALSE(loader->Data() == expectedValue);

    std::vector<std::string> dataList;
    ASSERT_TRUE(manager.GetDataList(dataList));
    ASSERT_EQ(dataList.size(), 1u);

    manager.DeleteData("someid");
}

//  This is to prepare data based on common test cases (see testdataoperationmanager_common.cpp)
void InitTestData()
{
    MysqlDataOperationManager manager(server, user, password, database, "filedatatest");
    ASSERT_NE(manager.StoreData("correct.empty.txt", std::vector<char>()), nullptr);    
    std::string correct_number_txt_str = "12345566";
    ASSERT_NE(manager.StoreData("correct.number.txt", std::vector<char>(correct_number_txt_str.begin(), correct_number_txt_str.end())), nullptr);    
    std::string correct_txt_str = "somestringcontent";
    ASSERT_NE(manager.StoreData("correct.txt", std::vector<char>(correct_txt_str.begin(), correct_txt_str.end())), nullptr);    
}

void RemoveTestData()
{
    MysqlDataOperationManager manager(server, user, password, database, "filedatatest");
    EXPECT_TRUE(manager.DeleteData("correct.txt"));
    EXPECT_TRUE(manager.DeleteData("correct.number.txt"));
    EXPECT_TRUE(manager.DeleteData("correct.empty.txt"));
}


TEST(MySQLDataOperationManager, Ready) 
{
    InitTestData();

    MysqlDataOperationManager manager(server, user, password, database, "filedatatest");
    CheckReadyToLoad(manager);

    RemoveTestData();
}

TEST(MySQLDataOperationManager, Content) 
{
    InitTestData();

    MysqlDataOperationManager manager(server, user, password, database, "filedatatest");
    CheckContents(manager);

    RemoveTestData();
}

TEST(MySQLDataOperationManager, SaveDelete) 
{
    InitTestData();

    MysqlDataOperationManager manager(server, user, password, database, "filedatatest");
    CheckSaveDelete(manager);

    RemoveTestData();
}
#endif
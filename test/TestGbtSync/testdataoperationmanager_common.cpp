#include "testdataoperationmanager_common.h"

void CheckReadyToLoad(DataOperationManagerBase& manager)
{
    EXPECT_NE(manager.GetDataHandler("correct.txt"), nullptr);
    EXPECT_NE(manager.GetDataHandler("correct.empty.txt"), nullptr);
    EXPECT_NE(manager.GetDataHandler("correct.number.txt"), nullptr);
    EXPECT_EQ(manager.GetDataHandler("wrong.txt"), nullptr);
    EXPECT_EQ(manager.GetDataHandler("notexist.txt"), nullptr);
}

void CheckContent(DataOperationManagerBase& manager, const std::string& id, const std::vector<char>& expectedValue)
{
    auto loader = manager.GetDataHandler(id);
    ASSERT_TRUE(loader != nullptr);
    EXPECT_FALSE(loader->IsLoaded());
    loader->Load();
    EXPECT_TRUE(loader->IsLoaded());
    EXPECT_TRUE(loader->Data() == expectedValue);
    std::vector<char> unexpectedValue = expectedValue;
    unexpectedValue.push_back('1');
    EXPECT_FALSE(loader->Data() == unexpectedValue);
    loader->Unload();
    EXPECT_FALSE(loader->IsLoaded());
}

void CheckContent(DataOperationManagerBase& manager, const std::string& id, const std::string& expectedValueStr)
{
    std::vector<char> expectedValue(expectedValueStr.begin(), expectedValueStr.end());
    CheckContent(manager, id, expectedValue);
}

void CheckContents(DataOperationManagerBase& manager)
{
    CheckContent(manager, "correct.txt", "somestringcontent");
    CheckContent(manager, "correct.empty.txt", "");
    CheckContent(manager, "correct.number.txt", "12345566");
}

void CheckSaveDelete(DataOperationManagerBase& manager)
{
    std::string dataStr = "some input to save";
    {
        //  make sure no file.
        manager.DeleteData("temp.txt");
        std::vector<char> data(dataStr.begin(), dataStr.end());
        auto loader = manager.StoreData("temp.txt", std::move(data), false);
        ASSERT_TRUE(loader != nullptr);
        EXPECT_TRUE(data.empty());
        EXPECT_TRUE(loader->IsLoaded());
        CheckContent(manager, "temp.txt", dataStr);
        EXPECT_TRUE(manager.DeleteData("temp.txt"));
    }
}

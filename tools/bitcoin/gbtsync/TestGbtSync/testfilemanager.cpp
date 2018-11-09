#include "gtest/gtest.h"

#include "gbtsync/gbtsync.h"
#include <fstream>
#include <cstdio>

TEST(DataManager, Pool) 
{
    //  cleanup before start testing
    std::remove("testdata/filemanagertestdata/temp.empty.txt");

    auto fileOperationManager = new FileDataOperationManager("testdata/filemanagertestdata/", {'G', 'B', 'T'}, {'G', 'B', 'T'}, "");
    DataManager manager(fileOperationManager);
    {
        auto diffResult = manager.DiffDataHandles();
        EXPECT_EQ(diffResult.first.size(), 2u);
        EXPECT_EQ(diffResult.second.size(), 0u);
    }
    {
        auto iter = manager.GetDataHandlers().find("correct.empty.txt");
        EXPECT_TRUE(iter != manager.GetDataHandlers().end());
    }
    {
        auto iter = manager.GetDataHandlers().find("correct.1.txt");
        EXPECT_TRUE(iter != manager.GetDataHandlers().end());
    }
    {
        std::ofstream ofile("testdata/filemanagertestdata/temp.empty.txt");
        ofile << "GBTGBT";
        ofile.close();
        {
            auto diffResult = manager.DiffDataHandles();
            EXPECT_EQ(diffResult.second.size(), 0u);

            auto& newFiles = diffResult.first;
            ASSERT_EQ(newFiles.size(), 1u);
            EXPECT_EQ(newFiles[0], "temp.empty.txt");
            auto iter = manager.GetDataHandlers().find("temp.empty.txt");
            EXPECT_TRUE(iter != manager.GetDataHandlers().end());
        }
        {
            std::remove("testdata/filemanagertestdata/temp.empty.txt");
            auto diffResult = manager.DiffDataHandles();
            EXPECT_EQ(diffResult.second.size(), 1u);
            EXPECT_EQ(diffResult.first.size(), 0u);
            auto iter = manager.GetDataHandlers().find("temp.empty.txt");
            EXPECT_TRUE(iter == manager.GetDataHandlers().end());
        }
    }    
}

TEST(DataManager, Data)
{
    auto fileOperationManager = new FileDataOperationManager("testdata/filemanagertestdata/", {'G', 'B', 'T'}, {'G', 'B', 'T'}, "");
    DataManager manager(fileOperationManager);
    auto newFiles = manager.DiffDataHandles();
    {
        const auto& loader = manager.GetDataHandlers().at("correct.1.txt");
        std::string compareStr = "[somestring]";
        EXPECT_TRUE(loader->GetData() == std::vector<char>(compareStr.begin(), compareStr.end()));
    }    
}
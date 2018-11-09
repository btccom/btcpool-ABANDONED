#include "gtest/gtest.h"

#include "gbtsync/gbtsync.h"
#include <algorithm>

TEST(FileLister, _1File) 
{
    FileDataOperationManager lister("testdata/filelistertestdata/dir1", {}, {}, "");    
    std::vector<std::string> files;
    ASSERT_TRUE(lister.GetDataList(files));
    //  check filenames
    EXPECT_NE(std::find(files.begin(), files.end(), "file1.txt"), files.end());
    //  check not exist
    EXPECT_EQ(std::find(files.begin(), files.end(), "file2.txt"), files.end());
}

TEST(FileLister, _2File) 
{
    FileDataOperationManager lister("testdata/filelistertestdata/dir2", {}, {}, "");    
    std::vector<std::string> files;
    ASSERT_TRUE(lister.GetDataList(files));
    EXPECT_EQ(files.size(), 2u);
    //  check filenames
    EXPECT_NE(std::find(files.begin(), files.end(), "file1.txt"), files.end());
    EXPECT_NE(std::find(files.begin(), files.end(), "file2.txt"), files.end());
    //  check not exist
    EXPECT_EQ(std::find(files.begin(), files.end(), "file3.txt"), files.end());
}

TEST(FileLister, _3File) 
{
    FileDataOperationManager lister("testdata/filelistertestdata/dir3", {}, {}, "");    
    std::vector<std::string> files;
    ASSERT_TRUE(lister.GetDataList(files));
    EXPECT_EQ(files.size(), 3u);
    //  check filenames
    EXPECT_NE(std::find(files.begin(), files.end(), "file1.txt"), files.end());
    EXPECT_NE(std::find(files.begin(), files.end(), "file2.txt"), files.end());
    EXPECT_NE(std::find(files.begin(), files.end(), "file3.txt"), files.end());
    //  check not exist
    EXPECT_EQ(std::find(files.begin(), files.end(), "file4.txt"), files.end());
}

TEST(FileLister, RegexDigitOnly) 
{
    FileDataOperationManager lister("testdata/filelistertestdata/regex", {}, {}, "");    
    std::vector<std::string> files;
    ASSERT_TRUE(lister.GetDataList(files, std::regex("(\\d+).txt")));
    ASSERT_EQ(files.size(), 2u);
    //  check filenames
    EXPECT_NE(std::find(files.begin(), files.end(), "12345.txt"), files.end());
    EXPECT_NE(std::find(files.begin(), files.end(), "112233.txt"), files.end());
}

TEST(FileLister, RegexNonDigitOnly) 
{
    FileDataOperationManager lister("testdata/filelistertestdata/regex", {}, {}, "");    
    std::vector<std::string> files;
    ASSERT_TRUE(lister.GetDataList(files, std::regex("(\\D+).txt")));
    ASSERT_EQ(files.size(), 1u);
    //  check filenames
    EXPECT_NE(std::find(files.begin(), files.end(), "aabbcc.txt"), files.end());
}

TEST(FileLister, RegexContainBothOnly) 
{
    FileDataOperationManager lister("testdata/filelistertestdata/regex", {}, {}, "");    
    std::vector<std::string> files;
    ASSERT_TRUE(lister.GetDataList(files, std::regex("(\\D+)(\\d+).txt")));
    ASSERT_EQ(files.size(), 2u);
    //  check filenames
    EXPECT_NE(std::find(files.begin(), files.end(), "file1.txt"), files.end());
    EXPECT_NE(std::find(files.begin(), files.end(), "somefile1.txt"), files.end());
}
    
TEST(FileLister, NoDir) 
{
    FileDataOperationManager lister("testdata/filelistertestdata/notexist", {}, {}, "");    
    std::vector<std::string> files;
    ASSERT_TRUE(lister.GetDataList(files));
    EXPECT_EQ(files.size(), 0u);
}
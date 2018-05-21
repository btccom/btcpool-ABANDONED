#include "gtest/gtest.h"
#include "gbtsync/dataoperation_file.h"
#include "testdataoperationmanager_common.h"

FileDataOperationManager Get_ABC_XYZ_OperationManager()
{
    return FileDataOperationManager("testdata/filedataoperationmanagertestdata/ABC_XYZ/", {'A', 'B', 'C'}, {'X', 'Y', 'Z'}, "");
}

FileDataOperationManager Get_ABC_XYZ_OperationManager_With_Trash()
{
    return FileDataOperationManager("testdata/filedataoperationmanagertestdata/ABC_XYZ/", {'A', 'B', 'C'}, {'X', 'Y', 'Z'}, "testdata/filedataoperationmanagertestdata/ABC_XYZ/trash/");
}


FileDataOperationManager Get_metadata_eof_OperationManager()
{
    return FileDataOperationManager("testdata/filedataoperationmanagertestdata/metadata_eof/", {'m', 'e', 't', 'a', 'd', 'a', 't', 'a'}, {'e', 'o', 'f'}, "");
}

TEST(FileDataOperationManager, ABC_XYZ_Ready) 
{
    FileDataOperationManager manager = Get_ABC_XYZ_OperationManager();
    CheckReadyToLoad(manager);
}

TEST(FileDataOperationManager, ABC_XYZ_Content) 
{
    FileDataOperationManager manager = Get_ABC_XYZ_OperationManager();
    CheckContents(manager);
}

TEST(FileDataOperationManager, ABC_XYZ_Save_Delete) 
{
    FileDataOperationManager manager = Get_ABC_XYZ_OperationManager();
    CheckSaveDelete(manager);
}

TEST(FileDataOperationManager, metadata_eof_Ready) 
{
    FileDataOperationManager manager = Get_metadata_eof_OperationManager();
    CheckReadyToLoad(manager);
}


TEST(FileDataOperationManager, metadata_eof_Content) 
{
    FileDataOperationManager manager = Get_metadata_eof_OperationManager();
    CheckContents(manager);
}


TEST(FileDataOperationManager, metadata_eof_Save_Delete) 
{
    FileDataOperationManager manager = Get_metadata_eof_OperationManager();
    CheckSaveDelete(manager);
}

TEST(FileDataOperationManager, ABC_XYZ_With_Trash_SaveDelete) 
{
    FileDataOperationManager manager = Get_ABC_XYZ_OperationManager_With_Trash();
    CheckSaveDelete(manager);
    FileDataOperationManager lister(manager.GetTrashPath(), manager.GetFilePrefix(), manager.GetFilePostfix(), "");
    std::vector<std::string> files;
    ASSERT_TRUE(lister.GetDataList(files));
    EXPECT_NE(std::find(files.begin(), files.end(), "temp.txt"), files.end());
    lister.DeleteData("temp.txt");
    ASSERT_TRUE(lister.GetDataList(files));
    EXPECT_EQ(std::find(files.begin(), files.end(), "temp.txt"), files.end());
}
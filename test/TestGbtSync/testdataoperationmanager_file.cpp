#include "gtest/gtest.h"
#include "gbtsync/dataoperation_file.h"
#include "testdataoperationmanager_common.h"

FileDataOperationManager Get_ABC_XYZ_OperationManager()
{
    return FileDataOperationManager("testdata/filedataoperationmanagertestdata/ABC_XYZ/", {'A', 'B', 'C'}, {'X', 'Y', 'Z'});
}

FileDataOperationManager Get_metadata_eof_OperationManager()
{
    return FileDataOperationManager("testdata/filedataoperationmanagertestdata/metadata_eof/", {'m', 'e', 't', 'a', 'd', 'a', 't', 'a'}, {'e', 'o', 'f'});
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
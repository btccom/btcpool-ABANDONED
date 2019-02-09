#ifndef _TEST_DATA_OPERATION_MANAGER_COMMON_H_
#define _TEST_DATA_OPERATION_MANAGER_COMMON_H_

#include "gtest/gtest.h"
#include "gbtsync/gbtsync.h"

void CheckReadyToLoad(DataOperationManagerBase &manager);
void CheckContent(
    DataOperationManagerBase &manager,
    const std::string &id,
    const std::vector<char> &expectedValue);
void CheckContent(
    DataOperationManagerBase &manager,
    const std::string &id,
    const std::string &expectedValueStr);
void CheckContents(DataOperationManagerBase &manager);
void CheckSaveDelete(DataOperationManagerBase &manager);

#endif // _TEST_DATA_OPERATION_MANAGER_COMMON_H_
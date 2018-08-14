#include "datamanager.h"
#include "dataoperation_base.h"

#include <assert.h>
#include <cstdio>
#include <unordered_set>
#include <vector>

using namespace std;

DataManager::DataManager(DataOperationManagerBase* operationManager)
    : m_FileOperationManager(operationManager)
    , m_syncDelete(true)
{
}

bool DataManager::AddData(std::string id, std::vector<char>&& data)
{
    if(m_DataHandlers.count(id))
        return false;   //  loader already exists
    auto fileDataLoader = m_FileOperationManager->StoreData(id, std::move(data), false);
    if(fileDataLoader == nullptr)
    {
        return false;   //  file already exists, but loader is not
    }
    m_DataHandlers[id] = std::move(fileDataLoader);
    return true;
}

bool DataManager::RemoveData(const std::string& id)
{
    if(m_FileOperationManager->DeleteData(id))
    {
        m_DataHandlers.erase(id);
        return true;
    }
    return false;
}

DataManager::AddAndRemoveDataListPair DataManager::DiffDataHandles(bool updateCache)
{
    AddAndRemoveDataListPair dataListPair;

    std::vector<std::string>& newFiles = dataListPair.first;
    std::vector<std::string>& removedFiles = dataListPair.second;

    std::vector<std::string> fileList;
    if(!m_FileOperationManager->GetDataList(fileList))
    {
        return dataListPair;
    }
    unordered_set<string> fileSet(fileList.begin(), fileList.end());
    for(auto& filePair : m_DataHandlers)
    {
        auto& filename = filePair.first;
        if(fileSet.count(filename) == 0)
        {
            removedFiles.push_back(filename);
        }
    }
    if(updateCache)
    {
        for(auto& filename : removedFiles)
        {
            m_DataHandlers.erase(filename);            
        }
    }

    for(auto& filename : fileList)
    {
        if(m_DataHandlers.count(filename) == 0)
        {
            auto fileDataLoader = m_FileOperationManager->GetDataHandler(filename);
            if(fileDataLoader != nullptr)
            {
                if(updateCache)
                    m_DataHandlers[filename] = std::move(fileDataLoader);
                newFiles.push_back(filename);
            }
        }
    }
    return dataListPair;
}

void DataManager::ClearLoadedData()
{
    for(auto& h : m_DataHandlers)
    {
        h.second->Unload();
    }
}

#include "dataoperation_base.h"


DataHandler::DataHandler(DataHandlerLoadOperationBase* loadOperation)
    : m_LoadOperation(loadOperation)
    , m_Loaded(false)
{

}

DataHandler::DataHandler(DataHandlerLoadOperationBase* loadOperation, std::vector<char>&& data)
    : m_LoadOperation(loadOperation)
    , m_Data(std::move(data))
    , m_Loaded(true)
{
}

bool DataHandler::Load()
{
    if(!m_Loaded)
    {
        if(!m_LoadOperation->DoLoad(m_Data))
        {
            return false;
        }
        m_Loaded = true;
    }
    return true;
}

void DataHandler::Unload()
{
    m_Data.clear();
    m_Data.shrink_to_fit();
    m_Loaded = false;
}

bool DataHandler::IsLoaded() const
{
    return m_Loaded;
}

#ifndef _FILE_OPERATIONS_H_
#define _FILE_OPERATIONS_H_

#include "dataoperation_base.h"

class DataHandlerLoadOperationFile : public DataHandlerLoadOperationBase
{
public:
    bool DoLoad(std::vector<char>& outData) override;
    const std::string& GetFilename() const
    {
        return m_Filename;
    }
    std::string Id() const override
    {
        return GetFilename();
    }

private:
    friend class FileDataOperationManager;
    DataHandlerLoadOperationFile(std::string filename, int startOffset, int dataSize);

    std::string m_Filename;
    int m_StartOffset;
    int m_DataSize;
};

class FileDataOperationManager : public DataOperationManagerBase
{
public:

    FileDataOperationManager(std::string path, std::vector<char> filePrefix, std::vector<char> filePosfix);

    std::unique_ptr<DataHandler> GetDataHandler(std::string id) const override;
    std::vector<std::string> GetDataList(std::regex regex = std::regex(".*"), bool checkNotation = false) const override;
    std::unique_ptr<DataHandler> StoreData(std::string id, std::vector<char>&& data, bool forceOverwrite = false) override;
    bool DeleteData(const std::string& id) override;

    int GetFileDataSize(const std::string& filename) const;
    bool IsFileReadyToLoad(const std::string& filename) const;

private:
    std::vector<char> m_FilePrefix;
    std::vector<char> m_FilePostfix;
    std::string m_DirPath;
};

#endif // _FILE_OPERATIONS_H_
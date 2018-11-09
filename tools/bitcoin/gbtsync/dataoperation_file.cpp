#include "dataoperation_file.h"
#include <fstream>
#include <dirent.h>
#include <stdio.h>
#include <glog/logging.h>

using namespace std;

DataHandlerLoadOperationFile::DataHandlerLoadOperationFile(std::string filename, int startOffset, int dataSize)
    : m_Filename(std::move(filename))
    , m_StartOffset(startOffset)
    , m_DataSize(dataSize)
{
    
}

bool DataHandlerLoadOperationFile::DoLoad(std::vector<char>& outData)
{
    ifstream ifile(m_Filename.c_str());
    if(!ifile.is_open())
        return false;
    outData.resize(m_DataSize);
    ifile.seekg(m_StartOffset, ifile.beg);
    ifile.read(outData.data(), m_DataSize);
    return true;
}



FileDataOperationManager::FileDataOperationManager(std::string path, std::vector<char> filePrefix, std::vector<char> filePosfix, std::string trashPath)
    : m_FilePrefix(std::move(filePrefix))
    , m_FilePostfix(std::move(filePosfix))
    , m_DirPath(std::move(path))
    , m_DirTrashPath(std::move(trashPath))
{
    auto createDirIfNotExist = [](const std::string& path)
    {
        DIR *dir;
        if ((dir = opendir(path.c_str())) == NULL)
        {
            if(-1 == system((std::string("mkdir -p ") + path).c_str()))
            {
                LOG(ERROR) << "Dir " << path.c_str() << " not exist. Error creating directory!\n";
            }
            else
            {
                LOG(WARNING) << "Dir " << path.c_str() << " not exist. Directory created!\n";            
            }
        }
    };
    createDirIfNotExist(m_DirPath);
    if(!m_DirTrashPath.empty())
    {
        createDirIfNotExist(m_DirTrashPath);
    }
}

bool FileDataOperationManager::GetDataList(std::vector<std::string>& out, std::regex regex, bool checkNotation)
{
    vector<string> files;
    DIR *dir;
    struct dirent *ent;
    if ((dir = opendir(m_DirPath.c_str())) != NULL)
    {
        while ((ent = readdir(dir)) != NULL)
        {
            if(ent->d_type == DT_REG)
            {
                if(regex_match(ent->d_name, regex))
                {
                    files.emplace_back(ent->d_name);
                }
            }
        }
        closedir(dir);
    }
    else
    {
        //  TODO: add error/warning message here.
    }

    if(checkNotation)
    {
        for(unsigned int i = 0; i < files.size(); ++i)
        {
            auto& file = files[i];
            while(!IsFileReadyToLoad(file) && i < files.size())
            {
                files[i] = files.back();
                files.resize(files.size() - 1);
            }
        }    
    }
    out = std::move(files);
    return true;
}


std::unique_ptr<DataHandler> FileDataOperationManager::GetDataHandler(std::string id) const
{
    unique_ptr<DataHandler> fileDataResult;
    std::string& filename = id;
    int dataSize = GetFileDataSize(filename);
    if(dataSize >= 0)
    {
        auto fileOperation = new DataHandlerLoadOperationFile(m_DirPath + filename, m_FilePrefix.size(), dataSize);
        fileDataResult = std::unique_ptr<DataHandler>(new DataHandler(fileOperation));
    }
    return fileDataResult;
}

int FileDataOperationManager::GetFileDataSize(const std::string& filename) const
{
    ifstream file((m_DirPath + filename).c_str());
    if(!file.is_open())
        return -1;

    const int prefixSize = m_FilePrefix.size();
    const int postfixSize = m_FilePostfix.size();
    int minFileSize = prefixSize + postfixSize;

    int dataSize = 0;
    if(minFileSize > 0)
    {
        file.seekg(0, file.end);
        const int fileSize = (int)file.tellg();
        if(minFileSize > fileSize)
            return -1;
        dataSize = fileSize - prefixSize - postfixSize;
    }

    if(prefixSize > 0)
    {
        std::vector<char> buffer(prefixSize);
        file.seekg(0, file.beg);
        file.read(buffer.data(), prefixSize);
        if(buffer != m_FilePrefix)
            return -1;
    }

    if(postfixSize > 0)
    {
        std::vector<char> buffer(postfixSize);
        file.seekg(-postfixSize, file.end);
        file.read(buffer.data(), postfixSize);
        if(buffer != m_FilePostfix)
            return -1;        
    }
    return dataSize;
}

bool FileDataOperationManager::IsFileReadyToLoad(const std::string& filename) const
{
    //  GetFileDataSize will add the path
    return GetFileDataSize(filename) >= 0;
}

std::unique_ptr<DataHandler> FileDataOperationManager::StoreData(std::string id, std::vector<char>&& data, bool forceOverwrite)
{
    std::string filename = m_DirPath + id;
    std::unique_ptr<DataHandler> result;
    bool fileExisted = false;
    {
        ifstream ifile(filename.c_str());
        fileExisted = ifile.is_open();
    }

    if(fileExisted && !forceOverwrite)
    {
        //  TODO: warning file existed and not force overwrite
        return result;
    }
    ofstream ofile(filename.c_str());
    if(!ofile.is_open())
    {
        //  TODO: error cannot write to file
        return result;
    }
    ofile.write(m_FilePrefix.data(), m_FilePrefix.size());
    ofile.write(data.data(), data.size());
    ofile.write(m_FilePostfix.data(), m_FilePostfix.size());
    ofile.flush();
    auto fileOperation = new DataHandlerLoadOperationFile(std::move(filename), m_FilePrefix.size(), data.size());    
    result = std::unique_ptr<DataHandler>(new DataHandler(fileOperation, std::move(data)));
    if(fileExisted)
    {
        //  TODO: warn that existing file is overwritten
    }
    return result;
}

bool FileDataOperationManager::DeleteData(const std::string& id)
{
    std::string fullPath = m_DirPath + id;
    if(m_DirTrashPath.empty())
    {
        return std::remove(fullPath.c_str()) == 0;
    }
    std::string fullTrashPath = m_DirTrashPath + id;
    return rename(fullPath.c_str(), fullTrashPath.c_str()) != -1;
}

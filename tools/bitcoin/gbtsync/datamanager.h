#ifndef _DATA_MANAGER_H_
#define _DATA_MANAGER_H_

#include <string>
#include <unordered_map>
#include <vector>
#include <memory>

class DataHandler;
class DataOperationManagerBase;

class DataManager {
public:
  using AddAndRemoveDataListPair =
      std::pair<std::vector<std::string>, std::vector<std::string>>;

  DataManager(DataOperationManagerBase *operationManager);
  //  store data persistently and keep info in the cache
  bool AddData(std::string id, std::vector<char> &&data);
  //  remove data from persistent storage and cache
  bool RemoveData(const std::string &id);

  //  list files from drive
  //  return new detected filenames
  AddAndRemoveDataListPair DiffDataHandles(bool updateCache = true);

  void SetName(std::string name) { m_Name = std::move(name); }

  const std::string &GetName() const { return m_Name; }

  const std::unordered_map<std::string, std::shared_ptr<DataHandler>> &
  GetDataHandlers() const {
    return m_DataHandlers;
  }

  std::shared_ptr<DataHandler> GetDataHandler(const std::string &id) const {
    auto iter = m_DataHandlers.find(id);
    if (iter != m_DataHandlers.end()) {
      return iter->second;
    }
    return std::shared_ptr<DataHandler>();
  }

  void EnableSyncDelete(bool b) { m_syncDelete = b; }

  bool IsSyncDelete() const { return m_syncDelete; }

  void ClearLoadedData();

private:
  std::unique_ptr<DataOperationManagerBase> m_FileOperationManager;
  std::unordered_map<std::string, std::shared_ptr<DataHandler>> m_DataHandlers;
  std::string m_Name;
  bool m_syncDelete;
};

#endif // _DATA_MANAGER_H_
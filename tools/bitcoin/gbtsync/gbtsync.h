#ifndef _GBTSYNC_H_
#define _GBTSYNC_H_

#include "dataoperation_file.h"
#include "dataoperation_mysql.h"
#include "datamanager.h"

#include <thread>
#include <chrono>
#include <iostream>
#include <vector>

#include <libconfig.h++>

using namespace std;

class SyncWorker {
public:
  void DoDiffs();

  std::unique_ptr<DataManager> dataManager;
  std::chrono::duration<double> diffPeriod;

  DataManager::AddAndRemoveDataListPair diffResult;
  std::chrono::time_point<std::chrono::system_clock> lastEndDiff;
};

class SyncManager {
public:
  SyncManager();
  void Run();
  void
  AddWorker(DataManager *dataManager, std::chrono::duration<double> diffPeriod);
  void Stop();

private:
  void DoInit();
  void DoDiffs();
  void DoSync();
  void Sync(
      DataManager::AddAndRemoveDataListPair &diffResult,
      DataManager &sourceManager,
      DataManager &destManager,
      bool giveupData);

private:
  std::vector<std::unique_ptr<SyncWorker>> m_Workers;
  bool m_KeepRun;
};

#endif
#include "gbtsync/gbtsync.h"
#include <unistd.h>
#include <libconfig.h++>
#include <glog/logging.h>
#include <signal.h>
#include <boost/interprocess/sync/file_lock.hpp>

using namespace libconfig;

SyncManager gSyncManager;

void handler(int sig) 
{
    gSyncManager.Stop();
}

void usage() {
  fprintf(stderr, "Usage:\n\tgbtsync -c \"gbtsync.cfg\" -l \"log_dir\"\n");
}

DataManager* CreateFileDataManagerFromSettings(const libconfig::Setting& setting)
{
    std::string path;
    std::string trashPath;
    std::string prefix, postfix;
    if(!setting.lookupValue("path", path))
    {
        return nullptr;
    }
    if(path == "")
    {
        return nullptr;
    }
    setting.lookupValue("prefix", prefix);
    setting.lookupValue("postfix", postfix);
    setting.lookupValue("trashpath", trashPath);

    auto fileOperationManager = new FileDataOperationManager(path
            , std::vector<char>(prefix.begin(), prefix.end())
            , std::vector<char>(postfix.begin(), postfix.end()), trashPath);
    return new DataManager(fileOperationManager);
}

DataManager* CreateMySQLDataManagerFromSettings(const libconfig::Setting& setting)
{
    std::string server;
    std::string username, password;
    std::string dbschema;
    std::string tablename = "filedata";
    int port = 0;
    if(!setting.lookupValue("server", server))
    {
        return nullptr;
    }
    if(!setting.lookupValue("username", username))
    {
        return nullptr;
    }
    if(!setting.lookupValue("password", password))
    {
        return nullptr;
    }
    if(!setting.lookupValue("dbschema", dbschema))
    {
        return nullptr;
    }
    setting.lookupValue("tablename", tablename);
    setting.lookupValue("port", port);

    auto mysqlOperationManager = new MysqlDataOperationManager(server, username, password, dbschema, tablename, port);
    return new DataManager(mysqlOperationManager);
    
}

DataManager* CreateDataManagerFromSettings(const libconfig::Setting& setting)
{
    DataManager* result = nullptr;
    string workerType;
    setting.lookupValue("type", workerType);
    if(workerType == "file")
    {
        result = CreateFileDataManagerFromSettings(setting);
    }
    else if(workerType == "mysql")
    {
        result = CreateMySQLDataManagerFromSettings(setting);
    } 
    if(result)
    {
        string name;
        setting.lookupValue("name", name);
        bool syncDelete = false;
        setting.lookupValue("syncdelete", syncDelete);
        result->SetName(std::move(name));
        result->EnableSyncDelete(syncDelete);
    }
    return result;
}


int main(int argc, char **argv)
{
    char *optLogDir = NULL;
    char *optConf = NULL;
    int c;

    if (argc <= 1)
    {
        usage();
        return 1;
    }
    while ((c = getopt(argc, argv, "c:l:h")) != -1)
    {
        switch (c)
        {
        case 'c':
            optConf = optarg;
            break;
        case 'l':
            optLogDir = optarg;
            break;
        case 'h':
        default:
            usage();
            exit(0);
        }
    }
    // Initialize Google's logging library.
    google::InitGoogleLogging(argv[0]);
    FLAGS_log_dir         = string(optLogDir);
    // Log messages at a level >= this flag are automatically sent to
    // stderr in addition to log files.
    FLAGS_stderrthreshold = 3;    // 3: FATAL
    FLAGS_max_log_size    = 100;  // max log file size 100 MB
    FLAGS_logbuflevel     = -1;   // don't buffer logs
    FLAGS_stop_logging_if_full_disk = true;
   

    libconfig::Config cfg;
    try
    {
        cfg.readFile(optConf);
    }
    catch (const FileIOException &fioex)
    {
        std::cerr << "I/O error while reading file." << std::endl;
        return (EXIT_FAILURE);
    }
    catch (const ParseException &pex)
    {
        std::cerr << "Parse error at " << pex.getFile() << ":" << pex.getLine()
                  << " - " << pex.getError() << std::endl;
        return (EXIT_FAILURE);
    }

    // lock cfg file:
    //    you can't run more than one process with the same config file
    /*boost::interprocess::file_lock pidFileLock(optConf);
    if (pidFileLock.try_lock() == false) {
        LOG(FATAL) << "lock cfg file fail";
        return(EXIT_FAILURE);
    }*/

    signal(SIGTERM, handler);
    signal(SIGINT,  handler);
    

    std::vector<unique_ptr<DataManager>> managers;
    std::vector<uint32_t> timePeriods;
    const Setting &root = cfg.getRoot();
    try
    {
        const Setting &workers = root["workers"];
        for (int i = 0; i < workers.getLength(); ++i)
        {
            const Setting &workerSetting = workers[i];
            DataManager* manager = CreateDataManagerFromSettings(workerSetting);
            if(manager)
            {
                int timePeriod = 5000;
                managers.emplace_back(manager);
                workerSetting.lookupValue("time", timePeriod);
                timePeriods.push_back(timePeriod);
            }
        }
    }
    catch (const SettingException &e) {
        LOG(FATAL) << "config missing: " << e.getPath();
        return 1;
    }
    catch (const std::exception &e) {
        LOG(FATAL) << "exception: " << e.what();
        return 1;
    }

    for(unsigned int i = 0; i < managers.size(); ++i)
    {
        std::chrono::milliseconds time(timePeriods[i]);
        gSyncManager.AddWorker(managers[i].release(), time);
    }
    thread appThread(&SyncManager::Run, &gSyncManager);

    appThread.join();

    google::ShutdownGoogleLogging();
    return 0;
}
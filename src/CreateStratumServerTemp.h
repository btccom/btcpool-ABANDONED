#ifndef _CREATE_STRATUM_SERVER_TEMP_H_
#define _CREATE_STRATUM_SERVER_TEMP_H_

#include <string>

namespace libconfig {
class Config;
}

class Server;

Server *createStratumServer(
    const std::string &type,
    const int32_t shareAvgSeconds,
    const libconfig::Config &config);

#endif
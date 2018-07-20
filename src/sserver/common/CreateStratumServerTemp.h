#ifndef _CREATE_STRATUM_SERVER_TEMP_H_
#define _CREATE_STRATUM_SERVER_TEMP_H_

#include <string>

class Server;

Server* createStratumServer(std::string type, const int32_t shareAvgSeconds);

#endif
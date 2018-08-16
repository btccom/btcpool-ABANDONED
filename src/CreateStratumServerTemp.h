#ifndef _CREATE_STRATUM_SERVER_TEMP_H_
#define _CREATE_STRATUM_SERVER_TEMP_H_

#include <string>

class Server;

Server* createStratumServer(const std::string &type, const int32_t shareAvgSeconds,
                            const std::string &auxPowSolvedShareTopic, /*bitcoin only. TODO: refactor this*/
                            const std::string &rskSolvedShareTopic     /*bitcoin only. TODO: refactor this*/);

#endif
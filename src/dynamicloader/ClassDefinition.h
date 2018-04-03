/*
 The MIT License (MIT)

 Copyright (c) [2016] [BTC.COM]

 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE.
 */
#ifndef CLASS_DEFINITION_H_
#define CLASS_DEFINITION_H_

#include "MysqlConnection.h"
#include "RedisConnection.h"

#include <string>

using std::string;


/**
* Paste the method's formal parameters & actual parameter to here.
* 
* Return value's type: DYCLASS_<className>_<methodName>_RETURN
* Formal parameters:   DYCLASS_<className>_<methodName>_FPARAMS
* Actual parameters:   DYCLASS_<className>_<methodName>_APARAMS
*
* Use DYCLASS_<className>_<className>_FPARAMS as constructor.
* Return value's type is not required for constructor.
*/


//-------------- BlockMaker --------------

#define DYCLASS_BlockMaker_BlockMaker_FPARAMS \
  const char *kafkaBrokers, const MysqlConnectInfo &poolDB
#define DYCLASS_BlockMaker_BlockMaker_APARAMS \
  kafkaBrokers, poolDB

#define DYCLASS_BlockMaker_addBitcoind_RETURN \
  void
#define DYCLASS_BlockMaker_addBitcoind_FPARAMS \
  const string &rpcAddress, const string &rpcUserpass
#define DYCLASS_BlockMaker_addBitcoind_APARAMS \
  rpcAddress, rpcUserpass

#define DYCLASS_BlockMaker_init_RETURN \
  bool
#define DYCLASS_BlockMaker_init_FPARAMS \
  /*empty*/
#define DYCLASS_BlockMaker_init_APARAMS \
  /*empty*/

#define DYCLASS_BlockMaker_stop_RETURN \
  void
#define DYCLASS_BlockMaker_stop_FPARAMS \
  /*empty*/
#define DYCLASS_BlockMaker_stop_APARAMS \
  /*empty*/

#define DYCLASS_BlockMaker_run_RETURN \
  void
#define DYCLASS_BlockMaker_run_FPARAMS \
  /*empty*/
#define DYCLASS_BlockMaker_run_APARAMS \
  /*empty*/


//-------------- GbtMaker --------------

#define DYCLASS_GbtMaker_GbtMaker_FPARAMS \
  const string &zmqBitcoindAddr, \
  const string &bitcoindRpcAddr, const string &bitcoindRpcUserpass, \
  const string &kafkaBrokers, uint32_t kRpcCallInterval, \
  bool isCheckZmq
#define DYCLASS_GbtMaker_GbtMaker_APARAMS \
  zmqBitcoindAddr, bitcoindRpcAddr, bitcoindRpcUserpass, \
  kafkaBrokers, kRpcCallInterval, isCheckZmq

#define DYCLASS_GbtMaker_init_RETURN \
  bool
#define DYCLASS_GbtMaker_init_FPARAMS \
  /*empty*/
#define DYCLASS_GbtMaker_init_APARAMS \
  /*empty*/

#define DYCLASS_GbtMaker_stop_RETURN \
  void
#define DYCLASS_GbtMaker_stop_FPARAMS \
  /*empty*/
#define DYCLASS_GbtMaker_stop_APARAMS \
  /*empty*/

#define DYCLASS_GbtMaker_run_RETURN \
  void
#define DYCLASS_GbtMaker_run_FPARAMS \
  /*empty*/
#define DYCLASS_GbtMaker_run_APARAMS \
  /*empty*/


//-------------- GwMaker --------------

#define DYCLASS_GwMaker_GwMaker_FPARAMS \
  const string &rskdRpcAddr, const string &rskdRpcUserpass, \
  const string &kafkaBrokers, uint32_t kRpcCallInterval
#define DYCLASS_GwMaker_GwMaker_APARAMS \
  rskdRpcAddr, rskdRpcUserpass, \
  kafkaBrokers, kRpcCallInterval

#define DYCLASS_GwMaker_init_RETURN \
  bool
#define DYCLASS_GwMaker_init_FPARAMS \
  /*empty*/
#define DYCLASS_GwMaker_init_APARAMS \
  /*empty*/

#define DYCLASS_GwMaker_stop_RETURN \
  void
#define DYCLASS_GwMaker_stop_FPARAMS \
  /*empty*/
#define DYCLASS_GwMaker_stop_APARAMS \
  /*empty*/

#define DYCLASS_GwMaker_run_RETURN \
  void
#define DYCLASS_GwMaker_run_FPARAMS \
  /*empty*/
#define DYCLASS_GwMaker_run_APARAMS \
  /*empty*/


//-------------- NMCAuxBlockMaker --------------

#define DYCLASS_NMCAuxBlockMaker_NMCAuxBlockMaker_FPARAMS \
  const string &zmqNamecoindAddr, \
  const string &rpcAddr, const string &rpcUserpass, \
  const string &kafkaBrokers, uint32_t kRpcCallInterval, \
  const string &fileLastRpcCallTime, bool isCheckZmq, \
  const string &coinbaseAddress
#define DYCLASS_NMCAuxBlockMaker_NMCAuxBlockMaker_APARAMS \
  zmqNamecoindAddr, rpcAddr, rpcUserpass, kafkaBrokers, \
  kRpcCallInterval, fileLastRpcCallTime, isCheckZmq, coinbaseAddress

#define DYCLASS_NMCAuxBlockMaker_init_RETURN \
  bool
#define DYCLASS_NMCAuxBlockMaker_init_FPARAMS \
  /*empty*/
#define DYCLASS_NMCAuxBlockMaker_init_APARAMS \
  /*empty*/

#define DYCLASS_NMCAuxBlockMaker_stop_RETURN \
  void
#define DYCLASS_NMCAuxBlockMaker_stop_FPARAMS \
  /*empty*/
#define DYCLASS_NMCAuxBlockMaker_stop_APARAMS \
  /*empty*/

#define DYCLASS_NMCAuxBlockMaker_run_RETURN \
  void
#define DYCLASS_NMCAuxBlockMaker_run_FPARAMS \
  /*empty*/
#define DYCLASS_NMCAuxBlockMaker_run_APARAMS \
  /*empty*/


//-------------- JobMaker --------------

#define DYCLASS_JobMaker_JobMaker_FPARAMS \
  bool isTestnet, \
  const string &kafkaBrokers, uint32_t stratumJobInterval, \
  const string &payoutAddr, uint32_t gbtLifeTime, \
  uint32_t emptyGbtLifeTime, const string &fileLastJobTime, \
  uint32_t rskNotifyPolicy, uint32_t blockVersion, \
	const string &poolCoinbaseInfo
#define DYCLASS_JobMaker_JobMaker_APARAMS \
  isTestnet, \
  kafkaBrokers, stratumJobInterval, payoutAddr, gbtLifeTime, \
  emptyGbtLifeTime, fileLastJobTime, rskNotifyPolicy, blockVersion, \
  poolCoinbaseInfo

#define DYCLASS_JobMaker_init_RETURN \
  bool
#define DYCLASS_JobMaker_init_FPARAMS \
  /*empty*/
#define DYCLASS_JobMaker_init_APARAMS \
  /*empty*/

#define DYCLASS_JobMaker_stop_RETURN \
  void
#define DYCLASS_JobMaker_stop_FPARAMS \
  /*empty*/
#define DYCLASS_JobMaker_stop_APARAMS \
  /*empty*/

#define DYCLASS_JobMaker_run_RETURN \
  void
#define DYCLASS_JobMaker_run_FPARAMS \
  /*empty*/
#define DYCLASS_JobMaker_run_APARAMS \
  /*empty*/


//-------------- ClientContainer --------------

#define DYCLASS_ClientContainer_ClientContainer_FPARAMS \
  bool isTestnet, const string &kafkaBrokers
#define DYCLASS_ClientContainer_ClientContainer_APARAMS \
  isTestnet, kafkaBrokers

#define DYCLASS_ClientContainer_addPools_RETURN \
  bool
#define DYCLASS_ClientContainer_addPools_FPARAMS \
  const string &poolName, const string &poolHost, \
  const int16_t poolPort, const string &workerName
#define DYCLASS_ClientContainer_addPools_APARAMS \
  poolName, poolHost, poolPort, workerName

#define DYCLASS_ClientContainer_init_RETURN \
  bool
#define DYCLASS_ClientContainer_init_FPARAMS \
  /*empty*/
#define DYCLASS_ClientContainer_init_APARAMS \
  /*empty*/

#define DYCLASS_ClientContainer_stop_RETURN \
  void
#define DYCLASS_ClientContainer_stop_FPARAMS \
  /*empty*/
#define DYCLASS_ClientContainer_stop_APARAMS \
  /*empty*/

#define DYCLASS_ClientContainer_run_RETURN \
  void
#define DYCLASS_ClientContainer_run_FPARAMS \
  /*empty*/
#define DYCLASS_ClientContainer_run_APARAMS \
  /*empty*/


//-------------- ShareLogWriter --------------

#define DYCLASS_ShareLogWriter_ShareLogWriter_FPARAMS \
  const char *kafkaBrokers, const string &dataDir, const string &kafkaGroupID
#define DYCLASS_ShareLogWriter_ShareLogWriter_APARAMS \
  kafkaBrokers, dataDir, kafkaGroupID

#define DYCLASS_ShareLogWriter_stop_RETURN \
  void
#define DYCLASS_ShareLogWriter_stop_FPARAMS \
  /*empty*/
#define DYCLASS_ShareLogWriter_stop_APARAMS \
  /*empty*/

#define DYCLASS_ShareLogWriter_run_RETURN \
  void
#define DYCLASS_ShareLogWriter_run_FPARAMS \
  /*empty*/
#define DYCLASS_ShareLogWriter_run_APARAMS \
  /*empty*/


//-------------- StratumClientWrapper --------------

#define DYCLASS_StratumClientWrapper_StratumClientWrapper_FPARAMS \
  const char *host, const uint32_t port, \
  const uint32_t numConnections, \
  const string &userName, const string &minerNamePrefix
#define DYCLASS_StratumClientWrapper_StratumClientWrapper_APARAMS \
  host, port, numConnections, userName, minerNamePrefix

#define DYCLASS_StratumClientWrapper_stop_RETURN \
  void
#define DYCLASS_StratumClientWrapper_stop_FPARAMS \
  /*empty*/
#define DYCLASS_StratumClientWrapper_stop_APARAMS \
  /*empty*/

#define DYCLASS_StratumClientWrapper_run_RETURN \
  void
#define DYCLASS_StratumClientWrapper_run_FPARAMS \
  /*empty*/
#define DYCLASS_StratumClientWrapper_run_APARAMS \
  /*empty*/


//-------------- ShareLogDumper --------------

#define DYCLASS_ShareLogDumper_ShareLogDumper_FPARAMS \
  const string &dataDir, time_t timestamp, const std::set<int32_t> &uids
#define DYCLASS_ShareLogDumper_ShareLogDumper_APARAMS \
  dataDir, timestamp, uids

#define DYCLASS_ShareLogDumper_dump2stdout_RETURN \
  void
#define DYCLASS_ShareLogDumper_dump2stdout_FPARAMS \
  /*empty*/
#define DYCLASS_ShareLogDumper_dump2stdout_APARAMS \
  /*empty*/


//-------------- ShareLogParser --------------

#define DYCLASS_ShareLogParser_ShareLogParser_FPARAMS \
  const string &dataDir, time_t timestamp, const MysqlConnectInfo &poolDBInfo
#define DYCLASS_ShareLogParser_ShareLogParser_APARAMS \
  dataDir, timestamp, poolDBInfo

#define DYCLASS_ShareLogParser_init_RETURN \
  bool
#define DYCLASS_ShareLogParser_init_FPARAMS \
  /*empty*/
#define DYCLASS_ShareLogParser_init_APARAMS \
  /*empty*/

#define DYCLASS_ShareLogParser_flushToDB_RETURN \
  bool
#define DYCLASS_ShareLogParser_flushToDB_FPARAMS \
  /*empty*/
#define DYCLASS_ShareLogParser_flushToDB_APARAMS \
  /*empty*/

#define DYCLASS_ShareLogParser_processUnchangedShareLog_RETURN \
  bool
#define DYCLASS_ShareLogParser_processUnchangedShareLog_FPARAMS \
  /*empty*/
#define DYCLASS_ShareLogParser_processUnchangedShareLog_APARAMS \
  /*empty*/


//-------------- ShareLogParserServer --------------

#define DYCLASS_ShareLogParserServer_ShareLogParserServer_FPARAMS \
  const string dataDir, const string &httpdHost, \
  unsigned short httpdPort, const MysqlConnectInfo &poolDBInfo, \
  const uint32_t kFlushDBInterval
#define DYCLASS_ShareLogParserServer_ShareLogParserServer_APARAMS \
  dataDir, httpdHost, httpdPort, poolDBInfo, kFlushDBInterval

#define DYCLASS_ShareLogParserServer_stop_RETURN \
  void
#define DYCLASS_ShareLogParserServer_stop_FPARAMS \
  /*empty*/
#define DYCLASS_ShareLogParserServer_stop_APARAMS \
  /*empty*/

#define DYCLASS_ShareLogParserServer_run_RETURN \
  void
#define DYCLASS_ShareLogParserServer_run_FPARAMS \
  /*empty*/
#define DYCLASS_ShareLogParserServer_run_APARAMS \
  /*empty*/


//-------------- StatsServer --------------

#define DYCLASS_StatsServer_StatsServer_FPARAMS \
  const char *kafkaBrokers, \
  const string &httpdHost, unsigned short httpdPort, \
  const MysqlConnectInfo *poolDBInfo, const RedisConnectInfo *redisInfo, \
  const string &redisKeyPrefix, const int redisKeyExpire, \
  const time_t kFlushDBInterval, const string &fileLastFlushTime
#define DYCLASS_StatsServer_StatsServer_APARAMS \
  kafkaBrokers, httpdHost, httpdPort, poolDBInfo, redisInfo, \
  redisKeyPrefix, redisKeyExpire, kFlushDBInterval, fileLastFlushTime

#define DYCLASS_StatsServer_init_RETURN \
  bool
#define DYCLASS_StatsServer_init_FPARAMS \
  /*empty*/
#define DYCLASS_StatsServer_init_APARAMS \
  /*empty*/

#define DYCLASS_StatsServer_stop_RETURN \
  void
#define DYCLASS_StatsServer_stop_FPARAMS \
  /*empty*/
#define DYCLASS_StatsServer_stop_APARAMS \
  /*empty*/

#define DYCLASS_StatsServer_run_RETURN \
  void
#define DYCLASS_StatsServer_run_FPARAMS \
  /*empty*/
#define DYCLASS_StatsServer_run_APARAMS \
  /*empty*/


//-------------- StratumServer --------------

#define DYCLASS_StratumServer_StratumServer_FPARAMS \
  bool isTestnet, \
  const char *ip, const unsigned short port, \
  const char *kafkaBrokers, \
  const string &userAPIUrl, \
  const uint8_t serverId, const string &fileLastNotifyTime, \
  bool isEnableSimulator, \
  bool isSubmitInvalidBlock, \
  bool isDevModeEnable, \
  float minerDifficulty, \
  const int32_t shareAvgSeconds
#define DYCLASS_StratumServer_StratumServer_APARAMS \
  isTestnet, \
  ip, port, kafkaBrokers, userAPIUrl, \
  serverId, fileLastNotifyTime, isEnableSimulator, isSubmitInvalidBlock, \
  isDevModeEnable, minerDifficulty, shareAvgSeconds

#define DYCLASS_StratumServer_init_RETURN \
  bool
#define DYCLASS_StratumServer_init_FPARAMS \
  /*empty*/
#define DYCLASS_StratumServer_init_APARAMS \
  /*empty*/

#define DYCLASS_StratumServer_stop_RETURN \
  void
#define DYCLASS_StratumServer_stop_FPARAMS \
  /*empty*/
#define DYCLASS_StratumServer_stop_APARAMS \
  /*empty*/

#define DYCLASS_StratumServer_run_RETURN \
  void
#define DYCLASS_StratumServer_run_FPARAMS \
  /*empty*/
#define DYCLASS_StratumServer_run_APARAMS \
  /*empty*/



#endif

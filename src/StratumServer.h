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
#ifndef STRATUM_SERVER_H_
#define STRATUM_SERVER_H_

#include "Common.h"

#include <map>
#include <vector>
#include <memory>

#include <arpa/inet.h>
#include <sys/socket.h>

#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/listener.h>
#include <event2/util.h>
#include <event2/event.h>

#include <glog/logging.h>

#include "bitcoin/core.h"
#include "Kafka.h"
#include "Stratum.h"
#include "StratumSession.h"

class Server;
class StratumJobEx;

////////////////////////////////// JobRepository ///////////////////////////////
class JobRepository {
  atomic<bool> running_;
  mutex lock_;
  std::map<uint64_t/* jobId */, shared_ptr<StratumJobEx> > exJobs_;

  KafkaConsumer kafkaConsumer_;  // consume topic: 'StratumJob'
  Server *server_;               // call server to send new job

  const time_t kMaxJobsLifeTime_;
  const time_t kMiningNotifyInterval_;

  time_t lastJobSendTime_;
  uint256 latestPrevBlockHash_;

  thread threadConsume_;
  void runThreadConsume();

  void consumeStratumJob(rd_kafka_message_t *rkmessage);
  void sendMiningNotify(shared_ptr<StratumJobEx> exJob);
  void tryCleanExpiredJobs();
  void checkAndSendMiningNotify();

public:
  JobRepository(const char *kafkaBrokers, Server *server);
  ~JobRepository();

  void stop();
  bool setupThreadConsume();
  void markAllJobsAsStale();

  shared_ptr<StratumJobEx> getStratumJobEx(const uint64_t jobId);
  shared_ptr<StratumJobEx> getLatestStratumJobEx();
};


////////////////////////////////// StratumJobEx ////////////////////////////////
class StratumJobEx {
  enum State {
    MINING = 0,
    STALE  = 1
  };

  //------------
  mutex lock_;
  State state_;

  void makeMiningNotifyStr();
  void generateCoinbaseTx(std::vector<char> *coinbaseBin,
                          const uint32 extraNonce1,
                          const string &extraNonce2Hex);

public:
  bool isClean_;
  StratumJob *sjob_;
  string miningNotify_;

public:
  StratumJobEx(StratumJob *sjob, bool isClean);
  ~StratumJobEx();

  void markStale();
  bool isStale();

  void generateBlockHeader(CBlockHeader *header,
                           const uint32 extraNonce1,
                           const string &extraNonce2Hex,
                           const vector<uint256> &merkleBranch,
                           const uint256 &hashPrevBlock,
                           const uint32 nBits, const int nVersion,
                           const uint32 nTime, const uint32 nonce);
};


///////////////////////////////////// Server ///////////////////////////////////
class Server {
  // NetIO
  struct sockaddr_in sin_;
  struct event_base* base_;
  struct event* signal_event_;
  struct evconnlistener* listener_;
  map<evutil_socket_t, StratumSession *> connections_;
  mutex connsLock_;  // lock for connections

  // Stratum
  KafkaProducer *kafkaProducerShareLog_;
  KafkaProducer *kafkaProducerSolvedShare_;

public:
  const int32_t kShareAvgSeconds_;
  JobRepository *jobRepository_;

public:
  Server();
  ~Server();

  bool setup(const char *ip, const unsigned short port, const char *kafkaBrokers);
  void run();
  void stop();

  void sendMiningNotifyToAll(shared_ptr<StratumJobEx> exJobPtr);

  void addConnection(evutil_socket_t fd, StratumSession *connection);
  void removeConnection(evutil_socket_t fd);

  static void listenerCallback(struct evconnlistener* listener,
                               evutil_socket_t socket,
                               struct sockaddr* saddr,
                               int socklen, void* server);
  static void readCallback (struct bufferevent *, void *connection);
  static void eventCallback(struct bufferevent *, short, void *connection);

  int checkShare(const uint64_t jobId,
                 const uint32 extraNonce1, const string &extraNonce2Hex,
                 const uint32_t nTime, const uint32_t nonce,
                 const uint256 &jobTarget, const string &workFullName);
  void sendShare2Kafka(const uint8_t *data, size_t len);
};


////////////////////////////////// StratumServer ///////////////////////////////
class StratumServer {
  atomic<bool> running_;

  Server server_;
  string ip_;
  unsigned short port_;
  int32_t serverId_;  // global unique, range: [0, 255]

  string kafkaBrokers_;

public:
  StratumServer(const char *ip, const unsigned short port,
                const char *kafkaBrokers);
  ~StratumServer();

  bool init();
  void stop();
  void run();
};


#endif

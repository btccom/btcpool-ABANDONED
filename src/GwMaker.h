/**
  File: GwMaker.h
  Purpose: Poll RSK node to get new work and send it to Kafka "RawGw" topic

  @author Martin Medina
  @copyright RSK
  @version 1.0 30/03/17 
*/

#ifndef GW_MAKER_H_
#define GW_MAKER_H_

#include "Common.h"
#include "Kafka.h"

class GwMaker {
  atomic<bool> running_;

  string rskdRpcAddr_;
  string rskdRpcUserpass_;
  uint32_t kRpcCallInterval_;

  string kafkaBrokers_;
  KafkaProducer kafkaProducer_;

  bool rskdRpcGw(string &resp);
  string makeRawGwMsg();

  void submitRawGwMsg();

  void kafkaProduceMsg(const void *payload, size_t len);

public:
  GwMaker(const string &rskdRpcAddr, const string &rskdRpcUserpass,
           const string &kafkaBrokers, uint32_t kRpcCallInterval);
  ~GwMaker();

  bool init();
  void stop();
  void run();
};

#endif

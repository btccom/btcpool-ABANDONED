/*
 The MIT License (MIT)

 Copyright (C) 2017 RSK Labs Ltd.

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

/**
  File: GwMaker.h
  Purpose: Poll RSK node to get new work and send it to Kafka "RawGw" topic

  @author Martin Medina
  @copyright RSK Labs Ltd.
  @version 1.0 30/03/17

  maintained by HaoLi (fatrat1117) and YihaoPeng since Feb 20, 2018
*/

#ifndef GW_MAKER_H_
#define GW_MAKER_H_

#include "Common.h"
#include "Kafka.h"
#include "utilities_js.hpp"
#include <event2/event.h>

struct GwMakerDefinition {
  string chainType_;
  bool enabled_;

  string rpcAddr_;
  string rpcUserPwd_;
  uint32_t rpcInterval_;

  string notifyHost_;
  uint32_t notifyPort_;

  string rawGwTopic_;
};

class GwMakerHandler {
public:
  virtual ~GwMakerHandler() = 0; // mark it's an abstract class
  virtual void init(const GwMakerDefinition &def) { def_ = def; }

  // read-only definition
  virtual const GwMakerDefinition &def() { return def_; }

  // Interface with the GwMaker.
  // There is a default implementation that use virtual functions below.
  // If the implementation does not meet the requirements, you can overload it
  // and ignore all the following virtual functions.
  virtual string makeRawGwMsg();

protected:
  // These virtual functions make it easier to implement the makeRawGwMsg()
  // interface. In most cases, you just need to override getRequestData() and
  // processRawGw(). If you have overloaded makeRawGwMsg() above, you can ignore
  // all the following functions.

  // Receive rpc response and generate RawGw message for the pool.
  virtual string processRawGw(const string &gw) { return ""; }

  // Call RPC `getwork` and get the response.
  virtual bool callRpcGw(string &resp);

  // Body of HTTP POST used by callRpcGw().
  // return "" if use HTTP GET.
  virtual string getRequestData() { return ""; }
  // HTTP header `User-Agent` used by callRpcGw().
  virtual string getUserAgent() { return "curl"; }

  // blockchain and RPC-server definitions
  GwMakerDefinition def_;
};

class GwMakerHandlerJson : public GwMakerHandler {
  virtual bool checkFields(JsonNode &r) = 0;
  virtual string constructRawMsg(JsonNode &r) = 0;
  string processRawGw(const string &gw) override;
};

class GwNotification {
  std::function<void(void)> callback_;

public:
  GwNotification(
      std::function<void(void)> callback,
      const string &httpdHost,
      unsigned short httpdPort);
  ~GwNotification();

  // httpd
  struct event_base *base_;
  string httpdHost_;
  unsigned short httpdPort_;

  static void httpdNotification(struct evhttp_request *req, void *arg);

  void setupHttpd();
  void runHttpd();
  void stop();
};

class GwMaker {
  shared_ptr<GwMakerHandler> handler_;
  atomic<bool> running_;

private:
  string kafkaBrokers_;
  KafkaProducer kafkaProducer_;
  shared_ptr<GwNotification> notification_;

  string makeRawGwMsg();
  void submitRawGwMsg();
  void kafkaProduceMsg(const void *payload, size_t len);

public:
  GwMaker(shared_ptr<GwMakerHandler> handle, const string &kafkaBrokers);
  virtual ~GwMaker();

  bool init();
  void stop();
  void run();

  // for logs
  string getChainType() { return handler_->def().chainType_; }
  string getRawGwTopic() { return handler_->def().rawGwTopic_; }
};

#endif

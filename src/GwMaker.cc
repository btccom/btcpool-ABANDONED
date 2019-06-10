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
#include "GwMaker.h"
#include "Utils.h"

#include <limits.h>
#include <glog/logging.h>
#include <boost/thread.hpp>

#include <event2/http.h>
#include <event2/http_struct.h>
#include <event2/buffer.h>
#include <event2/buffer_compat.h>
#include <event2/keyvalq_struct.h>

#include <chrono>
#include <thread>

///////////////////////////////GwMaker////////////////////////////////////
GwMaker::GwMaker(shared_ptr<GwMakerHandler> handler, const string &kafkaBrokers)
  : handler_(handler)
  , running_(true)
  , kafkaProducer_(
        kafkaBrokers.c_str(),
        handler->def().rawGwTopic_.c_str(),
        0 /* partition */) {
}

GwMaker::~GwMaker() {
}

bool GwMaker::init() {
  map<string, string> options;
  // set to 1 (0 is an illegal value here), deliver msg as soon as possible.
  options["queue.buffering.max.ms"] = "1";
  if (!kafkaProducer_.setup(&options)) {
    LOG(ERROR) << "kafka producer setup failure";
    return false;
  }

  // setup kafka and check if it's alive
  if (!kafkaProducer_.checkAlive()) {
    LOG(ERROR) << "kafka is NOT alive";
    return false;
  }

  if (handler_->def().notifyHost_.length() > 0) {
    auto callback = [&]() -> void { submitRawGwMsg(); };
    notification_ = make_shared<GwNotification>(
        callback, handler_->def().notifyHost_, handler_->def().notifyPort_);
    notification_->setupHttpd();
  }

  // TODO: check rskd is alive in a similar way as done for btcd

  return true;
}

void GwMaker::stop() {
  if (!running_) {
    return;
  }
  running_ = false;
  LOG(INFO) << "stop GwMaker " << handler_->def().chainType_
            << ", topic: " << handler_->def().rawGwTopic_;
}

void GwMaker::kafkaProduceMsg(const void *payload, size_t len) {
  kafkaProducer_.produce(payload, len);
}

string GwMaker::makeRawGwMsg() {
  return handler_->makeRawGwMsg();
}

void GwMaker::submitRawGwMsg() {

  const string rawGwMsg = makeRawGwMsg();
  if (rawGwMsg.length() == 0) {
    LOG(ERROR) << "get rawGw failure";
    return;
  }

  // submit to Kafka
  LOG(INFO) << "submit to Kafka msg len: " << rawGwMsg.length();
  kafkaProduceMsg(rawGwMsg.c_str(), rawGwMsg.length());
}

void GwMaker::run() {

  while (running_) {
    std::this_thread::sleep_for(
        std::chrono::milliseconds{handler_->def().rpcInterval_});
    submitRawGwMsg();
  }

  LOG(INFO) << "GwMaker " << handler_->def().chainType_
            << ", topic: " << handler_->def().rawGwTopic_ << " stopped";
}

///////////////////////////////GwNotification////////////////////////////////////
/*
 * https://wiki.parity.io/Mining.html
 * Parity HTTP Notification
 */
GwNotification::GwNotification(
    std::function<void(void)> callback,
    const string &httpdHost,
    unsigned short httpdPort)
  : callback_(callback)
  , base_(nullptr)
  , httpdHost_(httpdHost)
  , httpdPort_(httpdPort) {
}

GwNotification::~GwNotification() {
  stop();
}

void GwNotification::httpdNotification(struct evhttp_request *req, void *arg) {
  struct evbuffer *evb = evbuffer_new();
  Strings::EvBufferAdd(evb, "{\"err_no\":0,\"err_msg\":\"notify success\"}");
  evhttp_send_reply(req, HTTP_OK, "OK", evb);
  evbuffer_free(evb);

  string postData = string(
      (char *)EVBUFFER_DATA(req->input_buffer),
      EVBUFFER_LENGTH(req->input_buffer));
  LOG(INFO) << "GwNotification: makeRawGwMsg for notify " << postData;

  GwNotification *notification = (GwNotification *)arg;
  notification->callback_();
}

void GwNotification::setupHttpd() {
  std::thread t(std::bind(&GwNotification::runHttpd, this));
  t.detach();
}

void GwNotification::runHttpd() {
  struct evhttp_bound_socket *handle;
  struct evhttp *httpd;

  base_ = event_base_new();
  httpd = evhttp_new(base_);

  evhttp_set_allowed_methods(
      httpd, EVHTTP_REQ_GET | EVHTTP_REQ_POST | EVHTTP_REQ_HEAD);
  evhttp_set_timeout(httpd, 5 /* timeout in seconds */);

  evhttp_set_cb(httpd, "/notify", GwNotification::httpdNotification, this);

  handle =
      evhttp_bind_socket_with_handle(httpd, httpdHost_.c_str(), httpdPort_);
  if (!handle) {
    LOG(ERROR) << "couldn't bind to port: " << httpdPort_
               << ", host: " << httpdHost_ << ", exiting.";
    return;
  }
  event_base_dispatch(base_);
}

void GwNotification::stop() {
  LOG(INFO) << "stop Notification ...";

  event_base_loopexit(base_, NULL);
}

///////////////////////////////GwMakerHandler////////////////////////////////////
GwMakerHandler::~GwMakerHandler() {
}

string GwMakerHandler::makeRawGwMsg() {
  string gw;
  if (!callRpcGw(gw)) {
    return "";
  }
  LOG(INFO) << "getwork len=" << gw.length() << ", msg: " << gw.substr(0, 500)
            << (gw.size() > 500 ? "..." : "");
  return processRawGw(gw);
}

bool GwMakerHandler::callRpcGw(string &response) {
  string request = getRequestData();
  string userAgent = getUserAgent();

  bool res = rpcCall(
      def_.rpcAddr_.c_str(),
      def_.rpcUserPwd_.c_str(),
      request.empty() ? nullptr : request.c_str(),
      request.length(),
      response,
      userAgent.c_str());

  if (!res) {
    LOG(ERROR) << "call RPC failure";
    return false;
  }
  return true;
}

///////////////////////////////GwMakerHandlerJson///////////////////////////////////
string GwMakerHandlerJson::processRawGw(const string &msg) {
  JsonNode r;
  if (!JsonNode::parse(msg.c_str(), msg.c_str() + msg.length(), r)) {
    LOG(ERROR) << "decode gw failure: " << msg;
    return "";
  }

  // check fields
  if (!checkFields(r)) {
    LOG(ERROR) << "gw check fields failure";
    return "";
  }

  return constructRawMsg(r);
}

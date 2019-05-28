/*
 The MIT License (MIT)

 Copyright (c) [2019] [BTC.COM]

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

#include "Exporter.h"

#include "Collector.h"
#include "Metric.h"

#include <fmt/format.h>

#include <event2/buffer.h>
#include <event2/http.h>
#include <glog/logging.h>

#include <iterator>
#include <set>

namespace prometheus {

namespace {

static std::string FormatMetricType(Metric::Type t) {
  switch (t) {
  case Metric::Type::Counter:
    return "counter";
  case Metric::Type::Gauge:
    return "gauge";
  default:
    return "untyped";
  }
}

} // namespace

class Exporter : public IExporter {
public:
  Exporter();
  ~Exporter();
  bool setup(const std::string &address, uint16_t port, const std::string &path)
      override;
  bool registerCollector(std::shared_ptr<Collector> collector) override;
  bool unregisterCollector(std::shared_ptr<Collector> collector) override;
  bool run(struct event_base *base) override;

  static void handleRequest(struct evhttp_request *req, void *arg);

private:
  std::string exportMetrics();

  std::string address_;
  uint16_t port_;
  std::string path_;
  evhttp *httpd_;

  std::set<std::shared_ptr<Collector>> collectors_;
};

Exporter::Exporter() {
}

Exporter::~Exporter() {
  if (httpd_) {
    evhttp_free(httpd_);
  }
}

bool Exporter::setup(
    const std::string &address, uint16_t port, const std::string &path) {
  address_ = address;
  port_ = port;
  path_ = path;
  return true;
}

bool Exporter::registerCollector(
    std::shared_ptr<prometheus::Collector> collector) {
  return collectors_.insert(std::move(collector)).second;
}

bool Exporter::unregisterCollector(
    std::shared_ptr<prometheus::Collector> collector) {
  return collectors_.erase(collector);
}

bool Exporter::run(struct event_base *base) {
  httpd_ = evhttp_new(base);
  if (!httpd_) {
    return false;
  }
  evhttp_set_allowed_methods(
      httpd_, EVHTTP_REQ_GET | EVHTTP_REQ_POST | EVHTTP_REQ_HEAD);
  evhttp_set_timeout(httpd_, 5);
  evhttp_set_cb(httpd_, path_.c_str(), &Exporter::handleRequest, this);

  if (0 != evhttp_bind_socket(httpd_, address_.c_str(), port_)) {
    return false;
  }

  return true;
}

void Exporter::handleRequest(struct evhttp_request *req, void *arg) {
  auto exporter = static_cast<Exporter *>(arg);
  auto metrics = exporter->exportMetrics();
  auto headers = evhttp_request_get_output_headers(req);
  evhttp_add_header(headers, "content-type", "text/plain; version=0.0.4");
  evbuffer_add(
      evhttp_request_get_output_buffer(req), metrics.data(), metrics.size());
  evhttp_send_reply(req, HTTP_OK, "OK", nullptr);
}

std::string Exporter::exportMetrics() {
  std::string text;
  auto out = std::back_inserter(text);
  for (auto &collector : collectors_) {
    auto metrics = collector->collectMetrics();
    for (auto &metric : metrics) {
      auto &name = metric->getName();
      if (name.empty()) {
        continue;
      }

      auto &help = metric->getHelp();
      if (!help.empty()) {
        fmt::format_to(out, "# HELP {} {}\n", name, help);
      }
      fmt::format_to(
          out, "# TYPE {} {}\n", name, FormatMetricType(metric->getType()));
      fmt::format_to(out, "{}", name);
      auto &labels = metric->getLabels();
      if (!labels.empty()) {
        fmt::format_to(out, "{{");
        for (auto &label : labels) {
          fmt::format_to(out, "{}=\"{}\",", label.first, label.second);
        }
        fmt::format_to(out, "}}");
      }
      fmt::format_to(out, " {}\n", metric->getValue());
    }
  }
  return text;
}

std::unique_ptr<IExporter> CreateExporter() {
  return std::make_unique<Exporter>();
}

} // namespace prometheus

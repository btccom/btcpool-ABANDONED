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

#include "Metric.h"

namespace prometheus {

class MetricBase : public Metric {
protected:
  MetricBase(
      const std::string &name,
      Metric::Type type,
      const std::string &help,
      const std::map<std::string, std::string> &labels)
    : name_{name}
    , type_{type}
    , help_{help}
    , labels_{labels} {}

  const std::string &getName() const override { return name_; }
  Type getType() const override { return type_; }
  const std::string &getHelp() const override { return help_; }
  const std::map<std::string, std::string> &getLabels() const override {
    return labels_;
  }

protected:
  std::string name_;
  Metric::Type type_;
  std::string help_;
  std::map<std::string, std::string> labels_;
};

class MetricValue : public MetricBase {
public:
  MetricValue(
      const std::string &name,
      Metric::Type type,
      const std::string &help,
      const std::map<std::string, std::string> &labels,
      double value)
    : MetricBase{name, type, help, labels}
    , value_{value} {}

  double getValue() const { return value_; }

private:
  double value_;
};

class MetricFn : public MetricBase {
public:
  MetricFn(
      const std::string &name,
      Metric::Type type,
      const std::string &help,
      const std::map<std::string, std::string> &labels,
      std::function<double()> valueFn)
    : MetricBase{name, type, help, labels}
    , valueFn_{std::move(valueFn)} {}

  double getValue() const { return valueFn_(); }

private:
  std::function<double()> valueFn_;
};

std::shared_ptr<Metric> CreateMetric(
    const std::string &name,
    Metric::Type type,
    const std::string &help,
    const std::map<std::string, std::string> &labels,
    double value) {
  return std::make_shared<MetricValue>(name, type, help, labels, value);
}

std::shared_ptr<Metric> CreateMetric(
    const std::string &name,
    Metric::Type type,
    const std::string &help,
    const std::map<std::string, std::string> &labels,
    std::function<double()> valueFn) {
  return std::make_shared<MetricFn>(
      name, type, help, labels, std::move(valueFn));
}

} // namespace prometheus

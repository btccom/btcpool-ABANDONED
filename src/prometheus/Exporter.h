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

#pragma once

#include "event2/event.h"

#include <cstdint>
#include <memory>
#include <string>

namespace prometheus {

class Collector;

class IExporter {
public:
  virtual ~IExporter() = default;
  virtual bool
  setup(const std::string &address, uint16_t port, const std::string &path) = 0;
  virtual bool registerCollector(std::shared_ptr<Collector> collector) = 0;
  virtual bool unregisterCollector(std::shared_ptr<Collector> collector) = 0;
  virtual bool run(struct event_base *base) = 0;
};

std::unique_ptr<IExporter> CreateExporter();

} // namespace prometheus

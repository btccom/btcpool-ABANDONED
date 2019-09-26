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

#include <boost/circular_buffer.hpp>

#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

class WorkerPool {
public:
  explicit WorkerPool(size_t queueCapacity);
  WorkerPool(const WorkerPool &&) = delete;
  ~WorkerPool();

  void start(size_t numOfWorkers);
  void stop();
  void dispatch(std::function<void()> work);

private:
  void runWorker();

  boost::circular_buffer<std::function<void()>> works_;
  std::mutex worksMutex_;
  std::condition_variable worksNotEmpty_;
  std::condition_variable worksNotFull_;
  std::vector<std::thread> workers_;
  bool stop_;
};

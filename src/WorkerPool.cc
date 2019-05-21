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

#include "WorkerPool.h"

WorkerPool::WorkerPool(size_t queueCapacity)
  : works_{queueCapacity}
  , stop_{false} {
}

WorkerPool::~WorkerPool() {
  stop();
}

void WorkerPool::start(size_t numOfWorkers) {
  for (size_t i = 0; i < numOfWorkers; ++i) {
    workers_.emplace_back([this]() { runWorker(); });
  }
}

void WorkerPool::stop() {
  {
    std::unique_lock<std::mutex> l{worksMutex_};
    if (!stop_) {
      stop_ = true;
      worksNotEmpty_.notify_all();
      worksNotFull_.notify_all();
    }
  }

  for (auto &worker : workers_) {
    if (worker.joinable())
      worker.join();
  }
}

void WorkerPool::dispatch(std::function<void()> work) {
  if (!work) {
    return;
  }

  std::unique_lock<std::mutex> l{worksMutex_};
  worksNotFull_.wait(l, [this]() { return !works_.full() || stop_; });
  if (!stop_) {
    works_.push_back(std::move(work));
    worksNotEmpty_.notify_one();
  }
}

void WorkerPool::runWorker() {
  while (true) {
    std::unique_lock<std::mutex> l{worksMutex_};
    worksNotEmpty_.wait(l, [this]() { return !works_.empty() || stop_; });
    if (stop_) {
      break;
    } else {
      auto work = std::move(works_.front());
      works_.pop_front();
      worksNotFull_.notify_one();
      l.unlock();
      work();
    }
  }
}
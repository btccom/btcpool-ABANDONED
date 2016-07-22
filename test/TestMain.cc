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
#include <signal.h>
#include <execinfo.h>
#include <string>
#include "gtest/gtest.h"

#include <glog/logging.h>

using std::string;

//
// run all:      ./unittest
// run single:   ./unittest --gtest_filter=StratumSession\*
//
extern "C" {

static void handler(int sig);

// just for debug, should be removed when release
void handler(int sig) {
  void *array[10];
  size_t size;

  // get void*'s for all entries on the stack
  size = backtrace(array, 10);

  // print out all the frames to stderr
  fprintf(stderr, "Error: signal %d:\n", sig);
  backtrace_symbols_fd(array, size, 2);
  exit(1);
}
}

typedef char * CString;

int main(int argc, char **argv) {
  signal(SIGSEGV, handler);
  signal(SIGFPE, handler);
  signal(SIGPIPE, handler);

  // Initialize Google's logging library.
  google::InitGoogleLogging(argv[0]);
  FLAGS_logbuflevel = -1;
  FLAGS_logtostderr = true;
  FLAGS_colorlogtostderr = true;
  
  CString * newArgv = new CString [argc];
  memcpy(newArgv, argv, argc * sizeof(CString));
  string testname = "--gtest_filter=";
  if (argc == 2 && newArgv[1][0] != '-') {
    testname.append(newArgv[1]);
    newArgv[1] = (char*)testname.c_str();
  }
  testing::InitGoogleTest(&argc, newArgv);
  
  int ret = RUN_ALL_TESTS();
  delete [] newArgv;
  return ret;
}



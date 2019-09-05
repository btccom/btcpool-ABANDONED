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
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <err.h>
#include <errno.h>
#include <unistd.h>

#include <iostream>
#include <fstream>

#include <glog/logging.h>
#include <libconfig.h++>

#include "zlibstream/zstr.hpp"
#include "shares.hpp"

using namespace std;
using namespace libconfig;

void usage() {
  fprintf(
      stderr,
      "Usage:\n\tshare_convertor -i \"<input-sharelog-v2-file.bin>\" -o "
      "\"<output-sharelog-v1-file.bin>\"\n");
}

int main(int argc, char **argv) {
  char *inFile = NULL;
  char *outFile = NULL;
  int c;

  if (argc <= 1) {
    usage();
    return 1;
  }
  while ((c = getopt(argc, argv, "i:o:h")) != -1) {
    switch (c) {
    case 'i':
      inFile = optarg;
      break;
    case 'o':
      outFile = optarg;
      break;
    case 'h':
    default:
      usage();
      exit(0);
    }
  }

  // Initialize Google's logging library.
  google::InitGoogleLogging(argv[0]);
  FLAGS_logtostderr = 1;

  try {
    if (inFile == NULL) {
      LOG(FATAL) << "missing input file (-i \"<input-sharelog-v2-file.bin>\")";
      return 1;
    }

    if (outFile == NULL) {
      LOG(FATAL)
          << "missing output file (-o \"<output-sharelog-v1-file.bin>\")";
      return 1;
    }

    // open file (auto-detecting compression format or non-compression)
    LOG(INFO) << "open input file: " << inFile;
    zstr::ifstream in(inFile, std::ios::binary);
    if (!in) {
      LOG(FATAL) << "open input file failed: " << inFile;
      return 1;
    }

    LOG(INFO) << "open output file: " << outFile;
    std::ofstream out(outFile, std::ios::binary | std::ios::trunc);
    if (!out) {
      LOG(FATAL) << "open output file failed: " << outFile;
      return 1;
    }

    LOG(INFO) << "converting ShareBitcoinV2 to ShareBitcoinV1...";
    ShareBitcoinV2 v2;
    ShareBitcoinV1 v1;

    in.read((char *)&v2, sizeof(v2));
    while (!in.eof()) {
      if (v2.toShareBitcoinV1(v1)) {
        out.write((const char *)&v1, sizeof(v1));
      }
      in.read((char *)&v2, sizeof(v2));
    }
    LOG(INFO) << "completed.";
  } catch (const SettingException &e) {
    LOG(FATAL) << "config missing: " << e.getPath();
    return 1;
  }

  google::ShutdownGoogleLogging();
  return 0;
}

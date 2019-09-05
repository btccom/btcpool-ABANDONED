/*
 The MIT License (MIT)

 Copyright (c) [2018] [BTC.COM]

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
#include <arrow/io/file.h>
#include <arrow/util/logging.h>
#include <parquet/api/reader.h>
#include <parquet/api/writer.h>

#include "zlibstream/zstr.hpp"
#include "shares.hpp"

using namespace std;
using namespace libconfig;

using arrow::io::FileOutputStream;
using parquet::LogicalType;
using parquet::Repetition;
using parquet::Type;
using parquet::schema::GroupNode;
using parquet::schema::PrimitiveNode;

const size_t DEFAULT_NUM_ROWS_PER_ROW_GROUP = 1000000;

void usage() {
  fprintf(
      stderr,
      "Usage:\n\tsharelog_to_parquet -i \"<input-sharelog-v1-file.bin>\" -o "
      "\"<output-parquet-file.bin>\" [-n %d]\n",
      (int)DEFAULT_NUM_ROWS_PER_ROW_GROUP);
}

class ParquetWriter {
protected:
  std::shared_ptr<FileOutputStream> file;
  std::shared_ptr<parquet::ParquetFileWriter> fileWriter;

public:
  arrow::Status open(const string &outFile) {
    // Create a local file output stream instance.
    std::shared_ptr<FileOutputStream> out;
    auto stat = FileOutputStream::Open(outFile, &out);

    if (!stat.ok()) {
      return stat;
    }

    // Setup the parquet schema
    std::shared_ptr<GroupNode> schema = setupSchema();

    // Add writer properties
    parquet::WriterProperties::Builder builder;
    builder.compression(parquet::Compression::SNAPPY);
    std::shared_ptr<parquet::WriterProperties> props = builder.build();

    // Create a ParquetFileWriter instance
    fileWriter = parquet::ParquetFileWriter::Open(out, schema, props);

    return stat;
  }

  inline void addShares(const ShareBitcoinV1 *sharesBegin, size_t num) {
    const ShareBitcoinV1 *sharesEnd = sharesBegin + num;
    const ShareBitcoinV1 *share;

    // Create a RowGroupWriter instance
    auto rgWriter = fileWriter->AppendRowGroup();

    // job_id
    auto jobIdWriter =
        static_cast<parquet::Int64Writer *>(rgWriter->NextColumn());
    for (share = sharesBegin; share != sharesEnd; share++) {
      jobIdWriter->WriteBatch(1, nullptr, nullptr, (int64_t *)&(share->jobId_));
    }

    // worker_id
    auto workerIdWriter =
        static_cast<parquet::Int64Writer *>(rgWriter->NextColumn());
    for (share = sharesBegin; share != sharesEnd; share++) {
      workerIdWriter->WriteBatch(
          1, nullptr, nullptr, (int64_t *)&(share->workerHashId_));
    }

    // ip_long
    auto ipLongWriter =
        static_cast<parquet::Int32Writer *>(rgWriter->NextColumn());
    for (share = sharesBegin; share != sharesEnd; share++) {
      ipLongWriter->WriteBatch(1, nullptr, nullptr, (int32_t *)&(share->ip_));
    }

    // user_id
    auto userIdWriter =
        static_cast<parquet::Int32Writer *>(rgWriter->NextColumn());
    for (share = sharesBegin; share != sharesEnd; share++) {
      userIdWriter->WriteBatch(
          1, nullptr, nullptr, (int32_t *)&(share->userId_));
    }

    // share_diff
    auto shareDiffWriter =
        static_cast<parquet::Int64Writer *>(rgWriter->NextColumn());
    for (share = sharesBegin; share != sharesEnd; share++) {
      shareDiffWriter->WriteBatch(
          1, nullptr, nullptr, (int64_t *)&(share->share_));
    }

    // timestamp
    auto timestampWriter =
        static_cast<parquet::Int64Writer *>(rgWriter->NextColumn());
    for (share = sharesBegin; share != sharesEnd; share++) {
      int64_t timeMillis = share->timestamp_ * 1000;
      timestampWriter->WriteBatch(1, nullptr, nullptr, &timeMillis);
    }

    // block_bits
    auto blkBitsWriter =
        static_cast<parquet::Int32Writer *>(rgWriter->NextColumn());
    for (share = sharesBegin; share != sharesEnd; share++) {
      blkBitsWriter->WriteBatch(
          1, nullptr, nullptr, (int32_t *)&(share->blkBits_));
    }

    // result
    auto resultWriter =
        static_cast<parquet::Int32Writer *>(rgWriter->NextColumn());
    for (share = sharesBegin; share != sharesEnd; share++) {
      resultWriter->WriteBatch(
          1, nullptr, nullptr, (int32_t *)&(share->result_));
    }

    // Save current RowGroup
    rgWriter->Close();
  }

  ~ParquetWriter() { fileWriter->Close(); }

protected:
  std::shared_ptr<GroupNode> setupSchema() {
    parquet::schema::NodeVector fields;

    fields.push_back(PrimitiveNode::Make(
        "job_id", Repetition::REQUIRED, Type::INT64, LogicalType::INT_64));
    fields.push_back(PrimitiveNode::Make(
        "worker_id", Repetition::REQUIRED, Type::INT64, LogicalType::INT_64));
    fields.push_back(PrimitiveNode::Make(
        "ip_long", Repetition::REQUIRED, Type::INT32, LogicalType::INT_32));
    fields.push_back(PrimitiveNode::Make(
        "user_id", Repetition::REQUIRED, Type::INT32, LogicalType::INT_32));
    fields.push_back(PrimitiveNode::Make(
        "share_diff", Repetition::REQUIRED, Type::INT64, LogicalType::INT_64));
    fields.push_back(PrimitiveNode::Make(
        "timestamp",
        Repetition::REQUIRED,
        Type::INT64,
        LogicalType::TIMESTAMP_MILLIS));
    fields.push_back(PrimitiveNode::Make(
        "block_bits", Repetition::REQUIRED, Type::INT32, LogicalType::INT_32));
    fields.push_back(PrimitiveNode::Make(
        "result", Repetition::REQUIRED, Type::INT32, LogicalType::INT_32));

    // Create a GroupNode named 'share_bitcoin_v1' using the primitive nodes
    // defined above This GroupNode is the root node of the schema tree
    return std::static_pointer_cast<GroupNode>(
        GroupNode::Make("share_bitcoin_v1", Repetition::REQUIRED, fields));
  }
};

int main(int argc, char **argv) {
  char *inFile = NULL;
  char *outFile = NULL;
  size_t batchShareNum = DEFAULT_NUM_ROWS_PER_ROW_GROUP;
  int c;

  if (argc <= 1) {
    usage();
    return 1;
  }
  while ((c = getopt(argc, argv, "i:o:n:h")) != -1) {
    switch (c) {
    case 'i':
      inFile = optarg;
      break;
    case 'o':
      outFile = optarg;
      break;
    case 'n':
      batchShareNum = strtoull(optarg, nullptr, 10);
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
      LOG(FATAL) << "missing input file (-i \"<input-sharelog-v1-file.bin>\")";
      return 1;
    }

    if (outFile == NULL) {
      LOG(FATAL) << "missing output file (-o \"<output-parquet-file.bin>\")";
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
    ParquetWriter out;
    auto stat = out.open(outFile);
    if (!stat.ok()) {
      LOG(FATAL) << "open output file " << outFile
                 << " failed: " << stat.message();
      return 1;
    }

    ShareBitcoinV1 *shares = new ShareBitcoinV1[batchShareNum];
    size_t shareSize = sizeof(ShareBitcoinV1) * batchShareNum;
    size_t shareCounter = 0;

    in.read((char *)shares, shareSize);
    while (!in.eof()) {
      size_t shareNum = in.gcount() / sizeof(ShareBitcoinV1);
      out.addShares(shares, shareNum);

      shareCounter += shareNum;
      LOG(INFO) << "added " << shareCounter << " shares to parquet...";

      // read next batch of shares
      in.read((char *)shares, shareSize);
    }

    delete[] shares;
    LOG(INFO) << "completed.";
  } catch (const SettingException &e) {
    LOG(FATAL) << "config missing: " << e.getPath();
    return 1;
  }

  google::ShutdownGoogleLogging();
  return 0;
}

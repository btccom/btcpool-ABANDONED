#pragma once

#include <string>
#include <vector>

#include <arrow/io/file.h>
#include <arrow/util/logging.h>
#include <parquet/api/reader.h>
#include <parquet/api/writer.h>

#include "zlibstream/zstr.hpp"

using namespace std;

using arrow::io::FileOutputStream;
using parquet::LogicalType;
using parquet::Repetition;
using parquet::Type;
using parquet::schema::GroupNode;
using parquet::schema::PrimitiveNode;

const size_t DEFAULT_NUM_ROWS_PER_ROW_GROUP = 1000000;

class ParquetWriter {
protected:
  std::shared_ptr<parquet::ParquetFileWriter> fileWriter_;
  uint64_t index_ = 0;
  size_t shareNum_ = 0;

public:
  arrow::Status open(const string &outFile, time_t hour) {
    if (fileWriter_) {
      close();
    }

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
    fileWriter_ = parquet::ParquetFileWriter::Open(out, schema, props);

    index_ = static_cast<uint64_t>(hour) << 32;

    return stat;
  }

  void close() {
    if (fileWriter_) {
      if (shareNum_ > 0) {
        flushShares();
      }
      fileWriter_->Close();
      fileWriter_ = nullptr;
    }
  }

  virtual ~ParquetWriter() { close(); }

protected:
  virtual std::shared_ptr<GroupNode> setupSchema() = 0;
  virtual void flushShares() = 0;
};

template <typename SHARE>
class ParquetWriterT : public ParquetWriter {
protected:
  ~ParquetWriterT() { flushShares(); }

  // Need specialization
  std::shared_ptr<GroupNode> setupSchema() override;
  void flushShares() override;

public:
  void addShare(const SHARE &share);
};

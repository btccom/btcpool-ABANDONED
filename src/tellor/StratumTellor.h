#pragma once

#include "Stratum.h"
#include "CommonTellor.h"

#include "tellor/tellor.pb.h"
#include <uint256.h>

class ShareTellor
  : public sharebase::Unserializable<ShareTellor, sharebase::TellorMsg> {
public:
  const static uint32_t CURRENT_VERSION =
      0x0cb0001u; // first 0cb0: Tellor, second 0001: version 1

  ShareTellor() {
    set_version(0);
    set_workerhashid(0);
    set_userid(0);
    set_status(0);
    set_timestamp(0);
    set_ip("0.0.0.0");
    set_sharediff(0);
    set_blockbits(0);
    set_height(0);
    set_nonce(0);
    set_sessionid(0);
    set_username("");
    set_workername("");
  }
  ShareTellor(const ShareTellor &r) = default;
  ShareTellor &operator=(const ShareTellor &r) = default;

  double score() const {

    if (!StratumStatus::isAccepted(status()) || sharediff() == 0 ||
        blockbits() == 0) {
      return 0.0;
    }

    // Network diff may less than share diff on testnet or regression test
    // network. On regression test network, the network diff may be zero. But no
    // matter how low the network diff is, you can only dig one block at a time.

    double networkDiff = blockdiff();

    if (networkDiff < sharediff()) {
      return 1.0;
    } else {
      return (double)sharediff() / networkDiff;
    }
  }

  bool isValid() const {
    if (version() != CURRENT_VERSION) {
      return false;
    }
    if (workerhashid() == 0 || sharediff() == 0) {
      return false;
    }
    return true;
  }

  string toString() const {
    return Strings::Format(
        "share(jobId: %" PRIu64 ",height: %" PRIu64
        ",..., ip: %s, userId: %d, "
        "workerId: %" PRId64
        ", time: %u/%s, "
        "shareDiff: %" PRIu64 ", blockbits: %" PRIu64 ", nonce: %016" PRIx64
        ", "
        "sessionId: %08x, status: %d/%s, username: %s, workername: %s)",
        jobid(),
        height(),
        ip().c_str(),
        userid(),
        workerhashid(),
        timestamp(),
        date("%F %T", timestamp()).c_str(),
        sharediff(),
        blockbits(),
        nonce(),
        sessionid(),
        status(),
        StratumStatus::toString(status()),
        username(),
        workername());
  }
};

class StratumJobTellor : public StratumJob {
public:
  StratumJobTellor();
  string serializeToJson() const override;
  bool unserializeFromJson(const char *s, size_t len) override;
  bool initFromRawJob(const string &msg);
  uint64_t height() const override { return height_; }

  string challenge_;
  string publicAddress_;
  uint64_t height_; // we use eth height as trb height
  uint64_t requestId_;
  uint64_t difficulty_;
  uint64_t nTime_;
  uint64_t timestamp_;
};

class StratumServerTellor;
class StratumSessionTellor;

struct StratumTraitsTellor {
  using ServerType = StratumServerTellor;
  using SessionType = StratumSessionTellor;
  using LocalJobType = LocalJob;
  struct JobDiffType {
    uint64_t currentJobDiff_;
    std::set<uint64_t> jobDiffs_;

    JobDiffType &operator=(uint64_t diff) {
      jobDiffs_.insert(diff);
      currentJobDiff_ = diff;
      return *this;
    }
  };
};

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
#pragma once

#include "Network.h"
#include "KafkaRepeater.hpp"
#include "shares.hpp"
#include "utilities_js.hpp"


class ShareDiffChangerBitcoin : public KafkaRepeater {
public:
    // Inherit the constructor of the parent class
    using KafkaRepeater::KafkaRepeater;

    bool initStratumJobConsumer(const string &jobBrocker, const string &jobTopic, const string &jobGroupId, int64_t jobTimeOffset) {
        jobConsumer_ = new KafkaHighLevelConsumer(jobBrocker.c_str(), jobTopic.c_str(), 0/* patition */, jobGroupId.c_str());
        jobTimeOffset_ = jobTimeOffset;

        LOG(INFO) << "setup kafka consumer of stratum job...";
        if (!jobConsumer_->setup()) {
            LOG(ERROR) << "setup kafka consumer fail";
            return false;
        }

        return true;
    }


protected:
    uint32_t getBitsByTime(uint64_t time) {
        if (time <= currentTime_) {
            return currentBits_;
        }

        const int32_t kTimeoutMs = 1000;
        
        while (time > currentTime_) {
            //
            // consume message
            //
            rd_kafka_message_t *rkmessage;
            rkmessage = jobConsumer_->consumer(kTimeoutMs);

            // timeout, most of time it's not nullptr and set an error:
            //          rkmessage->err == RD_KAFKA_RESP_ERR__PARTITION_EOF
            if (rkmessage == nullptr) {
                continue;
            }

            // check error
            if (rkmessage->err) {
                if (rkmessage->err == RD_KAFKA_RESP_ERR__PARTITION_EOF) {
                    // Reached the end of the topic+partition queue on the broker.
                    // Not really an error.
                    //      LOG(INFO) << "consumer reached end of " << rd_kafka_topic_name(rkmessage->rkt)
                    //      << "[" << rkmessage->partition << "] "
                    //      << " message queue at offset " << rkmessage->offset;
                    // acturlly
                    rd_kafka_message_destroy(rkmessage);  /* Return message to rdkafka */
                    continue;
                }

                LOG(ERROR) << "consume error for topic " << rd_kafka_topic_name(rkmessage->rkt)
                           << "[" << rkmessage->partition << "] offset " << rkmessage->offset
                           << ": " << rd_kafka_message_errstr(rkmessage);

                if (rkmessage->err == RD_KAFKA_RESP_ERR__UNKNOWN_PARTITION ||
                    rkmessage->err == RD_KAFKA_RESP_ERR__UNKNOWN_TOPIC) {
                    LOG(FATAL) << "consume fatal";
                    running_ = false;
                    rd_kafka_message_destroy(rkmessage);  /* Return message to rdkafka */
                    continue;
                }

                rd_kafka_message_destroy(rkmessage);  /* Return message to rdkafka */
                continue;
            }
            
            DLOG(INFO) << "a new message, size: " << rkmessage->len;

            // parse stratum job
            unserializeStratumJob((const char*)rkmessage->payload, rkmessage->len, currentBits_, currentTime_);
            
            rd_kafka_message_destroy(rkmessage);  /* Return message to rdkafka */
        }

        return currentBits_;
    }

    bool unserializeStratumJob(const char *s, size_t len, uint32_t &bits, uint64_t &time) {
        JsonNode j;
        if (!JsonNode::parse(s, s + len, j)) {
            return false;
        }
        if (j["jobId"].type()        != Utilities::JS::type::Int ||
            j["gbtHash"].type()      != Utilities::JS::type::Str ||
            j["prevHash"].type()     != Utilities::JS::type::Str ||
            j["prevHashBeStr"].type()!= Utilities::JS::type::Str ||
            j["height"].type()       != Utilities::JS::type::Int ||
            j["coinbase1"].type()    != Utilities::JS::type::Str ||
            j["coinbase2"].type()    != Utilities::JS::type::Str ||
            j["merkleBranch"].type() != Utilities::JS::type::Str ||
            j["nVersion"].type()     != Utilities::JS::type::Int ||
            j["nBits"].type()        != Utilities::JS::type::Int ||
            j["nTime"].type()        != Utilities::JS::type::Int ||
            j["minTime"].type()      != Utilities::JS::type::Int ||
            j["coinbaseValue"].type()!= Utilities::JS::type::Int) {
            LOG(ERROR) << "parse stratum job failure: " << s;
            return false;
        }

        uint32_t nBits = j["nBits"].uint32();
        uint64_t nTime = j["nTime"].uint64();

        if (nBits != bits) {
            LOG(INFO) << "network diff changed, old bits: " << StringFormat("%08x", bits)
                      << ", new bits: " << StringFormat("%08x", nBits) << ", time: " << date("%F %T", nTime)
                      << std::endl; // you must add an endl or the log in runMessageNumberDisplayThread() may not displayed. I don't know the reason.
        }

        bits = nBits;
        time = nTime + jobTimeOffset_;
        return true;
    }

    KafkaHighLevelConsumer *jobConsumer_ = nullptr;
    
    uint64_t currentTime_ = 0;
    uint32_t currentBits_ = 0;
    int64_t jobTimeOffset_ = 0;
};

class ShareDiffChangerBitcoinV1 : public ShareDiffChangerBitcoin {
    // Inherit the constructor of the parent class
    using ShareDiffChangerBitcoin::ShareDiffChangerBitcoin;

    bool repeatMessage(rd_kafka_message_t *rkmessage) override {
        if (rkmessage->len != sizeof(ShareBitcoinV1)) {
            LOG(WARNING) << "Wrong ShareBitcoinV1 size: " << rkmessage->len << ", should be " << sizeof(ShareBitcoinV1);
            return false;
        }

        ShareBitcoinV1 shareV1;
        memcpy((uint8_t *)&shareV1, (const uint8_t *)rkmessage->payload, rkmessage->len);

        shareV1.blkBits_ = getBitsByTime(shareV1.timestamp_);

        sendToKafka(shareV1);
        return true;
    }
};

class ShareDiffChangerBitcoinV2ToV1 : public ShareDiffChangerBitcoin {
    // Inherit the constructor of the parent class
    using ShareDiffChangerBitcoin::ShareDiffChangerBitcoin;

    bool repeatMessage(rd_kafka_message_t *rkmessage) override {
        if (rkmessage->len != sizeof(ShareBitcoinV2)) {
            LOG(WARNING) << "Wrong ShareBitcoinV2 size: " << rkmessage->len << ", should be " << sizeof(ShareBitcoinV2);
            return false;
        }

        ShareBitcoinV2 shareV2;
        memcpy((uint8_t *)&shareV2, (const uint8_t *)rkmessage->payload, rkmessage->len);

        ShareBitcoinV1 shareV1;
        if (!shareV2.toShareBitcoinV1(shareV1)) {
          return false;
        }

        shareV1.blkBits_ = getBitsByTime(shareV1.timestamp_);
    
        sendToKafka(shareV1);
        return true;
    }
};

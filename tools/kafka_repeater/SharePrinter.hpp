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


class SharePrinterBitcoinV1 : public KafkaRepeater {
    // Inherit the constructor of the parent class
    using KafkaRepeater::KafkaRepeater;

    bool repeatMessage(rd_kafka_message_t *rkmessage) override {
        if (rkmessage->len != sizeof(ShareBitcoinV1)) {
            LOG(WARNING) << "Wrong ShareBitcoinV1 size: " << rkmessage->len << ", should be " << sizeof(ShareBitcoinV1);
            return false;
        }

        ShareBitcoinV1 shareV1;
        memcpy((uint8_t *)&shareV1, (const uint8_t *)rkmessage->payload, rkmessage->len);
        
        LOG(INFO) << shareV1.toString();
        return true;
    }
};

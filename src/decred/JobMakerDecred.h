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

#ifndef JOB_MAKER_DECRED_H_
#define JOB_MAKER_DECRED_H_

#include "JobMaker.h"
#include "CommonDecred.h"
#include "utilities_js.hpp"
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/composite_key.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/tag.hpp>

struct GetWorkDecred {
  GetWorkDecred(
      const string &data,
      const string &target,
      uint32_t createdAt,
      uint32_t height,
      NetworkDecred network,
      uint32_t size,
      uint16_t voters)
    : data(data)
    , target(target)
    , createdAt(createdAt)
    , height(height)
    , network(network)
    , size(size)
    , voters(voters) {}

  string data;
  string target;
  uint32_t createdAt;
  uint32_t height;
  NetworkDecred network;
  uint32_t size;
  uint16_t voters;
};

struct ByBestBlockDecred {};
struct ByCreationTimeDecred {};
struct ByDataDecred {};

// The getwork job dimensions
// - Height + voters + size + creation time: ordered non-unique for best block
// retrieval
// - Creation time: ordered non-unique for timeout handling
// - Data: hashed unique for duplication checks
using GetWorkDecredMap = boost::multi_index_container<
    GetWorkDecred,
    boost::multi_index::indexed_by<
        boost::multi_index::ordered_non_unique<
            boost::multi_index::tag<ByBestBlockDecred>,
            boost::multi_index::composite_key<
                GetWorkDecred,
                boost::multi_index::member<
                    GetWorkDecred,
                    NetworkDecred,
                    &GetWorkDecred::network>,
                boost::multi_index::
                    member<GetWorkDecred, uint32_t, &GetWorkDecred::height>,
                boost::multi_index::
                    member<GetWorkDecred, uint16_t, &GetWorkDecred::voters>,
                boost::multi_index::
                    member<GetWorkDecred, uint32_t, &GetWorkDecred::size>,
                boost::multi_index::member<
                    GetWorkDecred,
                    uint32_t,
                    &GetWorkDecred::createdAt>>>,
        boost::multi_index::ordered_non_unique<
            boost::multi_index::tag<ByCreationTimeDecred>,
            boost::multi_index::
                member<GetWorkDecred, uint32_t, &GetWorkDecred::createdAt>>,
        boost::multi_index::hashed_unique<
            boost::multi_index::tag<ByDataDecred>,
            boost::multi_index::
                member<GetWorkDecred, string, &GetWorkDecred::data>>>>;

class JobMakerHandlerDecred : public GwJobMakerHandler {
public:
  JobMakerHandlerDecred();
  bool processMsg(const string &msg) override;
  string makeStratumJobMsg() override;

private:
  bool processMsg(JsonNode &j);
  bool validate(JsonNode &j);
  void clearTimeoutWorks();

  GetWorkDecredMap works_;
};

#endif

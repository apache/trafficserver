/** @file
 *
 *  A brief file description
 *
 *  @section license License
 *
 *  Licensed to the Apache Software Foundation (ASF) under one
 *  or more contributor license agreements.  See the NOTICE file
 *  distributed with this work for additional information
 *  regarding copyright ownership.  The ASF licenses this file
 *  to you under the Apache License, Version 2.0 (the
 *  "License"); you may not use this file except in compliance
 *  with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include "tsutil/Metrics.h"
#include "proxy/http3/Http3.h"
#include "proxy/http3/Http3FrameCounter.h"

std::vector<Http3FrameType>
Http3FrameCounter::interests()
{
  return {Http3FrameType::DATA,         Http3FrameType::HEADERS,      Http3FrameType::X_RESERVED_1, Http3FrameType::CANCEL_PUSH,
          Http3FrameType::SETTINGS,     Http3FrameType::PUSH_PROMISE, Http3FrameType::X_RESERVED_2, Http3FrameType::GOAWAY,
          Http3FrameType::X_RESERVED_3, Http3FrameType::X_RESERVED_4, Http3FrameType::MAX_PUSH_ID,  Http3FrameType::X_MAX_DEFINED,
          Http3FrameType::UNKNOWN};
}

Http3ErrorUPtr
Http3FrameCounter::handle_frame(std::shared_ptr<const Http3Frame> frame, int32_t frame_seq, Http3StreamType s_type)
{
  Http3ErrorUPtr error  = Http3ErrorUPtr(nullptr);
  Http3FrameType f_type = frame->type();
  if (f_type > Http3FrameType::X_MAX_DEFINED) {
    f_type = Http3FrameType::X_MAX_DEFINED;
  }
  this->_frame_counts_in[static_cast<int>(f_type)]++;
  Metrics::Counter::increment(http3_frame_metrics_in[static_cast<int>(f_type)]);

  return error;
}

uint64_t
Http3FrameCounter::get_count(uint64_t type) const
{
  if (type == 0x21) { // TS_SSN_INFO_RECEIVED_FRAME_COUNT_H3_UNKNOWN in apidefs.h.in
    return this->_frame_counts_in[static_cast<int>(Http3FrameType::X_MAX_DEFINED)];
  } else {
    return this->_frame_counts_in[type];
  }
}

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

#pragma once

#include "proxy/http3/Http3Types.h"
#include "proxy/http3/Http3FrameHandler.h"

class Http3FrameCounter : public Http3FrameHandler
{
public:
  Http3FrameCounter(){};

  // Http3FrameHandler
  std::vector<Http3FrameType> interests() override;
  Http3ErrorUPtr handle_frame(std::shared_ptr<const Http3Frame> frame, int32_t frame_seq = -1,
                              Http3StreamType s_type = Http3StreamType::UNKNOWN) override;

  uint64_t get_count(uint64_t type) const;

private:
  // Counter for received frames
  std::atomic<uint64_t> _frame_counts_in[static_cast<int>(Http3FrameType::UNKNOWN) + 1] = {
    0, // DATA
    0, // HEADERS
    0, // X_RESERVED_1
    0, // CANCEL_PUSH
    0, // SETTINGS
    0, // PUSH_PROMISE
    0, // X_RESERVED_2
    0, // GOAWAY
    0, // X_RESERVED_3
    0, // X_RESERVED_4
    0, // UNDEFINED
    0, // UNDEFINED
    0, // UNDEFINED
    0, // MAX_PUSH_ID
    0  // UNKNOWN
  };
};

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

#include "tscore/ink_defs.h"
#include "tsutil/Metrics.h"
#include "Http3Types.h"

using ts::Metrics;

extern const uint32_t HTTP3_DEFAULT_HEADER_TABLE_SIZE;
extern const uint32_t HTTP3_DEFAULT_MAX_FIELD_SECTION_SIZE;
extern const uint32_t HTTP3_DEFAULT_QPACK_BLOCKED_STREAMS;
extern const uint32_t HTTP3_DEFAULT_NUM_PLACEHOLDERS;

class Http3
{
public:
  static void init();
};

// Statistics
struct Http3StatsBlock {
  // Example: Metrics::Counter::AtomicType *current_client_session_count;
  // Once created, e.g.
  // Metrics::Counter::increment(http3_rsb.current_client_session_count);
  Metrics::Counter::AtomicType *data_frames_in;
  Metrics::Counter::AtomicType *headers_frames_in;
  Metrics::Counter::AtomicType *cancel_push_frames_in;
  Metrics::Counter::AtomicType *settings_frames_in;
  Metrics::Counter::AtomicType *push_promise_frames_in;
  Metrics::Counter::AtomicType *goaway_frames_in;
  Metrics::Counter::AtomicType *max_push_id;
  Metrics::Counter::AtomicType *unknown_frames_in;
};

extern Http3StatsBlock               http3_rsb; // Container for statistics.
extern Metrics::Counter::AtomicType *http3_frame_metrics_in[static_cast<int>(Http3FrameType::UNKNOWN) + 1];

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

#include "proxy/http3/Http3.h"
#include "proxy/http3/Http3Types.h"

// Default values of settings defined by specs (draft-17)
const uint32_t HTTP3_DEFAULT_HEADER_TABLE_SIZE      = 0;
const uint32_t HTTP3_DEFAULT_MAX_FIELD_SECTION_SIZE = UINT32_MAX;
const uint32_t HTTP3_DEFAULT_QPACK_BLOCKED_STREAMS  = 0;
const uint32_t HTTP3_DEFAULT_NUM_PLACEHOLDERS       = 0;

Http3StatsBlock http3_rsb;

Metrics::Counter::AtomicType *http3_frame_metrics_in[static_cast<int>(Http3FrameType::UNKNOWN) + 1];

void
Http3::init()
{
  // Setup statistics
  http3_rsb.data_frames_in         = Metrics::Counter::createPtr("proxy.process.http3.data_frames_in");
  http3_rsb.headers_frames_in      = Metrics::Counter::createPtr("proxy.process.http3.headers_frames_in");
  http3_rsb.cancel_push_frames_in  = Metrics::Counter::createPtr("proxy.process.http3.cancel_push_frames_in");
  http3_rsb.settings_frames_in     = Metrics::Counter::createPtr("proxy.process.http3.settings_frames_in");
  http3_rsb.push_promise_frames_in = Metrics::Counter::createPtr("proxy.process.http3.push_promise_frames_in");
  http3_rsb.goaway_frames_in       = Metrics::Counter::createPtr("proxy.process.http3.goaway_frames_in");
  http3_rsb.max_push_id            = Metrics::Counter::createPtr("proxy.process.http3.max_push_id_frames_in");
  http3_rsb.unknown_frames_in      = Metrics::Counter::createPtr("proxy.process.http3.unknown_frames_in");

  http3_frame_metrics_in[static_cast<int>(Http3FrameType::DATA)]         = http3_rsb.data_frames_in;
  http3_frame_metrics_in[static_cast<int>(Http3FrameType::HEADERS)]      = http3_rsb.headers_frames_in;
  http3_frame_metrics_in[static_cast<int>(Http3FrameType::X_RESERVED_1)] = http3_rsb.unknown_frames_in;
  http3_frame_metrics_in[static_cast<int>(Http3FrameType::CANCEL_PUSH)]  = http3_rsb.cancel_push_frames_in;
  http3_frame_metrics_in[static_cast<int>(Http3FrameType::SETTINGS)]     = http3_rsb.settings_frames_in;
  http3_frame_metrics_in[static_cast<int>(Http3FrameType::PUSH_PROMISE)] = http3_rsb.push_promise_frames_in;
  http3_frame_metrics_in[static_cast<int>(Http3FrameType::X_RESERVED_2)] = http3_rsb.unknown_frames_in;
  http3_frame_metrics_in[static_cast<int>(Http3FrameType::GOAWAY)]       = http3_rsb.goaway_frames_in;
  http3_frame_metrics_in[static_cast<int>(Http3FrameType::X_RESERVED_3)] = http3_rsb.unknown_frames_in;
  http3_frame_metrics_in[static_cast<int>(Http3FrameType::X_RESERVED_4)] = http3_rsb.unknown_frames_in;
  http3_frame_metrics_in[10]                                             = http3_rsb.unknown_frames_in;
  http3_frame_metrics_in[11]                                             = http3_rsb.unknown_frames_in;
  http3_frame_metrics_in[12]                                             = http3_rsb.unknown_frames_in;
  http3_frame_metrics_in[static_cast<int>(Http3FrameType::MAX_PUSH_ID)]  = http3_rsb.max_push_id;
  http3_frame_metrics_in[static_cast<int>(Http3FrameType::UNKNOWN)]      = http3_rsb.unknown_frames_in;
}

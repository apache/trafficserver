/** @file

  Http2DebugNames

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 */

#include "Http2DebugNames.h"

#include "HTTP2.h"

const char *
Http2DebugNames::get_settings_param_name(uint16_t id)
{
  switch (id) {
  case HTTP2_SETTINGS_HEADER_TABLE_SIZE:
    return "HEADER_TABLE_SIZE";
  case HTTP2_SETTINGS_ENABLE_PUSH:
    return "ENABLE_PUSH";
  case HTTP2_SETTINGS_MAX_CONCURRENT_STREAMS:
    return "MAX_CONCURRENT_STREAMS";
  case HTTP2_SETTINGS_INITIAL_WINDOW_SIZE:
    return "INITIAL_WINDOW_SIZE";
  case HTTP2_SETTINGS_MAX_FRAME_SIZE:
    return "MAX_FRAME_SIZE";
  case HTTP2_SETTINGS_MAX_HEADER_LIST_SIZE:
    return "MAX_HEADER_LIST_SIZE";
  }

  return "UNKNOWN";
}

const char *
Http2DebugNames::get_state_name(uint16_t id)
{
  switch (id) {
  case HTTP2_STREAM_STATE_IDLE:
    return "HTTP2_STREAM_STATE_IDLE";
  case HTTP2_STREAM_STATE_RESERVED_LOCAL:
    return "HTTP2_STREAM_STATE_RESERVED_LOCAL";
  case HTTP2_STREAM_STATE_RESERVED_REMOTE:
    return "HTTP2_STREAM_STATE_RESERVED_REMOTE";
  case HTTP2_STREAM_STATE_OPEN:
    return "HTTP2_STREAM_STATE_OPEN";
  case HTTP2_STREAM_STATE_HALF_CLOSED_LOCAL:
    return "HTTP2_STREAM_STATE_HALF_CLOSED_LOCAL";
  case HTTP2_STREAM_STATE_HALF_CLOSED_REMOTE:
    return "HTTP2_STREAM_STATE_HALF_CLOSED_REMOTE";
  case HTTP2_STREAM_STATE_CLOSED:
    return "HTTP2_STREAM_STATE_CLOSED";
  }

  return "UNKNOWN";
}

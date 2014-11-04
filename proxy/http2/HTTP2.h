/** @file
 *
 *  Fundamental HTTP/2 protocol definitions and parsers.
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

#ifndef __HTTP2_H__
#define __HTTP2_H__

#include "ink_defs.h"
#include "ink_memory.h"
#include "HPACK.h"
#include "MIME.h"

class HTTPHdr;

typedef unsigned Http2StreamId;

// 6.9.2 Initial Flow Control Window Size - the flow control window can be come negative
// so we need to track it with a signed type.
typedef int32_t Http2WindowSize;

extern const char * const HTTP2_CONNECTION_PREFACE;
const size_t HTTP2_CONNECTION_PREFACE_LEN = 24;

const size_t HTTP2_FRAME_HEADER_LEN = 9;
const size_t HTTP2_GOAWAY_LEN = 8;
const size_t HTTP2_SETTINGS_PARAMETER_LEN = 6;

// 4.2. Frame Size. The absolute maximum size of a frame payload is 2^14-1 (16,383) octets.
const size_t HTTP2_MAX_FRAME_PAYLOAD = 16383;

enum Http2ErrorCode
{
  HTTP2_ERROR_NO_ERROR = 0,
  HTTP2_ERROR_PROTOCOL_ERROR = 1,
  HTTP2_ERROR_INTERNAL_ERROR = 2,
  HTTP2_ERROR_FLOW_CONTROL_ERROR = 3,
  HTTP2_ERROR_SETTINGS_TIMEOUT = 4,
  HTTP2_ERROR_STREAM_CLOSED = 5,
  HTTP2_ERROR_FRAME_SIZE_ERROR = 6,
  HTTP2_ERROR_REFUSED_STREAM = 7,
  HTTP2_ERROR_CANCEL = 8,
  HTTP2_ERROR_COMPRESSION_ERROR = 9,
  HTTP2_ERROR_CONNECT_ERROR = 10,
  HTTP2_ERROR_ENHANCE_YOUR_CALM = 11,
  HTTP2_ERROR_INADEQUATE_SECURITY = 12,
  HTTP2_ERROR_HTTP_1_1_REQUIRED = 13,

  HTTP2_ERROR_MAX,
};

enum Http2FrameType
{
  HTTP2_FRAME_TYPE_DATA = 0,
  HTTP2_FRAME_TYPE_HEADERS = 1,
  HTTP2_FRAME_TYPE_PRIORITY = 2,
  HTTP2_FRAME_TYPE_RST_STREAM = 3,
  HTTP2_FRAME_TYPE_SETTINGS = 4,
  HTTP2_FRAME_TYPE_PUSH_PROMISE = 5,
  HTTP2_FRAME_TYPE_PING = 6,
  HTTP2_FRAME_TYPE_GOAWAY = 7,
  HTTP2_FRAME_TYPE_WINDOW_UPDATE = 8,
  HTTP2_FRAME_TYPE_CONTINUATION = 9,
  HTTP2_FRAME_TYPE_ALTSVC = 10,
  HTTP2_FRAME_TYPE_BLOCKED = 11,

  HTTP2_FRAME_TYPE_MAX,
};

// 6.1 Data
enum Http2FrameFlagsData
{
  HTTP2_FLAGS_DATA_END_STREAM = 0x01,
  HTTP2_FLAGS_DATA_END_SEGMENT = 0x02,
  HTTP2_FLAGS_DATA_PAD_LOW = 0x08,
  HTTP2_FLAGS_DATA_PAD_HIGH = 0x10,
  HTTP2_FLAGS_DATA_COMPRESSESD = 0x20,

  HTTP2_FLAGS_DATA_MASK = 0x2B,
};

// 6.2 Headers
enum Http2FrameFlagsHeaders
{
  HTTP2_FLAGS_HEADERS_END_STREAM = 0x01,
  HTTP2_FLAGS_HEADERS_END_SEGMENT = 0x02,
  HTTP2_FLAGS_HEADERS_PAD_LOW = 0x08,
  HTTP2_FLAGS_HEADERS_PAD_HIGH = 0x10,
  HTTP2_FLAGS_HEADERS_PRIORITY = 0x20,

  HTTP2_FLAGS_HEADERS_MASK = 0x2B,
};

// 6.3 Priority
enum Http2FrameFlagsPriority
{
  HTTP2_FLAGS_PRIORITY_MASK = 0x00
};

// 6.3 Rst Stream
enum Http2FrameFlagsRstStream
{
  HTTP2_FLAGS_RST_STREAM_MASK = 0x00
};

// 6.4 Settings
enum Http2FrameFlagsSettings
{
  HTTP2_FLAGS_SETTINGS_ACK = 0x01,

  HTTP2_FLAGS_SETTINGS_MASK = 0x01
};

// 6.6 Push Promise
enum Http2FrameFlagsPushPromise
{
  HTTP2_FLAGS_PUSH_PROMISE_END_HEADERS = 0x04,
  HTTP2_FLAGS_PUSH_PROMISE_PAD_LOW = 0x08,
  HTTP2_FLAGS_PUSH_PROMISE_PAD_HIGH = 0x10,

  HTTP2_FLAGS_PUSH_PROMISE_MASK = 0x1C,
};

// 6.7 Ping
enum Http2FrameFlagsPing
{
  HTTP2_FLAGS_PING_ACK = 0x01,

  HTTP2_FLAGS_PING_MASK = 0x01
};

// 6.8 Goaway
enum Http2FrameFlagsGoaway
{
  HTTP2_FLAGS_GOAWAY_MASK = 0x00
};

// 6.9 Window Update
enum Http2FrameFlagsWindowUpdate
{
  HTTP2_FLAGS_WINDOW_UPDATE_MASK = 0x00
};

// 6.10 Continuation
enum Http2FrameFlagsContinuation
{
  HTTP2_FLAGS_CONTINUATION_END_HEADERS = 0x04,
  HTTP2_FLAGS_CONTINUATION_PAD_LOW = 0x08,
  HTTP2_FLAGS_CONTINUATION_PAD_HIGH = 0x10,

  HTTP2_FLAGS_CONTINUATION_MASK = 0x1C,
};

// 6.11 Altsvc
enum Http2FrameFlagsAltsvc
{
  HTTP2_FLAGS_ALTSVC_MASK = 0x00
};

// 6.12 Blocked
enum Http2FrameFlagsBlocked
{
  HTTP2_FLAGS_BLOCKED_MASK = 0x00
};

// 6.5.2 Defined SETTINGS Parameters
enum Http2SettingsIdentifier
{
  HTTP2_SETTINGS_HEADER_TABLE_SIZE = 1,
  HTTP2_SETTINGS_ENABLE_PUSH = 2,
  HTTP2_SETTINGS_MAX_CONCURRENT_STREAMS = 3,
  HTTP2_SETTINGS_INITIAL_WINDOW_SIZE = 4,
  HTTP2_SETTINGS_MAX_FRAME_SIZE = 5,
  HTTP2_SETTINGS_MAX_HEADER_LIST_SIZE = 6,

  HTTP2_SETTINGS_MAX
};

// 4.1. Frame Format
struct Http2FrameHeader
{
  uint32_t         length;
  uint8_t          type;
  uint8_t          flags;
  Http2StreamId    streamid;
};

// 6.5.1. SETTINGS Format
struct Http2SettingsParameter
{
  uint16_t  id;
  uint32_t  value;
};

// 6.8 GOAWAY Format
struct Http2Goaway
{
  Http2StreamId last_streamid;
  uint32_t      error_code;

  // NOTE: we don't (de)serialize the variable length debug data at this layer because there's
  // really nothing we can do with it without some out of band agreement. Trying to deal with it
  // just complicates memory management.
};

// 6.9.1 The Flow Control Window
static const Http2WindowSize HTTP2_MAX_WINDOW_SIZE = 0x7FFFFFFF;

// 6.9.2 Initial Flow Control Window Size
static const Http2WindowSize HTTP2_INITIAL_WINDOW_SIZE = 0x0000FFFF;

static inline bool
http2_is_client_streamid(Http2StreamId streamid) {
  return (streamid & 0x1u) == 0x1u;
}

static inline bool
http2_is_server_streamid(Http2StreamId streamid) {
  return (streamid & 0x1u) == 0x0u;
}

bool
http2_parse_frame_header(IOVec, Http2FrameHeader&);

bool
http2_write_frame_header(const Http2FrameHeader&, IOVec);

bool
http2_write_goaway(const Http2Goaway&, IOVec);

bool
http2_frame_header_is_valid(const Http2FrameHeader&);

bool
http2_settings_parameter_is_valid(const Http2SettingsParameter&);

bool
http2_parse_settings_parameter(IOVec, Http2SettingsParameter&);

MIMEParseResult
http2_parse_header_fragment(HTTPHdr *, IOVec, Http2HeaderTable&);

MIMEParseResult
convert_from_2_to_1_1_header(HTTPHdr * header);

int64_t
convert_from_1_1_to_2_header(HTTPHdr * in, uint8_t * out, uint64_t out_len, Http2HeaderTable& header_table);

#endif /* __HTTP2_H__ */

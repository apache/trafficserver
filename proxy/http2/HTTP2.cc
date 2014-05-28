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

#include "HTTP2.h"
#include "ink_assert.h"

const char * const HTTP2_CONNECTION_PREFACE = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";

union byte_pointer {
  byte_pointer(void * p) : ptr(p) {}

  void *      ptr;
  uint8_t *   u8;
  uint16_t *  u16;
  uint32_t *  u32;
};

template <typename T>
union byte_addressable_value
{
  uint8_t   bytes[sizeof(T)];
  T         value;
};

static void
write_and_advance(byte_pointer& dst, uint32_t src)
{
  byte_addressable_value<uint32_t> pval;

  pval.value = htonl(src);
  memcpy(dst.u8, pval.bytes, sizeof(pval.bytes));
  dst.u8 += sizeof(pval.bytes);
}

static void
write_and_advance(byte_pointer& dst, uint16_t src)
{
  byte_addressable_value<uint16_t> pval;

  pval.value = htons(src);
  memcpy(dst.u8, pval.bytes, sizeof(pval.bytes));
  dst.u8 += sizeof(pval.bytes);
}

static void
write_and_advance(byte_pointer& dst, uint8_t src)
{
  *dst.u8 = src;
  dst.u8++;
}

template<unsigned N> static void
memcpy_and_advance(uint8_t (&dst)[N], byte_pointer& src)
{
  memcpy(dst, src.u8, N);
  src.u8 += N;
}

void
memcpy_and_advance(uint8_t (&dst), byte_pointer& src)
{
  dst = *src.u8;
  ++src.u8;
}

static bool
http2_are_frame_flags_valid(uint8_t ftype, uint8_t fflags)
{
  static const uint8_t mask[HTTP2_FRAME_TYPE_MAX] = {
    HTTP2_FLAGS_DATA_MASK,
    HTTP2_FLAGS_HEADERS_MASK,
    HTTP2_FLAGS_PRIORITY_MASK,
    HTTP2_FLAGS_RST_STREAM_MASK,
    HTTP2_FLAGS_SETTINGS_MASK,
    HTTP2_FLAGS_PUSH_PROMISE_MASK,
    HTTP2_FLAGS_PING_MASK,
    HTTP2_FLAGS_GOAWAY_MASK,
    HTTP2_FLAGS_WINDOW_UPDATE_MASK,
    HTTP2_FLAGS_CONTINUATION_MASK,
    HTTP2_FLAGS_ALTSVC_MASK,
    HTTP2_FLAGS_BLOCKED_MASK,
  };

  // The frame flags are valid for this frame if nothing outside the defined bits is set.
  return (fflags & ~mask[ftype]) == 0;
}

bool
http2_frame_header_is_valid(const Http2FrameHeader& hdr)
{
  if (hdr.type >= HTTP2_FRAME_TYPE_MAX) {
    return false;
  }

  if (hdr.length > HTTP2_MAX_FRAME_PAYLOAD) {
    return false;
  }

  if (!http2_are_frame_flags_valid(hdr.type, hdr.flags)) {
    return false;
  }

  return true;
}

bool
http2_settings_parameter_is_valid(const Http2SettingsParameter& param)
{
  // Static maximum values for Settings parameters.
  static const unsigned settings_max[HTTP2_SETTINGS_MAX] = {
    0,
    UINT_MAX, // HTTP2_SETTINGS_HEADER_TABLE_SIZE
    1,        // HTTP2_SETTINGS_ENABLE_PUSH
    UINT_MAX, // HTTP2_SETTINGS_MAX_CONCURRENT_STREAMS
    HTTP2_MAX_WINDOW_SIZE, // HTTP2_SETTINGS_INITIAL_WINDOW_SIZE
    1,        // HTTP2_SETTINGS_COMPRESS_DATA
  };

  if (param.id == 0 || param.id >= HTTP2_SETTINGS_MAX) {
    return false;
  }

  if (param.value > settings_max[param.id]) {
    return false;
  }

  return true;
}

// 4.1.  Frame Format
//
// 0                   1                   2                   3
// 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// | R |     Length (14)           |   Type (8)    |   Flags (8)   |
// +-+-+-----------+---------------+-------------------------------+
// |R|                 Stream Identifier (31)                      |
// +-+-------------------------------------------------------------+
// |                   Frame Payload (0...)                      ...
// +---------------------------------------------------------------+

bool
http2_parse_frame_header(IOVec iov, Http2FrameHeader& hdr)
{
  byte_pointer ptr(iov.iov_base);
  byte_addressable_value<uint16_t> length;
  byte_addressable_value<uint32_t> streamid;

  if (unlikely(iov.iov_len < HTTP2_FRAME_HEADER_LEN)) {
    return false;
  }

  memcpy_and_advance(length.bytes, ptr);
  memcpy_and_advance(hdr.type, ptr);
  memcpy_and_advance(hdr.flags, ptr);
  memcpy_and_advance(streamid.bytes, ptr);

  length.bytes[0] &= 0x3F;  // Clear the 2 reserved high bits
  streamid.bytes[0] &= 0x7f;// Clear the high reserved bit
  hdr.length = ntohs(length.value);
  hdr.streamid = ntohl(streamid.value);

  return true;
}

bool
http2_write_frame_header(const Http2FrameHeader& hdr, IOVec iov)
{
  byte_pointer ptr(iov.iov_base);

  if (unlikely(iov.iov_len < HTTP2_FRAME_HEADER_LEN)) {
    return false;
  }

  write_and_advance(ptr, hdr.length);
  write_and_advance(ptr, hdr.type);
  write_and_advance(ptr, hdr.flags);
  write_and_advance(ptr, hdr.streamid);

  return true;
}

// 6.8. GOAWAY
//
// 0                   1                   2                   3
// 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |R|                  Last-Stream-ID (31)                        |
// +-+-------------------------------------------------------------+
// |                      Error Code (32)                          |
// +---------------------------------------------------------------+
// |                  Additional Debug Data (*)                    |
// +---------------------------------------------------------------+

bool
http2_write_goaway(const Http2Goaway& goaway, IOVec iov)
{
  byte_pointer ptr(iov.iov_base);

  if (unlikely(iov.iov_len < HTTP2_GOAWAY_LEN)) {
    return false;
  }

  write_and_advance(ptr, goaway.last_streamid);
  write_and_advance(ptr, goaway.error_code);

  return true;
}

// 6.5.1.  SETTINGS Format
//
// 0                   1                   2                   3
// 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// | Identifier (8)|        Value (32)                             |
// +---------------+-----------------------------------------------+
// |               |
// +---------------+

bool
http2_parse_settings_parameter(IOVec iov, Http2SettingsParameter& param)
{
  byte_pointer ptr(iov.iov_base);
  byte_addressable_value<uint32_t> pval;

  if (unlikely(iov.iov_len < HTTP2_SETTINGS_PARAMETER_LEN)) {
    return false;
  }

  memcpy_and_advance(param.id, ptr);
  memcpy_and_advance(pval.bytes, ptr);

  param.value = ntohl(pval.value);

  return true;
}

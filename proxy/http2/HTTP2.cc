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

#include "hdrs/VersionConverter.h"
#include "HTTP2.h"
#include "HPACK.h"

#include "tscore/ink_assert.h"
#include "tscpp/util/LocalBuffer.h"

#include "records/P_RecCore.h"
#include "records/P_RecProcess.h"

const char *const HTTP2_CONNECTION_PREFACE = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";

static const uint32_t HTTP2_MAX_TABLE_SIZE_LIMIT = 64 * 1024;

namespace
{
struct Http2HeaderName {
  const char *name = nullptr;
  int name_len     = 0;
};

static VersionConverter hvc;

} // namespace

// Statistics
RecRawStatBlock *http2_rsb;
static const char *const HTTP2_STAT_CURRENT_CLIENT_CONNECTION_NAME        = "proxy.process.http2.current_client_connections";
static const char *const HTTP2_STAT_CURRENT_ACTIVE_CLIENT_CONNECTION_NAME = "proxy.process.http2.current_active_client_connections";
static const char *const HTTP2_STAT_CURRENT_CLIENT_STREAM_NAME            = "proxy.process.http2.current_client_streams";
static const char *const HTTP2_STAT_TOTAL_CLIENT_STREAM_NAME              = "proxy.process.http2.total_client_streams";
static const char *const HTTP2_STAT_TOTAL_TRANSACTIONS_TIME_NAME          = "proxy.process.http2.total_transactions_time";
static const char *const HTTP2_STAT_TOTAL_CLIENT_CONNECTION_NAME          = "proxy.process.http2.total_client_connections";
static const char *const HTTP2_STAT_CONNECTION_ERRORS_NAME                = "proxy.process.http2.connection_errors";
static const char *const HTTP2_STAT_STREAM_ERRORS_NAME                    = "proxy.process.http2.stream_errors";
static const char *const HTTP2_STAT_SESSION_DIE_DEFAULT_NAME              = "proxy.process.http2.session_die_default";
static const char *const HTTP2_STAT_SESSION_DIE_OTHER_NAME                = "proxy.process.http2.session_die_other";
static const char *const HTTP2_STAT_SESSION_DIE_ACTIVE_NAME               = "proxy.process.http2.session_die_active";
static const char *const HTTP2_STAT_SESSION_DIE_INACTIVE_NAME             = "proxy.process.http2.session_die_inactive";
static const char *const HTTP2_STAT_SESSION_DIE_EOS_NAME                  = "proxy.process.http2.session_die_eos";
static const char *const HTTP2_STAT_SESSION_DIE_ERROR_NAME                = "proxy.process.http2.session_die_error";
static const char *const HTTP2_STAT_SESSION_DIE_HIGH_ERROR_RATE_NAME      = "proxy.process.http2.session_die_high_error_rate";
static const char *const HTTP2_STAT_MAX_SETTINGS_PER_FRAME_EXCEEDED_NAME  = "proxy.process.http2.max_settings_per_frame_exceeded";
static const char *const HTTP2_STAT_MAX_SETTINGS_PER_MINUTE_EXCEEDED_NAME = "proxy.process.http2.max_settings_per_minute_exceeded";
static const char *const HTTP2_STAT_MAX_SETTINGS_FRAMES_PER_MINUTE_EXCEEDED_NAME =
  "proxy.process.http2.max_settings_frames_per_minute_exceeded";
static const char *const HTTP2_STAT_MAX_PING_FRAMES_PER_MINUTE_EXCEEDED_NAME =
  "proxy.process.http2.max_ping_frames_per_minute_exceeded";
static const char *const HTTP2_STAT_MAX_PRIORITY_FRAMES_PER_MINUTE_EXCEEDED_NAME =
  "proxy.process.http2.max_priority_frames_per_minute_exceeded";
static const char *const HTTP2_STAT_INSUFFICIENT_AVG_WINDOW_UPDATE_NAME = "proxy.process.http2.insufficient_avg_window_update";
static const char *const HTTP2_STAT_MAX_CONCURRENT_STREAMS_EXCEEDED_IN_NAME =
  "proxy.process.http2.max_concurrent_streams_exceeded_in";
static const char *const HTTP2_STAT_MAX_CONCURRENT_STREAMS_EXCEEDED_OUT_NAME =
  "proxy.process.http2.max_concurrent_streams_exceeded_out";

union byte_pointer {
  byte_pointer(void *p) : ptr(p) {}
  void *ptr;
  uint8_t *u8;
  uint16_t *u16;
  uint32_t *u32;
};

template <typename T> union byte_addressable_value {
  uint8_t bytes[sizeof(T)];
  T value;
};

static void
write_and_advance(byte_pointer &dst, const uint8_t *src, size_t length)
{
  memcpy(dst.u8, src, length);
  dst.u8 += length;
}

static void
write_and_advance(byte_pointer &dst, uint32_t src)
{
  byte_addressable_value<uint32_t> pval;

  // cppcheck-suppress unreadVariable ; it's an union and be read as pval.bytes
  pval.value = htonl(src);
  memcpy(dst.u8, pval.bytes, sizeof(pval.bytes));
  dst.u8 += sizeof(pval.bytes);
}

static void
write_and_advance(byte_pointer &dst, uint16_t src)
{
  byte_addressable_value<uint16_t> pval;

  // cppcheck-suppress unreadVariable ; it's an union and be read as pval.bytes
  pval.value = htons(src);
  memcpy(dst.u8, pval.bytes, sizeof(pval.bytes));
  dst.u8 += sizeof(pval.bytes);
}

static void
write_and_advance(byte_pointer &dst, uint8_t src)
{
  *dst.u8 = src;
  dst.u8++;
}

template <unsigned N>
static void
memcpy_and_advance(uint8_t (&dst)[N], byte_pointer &src)
{
  memcpy(dst, src.u8, N);
  src.u8 += N;
}

static void
memcpy_and_advance(uint8_t(&dst), byte_pointer &src)
{
  dst = *src.u8;
  ++src.u8;
}

bool
http2_frame_header_is_valid(const Http2FrameHeader &hdr, unsigned max_frame_size)
{
  // 6.1 If a DATA frame is received whose stream identifier field is 0x0, the recipient MUST
  // respond with a connection error (Section 5.4.1) of type PROTOCOL_ERROR.
  if (hdr.type == HTTP2_FRAME_TYPE_DATA && hdr.streamid == 0) {
    return false;
  }

  return true;
}

bool
http2_settings_parameter_is_valid(const Http2SettingsParameter &param)
{
  // Static maximum values for Settings parameters.
  static const uint32_t settings_max[HTTP2_SETTINGS_MAX] = {
    0,
    UINT_MAX,              // HTTP2_SETTINGS_HEADER_TABLE_SIZE
    1,                     // HTTP2_SETTINGS_ENABLE_PUSH
    UINT_MAX,              // HTTP2_SETTINGS_MAX_CONCURRENT_STREAMS
    HTTP2_MAX_WINDOW_SIZE, // HTTP2_SETTINGS_INITIAL_WINDOW_SIZE
    16777215,              // HTTP2_SETTINGS_MAX_FRAME_SIZE
    UINT_MAX,              // HTTP2_SETTINGS_MAX_HEADER_LIST_SIZE
  };

  if (param.id == 0 || param.id >= HTTP2_SETTINGS_MAX) {
    // Do nothing - 6.5.2 Unsupported parameters MUST be ignored
    return true;
  }

  if (param.value > settings_max[param.id]) {
    return false;
  }

  if (param.id == HTTP2_SETTINGS_ENABLE_PUSH && param.value != 0 && param.value != 1) {
    return false;
  }

  if (param.id == HTTP2_SETTINGS_MAX_FRAME_SIZE && (param.value < (1 << 14) || param.value > (1 << 24) - 1)) {
    return false;
  }

  return true;
}

// 4.1.  Frame Format
//
//  0                   1                   2                   3
//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                 Length (24)                   |
// +---------------+---------------+---------------+
// |   Type (8)    |   Flags (8)   |
// +-+-+-----------+---------------+-------------------------------+
// |R|                 Stream Identifier (31)                      |
// +=+=============================================================+
// |                   Frame Payload (0...)                      ...
// +---------------------------------------------------------------+

bool
http2_parse_frame_header(IOVec iov, Http2FrameHeader &hdr)
{
  byte_pointer ptr(iov.iov_base);
  byte_addressable_value<uint32_t> length_and_type;
  byte_addressable_value<uint32_t> streamid;

  if (unlikely(iov.iov_len < HTTP2_FRAME_HEADER_LEN)) {
    return false;
  }

  memcpy_and_advance(length_and_type.bytes, ptr);
  memcpy_and_advance(hdr.flags, ptr);
  memcpy_and_advance(streamid.bytes, ptr);

  hdr.length        = ntohl(length_and_type.value) >> 8;
  hdr.type          = ntohl(length_and_type.value) & 0xff;
  streamid.bytes[0] &= 0x7f; // Clear the high reserved bit
  hdr.streamid      = ntohl(streamid.value);

  return true;
}

bool
http2_write_frame_header(const Http2FrameHeader &hdr, IOVec iov)
{
  byte_pointer ptr(iov.iov_base);

  if (unlikely(iov.iov_len < HTTP2_FRAME_HEADER_LEN)) {
    return false;
  }

  byte_addressable_value<uint32_t> length;
  // cppcheck-suppress unreadVariable ; it's an union and be read as pval.bytes
  length.value = htonl(hdr.length);
  // MSB length.bytes[0] is unused.
  write_and_advance(ptr, length.bytes[1]);
  write_and_advance(ptr, length.bytes[2]);
  write_and_advance(ptr, length.bytes[3]);

  write_and_advance(ptr, hdr.type);
  write_and_advance(ptr, hdr.flags);
  write_and_advance(ptr, hdr.streamid);

  return true;
}

bool
http2_write_rst_stream(uint32_t error_code, IOVec iov)
{
  byte_pointer ptr(iov.iov_base);

  write_and_advance(ptr, error_code);

  return true;
}

bool
http2_write_settings(const Http2SettingsParameter &param, const IOVec &iov)
{
  byte_pointer ptr(iov.iov_base);

  if (unlikely(iov.iov_len < HTTP2_SETTINGS_PARAMETER_LEN)) {
    return false;
  }

  write_and_advance(ptr, param.id);
  write_and_advance(ptr, param.value);

  return true;
}

bool
http2_write_ping(const uint8_t *opaque_data, IOVec iov)
{
  byte_pointer ptr(iov.iov_base);

  if (unlikely(iov.iov_len < HTTP2_PING_LEN)) {
    return false;
  }

  write_and_advance(ptr, opaque_data, HTTP2_PING_LEN);

  return true;
}

bool
http2_write_goaway(const Http2Goaway &goaway, IOVec iov)
{
  byte_pointer ptr(iov.iov_base);

  if (unlikely(iov.iov_len < HTTP2_GOAWAY_LEN)) {
    return false;
  }

  write_and_advance(ptr, goaway.last_streamid);
  write_and_advance(ptr, static_cast<uint32_t>(goaway.error_code));

  return true;
}

bool
http2_write_window_update(const uint32_t new_size, const IOVec &iov)
{
  byte_pointer ptr(iov.iov_base);
  write_and_advance(ptr, new_size);

  return true;
}

bool
http2_write_push_promise(const Http2PushPromise &push_promise, const uint8_t *src, size_t length, const IOVec &iov)
{
  byte_pointer ptr(iov.iov_base);
  write_and_advance(ptr, push_promise.promised_streamid);
  write_and_advance(ptr, src, length);
  return true;
}

bool
http2_parse_headers_parameter(IOVec iov, Http2HeadersParameter &params)
{
  byte_pointer ptr(iov.iov_base);
  memcpy_and_advance(params.pad_length, ptr);

  return true;
}

bool
http2_parse_priority_parameter(IOVec iov, Http2Priority &priority)
{
  byte_pointer ptr(iov.iov_base);
  byte_addressable_value<uint32_t> dependency;

  memcpy_and_advance(dependency.bytes, ptr);

  priority.exclusive_flag = dependency.bytes[0] & 0x80;

  dependency.bytes[0]        &= 0x7f; // Clear the highest bit for exclusive flag
  priority.stream_dependency = ntohl(dependency.value);

  memcpy_and_advance(priority.weight, ptr);

  return true;
}

bool
http2_parse_rst_stream(IOVec iov, Http2RstStream &rst_stream)
{
  byte_pointer ptr(iov.iov_base);
  byte_addressable_value<uint32_t> ec;

  memcpy_and_advance(ec.bytes, ptr);

  rst_stream.error_code = ntohl(ec.value);

  return true;
}

bool
http2_parse_settings_parameter(IOVec iov, Http2SettingsParameter &param)
{
  byte_pointer ptr(iov.iov_base);
  byte_addressable_value<uint16_t> pid;
  byte_addressable_value<uint32_t> pval;

  if (unlikely(iov.iov_len < HTTP2_SETTINGS_PARAMETER_LEN)) {
    return false;
  }

  memcpy_and_advance(pid.bytes, ptr);
  memcpy_and_advance(pval.bytes, ptr);

  param.id    = ntohs(pid.value);
  param.value = ntohl(pval.value);

  return true;
}

bool
http2_parse_goaway(IOVec iov, Http2Goaway &goaway)
{
  byte_pointer ptr(iov.iov_base);
  byte_addressable_value<uint32_t> sid;
  byte_addressable_value<uint32_t> ec;

  memcpy_and_advance(sid.bytes, ptr);
  memcpy_and_advance(ec.bytes, ptr);

  goaway.last_streamid = ntohl(sid.value);
  goaway.error_code    = static_cast<Http2ErrorCode>(ntohl(ec.value));
  return true;
}

bool
http2_parse_window_update(IOVec iov, uint32_t &size)
{
  byte_pointer ptr(iov.iov_base);
  byte_addressable_value<uint32_t> s;

  memcpy_and_advance(s.bytes, ptr);

  size = ntohl(s.value);

  return true;
}

ParseResult
http2_convert_header_from_2_to_1_1(HTTPHdr *headers)
{
  if (hvc.convert(*headers, 2, 1) == 0) {
    return PARSE_RESULT_DONE;
  } else {
    return PARSE_RESULT_ERROR;
  }
}

/**
  Convert HTTP/1.1 HTTPHdr to HTTP/2

  Assuming HTTP/2 Pseudo-Header Fields are reserved by passing a version to `HTTPHdr::create()`.
 */
ParseResult
http2_convert_header_from_1_1_to_2(HTTPHdr *headers)
{
  if (hvc.convert(*headers, 1, 2) == 0) {
    return PARSE_RESULT_DONE;
  } else {
    return PARSE_RESULT_ERROR;
  }

  return PARSE_RESULT_DONE;
}

Http2ErrorCode
http2_encode_header_blocks(HTTPHdr *in, uint8_t *out, uint32_t out_len, uint32_t *len_written, HpackHandle &handle,
                           int32_t maximum_table_size)
{
  // Limit the maximum table size to the configured value or 64kB at maximum, which is the size advertised by major clients
  maximum_table_size =
    std::min(maximum_table_size, static_cast<int32_t>(std::min(Http2::header_table_size_limit, HTTP2_MAX_TABLE_SIZE_LIMIT)));
  // Set maximum table size only if it is different from current maximum size
  if (maximum_table_size == hpack_get_maximum_table_size(handle)) {
    maximum_table_size = -1;
  }

  // TODO: It would be better to split Cookie header value
  int64_t result = hpack_encode_header_block(handle, out, out_len, in, maximum_table_size);
  if (result < 0) {
    return Http2ErrorCode::HTTP2_ERROR_COMPRESSION_ERROR;
  }
  if (len_written) {
    *len_written = result;
  }
  return Http2ErrorCode::HTTP2_ERROR_NO_ERROR;
}

/*
 * Decode Header Blocks to Header List.
 */
Http2ErrorCode
http2_decode_header_blocks(HTTPHdr *hdr, const uint8_t *buf_start, const uint32_t buf_len, uint32_t *len_read, HpackHandle &handle,
                           bool &trailing_header, uint32_t maximum_table_size)
{
  const MIMEField *field  = nullptr;
  const char *name        = nullptr;
  int name_len            = 0;
  const char *value       = nullptr;
  int value_len           = 0;
  bool is_trailing_header = trailing_header;
  int64_t result = hpack_decode_header_block(handle, hdr, buf_start, buf_len, Http2::max_header_list_size, maximum_table_size);

  if (result < 0) {
    if (result == HPACK_ERROR_COMPRESSION_ERROR) {
      return Http2ErrorCode::HTTP2_ERROR_COMPRESSION_ERROR;
    } else if (result == HPACK_ERROR_SIZE_EXCEEDED_ERROR) {
      return Http2ErrorCode::HTTP2_ERROR_ENHANCE_YOUR_CALM;
    }

    return Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR;
  }
  if (len_read) {
    *len_read = result;
  }

  MIMEFieldIter iter;
  unsigned int expected_pseudo_header_count = 4;
  unsigned int pseudo_header_count          = 0;

  if (is_trailing_header) {
    expected_pseudo_header_count = 0;
  }
  for (auto &field : *hdr) {
    name = field.name_get(&name_len);
    // Pseudo headers must appear before regular headers
    if (name_len && name[0] == ':') {
      ++pseudo_header_count;
      if (pseudo_header_count > expected_pseudo_header_count) {
        return Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR;
      }
    } else if (name_len <= 0) {
      return Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR;
    } else {
      if (pseudo_header_count != expected_pseudo_header_count) {
        return Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR;
      }
    }
  }

  // rfc7540,sec8.1.2.2: Any message containing connection-specific header
  // fields MUST be treated as malformed
  if (hdr->field_find(MIME_FIELD_CONNECTION, MIME_LEN_CONNECTION) != nullptr ||
      hdr->field_find(MIME_FIELD_KEEP_ALIVE, MIME_LEN_KEEP_ALIVE) != nullptr ||
      hdr->field_find(MIME_FIELD_PROXY_CONNECTION, MIME_LEN_PROXY_CONNECTION) != nullptr ||
      hdr->field_find(MIME_FIELD_TRANSFER_ENCODING, MIME_LEN_TRANSFER_ENCODING) != nullptr ||
      hdr->field_find(MIME_FIELD_UPGRADE, MIME_LEN_UPGRADE) != nullptr) {
    return Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR;
  }

  // :path pseudo header MUST NOT empty for http or https URIs
  field = hdr->field_find(PSEUDO_HEADER_PATH.data(), PSEUDO_HEADER_PATH.size());
  if (field) {
    field->value_get(&value_len);
    if (value_len == 0) {
      return Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR;
    }
  }

  // turn on that we have a trailer header
  const char trailer_name[] = "trailer";
  field                     = hdr->field_find(trailer_name, sizeof(trailer_name) - 1);
  if (field) {
    trailing_header = true;
  }

  // when The TE header field is received, it MUST NOT contain any
  // value other than "trailers".
  field = hdr->field_find(MIME_FIELD_TE, MIME_LEN_TE);
  if (field) {
    value = field->value_get(&value_len);
    if (!(value_len == 8 && memcmp(value, "trailers", 8) == 0)) {
      return Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR;
    }
  }

  if (!is_trailing_header) {
    // Check pseudo headers
    if (hdr->fields_count() >= 4) {
      if (hdr->field_find(PSEUDO_HEADER_SCHEME.data(), PSEUDO_HEADER_SCHEME.size()) == nullptr ||
          hdr->field_find(PSEUDO_HEADER_METHOD.data(), PSEUDO_HEADER_METHOD.size()) == nullptr ||
          hdr->field_find(PSEUDO_HEADER_PATH.data(), PSEUDO_HEADER_PATH.size()) == nullptr ||
          hdr->field_find(PSEUDO_HEADER_AUTHORITY.data(), PSEUDO_HEADER_AUTHORITY.size()) == nullptr ||
          hdr->field_find(PSEUDO_HEADER_STATUS.data(), PSEUDO_HEADER_STATUS.size()) != nullptr) {
        // Decoded header field is invalid
        return Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR;
      }
    } else {
      // Pseudo headers is insufficient
      return Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR;
    }
  }

  return Http2ErrorCode::HTTP2_ERROR_NO_ERROR;
}

// Initialize this subsystem with librecords configs (for now)
uint32_t Http2::max_concurrent_streams_in            = 100;
uint32_t Http2::min_concurrent_streams_in            = 10;
uint32_t Http2::max_active_streams_in                = 0;
bool Http2::throttling                               = false;
uint32_t Http2::stream_priority_enabled              = 0;
uint32_t Http2::initial_window_size_in               = 65535;
Http2FlowControlPolicy Http2::flow_control_policy_in = Http2FlowControlPolicy::STATIC_SESSION_AND_STATIC_STREAM;
uint32_t Http2::max_frame_size                       = 16384;
uint32_t Http2::header_table_size                    = 4096;
uint32_t Http2::max_header_list_size                 = 4294967295;
uint32_t Http2::accept_no_activity_timeout           = 120;
uint32_t Http2::no_activity_timeout_in               = 120;
uint32_t Http2::active_timeout_in                    = 0;
uint32_t Http2::push_diary_size                      = 256;
uint32_t Http2::zombie_timeout_in                    = 0;
float Http2::stream_error_rate_threshold             = 0.1;
uint32_t Http2::stream_error_sampling_threshold      = 10;
uint32_t Http2::max_settings_per_frame               = 7;
uint32_t Http2::max_settings_per_minute              = 14;
uint32_t Http2::max_settings_frames_per_minute       = 14;
uint32_t Http2::max_ping_frames_per_minute           = 60;
uint32_t Http2::max_priority_frames_per_minute       = 120;
float Http2::min_avg_window_update                   = 2560.0;
uint32_t Http2::con_slow_log_threshold               = 0;
uint32_t Http2::stream_slow_log_threshold            = 0;
uint32_t Http2::header_table_size_limit              = 65536;
uint32_t Http2::write_buffer_block_size              = 262144;
float Http2::write_size_threshold                    = 0.5;
uint32_t Http2::write_time_threshold                 = 100;
uint32_t Http2::buffer_water_mark                    = 0;

void
Http2::init()
{
  REC_EstablishStaticConfigInt32U(max_concurrent_streams_in, "proxy.config.http2.max_concurrent_streams_in");
  REC_EstablishStaticConfigInt32U(min_concurrent_streams_in, "proxy.config.http2.min_concurrent_streams_in");
  REC_EstablishStaticConfigInt32U(max_active_streams_in, "proxy.config.http2.max_active_streams_in");
  REC_EstablishStaticConfigInt32U(stream_priority_enabled, "proxy.config.http2.stream_priority_enabled");
  REC_EstablishStaticConfigInt32U(initial_window_size_in, "proxy.config.http2.initial_window_size_in");

  uint32_t flow_control_policy_in_int = 0;
  REC_EstablishStaticConfigInt32U(flow_control_policy_in_int, "proxy.config.http2.flow_control.policy_in");
  if (flow_control_policy_in_int > 2) {
    Error("Invalid value for proxy.config.http2.flow_control.policy_in: %d", flow_control_policy_in_int);
    flow_control_policy_in_int = 0;
  }
  flow_control_policy_in = static_cast<Http2FlowControlPolicy>(flow_control_policy_in_int);

  REC_EstablishStaticConfigInt32U(max_frame_size, "proxy.config.http2.max_frame_size");
  REC_EstablishStaticConfigInt32U(header_table_size, "proxy.config.http2.header_table_size");
  REC_EstablishStaticConfigInt32U(max_header_list_size, "proxy.config.http2.max_header_list_size");
  REC_EstablishStaticConfigInt32U(accept_no_activity_timeout, "proxy.config.http2.accept_no_activity_timeout");
  REC_EstablishStaticConfigInt32U(no_activity_timeout_in, "proxy.config.http2.no_activity_timeout_in");
  REC_EstablishStaticConfigInt32U(active_timeout_in, "proxy.config.http2.active_timeout_in");
  REC_EstablishStaticConfigInt32U(push_diary_size, "proxy.config.http2.push_diary_size");
  REC_EstablishStaticConfigInt32U(zombie_timeout_in, "proxy.config.http2.zombie_debug_timeout_in");
  REC_EstablishStaticConfigFloat(stream_error_rate_threshold, "proxy.config.http2.stream_error_rate_threshold");
  REC_EstablishStaticConfigInt32U(stream_error_sampling_threshold, "proxy.config.http2.stream_error_sampling_threshold");
  REC_EstablishStaticConfigInt32U(max_settings_per_frame, "proxy.config.http2.max_settings_per_frame");
  REC_EstablishStaticConfigInt32U(max_settings_per_minute, "proxy.config.http2.max_settings_per_minute");
  REC_EstablishStaticConfigInt32U(max_settings_frames_per_minute, "proxy.config.http2.max_settings_frames_per_minute");
  REC_EstablishStaticConfigInt32U(max_ping_frames_per_minute, "proxy.config.http2.max_ping_frames_per_minute");
  REC_EstablishStaticConfigInt32U(max_priority_frames_per_minute, "proxy.config.http2.max_priority_frames_per_minute");
  REC_EstablishStaticConfigFloat(min_avg_window_update, "proxy.config.http2.min_avg_window_update");
  REC_EstablishStaticConfigInt32U(con_slow_log_threshold, "proxy.config.http2.connection.slow.log.threshold");
  REC_EstablishStaticConfigInt32U(stream_slow_log_threshold, "proxy.config.http2.stream.slow.log.threshold");
  REC_EstablishStaticConfigInt32U(header_table_size_limit, "proxy.config.http2.header_table_size_limit");
  REC_EstablishStaticConfigInt32U(write_buffer_block_size, "proxy.config.http2.write_buffer_block_size");
  REC_EstablishStaticConfigFloat(write_size_threshold, "proxy.config.http2.write_size_threshold");
  REC_EstablishStaticConfigInt32U(write_time_threshold, "proxy.config.http2.write_time_threshold");
  REC_EstablishStaticConfigInt32U(buffer_water_mark, "proxy.config.http2.default_buffer_water_mark");

  // If any settings is broken, ATS should not start
  ink_release_assert(http2_settings_parameter_is_valid({HTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, max_concurrent_streams_in}));
  ink_release_assert(http2_settings_parameter_is_valid({HTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, min_concurrent_streams_in}));
  ink_release_assert(http2_settings_parameter_is_valid({HTTP2_SETTINGS_INITIAL_WINDOW_SIZE, initial_window_size_in}));
  ink_release_assert(http2_settings_parameter_is_valid({HTTP2_SETTINGS_MAX_FRAME_SIZE, max_frame_size}));
  ink_release_assert(http2_settings_parameter_is_valid({HTTP2_SETTINGS_HEADER_TABLE_SIZE, header_table_size}));
  ink_release_assert(http2_settings_parameter_is_valid({HTTP2_SETTINGS_MAX_HEADER_LIST_SIZE, max_header_list_size}));

#define HTTP2_CLEAR_DYN_STAT(x)          \
  do {                                   \
    RecSetRawStatSum(http2_rsb, x, 0);   \
    RecSetRawStatCount(http2_rsb, x, 0); \
  } while (0);

  // Setup statistics
  http2_rsb = RecAllocateRawStatBlock(static_cast<int>(HTTP2_N_STATS));
  RecRegisterRawStat(http2_rsb, RECT_PROCESS, HTTP2_STAT_CURRENT_CLIENT_CONNECTION_NAME, RECD_INT, RECP_NON_PERSISTENT,
                     static_cast<int>(HTTP2_STAT_CURRENT_CLIENT_SESSION_COUNT), RecRawStatSyncSum);
  HTTP2_CLEAR_DYN_STAT(HTTP2_STAT_CURRENT_CLIENT_SESSION_COUNT);
  RecRegisterRawStat(http2_rsb, RECT_PROCESS, HTTP2_STAT_CURRENT_ACTIVE_CLIENT_CONNECTION_NAME, RECD_INT, RECP_NON_PERSISTENT,
                     static_cast<int>(HTTP2_STAT_CURRENT_ACTIVE_CLIENT_CONNECTION_COUNT), RecRawStatSyncSum);
  HTTP2_CLEAR_DYN_STAT(HTTP2_STAT_CURRENT_ACTIVE_CLIENT_CONNECTION_COUNT);
  RecRegisterRawStat(http2_rsb, RECT_PROCESS, HTTP2_STAT_CURRENT_CLIENT_STREAM_NAME, RECD_INT, RECP_NON_PERSISTENT,
                     static_cast<int>(HTTP2_STAT_CURRENT_CLIENT_STREAM_COUNT), RecRawStatSyncSum);
  HTTP2_CLEAR_DYN_STAT(HTTP2_STAT_CURRENT_CLIENT_STREAM_COUNT);
  RecRegisterRawStat(http2_rsb, RECT_PROCESS, HTTP2_STAT_TOTAL_CLIENT_STREAM_NAME, RECD_INT, RECP_PERSISTENT,
                     static_cast<int>(HTTP2_STAT_TOTAL_CLIENT_STREAM_COUNT), RecRawStatSyncCount);
  RecRegisterRawStat(http2_rsb, RECT_PROCESS, HTTP2_STAT_TOTAL_TRANSACTIONS_TIME_NAME, RECD_INT, RECP_PERSISTENT,
                     static_cast<int>(HTTP2_STAT_TOTAL_TRANSACTIONS_TIME), RecRawStatSyncSum);
  RecRegisterRawStat(http2_rsb, RECT_PROCESS, HTTP2_STAT_TOTAL_CLIENT_CONNECTION_NAME, RECD_INT, RECP_PERSISTENT,
                     static_cast<int>(HTTP2_STAT_TOTAL_CLIENT_CONNECTION_COUNT), RecRawStatSyncSum);
  RecRegisterRawStat(http2_rsb, RECT_PROCESS, HTTP2_STAT_CONNECTION_ERRORS_NAME, RECD_INT, RECP_PERSISTENT,
                     static_cast<int>(HTTP2_STAT_CONNECTION_ERRORS_COUNT), RecRawStatSyncSum);
  RecRegisterRawStat(http2_rsb, RECT_PROCESS, HTTP2_STAT_STREAM_ERRORS_NAME, RECD_INT, RECP_PERSISTENT,
                     static_cast<int>(HTTP2_STAT_STREAM_ERRORS_COUNT), RecRawStatSyncSum);
  RecRegisterRawStat(http2_rsb, RECT_PROCESS, HTTP2_STAT_SESSION_DIE_DEFAULT_NAME, RECD_INT, RECP_PERSISTENT,
                     static_cast<int>(HTTP2_STAT_SESSION_DIE_DEFAULT), RecRawStatSyncSum);
  RecRegisterRawStat(http2_rsb, RECT_PROCESS, HTTP2_STAT_SESSION_DIE_OTHER_NAME, RECD_INT, RECP_PERSISTENT,
                     static_cast<int>(HTTP2_STAT_SESSION_DIE_OTHER), RecRawStatSyncSum);
  RecRegisterRawStat(http2_rsb, RECT_PROCESS, HTTP2_STAT_SESSION_DIE_EOS_NAME, RECD_INT, RECP_PERSISTENT,
                     static_cast<int>(HTTP2_STAT_SESSION_DIE_EOS), RecRawStatSyncSum);
  RecRegisterRawStat(http2_rsb, RECT_PROCESS, HTTP2_STAT_SESSION_DIE_ACTIVE_NAME, RECD_INT, RECP_PERSISTENT,
                     static_cast<int>(HTTP2_STAT_SESSION_DIE_ACTIVE), RecRawStatSyncSum);
  RecRegisterRawStat(http2_rsb, RECT_PROCESS, HTTP2_STAT_SESSION_DIE_INACTIVE_NAME, RECD_INT, RECP_PERSISTENT,
                     static_cast<int>(HTTP2_STAT_SESSION_DIE_INACTIVE), RecRawStatSyncSum);
  RecRegisterRawStat(http2_rsb, RECT_PROCESS, HTTP2_STAT_SESSION_DIE_ERROR_NAME, RECD_INT, RECP_PERSISTENT,
                     static_cast<int>(HTTP2_STAT_SESSION_DIE_ERROR), RecRawStatSyncSum);
  RecRegisterRawStat(http2_rsb, RECT_PROCESS, HTTP2_STAT_SESSION_DIE_HIGH_ERROR_RATE_NAME, RECD_INT, RECP_PERSISTENT,
                     static_cast<int>(HTTP2_STAT_SESSION_DIE_HIGH_ERROR_RATE), RecRawStatSyncSum);
  RecRegisterRawStat(http2_rsb, RECT_PROCESS, HTTP2_STAT_MAX_SETTINGS_PER_FRAME_EXCEEDED_NAME, RECD_INT, RECP_PERSISTENT,
                     static_cast<int>(HTTP2_STAT_MAX_SETTINGS_PER_FRAME_EXCEEDED), RecRawStatSyncSum);
  RecRegisterRawStat(http2_rsb, RECT_PROCESS, HTTP2_STAT_MAX_SETTINGS_PER_MINUTE_EXCEEDED_NAME, RECD_INT, RECP_PERSISTENT,
                     static_cast<int>(HTTP2_STAT_MAX_SETTINGS_PER_MINUTE_EXCEEDED), RecRawStatSyncSum);
  RecRegisterRawStat(http2_rsb, RECT_PROCESS, HTTP2_STAT_MAX_SETTINGS_FRAMES_PER_MINUTE_EXCEEDED_NAME, RECD_INT, RECP_PERSISTENT,
                     static_cast<int>(HTTP2_STAT_MAX_SETTINGS_FRAMES_PER_MINUTE_EXCEEDED), RecRawStatSyncSum);
  RecRegisterRawStat(http2_rsb, RECT_PROCESS, HTTP2_STAT_MAX_PING_FRAMES_PER_MINUTE_EXCEEDED_NAME, RECD_INT, RECP_PERSISTENT,
                     static_cast<int>(HTTP2_STAT_MAX_PING_FRAMES_PER_MINUTE_EXCEEDED), RecRawStatSyncSum);
  RecRegisterRawStat(http2_rsb, RECT_PROCESS, HTTP2_STAT_MAX_PRIORITY_FRAMES_PER_MINUTE_EXCEEDED_NAME, RECD_INT, RECP_PERSISTENT,
                     static_cast<int>(HTTP2_STAT_MAX_PRIORITY_FRAMES_PER_MINUTE_EXCEEDED), RecRawStatSyncSum);
  RecRegisterRawStat(http2_rsb, RECT_PROCESS, HTTP2_STAT_INSUFFICIENT_AVG_WINDOW_UPDATE_NAME, RECD_INT, RECP_PERSISTENT,
                     static_cast<int>(HTTP2_STAT_INSUFFICIENT_AVG_WINDOW_UPDATE), RecRawStatSyncSum);
  RecRegisterRawStat(http2_rsb, RECT_PROCESS, HTTP2_STAT_MAX_CONCURRENT_STREAMS_EXCEEDED_IN_NAME, RECD_INT, RECP_PERSISTENT,
                     static_cast<int>(HTTP2_STAT_MAX_CONCURRENT_STREAMS_EXCEEDED_IN), RecRawStatSyncSum);
  RecRegisterRawStat(http2_rsb, RECT_PROCESS, HTTP2_STAT_MAX_CONCURRENT_STREAMS_EXCEEDED_OUT_NAME, RECD_INT, RECP_PERSISTENT,
                     static_cast<int>(HTTP2_STAT_MAX_CONCURRENT_STREAMS_EXCEEDED_OUT), RecRawStatSyncSum);

  http2_init();
}

void
http2_init()
{
}

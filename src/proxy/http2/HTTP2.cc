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

#include "proxy/hdrs/VersionConverter.h"
#include "proxy/hdrs/HeaderValidator.h"
#include "proxy/http2/HTTP2.h"
#include "proxy/http2/HPACK.h"

#include "tscore/ink_assert.h"
#include "tsutil/LocalBuffer.h"

#include "../../records/P_RecCore.h"
#include "../../records/P_RecProcess.h"

const char *const HTTP2_CONNECTION_PREFACE = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";

static const uint32_t HTTP2_MAX_TABLE_SIZE_LIMIT = 64 * 1024;

namespace
{
struct Http2HeaderName {
  const char *name     = nullptr;
  int         name_len = 0;
};

static VersionConverter hvc;

} // namespace

// Statistics
Http2StatsBlock http2_rsb;

Metrics::Counter::AtomicType *http2_frame_metrics_in[HTTP2_FRAME_TYPE_MAX + 1];

union byte_pointer {
  byte_pointer(void *p) : ptr(p) {}
  void     *ptr;
  uint8_t  *u8;
  uint16_t *u16;
  uint32_t *u32;
};

template <typename T> union byte_addressable_value {
  uint8_t bytes[sizeof(T)];
  T       value;
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
http2_frame_header_is_valid(const Http2FrameHeader &hdr, unsigned /* max_frame_size ATS_UNUSED */)
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
  byte_pointer                     ptr(iov.iov_base);
  byte_addressable_value<uint32_t> length_and_type;
  byte_addressable_value<uint32_t> streamid;

  if (unlikely(iov.iov_len < HTTP2_FRAME_HEADER_LEN)) {
    return false;
  }

  memcpy_and_advance(length_and_type.bytes, ptr);
  memcpy_and_advance(hdr.flags, ptr);
  memcpy_and_advance(streamid.bytes, ptr);

  hdr.length         = ntohl(length_and_type.value) >> 8;
  hdr.type           = ntohl(length_and_type.value) & 0xff;
  streamid.bytes[0] &= 0x7f; // Clear the high reserved bit
  hdr.streamid       = ntohl(streamid.value);

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
  byte_pointer                     ptr(iov.iov_base);
  byte_addressable_value<uint32_t> dependency;

  memcpy_and_advance(dependency.bytes, ptr);

  priority.exclusive_flag = dependency.bytes[0] & 0x80;

  dependency.bytes[0]        &= 0x7f; // Clear the highest bit for exclusive flag
  priority.stream_dependency  = ntohl(dependency.value);

  memcpy_and_advance(priority.weight, ptr);

  return true;
}

bool
http2_parse_rst_stream(IOVec iov, Http2RstStream &rst_stream)
{
  byte_pointer                     ptr(iov.iov_base);
  byte_addressable_value<uint32_t> ec;

  memcpy_and_advance(ec.bytes, ptr);

  rst_stream.error_code = ntohl(ec.value);

  return true;
}

bool
http2_parse_settings_parameter(IOVec iov, Http2SettingsParameter &param)
{
  byte_pointer                     ptr(iov.iov_base);
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
  byte_pointer                     ptr(iov.iov_base);
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
  byte_pointer                     ptr(iov.iov_base);
  byte_addressable_value<uint32_t> s;

  memcpy_and_advance(s.bytes, ptr);

  size = ntohl(s.value);

  return true;
}

ParseResult
http2_convert_header_from_2_to_1_1(HTTPHdr *headers)
{
  if (hvc.convert(*headers, 2, 1) == 0) {
    return ParseResult::DONE;
  } else {
    return ParseResult::ERROR;
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
    return ParseResult::DONE;
  } else {
    return ParseResult::ERROR;
  }

  return ParseResult::DONE;
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
                           bool is_trailing_header, uint32_t maximum_table_size, bool is_outbound)
{
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
  return HeaderValidator::is_h2_h3_header_valid(*hdr, is_outbound, is_trailing_header) ? Http2ErrorCode::HTTP2_ERROR_NO_ERROR :
                                                                                         Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR;
}

// Initialize this subsystem with librecords configs (for now)
uint32_t               Http2::max_concurrent_streams_in = 100;
uint32_t               Http2::min_concurrent_streams_in = 10;
uint32_t               Http2::max_active_streams_in     = 0;
bool                   Http2::throttling                = false;
uint32_t               Http2::stream_priority_enabled   = 0;
uint32_t               Http2::initial_window_size_in    = 65535;
Http2FlowControlPolicy Http2::flow_control_policy_in    = Http2FlowControlPolicy::STATIC_SESSION_AND_STATIC_STREAM;
uint32_t               Http2::max_frame_size            = 16384;
uint32_t               Http2::header_table_size         = 4096;
uint32_t               Http2::max_header_list_size      = 4294967295;

uint32_t Http2::accept_no_activity_timeout   = 120;
uint32_t Http2::no_activity_timeout_in       = 120;
uint32_t Http2::active_timeout_in            = 0;
uint32_t Http2::incomplete_header_timeout_in = 10;
uint32_t Http2::push_diary_size              = 256;
uint32_t Http2::zombie_timeout_in            = 0;

uint32_t               Http2::max_concurrent_streams_out = 100;
uint32_t               Http2::min_concurrent_streams_out = 10;
uint32_t               Http2::max_active_streams_out     = 0;
uint32_t               Http2::initial_window_size_out    = 65535;
Http2FlowControlPolicy Http2::flow_control_policy_out    = Http2FlowControlPolicy::STATIC_SESSION_AND_STATIC_STREAM;
uint32_t               Http2::no_activity_timeout_out    = 120;

float    Http2::stream_error_rate_threshold        = 0.1;
uint32_t Http2::stream_error_sampling_threshold    = 10;
int32_t  Http2::max_settings_per_frame             = 7;
int32_t  Http2::max_settings_per_minute            = 14;
int32_t  Http2::max_settings_frames_per_minute     = 14;
int32_t  Http2::max_ping_frames_per_minute         = 60;
int32_t  Http2::max_priority_frames_per_minute     = 120;
int32_t  Http2::max_rst_stream_frames_per_minute   = 200;
int32_t  Http2::max_continuation_frames_per_minute = 120;
int32_t  Http2::max_empty_frames_per_minute        = 0;
float    Http2::min_avg_window_update              = 2560.0;
uint32_t Http2::con_slow_log_threshold             = 0;
uint32_t Http2::stream_slow_log_threshold          = 0;
uint32_t Http2::header_table_size_limit            = 65536;
uint32_t Http2::write_buffer_block_size            = 262144;
int64_t  Http2::write_buffer_block_size_index      = BUFFER_SIZE_INDEX_256K;
float    Http2::write_size_threshold               = 0.5;
uint32_t Http2::write_time_threshold               = 100;
uint32_t Http2::buffer_water_mark                  = 0;

void
Http2::init()
{
  RecEstablishStaticConfigUInt32(max_concurrent_streams_in, "proxy.config.http2.max_concurrent_streams_in");
  RecEstablishStaticConfigUInt32(min_concurrent_streams_in, "proxy.config.http2.min_concurrent_streams_in");
  RecEstablishStaticConfigUInt32(max_concurrent_streams_out, "proxy.config.http2.max_concurrent_streams_out");
  RecEstablishStaticConfigUInt32(min_concurrent_streams_out, "proxy.config.http2.min_concurrent_streams_out");

  RecEstablishStaticConfigUInt32(max_active_streams_in, "proxy.config.http2.max_active_streams_in");
  RecEstablishStaticConfigUInt32(stream_priority_enabled, "proxy.config.http2.stream_priority_enabled");

  RecEstablishStaticConfigUInt32(initial_window_size_in, "proxy.config.http2.initial_window_size_in");
  uint32_t flow_control_policy_in_int = 0;
  RecEstablishStaticConfigUInt32(flow_control_policy_in_int, "proxy.config.http2.flow_control.policy_in");
  if (flow_control_policy_in_int > 2) {
    Error("Invalid value for proxy.config.http2.flow_control.policy_in: %d", flow_control_policy_in_int);
    flow_control_policy_in_int = 0;
  }
  flow_control_policy_in = static_cast<Http2FlowControlPolicy>(flow_control_policy_in_int);

  RecEstablishStaticConfigUInt32(initial_window_size_out, "proxy.config.http2.initial_window_size_out");
  uint32_t flow_control_policy_out_int = 0;
  RecEstablishStaticConfigUInt32(flow_control_policy_out_int, "proxy.config.http2.flow_control.policy_out");
  if (flow_control_policy_out_int > 2) {
    Error("Invalid value for proxy.config.http2.flow_control.policy_out: %d", flow_control_policy_out_int);
    flow_control_policy_out_int = 0;
  }
  flow_control_policy_out = static_cast<Http2FlowControlPolicy>(flow_control_policy_out_int);

  RecEstablishStaticConfigUInt32(max_frame_size, "proxy.config.http2.max_frame_size");
  RecEstablishStaticConfigUInt32(header_table_size, "proxy.config.http2.header_table_size");
  RecEstablishStaticConfigUInt32(max_header_list_size, "proxy.config.http2.max_header_list_size");
  RecEstablishStaticConfigUInt32(accept_no_activity_timeout, "proxy.config.http2.accept_no_activity_timeout");
  RecEstablishStaticConfigUInt32(no_activity_timeout_in, "proxy.config.http2.no_activity_timeout_in");
  RecEstablishStaticConfigUInt32(no_activity_timeout_out, "proxy.config.http2.no_activity_timeout_out");
  RecEstablishStaticConfigUInt32(active_timeout_in, "proxy.config.http2.active_timeout_in");
  RecEstablishStaticConfigUInt32(incomplete_header_timeout_in, "proxy.config.http2.incomplete_header_timeout_in");
  RecEstablishStaticConfigUInt32(push_diary_size, "proxy.config.http2.push_diary_size");
  RecEstablishStaticConfigUInt32(zombie_timeout_in, "proxy.config.http2.zombie_debug_timeout_in");
  RecEstablishStaticConfigFloat(stream_error_rate_threshold, "proxy.config.http2.stream_error_rate_threshold");
  RecEstablishStaticConfigUInt32(stream_error_sampling_threshold, "proxy.config.http2.stream_error_sampling_threshold");
  RecEstablishStaticConfigInt32(max_settings_per_frame, "proxy.config.http2.max_settings_per_frame");
  RecEstablishStaticConfigInt32(max_settings_per_minute, "proxy.config.http2.max_settings_per_minute");
  RecEstablishStaticConfigInt32(max_settings_frames_per_minute, "proxy.config.http2.max_settings_frames_per_minute");
  RecEstablishStaticConfigInt32(max_ping_frames_per_minute, "proxy.config.http2.max_ping_frames_per_minute");
  RecEstablishStaticConfigInt32(max_priority_frames_per_minute, "proxy.config.http2.max_priority_frames_per_minute");
  RecEstablishStaticConfigInt32(max_rst_stream_frames_per_minute, "proxy.config.http2.max_rst_stream_frames_per_minute");
  RecEstablishStaticConfigInt32(max_continuation_frames_per_minute, "proxy.config.http2.max_continuation_frames_per_minute");
  RecEstablishStaticConfigInt32(max_empty_frames_per_minute, "proxy.config.http2.max_empty_frames_per_minute");
  RecEstablishStaticConfigFloat(min_avg_window_update, "proxy.config.http2.min_avg_window_update");
  RecEstablishStaticConfigUInt32(con_slow_log_threshold, "proxy.config.http2.connection.slow.log.threshold");
  RecEstablishStaticConfigUInt32(stream_slow_log_threshold, "proxy.config.http2.stream.slow.log.threshold");
  RecEstablishStaticConfigUInt32(header_table_size_limit, "proxy.config.http2.header_table_size_limit");
  RecEstablishStaticConfigUInt32(write_buffer_block_size, "proxy.config.http2.write_buffer_block_size");
  RecEstablishStaticConfigFloat(write_size_threshold, "proxy.config.http2.write_size_threshold");
  RecEstablishStaticConfigUInt32(write_time_threshold, "proxy.config.http2.write_time_threshold");
  RecEstablishStaticConfigUInt32(buffer_water_mark, "proxy.config.http2.default_buffer_water_mark");

  write_buffer_block_size_index = iobuffer_size_to_index(Http2::write_buffer_block_size, MAX_BUFFER_SIZE_INDEX);

  // If any settings is broken, ATS should not start
  ink_release_assert(http2_settings_parameter_is_valid({HTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, max_concurrent_streams_in}));
  ink_release_assert(http2_settings_parameter_is_valid({HTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, min_concurrent_streams_in}));
  ink_release_assert(http2_settings_parameter_is_valid({HTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, max_concurrent_streams_out}));
  ink_release_assert(http2_settings_parameter_is_valid({HTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, min_concurrent_streams_out}));
  ink_release_assert(http2_settings_parameter_is_valid({HTTP2_SETTINGS_INITIAL_WINDOW_SIZE, initial_window_size_in}));
  ink_release_assert(http2_settings_parameter_is_valid({HTTP2_SETTINGS_MAX_FRAME_SIZE, max_frame_size}));
  ink_release_assert(http2_settings_parameter_is_valid({HTTP2_SETTINGS_HEADER_TABLE_SIZE, header_table_size}));
  ink_release_assert(http2_settings_parameter_is_valid({HTTP2_SETTINGS_MAX_HEADER_LIST_SIZE, max_header_list_size}));

  // Setup statistics
  http2_rsb.current_client_session_count = Metrics::Gauge::createPtr("proxy.process.http2.current_client_connections");
  http2_rsb.current_server_session_count = Metrics::Gauge::createPtr("proxy.process.http2.current_server_connections");
  http2_rsb.current_active_client_connection_count =
    Metrics::Gauge::createPtr("proxy.process.http2.current_active_client_connections");
  http2_rsb.current_active_server_connection_count =
    Metrics::Gauge::createPtr("proxy.process.http2.current_active_server_connections");
  http2_rsb.current_client_stream_count      = Metrics::Gauge::createPtr("proxy.process.http2.current_client_streams");
  http2_rsb.current_server_stream_count      = Metrics::Gauge::createPtr("proxy.process.http2.current_server_streams");
  http2_rsb.total_client_stream_count        = Metrics::Counter::createPtr("proxy.process.http2.total_client_streams");
  http2_rsb.total_server_stream_count        = Metrics::Counter::createPtr("proxy.process.http2.total_server_streams");
  http2_rsb.total_transactions_time          = Metrics::Counter::createPtr("proxy.process.http2.total_transactions_time");
  http2_rsb.total_client_connection_count    = Metrics::Counter::createPtr("proxy.process.http2.total_client_connections");
  http2_rsb.total_server_connection_count    = Metrics::Counter::createPtr("proxy.process.http2.total_server_connections");
  http2_rsb.stream_errors_count              = Metrics::Counter::createPtr("proxy.process.http2.stream_errors");
  http2_rsb.connection_errors_count          = Metrics::Counter::createPtr("proxy.process.http2.connection_errors");
  http2_rsb.session_die_default              = Metrics::Counter::createPtr("proxy.process.http2.session_die_default");
  http2_rsb.session_die_other                = Metrics::Counter::createPtr("proxy.process.http2.session_die_other");
  http2_rsb.session_die_active               = Metrics::Counter::createPtr("proxy.process.http2.session_die_active");
  http2_rsb.session_die_inactive             = Metrics::Counter::createPtr("proxy.process.http2.session_die_inactive");
  http2_rsb.session_die_eos                  = Metrics::Counter::createPtr("proxy.process.http2.session_die_eos");
  http2_rsb.session_die_error                = Metrics::Counter::createPtr("proxy.process.http2.session_die_error");
  http2_rsb.session_die_high_error_rate      = Metrics::Counter::createPtr("proxy.process.http2.session_die_high_error_rate");
  http2_rsb.max_settings_per_frame_exceeded  = Metrics::Counter::createPtr("proxy.process.http2.max_settings_per_frame_exceeded");
  http2_rsb.max_settings_per_minute_exceeded = Metrics::Counter::createPtr("proxy.process.http2.max_settings_per_minute_exceeded");
  http2_rsb.max_settings_frames_per_minute_exceeded =
    Metrics::Counter::createPtr("proxy.process.http2.max_settings_frames_per_minute_exceeded");
  http2_rsb.max_ping_frames_per_minute_exceeded =
    Metrics::Counter::createPtr("proxy.process.http2.max_ping_frames_per_minute_exceeded");
  http2_rsb.max_priority_frames_per_minute_exceeded =
    Metrics::Counter::createPtr("proxy.process.http2.max_priority_frames_per_minute_exceeded");
  http2_rsb.max_rst_stream_frames_per_minute_exceeded =
    Metrics::Counter::createPtr("proxy.process.http2.max_rst_stream_frames_per_minute_exceeded");
  http2_rsb.max_continuation_frames_per_minute_exceeded =
    Metrics::Counter::createPtr("proxy.process.http2.max_continuation_frames_per_minute_exceeded");
  http2_rsb.max_empty_frames_per_minute_exceeded =
    Metrics::Counter::createPtr("proxy.process.http2.max_empty_frames_per_minute_exceeded");
  http2_rsb.insufficient_avg_window_update = Metrics::Counter::createPtr("proxy.process.http2.insufficient_avg_window_update");
  http2_rsb.max_concurrent_streams_exceeded_in =
    Metrics::Counter::createPtr("proxy.process.http2.max_concurrent_streams_exceeded_in");
  http2_rsb.max_concurrent_streams_exceeded_out =
    Metrics::Counter::createPtr("proxy.process.http2.max_concurrent_streams_exceeded_out");
  http2_rsb.data_frames_in          = Metrics::Counter::createPtr("proxy.process.http2.data_frames_in"),
  http2_rsb.headers_frames_in       = Metrics::Counter::createPtr("proxy.process.http2.headers_frames_in"),
  http2_rsb.priority_frames_in      = Metrics::Counter::createPtr("proxy.process.http2.priority_frames_in"),
  http2_rsb.rst_stream_frames_in    = Metrics::Counter::createPtr("proxy.process.http2.rst_stream_frames_in"),
  http2_rsb.settings_frames_in      = Metrics::Counter::createPtr("proxy.process.http2.settings_frames_in"),
  http2_rsb.push_promise_frames_in  = Metrics::Counter::createPtr("proxy.process.http2.push_promise_frames_in"),
  http2_rsb.ping_frames_in          = Metrics::Counter::createPtr("proxy.process.http2.ping_frames_in"),
  http2_rsb.goaway_frames_in        = Metrics::Counter::createPtr("proxy.process.http2.goaway_frames_in"),
  http2_rsb.window_update_frames_in = Metrics::Counter::createPtr("proxy.process.http2.window_update_frames_in"),
  http2_rsb.continuation_frames_in  = Metrics::Counter::createPtr("proxy.process.http2.continuation_frames_in"),
  http2_rsb.unknown_frames_in       = Metrics::Counter::createPtr("proxy.process.http2.unknown_frames_in"),

  http2_frame_metrics_in[0]  = http2_rsb.data_frames_in;
  http2_frame_metrics_in[1]  = http2_rsb.headers_frames_in;
  http2_frame_metrics_in[2]  = http2_rsb.priority_frames_in;
  http2_frame_metrics_in[3]  = http2_rsb.rst_stream_frames_in;
  http2_frame_metrics_in[4]  = http2_rsb.settings_frames_in;
  http2_frame_metrics_in[5]  = http2_rsb.push_promise_frames_in;
  http2_frame_metrics_in[6]  = http2_rsb.ping_frames_in;
  http2_frame_metrics_in[7]  = http2_rsb.goaway_frames_in;
  http2_frame_metrics_in[8]  = http2_rsb.window_update_frames_in;
  http2_frame_metrics_in[9]  = http2_rsb.continuation_frames_in;
  http2_frame_metrics_in[10] = http2_rsb.unknown_frames_in;

  http2_init();
}

void
http2_init()
{
}

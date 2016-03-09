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
#include "HPACK.h"
#include "HuffmanCodec.h"
#include "ts/ink_assert.h"
#include "P_RecCore.h"
#include "P_RecProcess.h"

const char *const HTTP2_CONNECTION_PREFACE = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
static size_t HPACK_LEN_STATUS_VALUE_STR = 3;

// Statistics
RecRawStatBlock *http2_rsb;
static char const *const HTTP2_STAT_CURRENT_CLIENT_SESSION_NAME = "proxy.process.http2.current_client_sessions";
static char const *const HTTP2_STAT_CURRENT_CLIENT_STREAM_NAME = "proxy.process.http2.current_client_streams";
static char const *const HTTP2_STAT_TOTAL_CLIENT_STREAM_NAME = "proxy.process.http2.total_client_streams";
static char const *const HTTP2_STAT_TOTAL_TRANSACTIONS_TIME_NAME = "proxy.process.http2.total_transactions_time";
static char const *const HTTP2_STAT_TOTAL_CLIENT_CONNECTION_NAME = "proxy.process.http2.total_client_connections";
static char const *const HTTP2_STAT_CONNECTION_ERRORS_NAME = "proxy.process.http2.connection_errors";
static char const *const HTTP2_STAT_STREAM_ERRORS_NAME = "proxy.process.http2.stream_errors";

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

  pval.value = htonl(src);
  memcpy(dst.u8, pval.bytes, sizeof(pval.bytes));
  dst.u8 += sizeof(pval.bytes);
}

static void
write_and_advance(byte_pointer &dst, uint16_t src)
{
  byte_addressable_value<uint16_t> pval;

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
memcpy_and_advance(uint8_t(&dst)[N], byte_pointer &src)
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

static bool
http2_frame_flags_are_valid(uint8_t ftype, uint8_t fflags)
{
  if (ftype >= HTTP2_FRAME_TYPE_MAX) {
    // Skip validation for Unkown frame type - [RFC 7540] 5.5. Extending HTTP/2
    return true;
  }

  // The frame flags are valid for this frame if nothing outside the defined bits is set.
  return (fflags & ~HTTP2_FRAME_FLAGS_MASKS[ftype]) == 0;
}

bool
http2_frame_header_is_valid(const Http2FrameHeader &hdr, unsigned max_frame_size)
{
  if (!http2_frame_flags_are_valid(hdr.type, hdr.flags)) {
    return false;
  }

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
    return false;
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

  hdr.length = ntohl(length_and_type.value) >> 8;
  hdr.type = ntohl(length_and_type.value) & 0xff;
  streamid.bytes[0] &= 0x7f; // Clear the high reserved bit
  hdr.streamid = ntohl(streamid.value);

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
http2_write_data(const uint8_t *src, size_t length, const IOVec &iov)
{
  byte_pointer ptr(iov.iov_base);
  write_and_advance(ptr, src, length);

  return true;
}

bool
http2_write_headers(const uint8_t *src, size_t length, const IOVec &iov)
{
  byte_pointer ptr(iov.iov_base);
  write_and_advance(ptr, src, length);

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
  write_and_advance(ptr, goaway.error_code);

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
http2_parse_headers_parameter(IOVec iov, Http2HeadersParameter &params)
{
  byte_pointer ptr(iov.iov_base);
  memcpy_and_advance(params.pad_length, ptr);

  return true;
}

bool
http2_parse_priority_parameter(IOVec iov, Http2Priority &params)
{
  byte_pointer ptr(iov.iov_base);
  byte_addressable_value<uint32_t> dependency;

  memcpy_and_advance(dependency.bytes, ptr);
  memcpy_and_advance(params.weight, ptr);

  params.stream_dependency = ntohl(dependency.value);

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

  param.id = ntohs(pid.value);
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
  goaway.error_code = ntohl(ec.value);
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

MIMEParseResult
convert_from_2_to_1_1_header(HTTPHdr *headers)
{
  MIMEField *field;

  ink_assert(http_hdr_type_get(headers->m_http) != HTTP_TYPE_UNKNOWN);

  if (http_hdr_type_get(headers->m_http) == HTTP_TYPE_REQUEST) {
    const char *scheme, *authority, *path, *method;
    int scheme_len, authority_len, path_len, method_len;

    // Get values of :scheme, :authority and :path to assemble requested URL
    if ((field = headers->field_find(HPACK_VALUE_SCHEME, HPACK_LEN_SCHEME)) != NULL && field->value_is_valid()) {
      scheme = field->value_get(&scheme_len);
    } else {
      return PARSE_ERROR;
    }

    if ((field = headers->field_find(HPACK_VALUE_AUTHORITY, HPACK_LEN_AUTHORITY)) != NULL && field->value_is_valid()) {
      authority = field->value_get(&authority_len);
    } else {
      return PARSE_ERROR;
    }

    if ((field = headers->field_find(HPACK_VALUE_PATH, HPACK_LEN_PATH)) != NULL && field->value_is_valid()) {
      path = field->value_get(&path_len);
    } else {
      return PARSE_ERROR;
    }

    // Parse URL
    Arena arena;
    size_t url_length = scheme_len + 3 + authority_len + path_len;
    char *url = arena.str_alloc(url_length);
    const char *url_start = url;

    memcpy(url, scheme, scheme_len);
    memcpy(url + scheme_len, "://", 3);
    memcpy(url + scheme_len + 3, authority, authority_len);
    memcpy(url + scheme_len + 3 + authority_len, path, path_len);
    url_parse(headers->m_heap, headers->m_http->u.req.m_url_impl, &url_start, url + url_length, 1);
    arena.str_free(url);

    // Get value of :method
    if ((field = headers->field_find(HPACK_VALUE_METHOD, HPACK_LEN_METHOD)) != NULL && field->value_is_valid()) {
      method = field->value_get(&method_len);

      int method_wks_idx = hdrtoken_tokenize(method, method_len);
      http_hdr_method_set(headers->m_heap, headers->m_http, method, method_wks_idx, method_len, 0);
    } else {
      return PARSE_ERROR;
    }

    // Combine Cookie headers ([RFC 7540] 8.1.2.5.)
    field = headers->field_find(MIME_FIELD_COOKIE, MIME_LEN_COOKIE);
    if (field) {
      headers->field_combine_dups(field, true, ';');
    }

    // Convert HTTP version to 1.1
    int32_t version = HTTP_VERSION(1, 1);
    http_hdr_version_set(headers->m_http, version);

    // Remove HTTP/2 style headers
    headers->field_delete(HPACK_VALUE_SCHEME, HPACK_LEN_SCHEME);
    headers->field_delete(HPACK_VALUE_METHOD, HPACK_LEN_METHOD);
    headers->field_delete(HPACK_VALUE_AUTHORITY, HPACK_LEN_AUTHORITY);
    headers->field_delete(HPACK_VALUE_PATH, HPACK_LEN_PATH);
  } else {
    int status_len;
    const char *status;

    if ((field = headers->field_find(HPACK_VALUE_STATUS, HPACK_LEN_STATUS)) != NULL) {
      status = field->value_get(&status_len);
      headers->status_set(http_parse_status(status, status + status_len));
    } else {
      return PARSE_ERROR;
    }

    // Remove HTTP/2 style headers
    headers->field_delete(HPACK_VALUE_STATUS, HPACK_LEN_STATUS);
  }

  // Check validity of all names and values
  MIMEFieldIter iter;
  for (const MIMEField *field = headers->iter_get_first(&iter); field != NULL; field = headers->iter_get_next(&iter)) {
    if (!field->name_is_valid() || !field->value_is_valid()) {
      return PARSE_ERROR;
    }
  }

  return PARSE_DONE;
}

static int64_t
http2_write_header_field(uint8_t *out, const uint8_t *end, MIMEFieldWrapper &header, Http2IndexingTable &indexing_table)
{
  HpackFieldType field_type = HPACK_FIELD_INDEXED_LITERAL;

  // Cookie less that 20 bytes and Authorization are never indexed
  // This policy is refer to Firefox and nghttp2
  int name_len = 0, value_len = 0;
  const char *name = header.name_get(&name_len);
  header.value_get(&value_len);
  if ((ptr_len_casecmp(name, name_len, MIME_FIELD_COOKIE, MIME_LEN_COOKIE) == 0 && value_len < 20) ||
      (ptr_len_casecmp(name, name_len, MIME_FIELD_AUTHORIZATION, MIME_LEN_AUTHORIZATION) == 0)) {
    field_type = HPACK_FIELD_NEVERINDEX_LITERAL;
  }

  // TODO Enable to configure selecting header field representation

  const Http2LookupIndexResult &result = indexing_table.get_index(header);
  if (result.index > 0) {
    if (result.value_is_indexed) {
      return encode_indexed_header_field(out, end, result.index);
    } else {
      return encode_literal_header_field_with_indexed_name(out, end, header, result.index, indexing_table, field_type);
    }
  } else {
    return encode_literal_header_field_with_new_name(out, end, header, indexing_table, field_type);
  }
}

int64_t
http2_write_psuedo_headers(HTTPHdr *in, uint8_t *out, uint64_t out_len, Http2IndexingTable &indexing_table)
{
  uint8_t *p = out;
  uint8_t *end = out + out_len;
  int64_t len;

  ink_assert(http_hdr_type_get(in->m_http) != HTTP_TYPE_UNKNOWN);

  // TODO Check whether buffer size is enough

  // Set psuedo header
  if (http_hdr_type_get(in->m_http) == HTTP_TYPE_RESPONSE) {
    char status_str[HPACK_LEN_STATUS_VALUE_STR + 1];
    snprintf(status_str, sizeof(status_str), "%d", in->status_get());

    // Add 'Status:' dummy header field
    MIMEField *status_field = mime_field_create(in->m_heap, in->m_http->m_fields_impl);
    mime_field_name_value_set(in->m_heap, in->m_mime, status_field, -1, HPACK_VALUE_STATUS, HPACK_LEN_STATUS, status_str,
                              HPACK_LEN_STATUS_VALUE_STR, 0, HPACK_LEN_STATUS + HPACK_LEN_STATUS_VALUE_STR, true);
    mime_hdr_field_attach(in->m_mime, status_field, 1, NULL);

    // Encode psuedo headers by HPACK
    MIMEFieldWrapper header(status_field, in->m_heap, in->m_http->m_fields_impl);

    len = http2_write_header_field(p, end, header, indexing_table);
    if (len == -1)
      return -1;
    p += len;

    // Remove dummy header field
    in->field_delete(HPACK_VALUE_STATUS, HPACK_LEN_STATUS);
  }

  return p - out;
}

int64_t
http2_write_header_fragment(HTTPHdr *in, MIMEFieldIter &field_iter, uint8_t *out, uint64_t out_len,
                            Http2IndexingTable &indexing_table, bool &cont)
{
  uint8_t *p = out;
  uint8_t *end = out + out_len;
  int64_t len;

  ink_assert(http_hdr_type_get(in->m_http) != HTTP_TYPE_UNKNOWN);
  ink_assert(in);

  // TODO Get a index value from the tables for the header field, and then
  // choose a representation type.
  // TODO Each indexing types per field should be passed by a caller, HTTP/2
  // implementation.

  // Get first header field which is required encoding
  MIMEField *field;
  if (!field_iter.m_block) {
    field = in->iter_get_first(&field_iter);
  } else {
    field = in->iter_get(&field_iter);
  }

  // Set mime headers
  cont = false;
  for (; field != NULL; field = in->iter_get_next(&field_iter)) {
    // Intermediaries SHOULD remove connection-specific header fields.
    int name_len;
    const char *name = field->name_get(&name_len);
    if ((name_len == MIME_LEN_CONNECTION && strncasecmp(name, MIME_FIELD_CONNECTION, name_len) == 0) ||
        (name_len == MIME_LEN_KEEP_ALIVE && strncasecmp(name, MIME_FIELD_KEEP_ALIVE, name_len) == 0) ||
        (name_len == MIME_LEN_PROXY_CONNECTION && strncasecmp(name, MIME_FIELD_PROXY_CONNECTION, name_len) == 0) ||
        (name_len == MIME_LEN_TRANSFER_ENCODING && strncasecmp(name, MIME_FIELD_TRANSFER_ENCODING, name_len) == 0) ||
        (name_len == MIME_LEN_UPGRADE && strncasecmp(name, MIME_FIELD_UPGRADE, name_len) == 0)) {
      continue;
    }

    MIMEFieldWrapper header(field, in->m_heap, in->m_http->m_fields_impl);
    if ((len = http2_write_header_field(p, end, header, indexing_table)) == -1) {
      if (p == out) {
        // no progress was made, header was too big for the buffer, skipping for now
        continue;
      }
      if (!cont) {
        // Parsing a part of headers is done
        cont = true;
        return p - out;
      } else {
        // Parse error
        return -1;
      }
    }
    p += len;
  }

  // Parsing all headers is done
  return p - out;
}

/*
 * Decode Header Blocks to Header List.
 */
int64_t
http2_decode_header_blocks(HTTPHdr *hdr, const uint8_t *buf_start, const uint8_t *buf_end, Http2IndexingTable &indexing_table,
                           bool &trailing_header)
{
  const uint8_t *cursor = buf_start;
  HdrHeap *heap = hdr->m_heap;
  HTTPHdrImpl *hh = hdr->m_http;
  bool header_field_started = false;
  bool is_trailing_header = trailing_header;

  while (cursor < buf_end) {
    int64_t read_bytes = 0;

    // decode a header field encoded by HPACK
    MIMEField *field = mime_field_create(heap, hh->m_fields_impl);
    MIMEFieldWrapper header(field, heap, hh->m_fields_impl);
    HpackFieldType ftype = hpack_parse_field_type(*cursor);

    switch (ftype) {
    case HPACK_FIELD_INDEX:
      read_bytes = decode_indexed_header_field(header, cursor, buf_end, indexing_table);
      if (read_bytes == HPACK_ERROR_COMPRESSION_ERROR) {
        return HPACK_ERROR_COMPRESSION_ERROR;
      }
      cursor += read_bytes;
      header_field_started = true;
      break;
    case HPACK_FIELD_INDEXED_LITERAL:
    case HPACK_FIELD_NOINDEX_LITERAL:
    case HPACK_FIELD_NEVERINDEX_LITERAL:
      read_bytes = decode_literal_header_field(header, cursor, buf_end, indexing_table);
      if (read_bytes == HPACK_ERROR_COMPRESSION_ERROR) {
        return HPACK_ERROR_COMPRESSION_ERROR;
      }
      cursor += read_bytes;
      header_field_started = true;
      break;
    case HPACK_FIELD_TABLESIZE_UPDATE:
      if (header_field_started) {
        return HPACK_ERROR_COMPRESSION_ERROR;
      }
      read_bytes = update_dynamic_table_size(cursor, buf_end, indexing_table);
      if (read_bytes == HPACK_ERROR_COMPRESSION_ERROR) {
        return HPACK_ERROR_COMPRESSION_ERROR;
      }
      cursor += read_bytes;
      continue;
    }

    int name_len = 0;
    const char *name = field->name_get(&name_len);

    // ':' started header name is only allowed for pseudo headers
    if (hdr->fields_count() >= 4 && (name_len <= 0 || name[0] == ':')) {
      // Decoded header field is invalid
      return HPACK_ERROR_HTTP2_PROTOCOL_ERROR;
    }

    // rfc7540,sec8.1.2.2: Any message containing connection-specific header
    // fields MUST be treated as malformed
    if (name == MIME_FIELD_CONNECTION) {
      return HPACK_ERROR_HTTP2_PROTOCOL_ERROR;
    }

    // :path pseudo header MUST NOT empty for http or https URIs
    if (static_cast<unsigned>(name_len) == HPACK_LEN_PATH && strncmp(name, HPACK_VALUE_PATH, name_len) == 0) {
      int value_len = 0;
      field->value_get(&value_len);
      if (value_len == 0) {
        return HPACK_ERROR_HTTP2_PROTOCOL_ERROR;
      }
    }

    // when The TE header field is received, it MUST NOT contain any value other than "trailers".
    if (name_len == MIME_LEN_TE && strncmp(name, MIME_FIELD_TE, name_len) == 0) {
      int value_len = 0;
      const char *value = field->value_get(&value_len);
      const char trailers[] = "trailers";
      if (!(value_len == (sizeof(trailers) - 1) && memcmp(value, trailers, value_len) == 0)) {
        return HPACK_ERROR_HTTP2_PROTOCOL_ERROR;
      }
    }

    // turn on that we have a trailer header
    const char trailer_name[] = "trailer";
    if (name_len == (sizeof(trailer_name) - 1) && strncmp(name, trailer_name, sizeof(trailer_name) - 1) == 0) {
      trailing_header = true;
    }

    // Store to HdrHeap
    mime_hdr_field_attach(hh->m_fields_impl, field, 1, NULL);
  }

  if (!is_trailing_header) {
    // Check psuedo headers
    if (hdr->fields_count() >= 4) {
      if (hdr->field_find(HPACK_VALUE_SCHEME, HPACK_LEN_SCHEME) == NULL ||
          hdr->field_find(HPACK_VALUE_METHOD, HPACK_LEN_METHOD) == NULL ||
          hdr->field_find(HPACK_VALUE_PATH, HPACK_LEN_PATH) == NULL ||
          hdr->field_find(HPACK_VALUE_AUTHORITY, HPACK_LEN_AUTHORITY) == NULL) {
        // Decoded header field is invalid
        return HPACK_ERROR_HTTP2_PROTOCOL_ERROR;
      }
    } else {
      // Psuedo headers is insufficient
      return HPACK_ERROR_HTTP2_PROTOCOL_ERROR;
    }
  }

  // Parsing all headers is done
  return cursor - buf_start;
}

// Initialize this subsystem with librecords configs (for now)
uint32_t Http2::max_concurrent_streams = 100;
uint32_t Http2::initial_window_size = 1048576;
uint32_t Http2::max_frame_size = 16384;
uint32_t Http2::header_table_size = 4096;
uint32_t Http2::max_header_list_size = 4294967295;
uint32_t Http2::max_request_header_size = 131072;
uint32_t Http2::accept_no_activity_timeout = 120;
uint32_t Http2::no_activity_timeout_in = 115;
uint32_t Http2::active_timeout_in = 0;

void
Http2::init()
{
  REC_EstablishStaticConfigInt32U(max_concurrent_streams, "proxy.config.http2.max_concurrent_streams_in");
  REC_EstablishStaticConfigInt32U(initial_window_size, "proxy.config.http2.initial_window_size_in");
  REC_EstablishStaticConfigInt32U(max_frame_size, "proxy.config.http2.max_frame_size");
  REC_EstablishStaticConfigInt32U(header_table_size, "proxy.config.http2.header_table_size");
  REC_EstablishStaticConfigInt32U(max_header_list_size, "proxy.config.http2.max_header_list_size");
  REC_EstablishStaticConfigInt32U(max_request_header_size, "proxy.config.http.request_header_max_size");
  REC_EstablishStaticConfigInt32U(accept_no_activity_timeout, "proxy.config.http2.accept_no_activity_timeout");
  REC_EstablishStaticConfigInt32U(no_activity_timeout_in, "proxy.config.http2.no_activity_timeout_in");
  REC_EstablishStaticConfigInt32U(active_timeout_in, "proxy.config.http2.active_timeout_in");

  // If any settings is broken, ATS should not start
  ink_release_assert(http2_settings_parameter_is_valid({HTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, max_concurrent_streams}) &&
                     http2_settings_parameter_is_valid({HTTP2_SETTINGS_INITIAL_WINDOW_SIZE, initial_window_size}) &&
                     http2_settings_parameter_is_valid({HTTP2_SETTINGS_MAX_FRAME_SIZE, max_frame_size}) &&
                     http2_settings_parameter_is_valid({HTTP2_SETTINGS_HEADER_TABLE_SIZE, header_table_size}) &&
                     http2_settings_parameter_is_valid({HTTP2_SETTINGS_MAX_HEADER_LIST_SIZE, max_header_list_size}));

  // Setup statistics
  http2_rsb = RecAllocateRawStatBlock(static_cast<int>(HTTP2_N_STATS));
  RecRegisterRawStat(http2_rsb, RECT_PROCESS, HTTP2_STAT_CURRENT_CLIENT_SESSION_NAME, RECD_INT, RECP_NON_PERSISTENT,
                     static_cast<int>(HTTP2_STAT_CURRENT_CLIENT_SESSION_COUNT), RecRawStatSyncSum);
  RecRegisterRawStat(http2_rsb, RECT_PROCESS, HTTP2_STAT_CURRENT_CLIENT_STREAM_NAME, RECD_INT, RECP_NON_PERSISTENT,
                     static_cast<int>(HTTP2_STAT_CURRENT_CLIENT_STREAM_COUNT), RecRawStatSyncSum);
  RecRegisterRawStat(http2_rsb, RECT_PROCESS, HTTP2_STAT_TOTAL_CLIENT_STREAM_NAME, RECD_INT, RECP_PERSISTENT,
                     static_cast<int>(HTTP2_STAT_TOTAL_TRANSACTIONS_TIME), RecRawStatSyncCount);
  RecRegisterRawStat(http2_rsb, RECT_PROCESS, HTTP2_STAT_TOTAL_TRANSACTIONS_TIME_NAME, RECD_INT, RECP_PERSISTENT,
                     static_cast<int>(HTTP2_STAT_TOTAL_TRANSACTIONS_TIME), RecRawStatSyncSum);
  RecRegisterRawStat(http2_rsb, RECT_PROCESS, HTTP2_STAT_TOTAL_CLIENT_CONNECTION_NAME, RECD_INT, RECP_PERSISTENT,
                     static_cast<int>(HTTP2_STAT_TOTAL_CLIENT_CONNECTION_COUNT), RecRawStatSyncSum);
  RecRegisterRawStat(http2_rsb, RECT_PROCESS, HTTP2_STAT_CONNECTION_ERRORS_NAME, RECD_INT, RECP_PERSISTENT,
                     static_cast<int>(HTTP2_STAT_CONNECTION_ERRORS_COUNT), RecRawStatSyncSum);
  RecRegisterRawStat(http2_rsb, RECT_PROCESS, HTTP2_STAT_STREAM_ERRORS_NAME, RECD_INT, RECP_PERSISTENT,
                     static_cast<int>(HTTP2_STAT_STREAM_ERRORS_COUNT), RecRawStatSyncSum);
}

#if TS_HAS_TESTS

void forceLinkRegressionHPACK();
void
forceLinkRegressionHPACKCaller()
{
  forceLinkRegressionHPACK();
}

#include "ts/TestBox.h"

/***********************************************************************************
 *                                                                                 *
 *                       Regression test for HTTP/2                                *
 *                                                                                 *
 ***********************************************************************************/

const static struct {
  uint8_t ftype;
  uint8_t fflags;
  bool valid;
} http2_frame_flags_test_case[] = {{HTTP2_FRAME_TYPE_DATA, 0x00, true},
                                   {HTTP2_FRAME_TYPE_DATA, 0x01, true},
                                   {HTTP2_FRAME_TYPE_DATA, 0x02, false},
                                   {HTTP2_FRAME_TYPE_DATA, 0x04, false},
                                   {HTTP2_FRAME_TYPE_DATA, 0x08, true},
                                   {HTTP2_FRAME_TYPE_DATA, 0x10, false},
                                   {HTTP2_FRAME_TYPE_DATA, 0x20, false},
                                   {HTTP2_FRAME_TYPE_DATA, 0x40, false},
                                   {HTTP2_FRAME_TYPE_DATA, 0x80, false},
                                   {HTTP2_FRAME_TYPE_HEADERS, 0x00, true},
                                   {HTTP2_FRAME_TYPE_HEADERS, 0x01, true},
                                   {HTTP2_FRAME_TYPE_HEADERS, 0x02, false},
                                   {HTTP2_FRAME_TYPE_HEADERS, 0x04, true},
                                   {HTTP2_FRAME_TYPE_HEADERS, 0x08, true},
                                   {HTTP2_FRAME_TYPE_HEADERS, 0x10, false},
                                   {HTTP2_FRAME_TYPE_HEADERS, 0x20, true},
                                   {HTTP2_FRAME_TYPE_HEADERS, 0x40, false},
                                   {HTTP2_FRAME_TYPE_HEADERS, 0x80, false},
                                   {HTTP2_FRAME_TYPE_PRIORITY, 0x00, true},
                                   {HTTP2_FRAME_TYPE_PRIORITY, 0x01, false},
                                   {HTTP2_FRAME_TYPE_PRIORITY, 0x02, false},
                                   {HTTP2_FRAME_TYPE_PRIORITY, 0x04, false},
                                   {HTTP2_FRAME_TYPE_PRIORITY, 0x08, false},
                                   {HTTP2_FRAME_TYPE_PRIORITY, 0x10, false},
                                   {HTTP2_FRAME_TYPE_PRIORITY, 0x20, false},
                                   {HTTP2_FRAME_TYPE_PRIORITY, 0x40, false},
                                   {HTTP2_FRAME_TYPE_PRIORITY, 0x80, false},
                                   {HTTP2_FRAME_TYPE_RST_STREAM, 0x00, true},
                                   {HTTP2_FRAME_TYPE_RST_STREAM, 0x01, false},
                                   {HTTP2_FRAME_TYPE_RST_STREAM, 0x02, false},
                                   {HTTP2_FRAME_TYPE_RST_STREAM, 0x04, false},
                                   {HTTP2_FRAME_TYPE_RST_STREAM, 0x08, false},
                                   {HTTP2_FRAME_TYPE_RST_STREAM, 0x10, false},
                                   {HTTP2_FRAME_TYPE_RST_STREAM, 0x20, false},
                                   {HTTP2_FRAME_TYPE_RST_STREAM, 0x40, false},
                                   {HTTP2_FRAME_TYPE_RST_STREAM, 0x80, false},
                                   {HTTP2_FRAME_TYPE_SETTINGS, 0x00, true},
                                   {HTTP2_FRAME_TYPE_SETTINGS, 0x01, true},
                                   {HTTP2_FRAME_TYPE_SETTINGS, 0x02, false},
                                   {HTTP2_FRAME_TYPE_SETTINGS, 0x04, false},
                                   {HTTP2_FRAME_TYPE_SETTINGS, 0x08, false},
                                   {HTTP2_FRAME_TYPE_SETTINGS, 0x10, false},
                                   {HTTP2_FRAME_TYPE_SETTINGS, 0x20, false},
                                   {HTTP2_FRAME_TYPE_SETTINGS, 0x40, false},
                                   {HTTP2_FRAME_TYPE_SETTINGS, 0x80, false},
                                   {HTTP2_FRAME_TYPE_PUSH_PROMISE, 0x00, true},
                                   {HTTP2_FRAME_TYPE_PUSH_PROMISE, 0x01, false},
                                   {HTTP2_FRAME_TYPE_PUSH_PROMISE, 0x02, false},
                                   {HTTP2_FRAME_TYPE_PUSH_PROMISE, 0x04, true},
                                   {HTTP2_FRAME_TYPE_PUSH_PROMISE, 0x08, true},
                                   {HTTP2_FRAME_TYPE_PUSH_PROMISE, 0x10, false},
                                   {HTTP2_FRAME_TYPE_PUSH_PROMISE, 0x20, false},
                                   {HTTP2_FRAME_TYPE_PUSH_PROMISE, 0x40, false},
                                   {HTTP2_FRAME_TYPE_PUSH_PROMISE, 0x80, false},
                                   {HTTP2_FRAME_TYPE_PING, 0x00, true},
                                   {HTTP2_FRAME_TYPE_PING, 0x01, true},
                                   {HTTP2_FRAME_TYPE_PING, 0x02, false},
                                   {HTTP2_FRAME_TYPE_PING, 0x04, false},
                                   {HTTP2_FRAME_TYPE_PING, 0x08, false},
                                   {HTTP2_FRAME_TYPE_PING, 0x10, false},
                                   {HTTP2_FRAME_TYPE_PING, 0x20, false},
                                   {HTTP2_FRAME_TYPE_PING, 0x40, false},
                                   {HTTP2_FRAME_TYPE_PING, 0x80, false},
                                   {HTTP2_FRAME_TYPE_GOAWAY, 0x00, true},
                                   {HTTP2_FRAME_TYPE_GOAWAY, 0x01, false},
                                   {HTTP2_FRAME_TYPE_GOAWAY, 0x02, false},
                                   {HTTP2_FRAME_TYPE_GOAWAY, 0x04, false},
                                   {HTTP2_FRAME_TYPE_GOAWAY, 0x08, false},
                                   {HTTP2_FRAME_TYPE_GOAWAY, 0x10, false},
                                   {HTTP2_FRAME_TYPE_GOAWAY, 0x20, false},
                                   {HTTP2_FRAME_TYPE_GOAWAY, 0x40, false},
                                   {HTTP2_FRAME_TYPE_GOAWAY, 0x80, false},
                                   {HTTP2_FRAME_TYPE_WINDOW_UPDATE, 0x00, true},
                                   {HTTP2_FRAME_TYPE_WINDOW_UPDATE, 0x01, false},
                                   {HTTP2_FRAME_TYPE_WINDOW_UPDATE, 0x02, false},
                                   {HTTP2_FRAME_TYPE_WINDOW_UPDATE, 0x04, false},
                                   {HTTP2_FRAME_TYPE_WINDOW_UPDATE, 0x08, false},
                                   {HTTP2_FRAME_TYPE_WINDOW_UPDATE, 0x10, false},
                                   {HTTP2_FRAME_TYPE_WINDOW_UPDATE, 0x20, false},
                                   {HTTP2_FRAME_TYPE_WINDOW_UPDATE, 0x40, false},
                                   {HTTP2_FRAME_TYPE_WINDOW_UPDATE, 0x80, false},
                                   {HTTP2_FRAME_TYPE_CONTINUATION, 0x00, true},
                                   {HTTP2_FRAME_TYPE_CONTINUATION, 0x01, false},
                                   {HTTP2_FRAME_TYPE_CONTINUATION, 0x02, false},
                                   {HTTP2_FRAME_TYPE_CONTINUATION, 0x04, true},
                                   {HTTP2_FRAME_TYPE_CONTINUATION, 0x08, false},
                                   {HTTP2_FRAME_TYPE_CONTINUATION, 0x10, false},
                                   {HTTP2_FRAME_TYPE_CONTINUATION, 0x20, false},
                                   {HTTP2_FRAME_TYPE_CONTINUATION, 0x40, false},
                                   {HTTP2_FRAME_TYPE_CONTINUATION, 0x80, false},
                                   {HTTP2_FRAME_TYPE_MAX, 0x00, true},
                                   {HTTP2_FRAME_TYPE_MAX, 0x01, true},
                                   {HTTP2_FRAME_TYPE_MAX, 0x02, true},
                                   {HTTP2_FRAME_TYPE_MAX, 0x04, true},
                                   {HTTP2_FRAME_TYPE_MAX, 0x08, true},
                                   {HTTP2_FRAME_TYPE_MAX, 0x10, true},
                                   {HTTP2_FRAME_TYPE_MAX, 0x20, true},
                                   {HTTP2_FRAME_TYPE_MAX, 0x40, true},
                                   {HTTP2_FRAME_TYPE_MAX, 0x80, true}};

REGRESSION_TEST(HTTP2_FRAME_FLAGS)(RegressionTest *t, int, int *pstatus)
{
  TestBox box(t, pstatus);
  box = REGRESSION_TEST_PASSED;

  for (unsigned int i = 0; i < sizeof(http2_frame_flags_test_case) / sizeof(http2_frame_flags_test_case[0]); ++i) {
    box.check(http2_frame_flags_are_valid(http2_frame_flags_test_case[i].ftype, http2_frame_flags_test_case[i].fflags) ==
                http2_frame_flags_test_case[i].valid,
              "Validation of frame flags (type: %d, flags: %d) are expected %d, but not", http2_frame_flags_test_case[i].ftype,
              http2_frame_flags_test_case[i].fflags, http2_frame_flags_test_case[i].valid);
  }
}

#endif /* TS_HAS_TESTS */

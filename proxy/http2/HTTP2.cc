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
#include "ink_assert.h"
#include "I_RecCore.h"

const char *const HTTP2_CONNECTION_PREFACE = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
static size_t HPACK_LEN_STATUS_VALUE_STR = 3;

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

void
memcpy_and_advance(uint8_t(&dst), byte_pointer &src)
{
  dst = *src.u8;
  ++src.u8;
}

static bool
http2_are_frame_flags_valid(uint8_t ftype, uint8_t fflags)
{
  static const uint8_t mask[HTTP2_FRAME_TYPE_MAX] = {
    HTTP2_FLAGS_DATA_MASK,          HTTP2_FLAGS_HEADERS_MASK,      HTTP2_FLAGS_PRIORITY_MASK, HTTP2_FLAGS_RST_STREAM_MASK,
    HTTP2_FLAGS_SETTINGS_MASK,      HTTP2_FLAGS_PUSH_PROMISE_MASK, HTTP2_FLAGS_PING_MASK,     HTTP2_FLAGS_GOAWAY_MASK,
    HTTP2_FLAGS_WINDOW_UPDATE_MASK, HTTP2_FLAGS_CONTINUATION_MASK,
  };

  // The frame flags are valid for this frame if nothing outside the defined bits is set.
  return (fflags & ~mask[ftype]) == 0;
}

bool
http2_frame_header_is_valid(const Http2FrameHeader &hdr, unsigned max_frame_size)
{
  if (hdr.type >= HTTP2_FRAME_TYPE_MAX) {
    return false;
  }

  if (hdr.length > max_frame_size) {
    return false;
  }

  if (!http2_are_frame_flags_valid(hdr.type, hdr.flags)) {
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
http2_write_settings(const Http2SettingsParameter &param, IOVec iov)
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
  if (iov.iov_len != HTTP2_PING_LEN)
    return false;

  memcpy(iov.iov_base, opaque_data, HTTP2_PING_LEN);

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


// 6.3.  PRIORITY
//
// 0                   1                   2                   3
// 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |E|                  Stream Dependency (31)                     |
// +-+-------------+-----------------------------------------------+
// |   Weight (8)  |
// +-+-------------+

bool
http2_parse_priority_parameter(IOVec iov, Http2Priority &params)
{
  byte_pointer ptr(iov.iov_base);
  byte_addressable_value<uint32_t> dependency;

  memcpy_and_advance(dependency.bytes, ptr);
  memcpy_and_advance(params.weight, ptr);

  params.stream_dependency = ntohs(dependency.value);

  return true;
}

// 6.4.  RST_STREAM
//
// 0                   1                   2                   3
// 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                        Error Code (32)                        |
// +---------------------------------------------------------------+

bool
http2_parse_rst_stream(IOVec iov, Http2RstStream &rst_stream)
{
  byte_pointer ptr(iov.iov_base);
  byte_addressable_value<uint32_t> ec;

  memcpy_and_advance(ec.bytes, ptr);

  rst_stream.error_code = ntohl(ec.value);

  return true;
}

// 6.5.1.  SETTINGS Format
//
// 0                   1                   2                   3
// 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |       Identifier (16)         |
// +-------------------------------+-------------------------------+
// |                        Value (32)                             |
// +---------------------------------------------------------------+

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


// 6.8.  GOAWAY
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


// 6.9.  WINDOW_UPDATE
//
// 0                   1                   2                   3
// 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |R|              Window Size Increment (31)                     |
// +-+-------------------------------------------------------------+

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
    if ((field = headers->field_find(HPACK_VALUE_SCHEME, HPACK_LEN_SCHEME)) != NULL) {
      scheme = field->value_get(&scheme_len);
    } else {
      return PARSE_ERROR;
    }

    if ((field = headers->field_find(HPACK_VALUE_AUTHORITY, HPACK_LEN_AUTHORITY)) != NULL) {
      authority = field->value_get(&authority_len);
    } else {
      return PARSE_ERROR;
    }

    if ((field = headers->field_find(HPACK_VALUE_PATH, HPACK_LEN_PATH)) != NULL) {
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
    if ((field = headers->field_find(HPACK_VALUE_METHOD, HPACK_LEN_METHOD)) != NULL) {
      method = field->value_get(&method_len);

      int method_wks_idx = hdrtoken_tokenize(method, method_len);
      http_hdr_method_set(headers->m_heap, headers->m_http, method, method_wks_idx, method_len, 0);
    } else {
      return PARSE_ERROR;
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

  // Intermediaries SHOULD also remove other connection-
  // specific header fields, such as Keep-Alive, Proxy-Connection,
  // Transfer-Encoding and Upgrade, even if they are not nominated by
  // Connection.
  headers->field_delete(MIME_FIELD_CONNECTION, MIME_LEN_CONNECTION);
  headers->field_delete(MIME_FIELD_KEEP_ALIVE, MIME_LEN_KEEP_ALIVE);
  headers->field_delete(MIME_FIELD_PROXY_CONNECTION, MIME_LEN_PROXY_CONNECTION);
  headers->field_delete(MIME_FIELD_TRANSFER_ENCODING, MIME_LEN_TRANSFER_ENCODING);
  headers->field_delete(MIME_FIELD_UPGRADE, MIME_LEN_UPGRADE);

  return PARSE_DONE;
}

int64_t
http2_write_psuedo_headers(HTTPHdr *in, uint8_t *out, uint64_t out_len, Http2DynamicTable & /* dynamic_table */)
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
                              HPACK_LEN_STATUS_VALUE_STR, true, HPACK_LEN_STATUS + HPACK_LEN_STATUS_VALUE_STR, 0);
    mime_hdr_field_attach(in->m_mime, status_field, 1, NULL);

    // Encode psuedo headers by HPACK
    MIMEFieldWrapper header(status_field, in->m_heap, in->m_http->m_fields_impl);
    len = encode_literal_header_field(p, end, header, HPACK_FIELD_NEVERINDEX_LITERAL);
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
                            Http2DynamicTable & /* dynamic_table */, bool &cont)
{
  uint8_t *p = out;
  uint8_t *end = out + out_len;
  int64_t len;

  ink_assert(http_hdr_type_get(in->m_http) != HTTP_TYPE_UNKNOWN);
  ink_assert(in);

  // TODO Get a index value from the tables for the header field, and then choose a representation type.
  // TODO Each indexing types per field should be passed by a caller, HTTP/2 implementation.

  // Get first header field which is required encoding
  MIMEField *field;
  if (!field_iter.m_block) {
    field = in->iter_get_first(&field_iter);
  } else {
    field = in->iter_get_next(&field_iter);
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

    MIMEFieldIter current_iter = field_iter;
    do {
      MIMEFieldWrapper header(field, in->m_heap, in->m_http->m_fields_impl);
      if ((len = encode_literal_header_field(p, end, header, HPACK_FIELD_INDEXED_LITERAL)) == -1) {
        if (!cont) {
          // Parsing a part of headers is done
          cont = true;
          field_iter = current_iter;
          return p - out;
        } else {
          // Parse error
          return -1;
        }
      }
      p += len;
    } while (field->has_dups() && (field = field->m_next_dup) != NULL);
  }

  // Parsing all headers is done
  return p - out;
}

int64_t
http2_parse_header_fragment(HTTPHdr *hdr, IOVec iov, Http2DynamicTable &dynamic_table, bool cont)
{
  const uint8_t *buf_start = (uint8_t *)iov.iov_base;
  const uint8_t *buf_end = buf_start + iov.iov_len;

  uint8_t *cursor = (uint8_t *)iov.iov_base; // place the cursor at the start
  HdrHeap *heap = hdr->m_heap;
  HTTPHdrImpl *hh = hdr->m_http;

  do {
    int64_t read_bytes = 0;

    // decode a header field encoded by HPACK
    MIMEField *field = mime_field_create(heap, hh->m_fields_impl);
    MIMEFieldWrapper header(field, heap, hh->m_fields_impl);
    HpackFieldType ftype = hpack_parse_field_type(*cursor);

    switch (ftype) {
    case HPACK_FIELD_INDEX:
      read_bytes = decode_indexed_header_field(header, cursor, buf_end, dynamic_table);
      if (read_bytes == HPACK_ERROR_COMPRESSION_ERROR) {
        if (cont) {
          // Parsing a part of headers is done
          return cursor - buf_start;
        } else {
          // Parse error
          return HPACK_ERROR_COMPRESSION_ERROR;
        }
      }
      cursor += read_bytes;
      break;
    case HPACK_FIELD_INDEXED_LITERAL:
    case HPACK_FIELD_NOINDEX_LITERAL:
    case HPACK_FIELD_NEVERINDEX_LITERAL:
      read_bytes = decode_literal_header_field(header, cursor, buf_end, dynamic_table);
      if (read_bytes == HPACK_ERROR_COMPRESSION_ERROR) {
        if (cont) {
          // Parsing a part of headers is done
          return cursor - buf_start;
        } else {
          // Parse error
          return HPACK_ERROR_COMPRESSION_ERROR;
        }
      }
      cursor += read_bytes;
      break;
    case HPACK_FIELD_TABLESIZE_UPDATE:
      read_bytes = update_dynamic_table_size(cursor, buf_end, dynamic_table);
      if (read_bytes == HPACK_ERROR_COMPRESSION_ERROR) {
        if (cont) {
          // Parsing a part of headers is done
          return cursor - buf_start;
        } else {
          // Parse error
          return HPACK_ERROR_COMPRESSION_ERROR;
        }
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

    // :path pseudo header MUST NOT empty for http or https URIs
    if (static_cast<unsigned>(name_len) == HPACK_LEN_PATH && strncmp(name, HPACK_VALUE_PATH, name_len) == 0) {
      int value_len = 0;
      field->value_get(&value_len);
      if (value_len == 0) {
        return HPACK_ERROR_HTTP2_PROTOCOL_ERROR;
      }
    }

    // when The TE header field is received, it MUST NOT contain any
    // value other than "trailers".
    if (name_len == MIME_LEN_TE && strncmp(name, MIME_FIELD_TE, name_len) == 0) {
      int value_len = 0;
      const char *value = field->value_get(&value_len);
      char trailers[] = "trailers";
      if (!(value_len == (sizeof(trailers) - 1) && memcmp(value, trailers, value_len) == 0)) {
        return HPACK_ERROR_HTTP2_PROTOCOL_ERROR;
      }
    }

    // Store to HdrHeap
    mime_hdr_field_attach(hh->m_fields_impl, field, 1, NULL);

    // Check psuedo headers
    if (hdr->fields_count() == 4) {
      if (hdr->field_find(HPACK_VALUE_SCHEME, HPACK_LEN_SCHEME) == NULL ||
          hdr->field_find(HPACK_VALUE_METHOD, HPACK_LEN_METHOD) == NULL ||
          hdr->field_find(HPACK_VALUE_PATH, HPACK_LEN_PATH) == NULL ||
          hdr->field_find(HPACK_VALUE_AUTHORITY, HPACK_LEN_AUTHORITY) == NULL) {
        // Decoded header field is invalid
        return HPACK_ERROR_HTTP2_PROTOCOL_ERROR;
      }
    }
  } while (cursor < buf_end);

  // Psuedo headers is insufficient
  if (hdr->fields_count() < 4 && !cont) {
    return HPACK_ERROR_HTTP2_PROTOCOL_ERROR;
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

void
Http2::init()
{
  REC_EstablishStaticConfigInt32U(max_concurrent_streams, "proxy.config.http2.max_concurrent_streams_in");
  REC_EstablishStaticConfigInt32U(initial_window_size, "proxy.config.http2.initial_window_size_in");
  REC_EstablishStaticConfigInt32U(max_frame_size, "proxy.config.http2.max_frame_size");
  REC_EstablishStaticConfigInt32U(header_table_size, "proxy.config.http2.header_table_size");
  REC_EstablishStaticConfigInt32U(max_header_list_size, "proxy.config.http2.max_header_list_size");
}


#if TS_HAS_TESTS

#include "TestBox.h"

// Constants for regression test
const static int BUFSIZE_FOR_REGRESSION_TEST = 128;
const static int MAX_TEST_FIELD_NUM = 8;

/***********************************************************************************
 *                                                                                 *
 *                   Test cases for regression test                                *
 *                                                                                 *
 * Some test cases are based on examples of specification.                         *
 * http://tools.ietf.org/html/draft-ietf-httpbis-header-compression-09#appendix-D  *
 *                                                                                 *
 ***********************************************************************************/

// D.1.  Integer Representation Examples
const static struct {
  uint32_t raw_integer;
  uint8_t *encoded_field;
  int encoded_field_len;
  int prefix;
} integer_test_case[] = {{10, (uint8_t *) "\x0A", 1, 5}, {1337, (uint8_t *) "\x1F\x9A\x0A", 3, 5}, {42, (uint8_t *) "\x2A", 1, 8}};

// Example: custom-key: custom-header
const static struct {
  char *raw_string;
  uint32_t raw_string_len;
  uint8_t *encoded_field;
  int encoded_field_len;
} string_test_case[] = {{(char *)"custom-key", 10, (uint8_t *) "\xA"
                                                               "custom-key",
                         11},
                        {(char *)"custom-key", 10, (uint8_t *) "\x88"
                                                               "\x25\xa8\x49\xe9\x5b\xa9\x7d\x7f",
                         9}};

// D.2.4.  Indexed Header Field
const static struct {
  int index;
  char *raw_name;
  char *raw_value;
  uint8_t *encoded_field;
  int encoded_field_len;
} indexed_test_case[] = {{2, (char *) ":method", (char *) "GET", (uint8_t *) "\x82", 1}};

// D.2.  Header Field Representation Examples
const static struct {
  char *raw_name;
  char *raw_value;
  int index;
  HpackFieldType type;
  uint8_t *encoded_field;
  int encoded_field_len;
} literal_test_case[] = {
  {(char *)"custom-key", (char *) "custom-header", 0, HPACK_FIELD_INDEXED_LITERAL, (uint8_t *) "\x40\x0a"
                                                                                               "custom-key\x0d"
                                                                                               "custom-header",
   26},
  {(char *)"custom-key", (char *) "custom-header", 0, HPACK_FIELD_NOINDEX_LITERAL, (uint8_t *) "\x00\x0a"
                                                                                               "custom-key\x0d"
                                                                                               "custom-header",
   26},
  {(char *)"custom-key", (char *) "custom-header", 0, HPACK_FIELD_NEVERINDEX_LITERAL, (uint8_t *) "\x10\x0a"
                                                                                                  "custom-key\x0d"
                                                                                                  "custom-header",
   26},
  {(char *)":path", (char *) "/sample/path", 4, HPACK_FIELD_INDEXED_LITERAL, (uint8_t *) "\x44\x0c"
                                                                                         "/sample/path",
   14},
  {(char *)":path", (char *) "/sample/path", 4, HPACK_FIELD_NOINDEX_LITERAL, (uint8_t *) "\x04\x0c"
                                                                                         "/sample/path",
   14},
  {(char *)":path", (char *) "/sample/path", 4, HPACK_FIELD_NEVERINDEX_LITERAL, (uint8_t *) "\x14\x0c"
                                                                                            "/sample/path",
   14},
  {(char *)"password", (char *) "secret", 0, HPACK_FIELD_INDEXED_LITERAL, (uint8_t *) "\x40\x08"
                                                                                      "password\x06"
                                                                                      "secret",
   17},
  {(char *)"password", (char *) "secret", 0, HPACK_FIELD_NOINDEX_LITERAL, (uint8_t *) "\x00\x08"
                                                                                      "password\x06"
                                                                                      "secret",
   17},
  {(char *)"password", (char *) "secret", 0, HPACK_FIELD_NEVERINDEX_LITERAL, (uint8_t *) "\x10\x08"
                                                                                         "password\x06"
                                                                                         "secret",
   17}};

// D.3.  Request Examples without Huffman Coding - D.3.1.  First Request
const static struct {
  char *raw_name;
  char *raw_value;
} raw_field_test_case[][MAX_TEST_FIELD_NUM] = {{
  {(char *)":method", (char *) "GET"},
  {(char *)":scheme", (char *) "http"},
  {(char *)":path", (char *) "/"},
  {(char *)":authority", (char *) "www.example.com"},
  {(char *)"", (char *) ""} // End of this test case
}};
const static struct {
  uint8_t *encoded_field;
  int encoded_field_len;
} encoded_field_test_case[] = {{(uint8_t *)"\x40"
                                           "\x7:method"
                                           "\x3GET"
                                           "\x40"
                                           "\x7:scheme"
                                           "\x4http"
                                           "\x40"
                                           "\x5:path"
                                           "\x1/"
                                           "\x40"
                                           "\xa:authority"
                                           "\xfwww.example.com",
                                64}};

/***********************************************************************************
 *                                                                                 *
 *                                Regression test codes                            *
 *                                                                                 *
 ***********************************************************************************/

REGRESSION_TEST(HPACK_EncodeInteger)(RegressionTest *t, int, int *pstatus)
{
  TestBox box(t, pstatus);
  box = REGRESSION_TEST_PASSED;
  uint8_t buf[BUFSIZE_FOR_REGRESSION_TEST];

  for (unsigned int i = 0; i < sizeof(integer_test_case) / sizeof(integer_test_case[0]); i++) {
    memset(buf, 0, BUFSIZE_FOR_REGRESSION_TEST);

    int len = encode_integer(buf, buf + BUFSIZE_FOR_REGRESSION_TEST, integer_test_case[i].raw_integer, integer_test_case[i].prefix);

    box.check(len == integer_test_case[i].encoded_field_len, "encoded length was %d, expecting %d", len,
              integer_test_case[i].encoded_field_len);
    box.check(len > 0 && memcmp(buf, integer_test_case[i].encoded_field, len) == 0, "encoded value was invalid");
  }
}

REGRESSION_TEST(HPACK_EncodeString)(RegressionTest *t, int, int *pstatus)
{
  TestBox box(t, pstatus);
  box = REGRESSION_TEST_PASSED;

  uint8_t buf[BUFSIZE_FOR_REGRESSION_TEST];
  int len;

  // FIXME Current encoder don't support huffman conding.
  for (unsigned int i = 0; i < 1; i++) {
    memset(buf, 0, BUFSIZE_FOR_REGRESSION_TEST);

    len = encode_string(buf, buf + BUFSIZE_FOR_REGRESSION_TEST, string_test_case[i].raw_string, string_test_case[i].raw_string_len);

    box.check(len == string_test_case[i].encoded_field_len, "encoded length was %d, expecting %d", len,
              integer_test_case[i].encoded_field_len);
    box.check(len > 0 && memcmp(buf, string_test_case[i].encoded_field, len) == 0, "encoded string was invalid");
  }
}

REGRESSION_TEST(HPACK_EncodeIndexedHeaderField)(RegressionTest *t, int, int *pstatus)
{
  TestBox box(t, pstatus);
  box = REGRESSION_TEST_PASSED;

  uint8_t buf[BUFSIZE_FOR_REGRESSION_TEST];

  for (unsigned int i = 0; i < sizeof(indexed_test_case) / sizeof(indexed_test_case[0]); i++) {
    memset(buf, 0, BUFSIZE_FOR_REGRESSION_TEST);

    int len = encode_indexed_header_field(buf, buf + BUFSIZE_FOR_REGRESSION_TEST, indexed_test_case[i].index);

    box.check(len == indexed_test_case[i].encoded_field_len, "encoded length was %d, expecting %d", len,
              indexed_test_case[i].encoded_field_len);
    box.check(len > 0 && memcmp(buf, indexed_test_case[i].encoded_field, len) == 0, "encoded value was invalid");
  }
}

REGRESSION_TEST(HPACK_EncodeLiteralHeaderField)(RegressionTest *t, int, int *pstatus)
{
  TestBox box(t, pstatus);
  box = REGRESSION_TEST_PASSED;

  uint8_t buf[BUFSIZE_FOR_REGRESSION_TEST];
  int len;

  for (unsigned int i = 0; i < sizeof(literal_test_case) / sizeof(literal_test_case[0]); i++) {
    memset(buf, 0, BUFSIZE_FOR_REGRESSION_TEST);

    ats_scoped_obj<HTTPHdr> headers(new HTTPHdr);
    headers->create(HTTP_TYPE_RESPONSE);
    MIMEField *field = mime_field_create(headers->m_heap, headers->m_http->m_fields_impl);
    MIMEFieldWrapper header(field, headers->m_heap, headers->m_http->m_fields_impl);

    header.value_set(literal_test_case[i].raw_value, strlen(literal_test_case[i].raw_value));
    if (literal_test_case[i].index > 0) {
      len = encode_literal_header_field(buf, buf + BUFSIZE_FOR_REGRESSION_TEST, header, literal_test_case[i].index,
                                        literal_test_case[i].type);
    } else {
      header.name_set(literal_test_case[i].raw_name, strlen(literal_test_case[i].raw_name));
      len = encode_literal_header_field(buf, buf + BUFSIZE_FOR_REGRESSION_TEST, header, literal_test_case[i].type);
    }

    box.check(len == literal_test_case[i].encoded_field_len, "encoded length was %d, expecting %d", len,
              literal_test_case[i].encoded_field_len);
    box.check(len > 0 && memcmp(buf, literal_test_case[i].encoded_field, len) == 0, "encoded value was invalid");
  }
}

REGRESSION_TEST(HPACK_Encode)(RegressionTest *t, int, int *pstatus)
{
  TestBox box(t, pstatus);
  box = REGRESSION_TEST_PASSED;

  uint8_t buf[BUFSIZE_FOR_REGRESSION_TEST];
  Http2DynamicTable dynamic_table;

  // FIXME Current encoder don't support indexing.
  for (unsigned int i = 0; i < sizeof(encoded_field_test_case) / sizeof(encoded_field_test_case[0]); i++) {
    ats_scoped_obj<HTTPHdr> headers(new HTTPHdr);
    headers->create(HTTP_TYPE_REQUEST);

    for (unsigned int j = 0; j < sizeof(raw_field_test_case[i]) / sizeof(raw_field_test_case[i][0]); j++) {
      const char *expected_name = raw_field_test_case[i][j].raw_name;
      const char *expected_value = raw_field_test_case[i][j].raw_value;
      if (strlen(expected_name) == 0)
        break;

      MIMEField *field = mime_field_create(headers->m_heap, headers->m_http->m_fields_impl);
      mime_field_name_value_set(headers->m_heap, headers->m_http->m_fields_impl, field, -1, expected_name, strlen(expected_name),
                                expected_value, strlen(expected_value), true, strlen(expected_name) + strlen(expected_value), 1);
      mime_hdr_field_attach(headers->m_http->m_fields_impl, field, 1, NULL);
    }

    memset(buf, 0, BUFSIZE_FOR_REGRESSION_TEST);
    uint64_t buf_len = BUFSIZE_FOR_REGRESSION_TEST;
    int64_t len = http2_write_psuedo_headers(headers, buf, buf_len, dynamic_table);
    buf_len -= len;

    MIMEFieldIter field_iter;
    bool cont = false;
    len += http2_write_header_fragment(headers, field_iter, buf, buf_len, dynamic_table, cont);

    box.check(len == encoded_field_test_case[i].encoded_field_len, "encoded length was %" PRId64 ", expecting %d", len,
              encoded_field_test_case[i].encoded_field_len);
    box.check(len > 0 && memcmp(buf, encoded_field_test_case[i].encoded_field, len) == 0, "encoded value was invalid");
  }
}

REGRESSION_TEST(HPACK_DecodeInteger)(RegressionTest *t, int, int *pstatus)
{
  TestBox box(t, pstatus);
  box = REGRESSION_TEST_PASSED;

  uint32_t actual;

  for (unsigned int i = 0; i < sizeof(integer_test_case) / sizeof(integer_test_case[0]); i++) {
    int len =
      decode_integer(actual, integer_test_case[i].encoded_field,
                     integer_test_case[i].encoded_field + integer_test_case[i].encoded_field_len, integer_test_case[i].prefix);

    box.check(len == integer_test_case[i].encoded_field_len, "decoded length was %d, expecting %d", len,
              integer_test_case[i].encoded_field_len);
    box.check(actual == integer_test_case[i].raw_integer, "decoded value was %d, expected %d", actual,
              integer_test_case[i].raw_integer);
  }
}

REGRESSION_TEST(HPACK_DecodeString)(RegressionTest *t, int, int *pstatus)
{
  TestBox box(t, pstatus);
  box = REGRESSION_TEST_PASSED;

  Arena arena;
  char *actual = NULL;
  uint32_t actual_len = 0;

  hpack_huffman_init();

  for (unsigned int i = 0; i < sizeof(string_test_case) / sizeof(string_test_case[0]); i++) {
    int len = decode_string(arena, &actual, actual_len, string_test_case[i].encoded_field,
                            string_test_case[i].encoded_field + string_test_case[i].encoded_field_len);

    box.check(len == string_test_case[i].encoded_field_len, "decoded length was %d, expecting %d", len,
              string_test_case[i].encoded_field_len);
    box.check(actual_len == string_test_case[i].raw_string_len, "length of decoded string was %d, expecting %d", actual_len,
              string_test_case[i].raw_string_len);
    box.check(memcmp(actual, string_test_case[i].raw_string, actual_len) == 0, "decoded string was invalid");
  }
}

REGRESSION_TEST(HPACK_DecodeIndexedHeaderField)(RegressionTest *t, int, int *pstatus)
{
  TestBox box(t, pstatus);
  box = REGRESSION_TEST_PASSED;

  Http2DynamicTable dynamic_table;

  for (unsigned int i = 0; i < sizeof(indexed_test_case) / sizeof(indexed_test_case[0]); i++) {
    ats_scoped_obj<HTTPHdr> headers(new HTTPHdr);
    headers->create(HTTP_TYPE_REQUEST);
    MIMEField *field = mime_field_create(headers->m_heap, headers->m_http->m_fields_impl);
    MIMEFieldWrapper header(field, headers->m_heap, headers->m_http->m_fields_impl);

    int len =
      decode_indexed_header_field(header, indexed_test_case[i].encoded_field,
                                  indexed_test_case[i].encoded_field + indexed_test_case[i].encoded_field_len, dynamic_table);

    box.check(len == indexed_test_case[i].encoded_field_len, "decoded length was %d, expecting %d", len,
              indexed_test_case[i].encoded_field_len);

    int name_len;
    const char *name = header.name_get(&name_len);
    box.check(len > 0 && memcmp(name, indexed_test_case[i].raw_name, name_len) == 0, "decoded header name was invalid");

    int actual_value_len;
    const char *actual_value = header.value_get(&actual_value_len);
    box.check(memcmp(actual_value, indexed_test_case[i].raw_value, actual_value_len) == 0, "decoded header value was invalid");
  }
}

REGRESSION_TEST(HPACK_DecodeLiteralHeaderField)(RegressionTest *t, int, int *pstatus)
{
  TestBox box(t, pstatus);
  box = REGRESSION_TEST_PASSED;

  Http2DynamicTable dynamic_table;

  for (unsigned int i = 0; i < sizeof(literal_test_case) / sizeof(literal_test_case[0]); i++) {
    ats_scoped_obj<HTTPHdr> headers(new HTTPHdr);
    headers->create(HTTP_TYPE_REQUEST);
    MIMEField *field = mime_field_create(headers->m_heap, headers->m_http->m_fields_impl);
    MIMEFieldWrapper header(field, headers->m_heap, headers->m_http->m_fields_impl);

    int len =
      decode_literal_header_field(header, literal_test_case[i].encoded_field,
                                  literal_test_case[i].encoded_field + literal_test_case[i].encoded_field_len, dynamic_table);

    box.check(len == literal_test_case[i].encoded_field_len, "decoded length was %d, expecting %d", len,
              literal_test_case[i].encoded_field_len);

    int name_len;
    const char *name = header.name_get(&name_len);
    box.check(name_len > 0 && memcmp(name, literal_test_case[i].raw_name, name_len) == 0, "decoded header name was invalid");

    int actual_value_len;
    const char *actual_value = header.value_get(&actual_value_len);
    box.check(actual_value_len > 0 && memcmp(actual_value, literal_test_case[i].raw_value, actual_value_len) == 0,
              "decoded header value was invalid");
  }
}

REGRESSION_TEST(HPACK_Decode)(RegressionTest *t, int, int *pstatus)
{
  TestBox box(t, pstatus);
  box = REGRESSION_TEST_PASSED;

  Http2DynamicTable dynamic_table;

  for (unsigned int i = 0; i < sizeof(encoded_field_test_case) / sizeof(encoded_field_test_case[0]); i++) {
    ats_scoped_obj<HTTPHdr> headers(new HTTPHdr);
    headers->create(HTTP_TYPE_REQUEST);

    http2_parse_header_fragment(headers,
                                make_iovec(encoded_field_test_case[i].encoded_field, encoded_field_test_case[i].encoded_field_len),
                                dynamic_table, false);

    for (unsigned int j = 0; j < sizeof(raw_field_test_case[i]) / sizeof(raw_field_test_case[i][0]); j++) {
      const char *expected_name = raw_field_test_case[i][j].raw_name;
      const char *expected_value = raw_field_test_case[i][j].raw_value;
      if (strlen(expected_name) == 0)
        break;

      MIMEField *field = headers->field_find(expected_name, strlen(expected_name));
      box.check(field != NULL, "A MIMEField that has \"%s\" as name doesn't exist", expected_name);

      if (field) {
        int actual_value_len;
        const char *actual_value = field->value_get(&actual_value_len);
        box.check(strncmp(expected_value, actual_value, actual_value_len) == 0,
                  "A MIMEField that has \"%s\" as value doesn't exist", expected_value);
      }
    }
  }
}

#endif /* TS_HAS_TESTS */

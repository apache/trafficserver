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

// Avoid a [-Werror,-Wunused-function] error until we need this overload ...
#if 0
static void
write_and_advance(byte_pointer& dst, uint16_t src)
{
  byte_addressable_value<uint16_t> pval;

  pval.value = htons(src);
  memcpy(dst.u8, pval.bytes, sizeof(pval.bytes));
  dst.u8 += sizeof(pval.bytes);
}
#endif

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
  static const uint32_t settings_max[HTTP2_SETTINGS_MAX] = {
    0,
    UINT_MAX, // HTTP2_SETTINGS_HEADER_TABLE_SIZE
    1,        // HTTP2_SETTINGS_ENABLE_PUSH
    UINT_MAX, // HTTP2_SETTINGS_MAX_CONCURRENT_STREAMS
    HTTP2_MAX_WINDOW_SIZE, // HTTP2_SETTINGS_INITIAL_WINDOW_SIZE
    16777215, // HTTP2_SETTINGS_MAX_FRAME_SIZE
    UINT_MAX, // HTTP2_SETTINGS_MAX_HEADER_LIST_SIZE
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
http2_parse_frame_header(IOVec iov, Http2FrameHeader& hdr)
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
  streamid.bytes[0] &= 0x7f;// Clear the high reserved bit
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
// |       Identifier (16)         |
// +-------------------------------+-------------------------------+
// |                        Value (32)                             |
// +---------------------------------------------------------------+

bool
http2_parse_settings_parameter(IOVec iov, Http2SettingsParameter& param)
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

MIMEParseResult
convert_from_2_to_1_1_header(HTTPHdr* headers)
{
  MIMEField* field;

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
    char* url = arena.str_alloc(url_length);
    const char* url_start = url;
    strncpy(url, scheme, scheme_len);
    strncpy(url+scheme_len, "://", 3);
    strncpy(url+scheme_len+3, authority, authority_len);
    strncpy(url+scheme_len+3+authority_len, path, path_len);
    url_parse(headers->m_heap, headers->m_http->u.req.m_url_impl, &url_start, url + url_length, 1);
    arena.str_free(url);

    // Get value of :method
    if ((field = headers->field_find(HPACK_VALUE_METHOD, HPACK_LEN_METHOD)) != NULL) {
      method = field->value_get(&method_len);

      int method_wks_idx = hdrtoken_tokenize(method, method_len);
      http_hdr_method_set(headers->m_heap, headers->m_http,
                          method, method_wks_idx, method_len, 0);
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
    const char* status;

    if ((field = headers->field_find(HPACK_VALUE_STATUS, HPACK_LEN_STATUS)) != NULL) {
      status = field->value_get(&status_len);
      headers->status_set(http_parse_status(status, status + status_len));
    } else {
      return PARSE_ERROR;
    }

    // Remove HTTP/2 style headers
    headers->field_delete(HPACK_VALUE_STATUS, HPACK_LEN_STATUS);
  }

  return PARSE_DONE;
}

int64_t
convert_from_1_1_to_2_header(HTTPHdr* in, uint8_t* out, uint64_t out_len, Http2HeaderTable& /* header_table */)
{
  uint8_t *p = out;
  uint8_t *end = out + out_len;
  int64_t len;

  ink_assert(http_hdr_type_get(in->m_http) != HTTP_TYPE_UNKNOWN);

  // TODO Get a index value from the tables for the header field, and then choose a representation type.
  // TODO Each indexing types per field should be passed by a caller, HTTP/2 implementation.

  MIMEField* field;
  MIMEFieldIter field_iter;
  for (field = in->iter_get_first(&field_iter); field != NULL; field = in->iter_get_next(&field_iter)) {
    do {
      MIMEFieldWrapper header(field, in->m_heap, in->m_http->m_fields_impl);
      if ((len = encode_literal_header_field(p, end, header, HPACK_FIELD_INDEXED_LITERAL)) == -1) {
        return -1;
      }
      p += len;
    } while (field->has_dups() && (field = field->m_next_dup) != NULL);
  }

  return p - out;
}

MIMEParseResult
http2_parse_header_fragment(HTTPHdr * hdr, IOVec iov, Http2HeaderTable& header_table)
{
  uint8_t * buf_start = (uint8_t *)iov.iov_base;
  uint8_t * buf_end = (uint8_t *)iov.iov_base + iov.iov_len;

  uint8_t * cursor = buf_start;
  HdrHeap * heap = hdr->m_heap;
  HTTPHdrImpl * hh = hdr->m_http;

  do {
    int64_t read_bytes = 0;

    if ((read_bytes = update_header_table_size(cursor, buf_end, header_table)) == -1) {
      return PARSE_ERROR;
    }

    // decode a header field encoded by HPACK
    MIMEField *field = mime_field_create(heap, hh->m_fields_impl);
    MIMEFieldWrapper header(field, heap, hh->m_fields_impl);
    HpackFieldType ftype = hpack_parse_field_type(*cursor);

    switch (ftype) {
    case HPACK_FIELD_INDEX:
      if ((read_bytes = decode_indexed_header_field(header, cursor, buf_end, header_table)) == -1) {
        return PARSE_ERROR;
      }
      cursor += read_bytes;
      break;
    case HPACK_FIELD_INDEXED_LITERAL:
    case HPACK_FIELD_NOINDEX_LITERAL:
    case HPACK_FIELD_NEVERINDEX_LITERAL:
      if ((read_bytes = decode_literal_header_field(header, cursor, buf_end, header_table)) == -1) {
        return PARSE_ERROR;
      }
      cursor += read_bytes;
      break;
    case HPACK_FIELD_TABLESIZE_UPDATE:
      // XXX not supported yet
      return PARSE_ERROR;
    }

    // Store to HdrHeap
    mime_hdr_field_attach(hh->m_fields_impl, field, 1, NULL);
  } while (cursor < buf_end);

  return PARSE_DONE;
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
  uint8_t* encoded_field;
  int encoded_field_len;
  int prefix;
} integer_test_case[] = {
  { 10, (uint8_t*)"\x0A", 1, 5 },
  { 1337, (uint8_t*)"\x1F\x9A\x0A", 3, 5 },
  { 42, (uint8_t*)"\x2A", 1, 8 }
};

// Example: custom-key: custom-header
const static struct {
  char* raw_string;
  uint32_t raw_string_len;
  uint8_t* encoded_field;
  int encoded_field_len;
} string_test_case[] = {
  { (char*)"custom-key", 10, (uint8_t*)"\xA" "custom-key", 11 },
  { (char*)"custom-key", 10, (uint8_t*)"\x88" "\x25\xa8\x49\xe9\x5b\xa9\x7d\x7f", 9 }
};

// D.2.4.  Indexed Header Field
const static struct {
  int index;
  char* raw_name;
  char* raw_value;
  uint8_t* encoded_field;
  int encoded_field_len;
} indexed_test_case[] = {
  { 2, (char*)":method", (char*)"GET", (uint8_t*)"\x82", 1 }
};

// D.2.  Header Field Representation Examples
const static struct {
  char* raw_name;
  char* raw_value;
  int index;
  HpackFieldType type;
  uint8_t* encoded_field;
  int encoded_field_len;
} literal_test_case[] = {
  { (char*)"custom-key", (char*)"custom-header", 0, HPACK_FIELD_INDEXED_LITERAL, (uint8_t*)"\x40\x0a" "custom-key\x0d" "custom-header", 26 },
  { (char*)"custom-key", (char*)"custom-header", 0, HPACK_FIELD_NOINDEX_LITERAL, (uint8_t*)"\x00\x0a" "custom-key\x0d" "custom-header", 26 },
  { (char*)"custom-key", (char*)"custom-header", 0, HPACK_FIELD_NEVERINDEX_LITERAL, (uint8_t*)"\x10\x0a" "custom-key\x0d" "custom-header", 26 },
  { (char*)":path", (char*)"/sample/path", 4, HPACK_FIELD_INDEXED_LITERAL, (uint8_t*)"\x44\x0c" "/sample/path", 14 },
  { (char*)":path", (char*)"/sample/path", 4, HPACK_FIELD_NOINDEX_LITERAL, (uint8_t*)"\x04\x0c" "/sample/path", 14 },
  { (char*)":path", (char*)"/sample/path", 4, HPACK_FIELD_NEVERINDEX_LITERAL, (uint8_t*)"\x14\x0c" "/sample/path", 14 },
  { (char*)"password", (char*)"secret", 0, HPACK_FIELD_INDEXED_LITERAL, (uint8_t*)"\x40\x08" "password\x06" "secret", 17 },
  { (char*)"password", (char*)"secret", 0, HPACK_FIELD_NOINDEX_LITERAL, (uint8_t*)"\x00\x08" "password\x06" "secret", 17 },
  { (char*)"password", (char*)"secret", 0, HPACK_FIELD_NEVERINDEX_LITERAL, (uint8_t*)"\x10\x08" "password\x06" "secret", 17 }
};

// D.3.  Request Examples without Huffman Coding - D.3.1.  First Request
const static struct {
  char* raw_name;
  char* raw_value;
} raw_field_test_case[][MAX_TEST_FIELD_NUM] = {
  {
    { (char*)":method",    (char*)"GET" },
    { (char*)":scheme",    (char*)"http" },
    { (char*)":path",      (char*)"/" },
    { (char*)":authority", (char*)"www.example.com" },
    { (char*)"", (char*)"" } // End of this test case
  }
};
const static struct {
  uint8_t* encoded_field;
  int encoded_field_len;
} encoded_field_test_case[] = {
  {
    (uint8_t*)"\x40" "\x7:method"    "\x3GET"
              "\x40" "\x7:scheme"    "\x4http"
              "\x40" "\x5:path"      "\x1/"
              "\x40" "\xa:authority" "\xfwww.example.com",
    64
  }
};

/***********************************************************************************
 *                                                                                 *
 *                                Regression test codes                            *
 *                                                                                 *
 ***********************************************************************************/

REGRESSION_TEST(HPACK_EncodeInteger)(RegressionTest * t, int, int *pstatus)
{
  TestBox box(t, pstatus);
  box = REGRESSION_TEST_PASSED;
  uint8_t buf[BUFSIZE_FOR_REGRESSION_TEST];

  for (unsigned int i=0; i<sizeof(integer_test_case)/sizeof(integer_test_case[0]); i++) {
    memset(buf, 0, BUFSIZE_FOR_REGRESSION_TEST);

    int len = encode_integer(buf, buf+BUFSIZE_FOR_REGRESSION_TEST, integer_test_case[i].raw_integer, integer_test_case[i].prefix);

    box.check(len == integer_test_case[i].encoded_field_len, "encoded length was %d, expecting %d",
        len, integer_test_case[i].encoded_field_len);
    box.check(memcmp(buf, integer_test_case[i].encoded_field, len) == 0, "encoded value was invalid");
  }
}

REGRESSION_TEST(HPACK_EncodeString)(RegressionTest * t, int, int *pstatus)
{
  TestBox box(t, pstatus);
  box = REGRESSION_TEST_PASSED;

  uint8_t buf[BUFSIZE_FOR_REGRESSION_TEST];
  int len;

  // FIXME Current encoder don't support huffman conding.
  for (unsigned int i=0; i<1; i++) {
    memset(buf, 0, BUFSIZE_FOR_REGRESSION_TEST);

    len = encode_string(buf, buf+BUFSIZE_FOR_REGRESSION_TEST, string_test_case[i].raw_string, string_test_case[i].raw_string_len);

    box.check(len == string_test_case[i].encoded_field_len, "encoded length was %d, expecting %d",
        len, integer_test_case[i].encoded_field_len);
    box.check(memcmp(buf, string_test_case[i].encoded_field, len) == 0, "encoded string was invalid");
  }
}

REGRESSION_TEST(HPACK_EncodeIndexedHeaderField)(RegressionTest * t, int, int *pstatus)
{
  TestBox box(t, pstatus);
  box = REGRESSION_TEST_PASSED;

  uint8_t buf[BUFSIZE_FOR_REGRESSION_TEST];

  for (unsigned int i=0; i<sizeof(indexed_test_case)/sizeof(indexed_test_case[0]); i++) {
    memset(buf, 0, BUFSIZE_FOR_REGRESSION_TEST);

    int len = encode_indexed_header_field(buf, buf+BUFSIZE_FOR_REGRESSION_TEST, indexed_test_case[i].index);

    box.check(len == indexed_test_case[i].encoded_field_len, "encoded length was %d, expecting %d",
        len, indexed_test_case[i].encoded_field_len);
    box.check(memcmp(buf, indexed_test_case[i].encoded_field, len) == 0, "encoded value was invalid");
  }
}

REGRESSION_TEST(HPACK_EncodeLiteralHeaderField)(RegressionTest * t, int, int *pstatus)
{
  TestBox box(t, pstatus);
  box = REGRESSION_TEST_PASSED;

  uint8_t buf[BUFSIZE_FOR_REGRESSION_TEST];
  int len;

  for (unsigned int i=0; i<sizeof(literal_test_case)/sizeof(literal_test_case[0]); i++) {
    memset(buf, 0, BUFSIZE_FOR_REGRESSION_TEST);

    HTTPHdr* headers = new HTTPHdr();
    headers->create(HTTP_TYPE_RESPONSE);
    MIMEField *field = mime_field_create(headers->m_heap, headers->m_http->m_fields_impl);
    MIMEFieldWrapper header(field, headers->m_heap, headers->m_http->m_fields_impl);

    header.value_set(literal_test_case[i].raw_value, strlen(literal_test_case[i].raw_value));
    if (literal_test_case[i].index > 0) {
      len = encode_literal_header_field(buf, buf+BUFSIZE_FOR_REGRESSION_TEST, header, literal_test_case[i].index, literal_test_case[i].type);
    } else {
      header.name_set(literal_test_case[i].raw_name, strlen(literal_test_case[i].raw_name));
      len = encode_literal_header_field(buf, buf+BUFSIZE_FOR_REGRESSION_TEST, header, literal_test_case[i].type);
    }

    box.check(len == literal_test_case[i].encoded_field_len, "encoded length was %d, expecting %d", len, literal_test_case[i].encoded_field_len);
    box.check(memcmp(buf, literal_test_case[i].encoded_field, len) == 0, "encoded value was invalid");
  }

}

REGRESSION_TEST(HPACK_Encode)(RegressionTest * t, int, int *pstatus)
{
  TestBox box(t, pstatus);
  box = REGRESSION_TEST_PASSED;

  uint8_t buf[BUFSIZE_FOR_REGRESSION_TEST];
  Http2HeaderTable header_table;

  // FIXME Current encoder don't support indexing.
  for (unsigned int i=0; i<sizeof(encoded_field_test_case)/sizeof(encoded_field_test_case[0]); i++) {
    HTTPHdr* headers = new HTTPHdr();
    headers->create(HTTP_TYPE_REQUEST);

    for (unsigned int j=0; j<sizeof(raw_field_test_case[i])/sizeof(raw_field_test_case[i][0]); j++) {
      const char* expected_name  = raw_field_test_case[i][j].raw_name;
      const char* expected_value = raw_field_test_case[i][j].raw_value;
      if (strlen(expected_name) == 0) break;

      MIMEField* field = mime_field_create(headers->m_heap, headers->m_http->m_fields_impl);
      mime_field_name_value_set(headers->m_heap, headers->m_http->m_fields_impl, field, -1,
          expected_name,  strlen(expected_name), expected_value, strlen(expected_value),
          true, strlen(expected_name) + strlen(expected_value), 1);
      mime_hdr_field_attach(headers->m_http->m_fields_impl, field, 1, NULL);
    }

    memset(buf, 0, BUFSIZE_FOR_REGRESSION_TEST);
    int len = convert_from_1_1_to_2_header(headers, buf, BUFSIZE_FOR_REGRESSION_TEST, header_table);

    box.check(len == encoded_field_test_case[i].encoded_field_len, "encoded length was %d, expecting %d",
        len, encoded_field_test_case[i].encoded_field_len);
    box.check(memcmp(buf, encoded_field_test_case[i].encoded_field, len) == 0, "encoded value was invalid");
  }
}

REGRESSION_TEST(HPACK_DecodeInteger)(RegressionTest * t, int, int *pstatus)
{
  TestBox box(t, pstatus);
  box = REGRESSION_TEST_PASSED;

  uint32_t actual;

  for (unsigned int i=0; i<sizeof(integer_test_case)/sizeof(integer_test_case[0]); i++) {
    int len = decode_integer(actual, integer_test_case[i].encoded_field,
        integer_test_case[i].encoded_field + integer_test_case[i].encoded_field_len,
        integer_test_case[i].prefix);

    box.check(len == integer_test_case[i].encoded_field_len, "decoded length was %d, expecting %d",
        len, integer_test_case[i].encoded_field_len);
    box.check(actual == integer_test_case[i].raw_integer, "decoded value was %d, expected %d",
        actual, integer_test_case[i].raw_integer);
  }
}

REGRESSION_TEST(HPACK_DecodeString)(RegressionTest * t, int, int *pstatus)
{
  TestBox box(t, pstatus);
  box = REGRESSION_TEST_PASSED;

  char* actual;
  uint32_t actual_len;

  hpack_huffman_init();

  for (unsigned int i=0; i<sizeof(string_test_case)/sizeof(string_test_case[0]); i++) {
    int len = decode_string(&actual, actual_len, string_test_case[i].encoded_field,
        string_test_case[i].encoded_field + string_test_case[i].encoded_field_len);

    box.check(len == string_test_case[i].encoded_field_len, "decoded length was %d, expecting %d",
        len, string_test_case[i].encoded_field_len);
    box.check(actual_len == string_test_case[i].raw_string_len, "length of decoded string was %d, expecting %d",
        actual_len, string_test_case[i].raw_string_len);
    box.check(memcmp(actual, string_test_case[i].raw_string, actual_len) == 0, "decoded string was invalid");

    ats_free(actual);
  }
}

REGRESSION_TEST(HPACK_DecodeIndexedHeaderField)(RegressionTest * t, int, int *pstatus)
{
  TestBox box(t, pstatus);
  box = REGRESSION_TEST_PASSED;

  Http2HeaderTable header_table;

  for (unsigned int i=0; i<sizeof(indexed_test_case)/sizeof(indexed_test_case[0]); i++) {
    HTTPHdr* headers = new HTTPHdr();
    headers->create(HTTP_TYPE_REQUEST);
    MIMEField *field = mime_field_create(headers->m_heap, headers->m_http->m_fields_impl);
    MIMEFieldWrapper header(field, headers->m_heap, headers->m_http->m_fields_impl);

    int len = decode_indexed_header_field(header, indexed_test_case[i].encoded_field,
        indexed_test_case[i].encoded_field+indexed_test_case[i].encoded_field_len, header_table);

    box.check(len == indexed_test_case[i].encoded_field_len, "decoded length was %d, expecting %d",
        len, indexed_test_case[i].encoded_field_len);

    int name_len;
    const char* name = header.name_get(&name_len);
    box.check(memcmp(name, indexed_test_case[i].raw_name, name_len) == 0,
      "decoded header name was invalid");

    int actual_value_len;
    const char* actual_value = header.value_get(&actual_value_len);
    box.check(memcmp(actual_value, indexed_test_case[i].raw_value, actual_value_len) == 0,
      "decoded header value was invalid");
  }
}

REGRESSION_TEST(HPACK_DecodeLiteralHeaderField)(RegressionTest * t, int, int *pstatus)
{
  TestBox box(t, pstatus);
  box = REGRESSION_TEST_PASSED;

  Http2HeaderTable header_table;

  for (unsigned int i=0; i<sizeof(literal_test_case)/sizeof(literal_test_case[0]); i++) {
    HTTPHdr* headers = new HTTPHdr();
    headers->create(HTTP_TYPE_REQUEST);
    MIMEField *field = mime_field_create(headers->m_heap, headers->m_http->m_fields_impl);
    MIMEFieldWrapper header(field, headers->m_heap, headers->m_http->m_fields_impl);

    int len = decode_literal_header_field(header, literal_test_case[i].encoded_field,
        literal_test_case[i].encoded_field+literal_test_case[i].encoded_field_len, header_table);

    box.check(len == literal_test_case[i].encoded_field_len, "decoded length was %d, expecting %d",
        len, literal_test_case[i].encoded_field_len);

    int name_len;
    const char* name = header.name_get(&name_len);
    box.check(memcmp(name, literal_test_case[i].raw_name, name_len) == 0,
      "decoded header name was invalid");

    int actual_value_len;
    const char* actual_value = header.value_get(&actual_value_len);
    box.check(memcmp(actual_value, literal_test_case[i].raw_value, actual_value_len) == 0,
      "decoded header value was invalid");
  }
}

REGRESSION_TEST(HPACK_Decode)(RegressionTest * t, int, int *pstatus)
{
  TestBox box(t, pstatus);
  box = REGRESSION_TEST_PASSED;

  Http2HeaderTable header_table;

  for (unsigned int i=0; i<sizeof(encoded_field_test_case)/sizeof(encoded_field_test_case[0]); i++) {
    HTTPHdr* headers = new HTTPHdr();
    headers->create(HTTP_TYPE_REQUEST);

    http2_parse_header_fragment(headers, make_iovec(encoded_field_test_case[i].encoded_field, encoded_field_test_case[i].encoded_field_len), header_table);

    for (unsigned int j=0; j<sizeof(raw_field_test_case[i])/sizeof(raw_field_test_case[i][0]); j++) {
      const char* expected_name  = raw_field_test_case[i][j].raw_name;
      const char* expected_value = raw_field_test_case[i][j].raw_value;
      if (strlen(expected_name) == 0) break;

      MIMEField* field = headers->field_find(expected_name, strlen(expected_name));
      box.check(field != NULL, "A MIMEField that has \"%s\" as name doesn't exist", expected_name);

      if (field) {
        int actual_value_len;
        const char* actual_value = field->value_get(&actual_value_len);
        box.check(strncmp(expected_value, actual_value, actual_value_len) == 0, "A MIMEField that has \"%s\" as value doesn't exist", expected_value);
      }
    }
  }
}

#endif /* TS_HAS_TESTS */

/** @file
 *
 *  Regression Tests for HPACK
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
#include "ts/TestBox.h"

// Constants for regression test
const static int BUFSIZE_FOR_REGRESSION_TEST = 128;
const static int MAX_TEST_FIELD_NUM = 8;

/***********************************************************************************
 *                                                                                 *
 *                       Regression test for HPACK                                 *
 *                                                                                 *
 *  Some test cases are based on examples of specification.                        *
 *  - https://tools.ietf.org/html/rfc7541#appendix-C                               *
 *                                                                                 *
 ***********************************************************************************/

// [RFC 7541] C.1. Integer Representation Examples
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
} string_test_case[] = {{(char *)"", 0,            (uint8_t *) "\x0"
                                                               "",
                         1},
                        {(char *)"custom-key", 10, (uint8_t *) "\xA"
                                                               "custom-key",
                         11},
                        {(char *)"", 0,            (uint8_t *) "\x80"
                                                               "",
                         1},
                        {(char *)"custom-key", 10, (uint8_t *) "\x88"
                                                               "\x25\xa8\x49\xe9\x5b\xa9\x7d\x7f",
                         9}};

// [RFC 7541] C.2.4. Indexed Header Field
const static struct {
  int index;
  char *raw_name;
  char *raw_value;
  uint8_t *encoded_field;
  int encoded_field_len;
} indexed_test_case[] = {{2, (char *) ":method", (char *) "GET", (uint8_t *) "\x82", 1}};

// [RFC 7541] C.2. Header Field Representation Examples
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
   17},
  // with Huffman Coding
  {(char *)"custom-key", (char *) "custom-header", 0, HPACK_FIELD_INDEXED_LITERAL,
   (uint8_t *) "\x40"
               "\x88\x25\xa8\x49\xe9\x5b\xa9\x7d\x7f"
               "\x89\x25\xa8\x49\xe9\x5a\x72\x8e\x42\xd9",
   20},
  {(char *)"custom-key", (char *) "custom-header", 0, HPACK_FIELD_NOINDEX_LITERAL,
   (uint8_t *) "\x00"
               "\x88\x25\xa8\x49\xe9\x5b\xa9\x7d\x7f"
               "\x89\x25\xa8\x49\xe9\x5a\x72\x8e\x42\xd9",
   20},
  {(char *)"custom-key", (char *) "custom-header", 0, HPACK_FIELD_NEVERINDEX_LITERAL,
   (uint8_t *) "\x10"
               "\x88\x25\xa8\x49\xe9\x5b\xa9\x7d\x7f"
               "\x89\x25\xa8\x49\xe9\x5a\x72\x8e\x42\xd9",
   20},
  {(char *)":path", (char *) "/sample/path", 4, HPACK_FIELD_INDEXED_LITERAL, (uint8_t *) "\x44"
                                                                                         "\x89\x61\x03\xa6\xba\x0a\xc5\x63\x4c\xff",
   11},
  {(char *)":path", (char *) "/sample/path", 4, HPACK_FIELD_NOINDEX_LITERAL, (uint8_t *) "\x04"
                                                                                         "\x89\x61\x03\xa6\xba\x0a\xc5\x63\x4c\xff",
   11},
  {(char *)":path", (char *) "/sample/path", 4, HPACK_FIELD_NEVERINDEX_LITERAL,
   (uint8_t *) "\x14"
               "\x89\x61\x03\xa6\xba\x0a\xc5\x63\x4c\xff",
   11},
  {(char *)"password", (char *) "secret", 0, HPACK_FIELD_INDEXED_LITERAL, (uint8_t *) "\x40"
                                                                                      "\x86\xac\x68\x47\x83\xd9\x27"
                                                                                      "\x84\x41\x49\x61\x53",
   13},
  {(char *)"password", (char *) "secret", 0, HPACK_FIELD_NOINDEX_LITERAL, (uint8_t *) "\x00"
                                                                                      "\x86\xac\x68\x47\x83\xd9\x27"
                                                                                      "\x84\x41\x49\x61\x53",
   13},
  {(char *)"password", (char *) "secret", 0, HPACK_FIELD_NEVERINDEX_LITERAL, (uint8_t *) "\x10"
                                                                                         "\x86\xac\x68\x47\x83\xd9\x27"
                                                                                         "\x84\x41\x49\x61\x53",
   13}};

// [RFC 7541] C.3. Request Examples without Huffman Coding - C.3.1. First Request
// [RFC 7541] C.4. Request Examples with Huffman Coding - C.4.1. First Request
const static struct {
  char *raw_name;
  char *raw_value;
} raw_field_test_case[][MAX_TEST_FIELD_NUM] = {{
                                                 {(char *)":method", (char *) "GET"},
                                                 {(char *)":scheme", (char *) "http"},
                                                 {(char *)":path", (char *) "/"},
                                                 {(char *)":authority", (char *) "www.example.com"},
                                                 {(char *)"", (char *) ""} // End of this test case
                                               },
                                               {
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
                                64},
                               {(uint8_t *)"\x40"
                                           "\x85\xb9\x49\x53\x39\xe4"
                                           "\x83\xc5\x83\x7f"
                                           "\x40"
                                           "\x85\xb8\x82\x4e\x5a\x4b"
                                           "\x83\x9d\x29\xaf"
                                           "\x40"
                                           "\x84\xb9\x58\xd3\x3f"
                                           "\x81\x63"
                                           "\x40"
                                           "\x88\xb8\x3b\x53\x39\xec\x32\x7d\x7f"
                                           "\x8c\xf1\xe3\xc2\xe5\xf2\x3a\x6b\xa0\xab\x90\xf4\xff",
                                53}};


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

  // FIXME Current encoder support only huffman conding.
  for (unsigned int i = 2; i < sizeof(string_test_case) / sizeof(string_test_case[0]); i++) {
    memset(buf, 0, BUFSIZE_FOR_REGRESSION_TEST);

    len = encode_string(buf, buf + BUFSIZE_FOR_REGRESSION_TEST, string_test_case[i].raw_string, string_test_case[i].raw_string_len);

    box.check(len == string_test_case[i].encoded_field_len, "encoded length was %d, expecting %d", len,
              string_test_case[i].encoded_field_len);
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

  for (unsigned int i = 9; i < sizeof(literal_test_case) / sizeof(literal_test_case[0]); i++) {
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
  for (unsigned int i = 1; i < sizeof(encoded_field_test_case) / sizeof(encoded_field_test_case[0]); i++) {
    ats_scoped_obj<HTTPHdr> headers(new HTTPHdr);
    headers->create(HTTP_TYPE_REQUEST);

    for (unsigned int j = 0; j < sizeof(raw_field_test_case[i]) / sizeof(raw_field_test_case[i][0]); j++) {
      const char *expected_name = raw_field_test_case[i][j].raw_name;
      const char *expected_value = raw_field_test_case[i][j].raw_value;
      if (strlen(expected_name) == 0)
        break;

      MIMEField *field = mime_field_create(headers->m_heap, headers->m_http->m_fields_impl);
      mime_field_name_value_set(headers->m_heap, headers->m_http->m_fields_impl, field, -1, expected_name, strlen(expected_name),
                                expected_value, strlen(expected_value), 1, strlen(expected_name) + strlen(expected_value), true);
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

    http2_decode_header_blocks(headers, encoded_field_test_case[i].encoded_field,
                               encoded_field_test_case[i].encoded_field + encoded_field_test_case[i].encoded_field_len,
                               dynamic_table);

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

void
forceLinkRegressionHPACK()
{
  // NOTE: Do Nothing
}

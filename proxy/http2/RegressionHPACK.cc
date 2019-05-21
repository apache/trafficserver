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

#include "HPACK.h"
#include "HuffmanCodec.h"
#include "tscore/TestBox.h"

// Constants for regression test
const static int DYNAMIC_TABLE_SIZE_FOR_REGRESSION_TEST = 256;
const static int BUFSIZE_FOR_REGRESSION_TEST            = 128;
const static int MAX_TEST_FIELD_NUM                     = 8;
const static int MAX_REQUEST_HEADER_SIZE                = 131072;
const static int MAX_TABLE_SIZE                         = 4096;

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
  const uint8_t *encoded_field;
  int encoded_field_len;
  int prefix;
} integer_test_case[] = {{10, reinterpret_cast<const uint8_t *>("\x0A"), 1, 5},
                         {1337, reinterpret_cast<const uint8_t *>("\x1F\x9A\x0A"), 3, 5},
                         {42, reinterpret_cast<const uint8_t *>(R"(*)"), 1, 8}};

// Example: custom-key: custom-header
const static struct {
  char *raw_string;
  uint32_t raw_string_len;
  const uint8_t *encoded_field;
  int encoded_field_len;
} string_test_case[] = {{(char *)"", 0,
                         reinterpret_cast<const uint8_t *>("\x0"
                                                           ""),
                         1},
                        {(char *)"custom-key", 10,
                         reinterpret_cast<const uint8_t *>("\xA"
                                                           "custom-key"),
                         11},
                        {(char *)"", 0,
                         reinterpret_cast<const uint8_t *>("\x80"
                                                           ""),
                         1},
                        {(char *)"custom-key", 10,
                         reinterpret_cast<const uint8_t *>("\x88"
                                                           "\x25\xa8\x49\xe9\x5b\xa9\x7d\x7f"),
                         9}};

// [RFC 7541] C.2.4. Indexed Header Field
const static struct {
  int index;
  char *raw_name;
  char *raw_value;
  const uint8_t *encoded_field;
  int encoded_field_len;
} indexed_test_case[] = {{2, (char *)":method", (char *)"GET", reinterpret_cast<const uint8_t *>("\x82"), 1}};

// [RFC 7541] C.2. Header Field Representation Examples
const static struct {
  char *raw_name;
  char *raw_value;
  int index;
  HpackField type;
  const uint8_t *encoded_field;
  int encoded_field_len;
} literal_test_case[] = {{(char *)"custom-key", (char *)"custom-header", 0, HpackField::INDEXED_LITERAL,
                          reinterpret_cast<const uint8_t *>("\x40\x0a"
                                                            "custom-key\x0d"
                                                            "custom-header"),
                          26},
                         {(char *)"custom-key", (char *)"custom-header", 0, HpackField::NOINDEX_LITERAL,
                          reinterpret_cast<const uint8_t *>("\x00\x0a"
                                                            "custom-key\x0d"
                                                            "custom-header"),
                          26},
                         {(char *)"custom-key", (char *)"custom-header", 0, HpackField::NEVERINDEX_LITERAL,
                          reinterpret_cast<const uint8_t *>("\x10\x0a"
                                                            "custom-key\x0d"
                                                            "custom-header"),
                          26},
                         {(char *)":path", (char *)"/sample/path", 4, HpackField::INDEXED_LITERAL,
                          reinterpret_cast<const uint8_t *>("\x44\x0c"
                                                            "/sample/path"),
                          14},
                         {(char *)":path", (char *)"/sample/path", 4, HpackField::NOINDEX_LITERAL,
                          reinterpret_cast<const uint8_t *>("\x04\x0c"
                                                            "/sample/path"),
                          14},
                         {(char *)":path", (char *)"/sample/path", 4, HpackField::NEVERINDEX_LITERAL,
                          reinterpret_cast<const uint8_t *>("\x14\x0c"
                                                            "/sample/path"),
                          14},
                         {(char *)"password", (char *)"secret", 0, HpackField::INDEXED_LITERAL,
                          reinterpret_cast<const uint8_t *>("\x40\x08"
                                                            "password\x06"
                                                            "secret"),
                          17},
                         {(char *)"password", (char *)"secret", 0, HpackField::NOINDEX_LITERAL,
                          reinterpret_cast<const uint8_t *>("\x00\x08"
                                                            "password\x06"
                                                            "secret"),
                          17},
                         {(char *)"password", (char *)"secret", 0, HpackField::NEVERINDEX_LITERAL,
                          reinterpret_cast<const uint8_t *>("\x10\x08"
                                                            "password\x06"
                                                            "secret"),
                          17},
                         // with Huffman Coding
                         {(char *)"custom-key", (char *)"custom-header", 0, HpackField::INDEXED_LITERAL,
                          reinterpret_cast<const uint8_t *>("\x40"
                                                            "\x88\x25\xa8\x49\xe9\x5b\xa9\x7d\x7f"
                                                            "\x89\x25\xa8\x49\xe9\x5a\x72\x8e\x42\xd9"),
                          20},
                         {(char *)"custom-key", (char *)"custom-header", 0, HpackField::NOINDEX_LITERAL,
                          reinterpret_cast<const uint8_t *>("\x00"
                                                            "\x88\x25\xa8\x49\xe9\x5b\xa9\x7d\x7f"
                                                            "\x89\x25\xa8\x49\xe9\x5a\x72\x8e\x42\xd9"),
                          20},
                         {(char *)"custom-key", (char *)"custom-header", 0, HpackField::NEVERINDEX_LITERAL,
                          reinterpret_cast<const uint8_t *>("\x10"
                                                            "\x88\x25\xa8\x49\xe9\x5b\xa9\x7d\x7f"
                                                            "\x89\x25\xa8\x49\xe9\x5a\x72\x8e\x42\xd9"),
                          20},
                         {(char *)":path", (char *)"/sample/path", 4, HpackField::INDEXED_LITERAL,
                          reinterpret_cast<const uint8_t *>("\x44"
                                                            "\x89\x61\x03\xa6\xba\x0a\xc5\x63\x4c\xff"),
                          11},
                         {(char *)":path", (char *)"/sample/path", 4, HpackField::NOINDEX_LITERAL,
                          reinterpret_cast<const uint8_t *>("\x04"
                                                            "\x89\x61\x03\xa6\xba\x0a\xc5\x63\x4c\xff"),
                          11},
                         {(char *)":path", (char *)"/sample/path", 4, HpackField::NEVERINDEX_LITERAL,
                          reinterpret_cast<const uint8_t *>("\x14"
                                                            "\x89\x61\x03\xa6\xba\x0a\xc5\x63\x4c\xff"),
                          11},
                         {(char *)"password", (char *)"secret", 0, HpackField::INDEXED_LITERAL,
                          reinterpret_cast<const uint8_t *>("\x40"
                                                            "\x86\xac\x68\x47\x83\xd9\x27"
                                                            "\x84\x41\x49\x61\x53"),
                          13},
                         {(char *)"password", (char *)"secret", 0, HpackField::NOINDEX_LITERAL,
                          reinterpret_cast<const uint8_t *>("\x00"
                                                            "\x86\xac\x68\x47\x83\xd9\x27"
                                                            "\x84\x41\x49\x61\x53"),
                          13},
                         {(char *)"password", (char *)"secret", 0, HpackField::NEVERINDEX_LITERAL,
                          reinterpret_cast<const uint8_t *>("\x10"
                                                            "\x86\xac\x68\x47\x83\xd9\x27"
                                                            "\x84\x41\x49\x61\x53"),
                          13}};

// [RFC 7541] C.3. Request Examples without Huffman Coding - C.3.1. First Request
// [RFC 7541] C.4. Request Examples with Huffman Coding - C.4.1. First Request
const static struct {
  char *raw_name;
  char *raw_value;
} raw_field_request_test_case[][MAX_TEST_FIELD_NUM] = {{
                                                         {(char *)":method", (char *)"GET"},
                                                         {(char *)":scheme", (char *)"http"},
                                                         {(char *)":path", (char *)"/"},
                                                         {(char *)":authority", (char *)"www.example.com"},
                                                         {(char *)"", (char *)""} // End of this test case
                                                       },
                                                       {
                                                         {(char *)":method", (char *)"GET"},
                                                         {(char *)":scheme", (char *)"http"},
                                                         {(char *)":path", (char *)"/"},
                                                         {(char *)":authority", (char *)"www.example.com"},
                                                         {(char *)"", (char *)""} // End of this test case
                                                       }};
const static struct {
  const uint8_t *encoded_field;
  int encoded_field_len;
} encoded_field_request_test_case[] = {{reinterpret_cast<const uint8_t *>("\x40"
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
                                                                          "\xfwww.example.com"),
                                        64},
                                       {reinterpret_cast<const uint8_t *>("\x40"
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
                                                                          "\x8c\xf1\xe3\xc2\xe5\xf2\x3a\x6b\xa0\xab\x90\xf4\xff"),
                                        53}};

// [RFC 7541] C.6. Response Examples with Huffman Coding
const static struct {
  char *raw_name;
  char *raw_value;
} raw_field_response_test_case[][MAX_TEST_FIELD_NUM] = {
  {
    {(char *)":status", (char *)"302"},
    {(char *)"cache-control", (char *)"private"},
    {(char *)"date", (char *)"Mon, 21 Oct 2013 20:13:21 GMT"},
    {(char *)"location", (char *)"https://www.example.com"},
    {(char *)"", (char *)""} // End of this test case
  },
  {
    {(char *)":status", (char *)"307"},
    {(char *)"cache-control", (char *)"private"},
    {(char *)"date", (char *)"Mon, 21 Oct 2013 20:13:21 GMT"},
    {(char *)"location", (char *)"https://www.example.com"},
    {(char *)"", (char *)""} // End of this test case
  },
  {
    {(char *)":status", (char *)"200"},
    {(char *)"cache-control", (char *)"private"},
    {(char *)"date", (char *)"Mon, 21 Oct 2013 20:13:22 GMT"},
    {(char *)"location", (char *)"https://www.example.com"},
    {(char *)"content-encoding", (char *)"gzip"},
    {(char *)"set-cookie", (char *)"foo=ASDJKHQKBZXOQWEOPIUAXQWEOIU; max-age=3600; version=1"},
    {(char *)"", (char *)""} // End of this test case
  }};
const static struct {
  const uint8_t *encoded_field;
  int encoded_field_len;
} encoded_field_response_test_case[] = {
  {reinterpret_cast<const uint8_t *>("\x48\x82"
                                     "\x64\x02"
                                     "\x58\x85"
                                     "\xae\xc3\x77\x1a\x4b"
                                     "\x61\x96"
                                     "\xd0\x7a\xbe\x94\x10\x54\xd4\x44\xa8\x20\x05\x95\x04\x0b\x81\x66"
                                     "\xe0\x82\xa6\x2d\x1b\xff"
                                     "\x6e\x91"
                                     "\x9d\x29\xad\x17\x18\x63\xc7\x8f\x0b\x97\xc8\xe9\xae\x82\xae\x43"
                                     "\xd3"),
   54},
  {reinterpret_cast<const uint8_t *>("\x48\x83"
                                     "\x64\x0e\xff"
                                     "\xc1"
                                     "\xc0"
                                     "\xbf"),
   8},
  {reinterpret_cast<const uint8_t *>("\x88"
                                     "\xc1"
                                     "\x61\x96"
                                     "\xd0\x7a\xbe\x94\x10\x54\xd4\x44\xa8\x20\x05\x95\x04\x0b\x81\x66"
                                     "\xe0\x84\xa6\x2d\x1b\xff"
                                     "\xc0"
                                     "\x5a\x83"
                                     "\x9b\xd9\xab"
                                     "\x77\xad"
                                     "\x94\xe7\x82\x1d\xd7\xf2\xe6\xc7\xb3\x35\xdf\xdf\xcd\x5b\x39\x60"
                                     "\xd5\xaf\x27\x08\x7f\x36\x72\xc1\xab\x27\x0f\xb5\x29\x1f\x95\x87"
                                     "\x31\x60\x65\xc0\x03\xed\x4e\xe5\xb1\x06\x3d\x50\x07"),
   79}};
const static struct {
  uint32_t size;
  char *name;
  char *value;
} dynamic_table_response_test_case[][MAX_TEST_FIELD_NUM] = {
  {
    {63, (char *)"location", (char *)"https://www.example.com"},
    {65, (char *)"date", (char *)"Mon, 21 Oct 2013 20:13:21 GMT"},
    {52, (char *)"cache-control", (char *)"private"},
    {42, (char *)":status", (char *)"302"},
    {0, (char *)"", (char *)""} // End of this test case
  },
  {
    {42, (char *)":status", (char *)"307"},
    {63, (char *)"location", (char *)"https://www.example.com"},
    {65, (char *)"date", (char *)"Mon, 21 Oct 2013 20:13:21 GMT"},
    {52, (char *)"cache-control", (char *)"private"},
    {0, (char *)"", (char *)""} // End of this test case
  },
  {
    {98, (char *)"set-cookie", (char *)"foo=ASDJKHQKBZXOQWEOPIUAXQWEOIU; max-age=3600; version=1"},
    {52, (char *)"content-encoding", (char *)"gzip"},
    {65, (char *)"date", (char *)"Mon, 21 Oct 2013 20:13:22 GMT"},
    {0, (char *)"", (char *)""} // End of this test case
  }};

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

  for (const auto &i : integer_test_case) {
    memset(buf, 0, BUFSIZE_FOR_REGRESSION_TEST);

    int len = encode_integer(buf, buf + BUFSIZE_FOR_REGRESSION_TEST, i.raw_integer, i.prefix);

    box.check(len == i.encoded_field_len, "encoded length was %d, expecting %d", len, i.encoded_field_len);
    box.check(len > 0 && memcmp(buf, i.encoded_field, len) == 0, "encoded value was invalid");
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

  for (const auto &i : indexed_test_case) {
    memset(buf, 0, BUFSIZE_FOR_REGRESSION_TEST);

    int len = encode_indexed_header_field(buf, buf + BUFSIZE_FOR_REGRESSION_TEST, i.index);

    box.check(len == i.encoded_field_len, "encoded length was %d, expecting %d", len, i.encoded_field_len);
    box.check(len > 0 && memcmp(buf, i.encoded_field, len) == 0, "encoded value was invalid");
  }
}

REGRESSION_TEST(HPACK_EncodeLiteralHeaderField)(RegressionTest *t, int, int *pstatus)
{
  TestBox box(t, pstatus);
  box = REGRESSION_TEST_PASSED;

  uint8_t buf[BUFSIZE_FOR_REGRESSION_TEST];
  int len;
  HpackIndexingTable indexing_table(4096);

  for (unsigned int i = 9; i < sizeof(literal_test_case) / sizeof(literal_test_case[0]); i++) {
    memset(buf, 0, BUFSIZE_FOR_REGRESSION_TEST);

    ats_scoped_obj<HTTPHdr> headers(new HTTPHdr);
    headers->create(HTTP_TYPE_RESPONSE);
    MIMEField *field = mime_field_create(headers->m_heap, headers->m_http->m_fields_impl);
    MIMEFieldWrapper header(field, headers->m_heap, headers->m_http->m_fields_impl);

    header.name_set(literal_test_case[i].raw_name, strlen(literal_test_case[i].raw_name));
    header.value_set(literal_test_case[i].raw_value, strlen(literal_test_case[i].raw_value));
    if (literal_test_case[i].index > 0) {
      len = encode_literal_header_field_with_indexed_name(buf, buf + BUFSIZE_FOR_REGRESSION_TEST, header,
                                                          literal_test_case[i].index, indexing_table, literal_test_case[i].type);
    } else {
      header.name_set(literal_test_case[i].raw_name, strlen(literal_test_case[i].raw_name));
      len = encode_literal_header_field_with_new_name(buf, buf + BUFSIZE_FOR_REGRESSION_TEST, header, indexing_table,
                                                      literal_test_case[i].type);
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
  HpackIndexingTable indexing_table(4096);
  indexing_table.update_maximum_size(DYNAMIC_TABLE_SIZE_FOR_REGRESSION_TEST);

  for (unsigned int i = 0; i < sizeof(encoded_field_response_test_case) / sizeof(encoded_field_response_test_case[0]); i++) {
    ats_scoped_obj<HTTPHdr> headers(new HTTPHdr);
    headers->create(HTTP_TYPE_RESPONSE);

    for (unsigned int j = 0; j < sizeof(raw_field_response_test_case[i]) / sizeof(raw_field_response_test_case[i][0]); j++) {
      const char *expected_name  = raw_field_response_test_case[i][j].raw_name;
      const char *expected_value = raw_field_response_test_case[i][j].raw_value;
      if (strlen(expected_name) == 0) {
        break;
      }

      MIMEField *field = mime_field_create(headers->m_heap, headers->m_http->m_fields_impl);
      field->name_set(headers->m_heap, headers->m_http->m_fields_impl, expected_name, strlen(expected_name));
      field->value_set(headers->m_heap, headers->m_http->m_fields_impl, expected_value, strlen(expected_value));
      mime_hdr_field_attach(headers->m_http->m_fields_impl, field, 1, nullptr);
    }

    memset(buf, 0, BUFSIZE_FOR_REGRESSION_TEST);
    uint64_t buf_len = BUFSIZE_FOR_REGRESSION_TEST;
    int64_t len      = hpack_encode_header_block(indexing_table, buf, buf_len, headers);

    if (len < 0) {
      box.check(false, "hpack_encode_header_blocks returned negative value: %" PRId64, len);
      break;
    }

    box.check(len == encoded_field_response_test_case[i].encoded_field_len, "encoded length was %" PRId64 ", expecting %d", len,
              encoded_field_response_test_case[i].encoded_field_len);
    box.check(len > 0 && memcmp(buf, encoded_field_response_test_case[i].encoded_field, len) == 0, "encoded value was invalid");

    // Check dynamic table
    uint32_t expected_dynamic_table_size = 0;
    for (unsigned int j = 0; j < sizeof(dynamic_table_response_test_case[i]) / sizeof(dynamic_table_response_test_case[i][0]);
         j++) {
      const char *expected_name  = dynamic_table_response_test_case[i][j].name;
      const char *expected_value = dynamic_table_response_test_case[i][j].value;
      int expected_name_len      = strlen(expected_name);
      int expected_value_len     = strlen(expected_value);

      if (expected_name_len == 0) {
        break;
      }

      HpackLookupResult lookupResult = indexing_table.lookup(expected_name, expected_name_len, expected_value, expected_value_len);
      box.check(lookupResult.match_type == HpackMatch::EXACT && lookupResult.index_type == HpackIndex::DYNAMIC,
                "the header field is not indexed");

      expected_dynamic_table_size += dynamic_table_response_test_case[i][j].size;
    }
    box.check(indexing_table.size() == expected_dynamic_table_size, "dynamic table is unexpected size: %d", indexing_table.size());
  }
}

REGRESSION_TEST(HPACK_DecodeInteger)(RegressionTest *t, int, int *pstatus)
{
  TestBox box(t, pstatus);
  box = REGRESSION_TEST_PASSED;

  uint32_t actual;

  for (const auto &i : integer_test_case) {
    int len = decode_integer(actual, i.encoded_field, i.encoded_field + i.encoded_field_len, i.prefix);

    box.check(len == i.encoded_field_len, "decoded length was %d, expecting %d", len, i.encoded_field_len);
    box.check(actual == i.raw_integer, "decoded value was %d, expected %d", actual, i.raw_integer);
  }
}

REGRESSION_TEST(HPACK_DecodeString)(RegressionTest *t, int, int *pstatus)
{
  TestBox box(t, pstatus);
  box = REGRESSION_TEST_PASSED;

  Arena arena;
  char *actual        = nullptr;
  uint32_t actual_len = 0;

  hpack_huffman_init();

  for (const auto &i : string_test_case) {
    int len = decode_string(arena, &actual, actual_len, i.encoded_field, i.encoded_field + i.encoded_field_len);

    box.check(len == i.encoded_field_len, "decoded length was %d, expecting %d", len, i.encoded_field_len);
    box.check(actual_len == i.raw_string_len, "length of decoded string was %d, expecting %d", actual_len, i.raw_string_len);
    box.check(memcmp(actual, i.raw_string, actual_len) == 0, "decoded string was invalid");
  }
}

REGRESSION_TEST(HPACK_DecodeIndexedHeaderField)(RegressionTest *t, int, int *pstatus)
{
  TestBox box(t, pstatus);
  box = REGRESSION_TEST_PASSED;

  HpackIndexingTable indexing_table(4096);

  for (const auto &i : indexed_test_case) {
    ats_scoped_obj<HTTPHdr> headers(new HTTPHdr);
    headers->create(HTTP_TYPE_REQUEST);
    MIMEField *field = mime_field_create(headers->m_heap, headers->m_http->m_fields_impl);
    MIMEFieldWrapper header(field, headers->m_heap, headers->m_http->m_fields_impl);

    int len = decode_indexed_header_field(header, i.encoded_field, i.encoded_field + i.encoded_field_len, indexing_table);

    box.check(len == i.encoded_field_len, "decoded length was %d, expecting %d", len, i.encoded_field_len);

    int name_len;
    const char *name = header.name_get(&name_len);
    box.check(len > 0 && memcmp(name, i.raw_name, name_len) == 0, "decoded header name was invalid");

    int actual_value_len;
    const char *actual_value = header.value_get(&actual_value_len);
    box.check(memcmp(actual_value, i.raw_value, actual_value_len) == 0, "decoded header value was invalid");
  }
}

REGRESSION_TEST(HPACK_DecodeLiteralHeaderField)(RegressionTest *t, int, int *pstatus)
{
  TestBox box(t, pstatus);
  box = REGRESSION_TEST_PASSED;

  HpackIndexingTable indexing_table(4096);

  for (const auto &i : literal_test_case) {
    ats_scoped_obj<HTTPHdr> headers(new HTTPHdr);
    headers->create(HTTP_TYPE_REQUEST);
    MIMEField *field = mime_field_create(headers->m_heap, headers->m_http->m_fields_impl);
    MIMEFieldWrapper header(field, headers->m_heap, headers->m_http->m_fields_impl);

    int len = decode_literal_header_field(header, i.encoded_field, i.encoded_field + i.encoded_field_len, indexing_table);

    box.check(len == i.encoded_field_len, "decoded length was %d, expecting %d", len, i.encoded_field_len);

    int name_len;
    const char *name = header.name_get(&name_len);
    box.check(name_len > 0 && memcmp(name, i.raw_name, name_len) == 0, "decoded header name was invalid");

    int actual_value_len;
    const char *actual_value = header.value_get(&actual_value_len);
    box.check(actual_value_len > 0 && memcmp(actual_value, i.raw_value, actual_value_len) == 0, "decoded header value was invalid");
  }
}

REGRESSION_TEST(HPACK_Decode)(RegressionTest *t, int, int *pstatus)
{
  TestBox box(t, pstatus);
  box = REGRESSION_TEST_PASSED;

  HpackIndexingTable indexing_table(4096);

  for (unsigned int i = 0; i < sizeof(encoded_field_request_test_case) / sizeof(encoded_field_request_test_case[0]); i++) {
    ats_scoped_obj<HTTPHdr> headers(new HTTPHdr);
    headers->create(HTTP_TYPE_REQUEST);

    hpack_decode_header_block(indexing_table, headers, encoded_field_request_test_case[i].encoded_field,
                              encoded_field_request_test_case[i].encoded_field_len, MAX_REQUEST_HEADER_SIZE, MAX_TABLE_SIZE);

    for (unsigned int j = 0; j < sizeof(raw_field_request_test_case[i]) / sizeof(raw_field_request_test_case[i][0]); j++) {
      const char *expected_name  = raw_field_request_test_case[i][j].raw_name;
      const char *expected_value = raw_field_request_test_case[i][j].raw_value;
      if (strlen(expected_name) == 0) {
        break;
      }

      MIMEField *field = headers->field_find(expected_name, strlen(expected_name));
      box.check(field != nullptr, "A MIMEField that has \"%s\" as name doesn't exist", expected_name);

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

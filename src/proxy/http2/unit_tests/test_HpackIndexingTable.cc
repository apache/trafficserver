/** @file

    Catch-based unit tests for HPACK

    Some test cases are based on examples of specification.
    - https://tools.ietf.org/html/rfc7541#appendix-C

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

#include <memory>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "proxy/http2/HPACK.h"
#include "proxy/hdrs/XPACK.h"

static constexpr int      DYNAMIC_TABLE_SIZE_FOR_REGRESSION_TEST = 256;
static constexpr int      BUFSIZE_FOR_REGRESSION_TEST            = 128;
static constexpr int      MAX_TEST_FIELD_NUM                     = 8;
static constexpr int      MAX_REQUEST_HEADER_SIZE                = 131072;
static constexpr int      MAX_TABLE_SIZE                         = 4096;
static constexpr int      MAX_FIELD_SIZE                         = 32768;
static constexpr uint64_t OVERSIZED_HPACK_INDEX                  = (static_cast<uint64_t>(1) << 32) + 58;

namespace
{
/**
  When HTTHdr::create is called, HTTPHdr::destroy needs to be called to free HdrHeap.
  When Issue #10541 is fixed, we don't need this helper.
*/
void
destroy_http_hdr(HTTPHdr *hdr)
{
  hdr->destroy();
  delete hdr;
}

int64_t
encode_oversized_hpack_index(uint8_t *buf, size_t buf_len, uint8_t prefix, uint8_t flag)
{
  memset(buf, 0, buf_len);
  buf[0] = flag;

  int64_t len = xpack_encode_integer(buf, buf + buf_len, OVERSIZED_HPACK_INDEX, prefix);
  REQUIRE(len > 0);

  return len;
}
} // namespace

TEST_CASE("HPACK low level APIs", "[hpack]")
{
  SECTION("indexed_header_field")
  {
    // [RFC 7541] C.2.4. Indexed Header Field
    const static struct {
      int      index;
      char    *raw_name;
      char    *raw_value;
      uint8_t *encoded_field;
      int      encoded_field_len;
    } indexed_test_case[] = {
      {2, (char *)":method", (char *)"GET", (uint8_t *)"\x82", 1},
    };

    SECTION("encoding")
    {
      uint8_t buf[BUFSIZE_FOR_REGRESSION_TEST];

      for (const auto &i : indexed_test_case) {
        memset(buf, 0, BUFSIZE_FOR_REGRESSION_TEST);

        int64_t len = encode_indexed_header_field(buf, buf + BUFSIZE_FOR_REGRESSION_TEST, i.index);

        REQUIRE(len > 0);
        REQUIRE(len == i.encoded_field_len);
        REQUIRE(memcmp(buf, i.encoded_field, len) == 0);
      }
    }

    SECTION("decoding")
    {
      HpackIndexingTable indexing_table(4096);

      for (const auto &i : indexed_test_case) {
        std::unique_ptr<HTTPHdr, void (*)(HTTPHdr *)> headers(new HTTPHdr, destroy_http_hdr);
        headers->create(HTTPType::REQUEST);
        MIMEField       *field = mime_field_create(headers->m_heap, headers->m_http->m_fields_impl);
        MIMEFieldWrapper header(field, headers->m_heap, headers->m_http->m_fields_impl);

        int len = decode_indexed_header_field(header, i.encoded_field, i.encoded_field + i.encoded_field_len, indexing_table);
        REQUIRE(len == i.encoded_field_len);

        auto name{header.name_get()};
        REQUIRE(name.length() > 0);
        REQUIRE(name == std::string_view{i.raw_name});

        auto actual_value{header.value_get()};
        REQUIRE(actual_value.length() > 0);
        REQUIRE(actual_value == std::string_view{i.raw_value});
      }
    }

    SECTION("rejects oversized indexed header field index")
    {
      uint8_t buf[BUFSIZE_FOR_REGRESSION_TEST];
      int64_t encoded_len = encode_oversized_hpack_index(buf, sizeof(buf), 7, 0x80);

      HpackIndexingTable                            indexing_table(4096);
      std::unique_ptr<HTTPHdr, void (*)(HTTPHdr *)> headers(new HTTPHdr, destroy_http_hdr);
      headers->create(HTTPType::REQUEST);
      MIMEField       *field = mime_field_create(headers->m_heap, headers->m_http->m_fields_impl);
      MIMEFieldWrapper header(field, headers->m_heap, headers->m_http->m_fields_impl);

      int64_t len = decode_indexed_header_field(header, buf, buf + encoded_len, indexing_table);

      REQUIRE(len == HPACK_ERROR_COMPRESSION_ERROR);
    }
  }

  SECTION("literal_header_field")
  {
    // [RFC 7541] C.2. Header Field Representation Examples
    const static struct {
      char      *raw_name;
      char      *raw_value;
      int        index;
      HpackField type;
      uint8_t   *encoded_field;
      int        encoded_field_len;
    } literal_test_case[] = {
      {(char *)"custom-key", (char *)"custom-header", 0, HpackField::INDEXED_LITERAL,
       (uint8_t *)"\x40\x0a"
                  "custom-key\x0d"
                  "custom-header",                            26},
      {(char *)"custom-key", (char *)"custom-header", 0, HpackField::NOINDEX_LITERAL,
       (uint8_t *)"\x00\x0a"
                  "custom-key\x0d"
                  "custom-header",                            26},
      {(char *)"custom-key", (char *)"custom-header", 0, HpackField::NEVERINDEX_LITERAL,
       (uint8_t *)"\x10\x0a"
                  "custom-key\x0d"
                  "custom-header",                            26},
      {(char *)":path",      (char *)"/sample/path",  4, HpackField::INDEXED_LITERAL,
       (uint8_t *)"\x44\x0c"
                  "/sample/path",                             14},
      {(char *)":path",      (char *)"/sample/path",  4, HpackField::NOINDEX_LITERAL,
       (uint8_t *)"\x04\x0c"
                  "/sample/path",                             14},
      {(char *)":path",      (char *)"/sample/path",  4, HpackField::NEVERINDEX_LITERAL,
       (uint8_t *)"\x14\x0c"
                  "/sample/path",                             14},
      {(char *)"password",   (char *)"secret",        0, HpackField::INDEXED_LITERAL,
       (uint8_t *)"\x40\x08"
                  "password\x06"
                  "secret",                                   17},
      {(char *)"password",   (char *)"secret",        0, HpackField::NOINDEX_LITERAL,
       (uint8_t *)"\x00\x08"
                  "password\x06"
                  "secret",                                   17},
      {(char *)"password",   (char *)"secret",        0, HpackField::NEVERINDEX_LITERAL,
       (uint8_t *)"\x10\x08"
                  "password\x06"
                  "secret",                                   17},
      // with Huffman Coding
      {(char *)"custom-key", (char *)"custom-header", 0, HpackField::INDEXED_LITERAL,
       (uint8_t *)"\x40"
                  "\x88\x25\xa8\x49\xe9\x5b\xa9\x7d\x7f"
                  "\x89\x25\xa8\x49\xe9\x5a\x72\x8e\x42\xd9", 20},
      {(char *)"custom-key", (char *)"custom-header", 0, HpackField::NOINDEX_LITERAL,
       (uint8_t *)"\x00"
                  "\x88\x25\xa8\x49\xe9\x5b\xa9\x7d\x7f"
                  "\x89\x25\xa8\x49\xe9\x5a\x72\x8e\x42\xd9", 20},
      {(char *)"custom-key", (char *)"custom-header", 0, HpackField::NEVERINDEX_LITERAL,
       (uint8_t *)"\x10"
                  "\x88\x25\xa8\x49\xe9\x5b\xa9\x7d\x7f"
                  "\x89\x25\xa8\x49\xe9\x5a\x72\x8e\x42\xd9", 20},
      {(char *)":path",      (char *)"/sample/path",  4, HpackField::INDEXED_LITERAL,
       (uint8_t *)"\x44"
                  "\x89\x61\x03\xa6\xba\x0a\xc5\x63\x4c\xff", 11},
      {(char *)":path",      (char *)"/sample/path",  4, HpackField::NOINDEX_LITERAL,
       (uint8_t *)"\x04"
                  "\x89\x61\x03\xa6\xba\x0a\xc5\x63\x4c\xff", 11},
      {(char *)":path",      (char *)"/sample/path",  4, HpackField::NEVERINDEX_LITERAL,
       (uint8_t *)"\x14"
                  "\x89\x61\x03\xa6\xba\x0a\xc5\x63\x4c\xff", 11},
      {(char *)"password",   (char *)"secret",        0, HpackField::INDEXED_LITERAL,
       (uint8_t *)"\x40"
                  "\x86\xac\x68\x47\x83\xd9\x27"
                  "\x84\x41\x49\x61\x53",                     13},
      {(char *)"password",   (char *)"secret",        0, HpackField::NOINDEX_LITERAL,
       (uint8_t *)"\x00"
                  "\x86\xac\x68\x47\x83\xd9\x27"
                  "\x84\x41\x49\x61\x53",                     13},
      {(char *)"password",   (char *)"secret",        0, HpackField::NEVERINDEX_LITERAL,
       (uint8_t *)"\x10"
                  "\x86\xac\x68\x47\x83\xd9\x27"
                  "\x84\x41\x49\x61\x53",                     13},
    };

    SECTION("encoding")
    {
      {
        uint8_t            buf[BUFSIZE_FOR_REGRESSION_TEST];
        int64_t            len;
        HpackIndexingTable indexing_table(4096);

        for (unsigned int i = 9; i < sizeof(literal_test_case) / sizeof(literal_test_case[0]); i++) {
          memset(buf, 0, BUFSIZE_FOR_REGRESSION_TEST);

          HpackHeaderField header{literal_test_case[i].raw_name, literal_test_case[i].raw_value};

          if (literal_test_case[i].index > 0) {
            len =
              encode_literal_header_field_with_indexed_name(buf, buf + BUFSIZE_FOR_REGRESSION_TEST, header,
                                                            literal_test_case[i].index, indexing_table, literal_test_case[i].type);
          } else {
            len = encode_literal_header_field_with_new_name(buf, buf + BUFSIZE_FOR_REGRESSION_TEST, header, indexing_table,
                                                            literal_test_case[i].type);
          }

          REQUIRE(len > 0);
          REQUIRE(len == literal_test_case[i].encoded_field_len);
          // coverity[overrun-buffer-arg] - len is validated positive above
          REQUIRE(memcmp(buf, literal_test_case[i].encoded_field, len) == 0);
        }
      }
    }

    SECTION("decoding")
    {
      {
        HpackIndexingTable indexing_table(4096);

        for (const auto &i : literal_test_case) {
          std::unique_ptr<HTTPHdr, void (*)(HTTPHdr *)> headers(new HTTPHdr, destroy_http_hdr);
          headers->create(HTTPType::REQUEST);
          MIMEField       *field = mime_field_create(headers->m_heap, headers->m_http->m_fields_impl);
          MIMEFieldWrapper header(field, headers->m_heap, headers->m_http->m_fields_impl);

          int len = decode_literal_header_field(header, i.encoded_field, i.encoded_field + i.encoded_field_len, indexing_table,
                                                MAX_FIELD_SIZE);
          REQUIRE(len == i.encoded_field_len);

          auto name{header.name_get()};
          REQUIRE(name.length() > 0);
          REQUIRE(name == std::string_view{i.raw_name});

          auto actual_value{header.value_get()};
          REQUIRE(actual_value.length() > 0);
          REQUIRE(actual_value == std::string_view{i.raw_value});
        }
      }
    }

    SECTION("rejects oversized literal header field name index")
    {
      const static struct {
        uint8_t prefix;
        uint8_t flag;
      } oversized_literal_index_cases[] = {
        {6, 0x40}, // INDEXED_LITERAL
        {4, 0x00}, // NOINDEX_LITERAL
        {4, 0x10}, // NEVERINDEX_LITERAL
      };

      for (const auto &i : oversized_literal_index_cases) {
        uint8_t buf[BUFSIZE_FOR_REGRESSION_TEST];
        int64_t encoded_len = encode_oversized_hpack_index(buf, sizeof(buf), i.prefix, i.flag);
        uint8_t value[]     = {0x05, 'v', 'a', 'l', 'u', 'e'};
        memcpy(buf + encoded_len, value, sizeof(value));
        encoded_len += sizeof(value);

        HpackIndexingTable                            indexing_table(4096);
        std::unique_ptr<HTTPHdr, void (*)(HTTPHdr *)> headers(new HTTPHdr, destroy_http_hdr);
        headers->create(HTTPType::REQUEST);
        MIMEField       *field = mime_field_create(headers->m_heap, headers->m_http->m_fields_impl);
        MIMEFieldWrapper header(field, headers->m_heap, headers->m_http->m_fields_impl);

        int64_t len = decode_literal_header_field(header, buf, buf + encoded_len, indexing_table, MAX_FIELD_SIZE);

        REQUIRE(len == HPACK_ERROR_COMPRESSION_ERROR);
      }
    }

    SECTION("dynamic table is not mutated when encoding fails")
    {
      HpackHeaderField const header{"custom-key", "custom-header"};
      uint8_t                buf[1];

      // A zero-length output buffer guarantees the first xpack_encode_*
      // call fails.  The dynamic table must remain untouched.
      {
        HpackIndexingTable indexing_table(4096);
        uint32_t const     baseline_size = indexing_table.size();

        int64_t len =
          encode_literal_header_field_with_indexed_name(buf, buf, header, 4, indexing_table, HpackField::INDEXED_LITERAL);
        REQUIRE(len == HPACK_ERROR_COMPRESSION_ERROR);
        REQUIRE(indexing_table.size() == baseline_size);
      }
      {
        HpackIndexingTable indexing_table(4096);
        uint32_t const     baseline_size = indexing_table.size();

        int64_t len = encode_literal_header_field_with_new_name(buf, buf, header, indexing_table, HpackField::INDEXED_LITERAL);
        REQUIRE(len == HPACK_ERROR_COMPRESSION_ERROR);
        REQUIRE(indexing_table.size() == baseline_size);
      }

      // The size-update branch in hpack_encode_header_block must likewise
      // leave maximum_size untouched on encode failure.
      {
        std::unique_ptr<HTTPHdr, void (*)(HTTPHdr *)> headers(new HTTPHdr, destroy_http_hdr);
        headers->create(HTTPType::REQUEST);

        HpackIndexingTable indexing_table(4096);
        uint32_t const     baseline_max = indexing_table.maximum_size();

        int64_t len = hpack_encode_header_block(indexing_table, buf, 0, headers.get(), 256);
        REQUIRE(len == HPACK_ERROR_COMPRESSION_ERROR);
        REQUIRE(indexing_table.maximum_size() == baseline_max);
      }
    }
  }
}

TEST_CASE("HPACK high level APIs", "[hpack]")
{
  SECTION("encoding")
  {
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
      },
    };

    const static struct {
      uint8_t *encoded_field;
      int      encoded_field_len;
    } encoded_field_response_test_case[] = {
      {(uint8_t *)"\x48\x82"
                  "\x64\x02"
                  "\x58\x85"
                  "\xae\xc3\x77\x1a\x4b"
                  "\x61\x96"
                  "\xd0\x7a\xbe\x94\x10\x54\xd4\x44\xa8\x20\x05\x95\x04\x0b\x81\x66"
                  "\xe0\x82\xa6\x2d\x1b\xff"
                  "\x6e\x91"
                  "\x9d\x29\xad\x17\x18\x63\xc7\x8f\x0b\x97\xc8\xe9\xae\x82\xae\x43"
                  "\xd3",                                                 54},
      {(uint8_t *)"\x48\x83"
                  "\x64\x0e\xff"
                  "\xc1"
                  "\xc0"
                  "\xbf",                                                 8 },
      {(uint8_t *)"\x88"
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
                  "\x31\x60\x65\xc0\x03\xed\x4e\xe5\xb1\x06\x3d\x50\x07", 79},
    };

    const static struct {
      uint32_t size;
      char    *name;
      char    *value;
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
      },
    };

    uint8_t            buf[BUFSIZE_FOR_REGRESSION_TEST];
    HpackIndexingTable indexing_table(4096);
    indexing_table.update_maximum_size(DYNAMIC_TABLE_SIZE_FOR_REGRESSION_TEST);

    for (unsigned int i = 0; i < sizeof(encoded_field_response_test_case) / sizeof(encoded_field_response_test_case[0]); i++) {
      std::unique_ptr<HTTPHdr, void (*)(HTTPHdr *)> headers(new HTTPHdr, destroy_http_hdr);
      headers->create(HTTPType::RESPONSE);

      for (unsigned int j = 0; j < sizeof(raw_field_response_test_case[i]) / sizeof(raw_field_response_test_case[i][0]); j++) {
        const char *expected_name  = raw_field_response_test_case[i][j].raw_name;
        const char *expected_value = raw_field_response_test_case[i][j].raw_value;
        if (strlen(expected_name) == 0) {
          break;
        }

        MIMEField *field = mime_field_create(headers->m_heap, headers->m_http->m_fields_impl);
        field->name_set(headers->m_heap, headers->m_http->m_fields_impl, std::string_view{expected_name});
        field->value_set(headers->m_heap, headers->m_http->m_fields_impl, std::string_view{expected_value});
        mime_hdr_field_attach(headers->m_http->m_fields_impl, field, 1, nullptr);
      }

      memset(buf, 0, BUFSIZE_FOR_REGRESSION_TEST);
      uint64_t buf_len = BUFSIZE_FOR_REGRESSION_TEST;
      int64_t  len     = hpack_encode_header_block(indexing_table, buf, buf_len, headers.get());

      REQUIRE(len > 0);
      REQUIRE(len == encoded_field_response_test_case[i].encoded_field_len);
      REQUIRE(memcmp(buf, encoded_field_response_test_case[i].encoded_field, len) == 0);

      // Check dynamic table
      uint32_t expected_dynamic_table_size = 0;
      for (unsigned int j = 0; j < sizeof(dynamic_table_response_test_case[i]) / sizeof(dynamic_table_response_test_case[i][0]);
           j++) {
        const char *expected_name  = dynamic_table_response_test_case[i][j].name;
        const char *expected_value = dynamic_table_response_test_case[i][j].value;

        if (strlen(expected_name) == 0) {
          break;
        }

        HpackHeaderField  expected_header{expected_name, expected_value};
        HpackLookupResult lookupResult = indexing_table.lookup(expected_header);

        CHECK(lookupResult.match_type == HpackMatch::EXACT);
        CHECK(lookupResult.index_type == HpackIndex::DYNAMIC);

        expected_dynamic_table_size += dynamic_table_response_test_case[i][j].size;
      }
      REQUIRE(indexing_table.size() == expected_dynamic_table_size);
    }
  }

  SECTION("decoding")
  {
    // [RFC 7541] C.3. Request Examples without Huffman Coding - C.3.1. First Request
    // [RFC 7541] C.4. Request Examples with Huffman Coding - C.4.1. First Request
    const static struct {
      char *raw_name;
      char *raw_value;
    } raw_field_request_test_case[][MAX_TEST_FIELD_NUM] = {
      {
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
      },
    };

    const static struct {
      uint8_t *encoded_field;
      int      encoded_field_len;
    } encoded_field_request_test_case[] = {
      {(uint8_t *)"\x40"
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
                  "\xfwww.example.com",                                   64},
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
                  "\x8c\xf1\xe3\xc2\xe5\xf2\x3a\x6b\xa0\xab\x90\xf4\xff", 53},
    };

    HpackIndexingTable indexing_table(4096);

    for (unsigned int i = 0; i < sizeof(encoded_field_request_test_case) / sizeof(encoded_field_request_test_case[0]); i++) {
      std::unique_ptr<HTTPHdr, void (*)(HTTPHdr *)> headers(new HTTPHdr, destroy_http_hdr);
      headers->create(HTTPType::REQUEST);

      hpack_decode_header_block(indexing_table, headers.get(), encoded_field_request_test_case[i].encoded_field,
                                encoded_field_request_test_case[i].encoded_field_len, MAX_REQUEST_HEADER_SIZE, MAX_TABLE_SIZE,
                                MAX_FIELD_SIZE);

      for (unsigned int j = 0; j < sizeof(raw_field_request_test_case[i]) / sizeof(raw_field_request_test_case[i][0]); j++) {
        const char *expected_name  = raw_field_request_test_case[i][j].raw_name;
        const char *expected_value = raw_field_request_test_case[i][j].raw_value;
        if (strlen(expected_name) == 0) {
          break;
        }

        MIMEField *field = headers->field_find(std::string_view{expected_name});
        CHECK(field != nullptr);

        if (field) {
          auto actual_value{field->value_get()};
          CHECK(actual_value == std::string_view{expected_value});
        }
      }
    }
  }

  SECTION("dynamic table size update")
  {
    HpackIndexingTable indexing_table(4096);
    REQUIRE(indexing_table.maximum_size() == 4096);
    REQUIRE(indexing_table.size() == 0);

    // add entries in dynamic table
    {
      std::unique_ptr<HTTPHdr, void (*)(HTTPHdr *)> headers(new HTTPHdr, destroy_http_hdr);
      headers->create(HTTPType::REQUEST);

      // C.3.1.  First Request
      uint8_t data[] = {0x82, 0x86, 0x84, 0x41, 0x0f, 0x77, 0x77, 0x77, 0x2e, 0x65,
                        0x78, 0x61, 0x6d, 0x70, 0x6c, 0x65, 0x2e, 0x63, 0x6f, 0x6d};

      int64_t len = hpack_decode_header_block(indexing_table, headers.get(), data, sizeof(data), MAX_REQUEST_HEADER_SIZE,
                                              MAX_TABLE_SIZE, MAX_FIELD_SIZE);
      CHECK(len == sizeof(data));
      CHECK(indexing_table.maximum_size() == 4096);
      CHECK(indexing_table.size() == 57);
    }

    // clear all entries by setting a maximum size of 0
    {
      std::unique_ptr<HTTPHdr, void (*)(HTTPHdr *)> headers(new HTTPHdr, destroy_http_hdr);
      headers->create(HTTPType::REQUEST);

      uint8_t data[] = {0x20};

      int64_t len = hpack_decode_header_block(indexing_table, headers.get(), data, sizeof(data), MAX_REQUEST_HEADER_SIZE,
                                              MAX_TABLE_SIZE, MAX_FIELD_SIZE);
      CHECK(len == sizeof(data));
      CHECK(indexing_table.maximum_size() == 0);
      CHECK(indexing_table.size() == 0);
    }

    // make the maximum size back to 4096
    {
      std::unique_ptr<HTTPHdr, void (*)(HTTPHdr *)> headers(new HTTPHdr, destroy_http_hdr);
      headers->create(HTTPType::REQUEST);

      uint8_t data[] = {0x3f, 0xe1, 0x1f};

      int64_t len = hpack_decode_header_block(indexing_table, headers.get(), data, sizeof(data), MAX_REQUEST_HEADER_SIZE,
                                              MAX_TABLE_SIZE, MAX_FIELD_SIZE);
      CHECK(len == sizeof(data));
      CHECK(indexing_table.maximum_size() == 4096);
      CHECK(indexing_table.size() == 0);
    }

    // error with exceeding the limit (MAX_TABLE_SIZE)
    {
      std::unique_ptr<HTTPHdr, void (*)(HTTPHdr *)> headers(new HTTPHdr, destroy_http_hdr);
      headers->create(HTTPType::REQUEST);

      uint8_t data[] = {0x3f, 0xe2, 0x1f};

      int64_t len = hpack_decode_header_block(indexing_table, headers.get(), data, sizeof(data), MAX_REQUEST_HEADER_SIZE,
                                              MAX_TABLE_SIZE, MAX_FIELD_SIZE);
      CHECK(len == HPACK_ERROR_COMPRESSION_ERROR);
    }
  }
}

namespace
{
// Build a single literal-with-incremental-indexing header field (RFC 7541 6.2.1)
// with a non-Huffman, new (index 0) name and value. The name and value lengths
// are written using HPACK integer encoding (7-bit prefix, high bit reserved for
// the Huffman flag, which is left clear here).
std::vector<uint8_t>
build_literal_incremental_field(std::string_view name, std::string_view value)
{
  std::vector<uint8_t> block;

  // Pattern '01' in the top two bits selects "Literal Header Field with
  // Incremental Indexing", and a 6-bit index of 0 means a new name follows.
  block.push_back(0x40);

  uint8_t prefix[16];

  // Header field name: length prefix (n = 7) followed by the raw octets.
  prefix[0]                    = 0x00; // clear the Huffman flag in the prefix octet
  const int64_t name_len_bytes = xpack_encode_integer(prefix, prefix + sizeof(prefix), name.length(), 7);
  block.insert(block.end(), prefix, prefix + name_len_bytes);
  block.insert(block.end(), name.begin(), name.end());

  // Header field value: length prefix (n = 7) followed by the raw octets.
  prefix[0]                     = 0x00; // clear the Huffman flag in the prefix octet
  const int64_t value_len_bytes = xpack_encode_integer(prefix, prefix + sizeof(prefix), value.length(), 7);
  block.insert(block.end(), prefix, prefix + value_len_bytes);
  block.insert(block.end(), value.begin(), value.end());

  return block;
}

// Build a single literal-with-incremental-indexing header field (RFC 7541 6.2.1)
// using an INDEXED name (the name is taken from the static/dynamic table at the
// given index) and a new, non-Huffman value. The 6-bit prefix carries the table
// index. Used to exercise the oversized-value path when the name does not come
// from the wire.
std::vector<uint8_t>
build_literal_incremental_field_indexed_name(uint32_t name_index, std::string_view value)
{
  std::vector<uint8_t> block;

  uint8_t prefix[16];

  // Pattern '01' in the top two bits selects "Literal Header Field with
  // Incremental Indexing". The remaining 6 bits encode the name's table index.
  prefix[0]                    = 0x40;
  const int64_t name_idx_bytes = xpack_encode_integer(prefix, prefix + sizeof(prefix), name_index, 6);
  block.insert(block.end(), prefix, prefix + name_idx_bytes);

  // Header field value: length prefix (n = 7) followed by the raw octets.
  prefix[0]                     = 0x00; // clear the Huffman flag in the prefix octet
  const int64_t value_len_bytes = xpack_encode_integer(prefix, prefix + sizeof(prefix), value.length(), 7);
  block.insert(block.end(), prefix, prefix + value_len_bytes);
  block.insert(block.end(), value.begin(), value.end());

  return block;
}
} // namespace

// The header field-size limit caps both the name and the value at UINT16_MAX
// (65535) octets. An HPACK-decoded literal header field whose name or value is
// longer than UINT16_MAX cannot be stored under that cap, so the decoder rejects
// such a field with HPACK_ERROR_COMPRESSION_ERROR (a connection error) BEFORE
// inserting anything into the dynamic indexing table.
TEST_CASE("HPACK oversized literal field is rejected", "[hpack]")
{
  // Use a generous header-size and per-field budget so the failure is
  // attributable to the uint16_t field-length limit, not to the max_header_size
  // or header_field_max_size decoder limits.
  constexpr uint32_t LARGE_MAX_HEADER_SIZE = 1 * 1024 * 1024;
  constexpr size_t   OVERSIZED_LEN         = 70000; // > UINT16_MAX (65535)

  SECTION("oversized value")
  {
    HpackIndexingTable indexing_table(4096);
    REQUIRE(indexing_table.size() == 0);

    std::string          value(OVERSIZED_LEN, 'A');
    std::vector<uint8_t> block = build_literal_incremental_field("x-big-value", value);

    std::unique_ptr<HTTPHdr, void (*)(HTTPHdr *)> headers(new HTTPHdr, destroy_http_hdr);
    headers->create(HTTPType::REQUEST);

    int64_t len = hpack_decode_header_block(indexing_table, headers.get(), block.data(), block.size(), LARGE_MAX_HEADER_SIZE,
                                            MAX_TABLE_SIZE, LARGE_MAX_HEADER_SIZE);

    CHECK(len == HPACK_ERROR_COMPRESSION_ERROR);

    // The unstorable field must not have been inserted into the dynamic table.
    CHECK(indexing_table.size() == 0);
  }

  SECTION("oversized name")
  {
    HpackIndexingTable indexing_table(4096);
    REQUIRE(indexing_table.size() == 0);

    std::string          name(OVERSIZED_LEN, 'a');
    std::vector<uint8_t> block = build_literal_incremental_field(name, "small-value");

    std::unique_ptr<HTTPHdr, void (*)(HTTPHdr *)> headers(new HTTPHdr, destroy_http_hdr);
    headers->create(HTTPType::REQUEST);

    int64_t len = hpack_decode_header_block(indexing_table, headers.get(), block.data(), block.size(), LARGE_MAX_HEADER_SIZE,
                                            MAX_TABLE_SIZE, LARGE_MAX_HEADER_SIZE);

    CHECK(len == HPACK_ERROR_COMPRESSION_ERROR);

    // The unstorable field must not have been inserted into the dynamic table.
    CHECK(indexing_table.size() == 0);
  }

  SECTION("indexed name with oversized value")
  {
    HpackIndexingTable indexing_table(4096);
    REQUIRE(indexing_table.size() == 0);

    // Static-table index 4 is ":path", so only the value comes from the wire.
    constexpr uint32_t STATIC_INDEX_PATH = 4;

    std::string          value(OVERSIZED_LEN, 'A');
    std::vector<uint8_t> block = build_literal_incremental_field_indexed_name(STATIC_INDEX_PATH, value);

    std::unique_ptr<HTTPHdr, void (*)(HTTPHdr *)> headers(new HTTPHdr, destroy_http_hdr);
    headers->create(HTTPType::REQUEST);

    int64_t len = hpack_decode_header_block(indexing_table, headers.get(), block.data(), block.size(), LARGE_MAX_HEADER_SIZE,
                                            MAX_TABLE_SIZE, LARGE_MAX_HEADER_SIZE);

    CHECK(len == HPACK_ERROR_COMPRESSION_ERROR);

    // The unstorable field must not have been inserted into the dynamic table.
    CHECK(indexing_table.size() == 0);
  }
}

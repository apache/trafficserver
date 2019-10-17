/** @file

  Catch based unit tests for XPACK

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

#define CATCH_CONFIG_MAIN

#include "catch.hpp"

#include "XPACK.h"
#include "HuffmanCodec.h"

static constexpr int BUFSIZE_FOR_REGRESSION_TEST = 128;

TEST_CASE("XPACK_Integer", "[xpack]")
{
  // [RFC 7541] C.1. Integer Representation Examples
  static const struct {
    uint32_t raw_integer;
    uint8_t *encoded_field;
    int encoded_field_len;
    int prefix;
  } integer_test_case[] = {{10, (uint8_t *)"\x0a", 1, 5}, {1337, (uint8_t *)"\x1F\x9A\x0A", 3, 5}, {42, (uint8_t *)R"(*)", 1, 8}};

  SECTION("Encoding")
  {
    for (const auto &i : integer_test_case) {
      uint8_t buf[BUFSIZE_FOR_REGRESSION_TEST] = {0};

      int len = xpack_encode_integer(buf, buf + BUFSIZE_FOR_REGRESSION_TEST, i.raw_integer, i.prefix);

      REQUIRE(len > 0);
      REQUIRE(len == i.encoded_field_len);
      REQUIRE(memcmp(buf, i.encoded_field, len) == 0);
    }
  }

  SECTION("Decoding")
  {
    for (const auto &i : integer_test_case) {
      uint64_t actual = 0;
      int len         = xpack_decode_integer(actual, i.encoded_field, i.encoded_field + i.encoded_field_len, i.prefix);

      REQUIRE(len == i.encoded_field_len);
      REQUIRE(actual == i.raw_integer);
    }
  }
}

TEST_CASE("XPACK_String", "[xpack]")
{
  // Example: custom-key: custom-header
  const static struct {
    char *raw_string;
    uint32_t raw_string_len;
    uint8_t *encoded_field;
    int encoded_field_len;
  } string_test_case[] = {{(char *)"", 0,
                           (uint8_t *)"\x0"
                                      "",
                           1},
                          {(char *)"custom-key", 10,
                           (uint8_t *)"\xA"
                                      "custom-key",
                           11},
                          {(char *)"", 0,
                           (uint8_t *)"\x80"
                                      "",
                           1},
                          {(char *)"custom-key", 10,
                           (uint8_t *)"\x88"
                                      "\x25\xa8\x49\xe9\x5b\xa9\x7d\x7f",
                           9}};

  SECTION("Encoding")
  {
    // FIXME Current encoder support only huffman conding.
    for (unsigned int i = 2; i < sizeof(string_test_case) / sizeof(string_test_case[0]); i++) {
      uint8_t buf[BUFSIZE_FOR_REGRESSION_TEST] = {0};
      int len = xpack_encode_string(buf, buf + BUFSIZE_FOR_REGRESSION_TEST, string_test_case[i].raw_string,
                                    string_test_case[i].raw_string_len);

      REQUIRE(len > 0);
      REQUIRE(len == string_test_case[i].encoded_field_len);
      REQUIRE(memcmp(buf, string_test_case[i].encoded_field, len) == 0);
    }
  }

  SECTION("Decoding")
  {
    // Decoding string needs huffman tree
    hpack_huffman_init();

    for (const auto &i : string_test_case) {
      Arena arena;
      char *actual        = nullptr;
      uint64_t actual_len = 0;
      int len             = xpack_decode_string(arena, &actual, actual_len, i.encoded_field, i.encoded_field + i.encoded_field_len);

      REQUIRE(len == i.encoded_field_len);
      REQUIRE(actual_len == i.raw_string_len);
      REQUIRE(memcmp(actual, i.raw_string, actual_len) == 0);
    }
  }
}

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

#include "proxy/hdrs/XPACK.h"
#include "proxy/hdrs/HuffmanCodec.h"

static constexpr int BUFSIZE_FOR_REGRESSION_TEST = 128;

TEST_CASE("XPACK_Integer", "[xpack]")
{
  // [RFC 7541] C.1. Integer Representation Examples
  static const struct {
    uint32_t raw_integer;
    uint8_t *encoded_field;
    int encoded_field_len;
    int prefix;
  } integer_test_case[] = {
    {10,   (uint8_t *)"\x0a",         1, 5},
    {1337, (uint8_t *)"\x1F\x9A\x0A", 3, 5},
    {42,   (uint8_t *)R"(*)",         1, 8}
  };

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
  } string_test_case[] = {
    {(char *)"",                        0,
     (uint8_t *)"\x0"
                "",                                                                                     1 },
    {(char *)"custom-key",              10,
     (uint8_t *)"\xA"
                "custom-key",                                                                           11},
    {(char *)"",                        0,
     (uint8_t *)"\x80"
                "",                                                                                     1 },
    {(char *)"custom-key",              10,
     (uint8_t *)"\x88"
                "\x25\xa8\x49\xe9\x5b\xa9\x7d\x7f",                                                     9 },
    {(char *)"cw Times New Roman_σ=1", 23,
     (uint8_t *)"\x95"
                "\x27\x85\x37\x9a\x92\xa1\x4d\x25\xf0\xa6\xd3\xd2\x3a\xa2\xff\xff\xf6\xff\xff\x44\x01", 22},
  };

  SECTION("Encoding")
  {
    // FIXME Current encoder support only huffman conding.
    for (unsigned int i = 2; i < sizeof(string_test_case) / sizeof(string_test_case[0]); i++) {
      uint8_t buf[BUFSIZE_FOR_REGRESSION_TEST] = {0};
      int64_t len = xpack_encode_string(buf, buf + BUFSIZE_FOR_REGRESSION_TEST, string_test_case[i].raw_string,
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

  SECTION("Dynamic Table")
  {
    constexpr uint16_t MAX_SIZE = 128;
    XpackDynamicTable dt(MAX_SIZE);
    XpackLookupResult result;
    const char *name  = nullptr;
    size_t name_len   = 0;
    const char *value = nullptr;
    size_t value_len  = 0;

    // Check the initial state
    REQUIRE(dt.size() == 0);
    REQUIRE(dt.maximum_size() == MAX_SIZE);
    REQUIRE(dt.is_empty());
    REQUIRE(dt.count() == 0);
    result = dt.lookup(0, &name, &name_len, &value, &value_len);
    REQUIRE(result.match_type == XpackLookupResult::MatchType::NONE);
    result = dt.lookup(1, &name, &name_len, &value, &value_len);
    REQUIRE(result.match_type == XpackLookupResult::MatchType::NONE);
    result = dt.lookup(MAX_SIZE - 1, &name, &name_len, &value, &value_len);
    REQUIRE(result.match_type == XpackLookupResult::MatchType::NONE);
    result = dt.lookup(MAX_SIZE, &name, &name_len, &value, &value_len);
    REQUIRE(result.match_type == XpackLookupResult::MatchType::NONE);
    result = dt.lookup(MAX_SIZE + 1, &name, &name_len, &value, &value_len);
    REQUIRE(result.match_type == XpackLookupResult::MatchType::NONE);

    // Insdert one entry
    dt.insert_entry("name1", "value1");
    REQUIRE(dt.size() == strlen("name1") + strlen("value1") + 32);
    REQUIRE(dt.maximum_size() == MAX_SIZE);
    REQUIRE(dt.count() == 1);
    REQUIRE(dt.largest_index() == 0);
    result = dt.lookup(0, &name, &name_len, &value, &value_len);
    REQUIRE(result.match_type == XpackLookupResult::MatchType::EXACT);
    REQUIRE(name_len == strlen("name1"));
    REQUIRE(memcmp(name, "name1", name_len) == 0);
    REQUIRE(value_len == strlen("value1"));
    REQUIRE(memcmp(value, "value1", value_len) == 0);
    result = dt.lookup(dt.largest_index() + 1, &name, &name_len, &value, &value_len);
    REQUIRE(result.match_type == XpackLookupResult::MatchType::NONE);

    result = dt.lookup(MAX_SIZE - 1, &name, &name_len, &value, &value_len);
    REQUIRE(result.match_type == XpackLookupResult::MatchType::NONE);
    result = dt.lookup(MAX_SIZE, &name, &name_len, &value, &value_len);
    REQUIRE(result.match_type == XpackLookupResult::MatchType::NONE);
    result = dt.lookup(MAX_SIZE + 1, &name, &name_len, &value, &value_len);
    REQUIRE(result.match_type == XpackLookupResult::MatchType::NONE);

    // Insdert one more entry
    dt.insert_entry("name2", "value2");
    REQUIRE(dt.size() == strlen("name1") + strlen("value1") + 32 + strlen("name2") + strlen("value2") + 32);
    REQUIRE(dt.maximum_size() == MAX_SIZE);
    REQUIRE(dt.count() == 2);
    REQUIRE(dt.largest_index() == 1);
    result = dt.lookup(0, &name, &name_len, &value, &value_len);
    REQUIRE(result.match_type == XpackLookupResult::MatchType::EXACT);
    REQUIRE(name_len == strlen("name1"));
    REQUIRE(memcmp(name, "name1", name_len) == 0);
    REQUIRE(value_len == strlen("value1"));
    REQUIRE(memcmp(value, "value1", value_len) == 0);
    result = dt.lookup(1, &name, &name_len, &value, &value_len);
    REQUIRE(result.match_type == XpackLookupResult::MatchType::EXACT);
    REQUIRE(name_len == strlen("name2"));
    REQUIRE(memcmp(name, "name2", name_len) == 0);
    REQUIRE(value_len == strlen("value2"));
    REQUIRE(memcmp(value, "value2", value_len) == 0);
    result = dt.lookup(dt.largest_index() + 1, &name, &name_len, &value, &value_len);
    REQUIRE(result.match_type == XpackLookupResult::MatchType::NONE);
    result = dt.lookup_relative(0, &name, &name_len, &value, &value_len);
    REQUIRE(result.match_type == XpackLookupResult::MatchType::EXACT);
    REQUIRE(name_len == strlen("name2"));
    REQUIRE(memcmp(name, "name2", name_len) == 0);
    REQUIRE(value_len == strlen("value2"));
    REQUIRE(memcmp(value, "value2", value_len) == 0);
    result = dt.lookup_relative(1, &name, &name_len, &value, &value_len);
    REQUIRE(result.match_type == XpackLookupResult::MatchType::EXACT);
    REQUIRE(name_len == strlen("name1"));
    REQUIRE(memcmp(name, "name1", name_len) == 0);
    REQUIRE(value_len == strlen("value1"));
    REQUIRE(memcmp(value, "value1", value_len) == 0);
    result = dt.lookup(MAX_SIZE - 1, &name, &name_len, &value, &value_len);
    REQUIRE(result.match_type == XpackLookupResult::MatchType::NONE);
    result = dt.lookup(MAX_SIZE, &name, &name_len, &value, &value_len);
    REQUIRE(result.match_type == XpackLookupResult::MatchType::NONE);
    result = dt.lookup(MAX_SIZE + 1, &name, &name_len, &value, &value_len);
    REQUIRE(result.match_type == XpackLookupResult::MatchType::NONE);

    // Insdert one more entry (this should evict the first entry)
    dt.insert_entry("name3", "value3");
    REQUIRE(dt.size() == strlen("name2") + strlen("value2") + 32 + strlen("name3") + strlen("value3") + 32);
    REQUIRE(dt.maximum_size() == MAX_SIZE);
    REQUIRE(dt.count() == 2);
    REQUIRE(dt.largest_index() == 2);
    result = dt.lookup(0, &name, &name_len, &value, &value_len);
    REQUIRE(result.match_type == XpackLookupResult::MatchType::NONE);
    result = dt.lookup(1, &name, &name_len, &value, &value_len);
    REQUIRE(result.match_type == XpackLookupResult::MatchType::EXACT);
    REQUIRE(name_len == strlen("name2"));
    REQUIRE(memcmp(name, "name2", name_len) == 0);
    REQUIRE(value_len == strlen("value2"));
    REQUIRE(memcmp(value, "value2", value_len) == 0);
    result = dt.lookup(2, &name, &name_len, &value, &value_len);
    REQUIRE(result.match_type == XpackLookupResult::MatchType::EXACT);
    REQUIRE(name_len == strlen("name3"));
    REQUIRE(memcmp(name, "name3", name_len) == 0);
    REQUIRE(value_len == strlen("value3"));
    REQUIRE(memcmp(value, "value3", value_len) == 0);
    result = dt.lookup(dt.largest_index() + 1, &name, &name_len, &value, &value_len);
    REQUIRE(result.match_type == XpackLookupResult::MatchType::NONE);
    result = dt.lookup_relative(0, &name, &name_len, &value, &value_len);
    REQUIRE(result.match_type == XpackLookupResult::MatchType::EXACT);
    REQUIRE(name_len == strlen("name3"));
    REQUIRE(memcmp(name, "name3", name_len) == 0);
    REQUIRE(value_len == strlen("value3"));
    REQUIRE(memcmp(value, "value3", value_len) == 0);
    result = dt.lookup_relative(1, &name, &name_len, &value, &value_len);
    REQUIRE(result.match_type == XpackLookupResult::MatchType::EXACT);
    REQUIRE(name_len == strlen("name2"));
    REQUIRE(memcmp(name, "name2", name_len) == 0);
    REQUIRE(value_len == strlen("value2"));
    REQUIRE(memcmp(value, "value2", value_len) == 0);

    // Insdert one more entry (this should evict all existing entries)
    dt.insert_entry("name4-1234567890123456789012345", "value4-9876543210987654321098765");
    REQUIRE(dt.size() == strlen("name4-1234567890123456789012345") + strlen("value4-9876543210987654321098765") + 32);
    REQUIRE(dt.maximum_size() == MAX_SIZE);
    REQUIRE(dt.count() == 1);
    REQUIRE(dt.largest_index() == 3);
    result = dt.lookup(3, &name, &name_len, &value, &value_len);
    REQUIRE(result.match_type == XpackLookupResult::MatchType::EXACT);
    REQUIRE(name_len == strlen("name4-1234567890123456789012345"));
    REQUIRE(memcmp(name, "name4-1234567890123456789012345", name_len) == 0);
    REQUIRE(value_len == strlen("value4-9876543210987654321098765"));
    REQUIRE(memcmp(value, "value4-9876543210987654321098765", value_len) == 0);
    result = dt.lookup(dt.largest_index() - 1, &name, &name_len, &value, &value_len);
    REQUIRE(result.match_type == XpackLookupResult::MatchType::NONE);
    result = dt.lookup(dt.largest_index(), &name, &name_len, &value, &value_len);
    REQUIRE(result.match_type == XpackLookupResult::MatchType::EXACT);
    result = dt.lookup(dt.largest_index() + 1, &name, &name_len, &value, &value_len);
    REQUIRE(result.match_type == XpackLookupResult::MatchType::NONE);

    // Update the maximum size (this should not evict anything)
    size_t current_size = dt.size();
    dt.update_maximum_size(current_size);
    REQUIRE(dt.size() == current_size);
    REQUIRE(dt.maximum_size() == current_size);
    REQUIRE(dt.count() == 1);
    REQUIRE(dt.largest_index() == 3);
    result = dt.lookup(3, &name, &name_len, &value, &value_len);
    REQUIRE(result.match_type == XpackLookupResult::MatchType::EXACT);
    REQUIRE(name_len == strlen("name4-1234567890123456789012345"));
    REQUIRE(memcmp(name, "name4-1234567890123456789012345", name_len) == 0);
    REQUIRE(value_len == strlen("value4-9876543210987654321098765"));
    REQUIRE(memcmp(value, "value4-9876543210987654321098765", value_len) == 0);

    // Update the maximum size (this should evict everything)
    dt.update_maximum_size(0);
    REQUIRE(dt.size() == 0);
    REQUIRE(dt.maximum_size() == 0);
    REQUIRE(dt.is_empty());
    REQUIRE(dt.count() == 0);
    result = dt.lookup(1, &name, &name_len, &value, &value_len);
    REQUIRE(result.match_type == XpackLookupResult::MatchType::NONE);
    result = dt.lookup(2, &name, &name_len, &value, &value_len);
    REQUIRE(result.match_type == XpackLookupResult::MatchType::NONE);

    // Reset the maximum size
    dt.update_maximum_size(4096);
    REQUIRE(dt.size() == 0);
    REQUIRE(dt.maximum_size() == 4096);
    REQUIRE(dt.is_empty());
    REQUIRE(dt.count() == 0);

    // Insert an oversided item
    dt.insert_entry("name1", "value1");              // This should be evicted
    dt.insert_entry("", UINT32_MAX, "", UINT32_MAX); // This should not even cause a buffer over run
    REQUIRE(dt.size() == 0);
    REQUIRE(dt.maximum_size() == 4096);
    REQUIRE(dt.is_empty());
    REQUIRE(dt.count() == 0);
  }
}

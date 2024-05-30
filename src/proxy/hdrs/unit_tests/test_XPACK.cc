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

#include <string>
#include <string_view>
#define CATCH_CONFIG_MAIN

#include "catch.hpp"

#include "proxy/hdrs/XPACK.h"
#include "proxy/hdrs/HuffmanCodec.h"

static constexpr int BUFSIZE_FOR_REGRESSION_TEST = 128;

std::string
get_long_string(int size)
{
  std::string s(size, '0');
  auto        p = s.data();
  for (int i = 0; i < size; ++i) {
    p[i] = '0' + (i % 10);
  }
  return s;
}

TEST_CASE("XPACK_Integer", "[xpack]")
{
  // [RFC 7541] C.1. Integer Representation Examples
  static const struct {
    uint32_t raw_integer;
    uint8_t *encoded_field;
    int      encoded_field_len;
    int      prefix;
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
      int      len    = xpack_decode_integer(actual, i.encoded_field, i.encoded_field + i.encoded_field_len, i.prefix);

      REQUIRE(len == i.encoded_field_len);
      REQUIRE(actual == i.raw_integer);
    }
  }
}

TEST_CASE("XPACK_String", "[xpack]")
{
  // Example: custom-key: custom-header
  const static struct {
    char    *raw_string;
    uint32_t raw_string_len;
    uint8_t *encoded_field;
    int      encoded_field_len;
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
      Arena    arena;
      char    *actual     = nullptr;
      uint64_t actual_len = 0;
      int      len        = xpack_decode_string(arena, &actual, actual_len, i.encoded_field, i.encoded_field + i.encoded_field_len);

      REQUIRE(len == i.encoded_field_len);
      REQUIRE(actual_len == i.raw_string_len);
      REQUIRE(memcmp(actual, i.raw_string, actual_len) == 0);
    }
  }

  SECTION("Zero-size Dynamic Table")
  {
    XpackDynamicTable dt(0);
    XpackLookupResult result;

    REQUIRE(dt.size() == 0);
    REQUIRE(dt.maximum_size() == 0);
    REQUIRE(dt.is_empty());
    REQUIRE(dt.count() == 0);
    result = dt.lookup("", "");
    REQUIRE(result.match_type == XpackLookupResult::MatchType::NONE);
  }

  SECTION("Dynamic Table")
  {
    constexpr uint16_t MAX_SIZE = 128;
    XpackDynamicTable  dt(MAX_SIZE);
    XpackLookupResult  result;
    const char        *name      = nullptr;
    size_t             name_len  = 0;
    const char        *value     = nullptr;
    size_t             value_len = 0;

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

    // Insert one entry
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

    // Insert one more entry
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

    // Insert one more entry (this should evict the first entry)
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

    // Insert one more entry (this should evict all existing entries)
    std::string field_4 = get_long_string(50);
    dt.insert_entry(field_4, field_4); // 100 bytes. _head should now be 0.
    REQUIRE(dt.size() == 2 * field_4.length() + 32);
    REQUIRE(dt.maximum_size() == MAX_SIZE);
    REQUIRE(dt.count() == 1);
    REQUIRE(dt.largest_index() == 3);
    result = dt.lookup(3, &name, &name_len, &value, &value_len);
    REQUIRE(result.match_type == XpackLookupResult::MatchType::EXACT);
    REQUIRE(name_len == field_4.length());
    REQUIRE(memcmp(name, field_4.data(), name_len) == 0);
    REQUIRE(value_len == field_4.length());
    REQUIRE(memcmp(value, field_4.data(), value_len) == 0);
    result = dt.lookup(dt.largest_index() - 1, &name, &name_len, &value, &value_len);
    REQUIRE(result.match_type == XpackLookupResult::MatchType::NONE);
    result = dt.lookup(dt.largest_index(), &name, &name_len, &value, &value_len);
    REQUIRE(result.match_type == XpackLookupResult::MatchType::EXACT);
    result = dt.lookup(dt.largest_index() + 1, &name, &name_len, &value, &value_len);
    REQUIRE(result.match_type == XpackLookupResult::MatchType::NONE);

    // Update the maximum size to the current used size (this should not evict anything).
    size_t current_size = dt.size();
    dt.update_maximum_size(current_size);
    REQUIRE(dt.size() == current_size);
    REQUIRE(dt.maximum_size() == current_size);
    REQUIRE(dt.count() == 1);
    REQUIRE(dt.largest_index() == 3);
    result = dt.lookup(3, &name, &name_len, &value, &value_len);
    REQUIRE(result.match_type == XpackLookupResult::MatchType::EXACT);
    REQUIRE(name_len == field_4.length());
    REQUIRE(memcmp(name, field_4.data(), name_len) == 0);
    REQUIRE(value_len == field_4.length());
    REQUIRE(memcmp(value, field_4.data(), value_len) == 0);

    // Expand the maximum size (this should not evict anything).
    constexpr uint16_t LARGER_MAX_SIZE = 4096;
    dt.update_maximum_size(LARGER_MAX_SIZE);
    REQUIRE(dt.size() == current_size);
    REQUIRE(dt.maximum_size() == LARGER_MAX_SIZE);
    REQUIRE(dt.count() == 1);
    // Note that largest_index must always be preserved across all resizes and evictions.
    REQUIRE(dt.largest_index() == 3);
    result = dt.lookup(dt.largest_index(), &name, &name_len, &value, &value_len);
    REQUIRE(result.match_type == XpackLookupResult::MatchType::EXACT);
    REQUIRE(name_len == field_4.length());
    REQUIRE(memcmp(name, field_4.data(), name_len) == 0);
    REQUIRE(value_len == field_4.length());
    REQUIRE(memcmp(value, field_4.data(), value_len) == 0);

    // Add a new entry and make sure the existing valid entry is not overwritten.
    std::string field_5 = get_long_string(100);
    dt.insert_entry(field_5, field_5);
    REQUIRE(dt.count() == 2);
    REQUIRE(dt.size() == 2 * field_4.length() + 32 + 2 * field_5.length() + 32);
    result = dt.lookup(dt.largest_index() - 1, &name, &name_len, &value, &value_len);
    REQUIRE(result.match_type == XpackLookupResult::MatchType::EXACT);
    REQUIRE(name_len == field_4.length());
    REQUIRE(memcmp(name, field_4.data(), name_len) == 0);
    REQUIRE(value_len == field_4.length());
    REQUIRE(memcmp(value, field_4.data(), value_len) == 0);
    result = dt.lookup(dt.largest_index(), &name, &name_len, &value, &value_len);
    REQUIRE(result.match_type == XpackLookupResult::MatchType::EXACT);
    REQUIRE(name_len == field_5.length());
    REQUIRE(memcmp(name, field_5.data(), name_len) == 0);
    REQUIRE(value_len == field_5.length());
    REQUIRE(memcmp(value, field_5.data(), value_len) == 0);

    // Update (shrink) the maximum size to 0 (this should evict everything)
    auto const previous_largest_index = dt.largest_index();
    dt.update_maximum_size(0);
    REQUIRE(dt.size() == 0);
    REQUIRE(dt.maximum_size() == 0);
    REQUIRE(dt.is_empty());
    REQUIRE(dt.count() == 0);
    for (auto i = 0u; i <= previous_largest_index; ++i) {
      result = dt.lookup(i, &name, &name_len, &value, &value_len);
      REQUIRE(result.match_type == XpackLookupResult::MatchType::NONE);
    }

    // Update the maximum size to a new value.
    dt.update_maximum_size(LARGER_MAX_SIZE);
    REQUIRE(dt.size() == 0);
    REQUIRE(dt.maximum_size() == LARGER_MAX_SIZE);
    REQUIRE(dt.is_empty());
    REQUIRE(dt.count() == 0);

    // Insert a new item.
    dt.insert_entry("name1", "value1");
    REQUIRE(dt.maximum_size() == LARGER_MAX_SIZE);
    REQUIRE(dt.count() == 1);
    // Note that indexing will continue from the last index, despite eviction.
    REQUIRE(dt.largest_index() == 5);
    // The old index values should not match.
    result = dt.lookup(dt.largest_index() - 1, &name, &name_len, &value, &value_len);
    REQUIRE(result.match_type == XpackLookupResult::MatchType::NONE);
    // The last inserted item should still exist though.
    result = dt.lookup(dt.largest_index(), &name, &name_len, &value, &value_len);
    REQUIRE(result.match_type == XpackLookupResult::MatchType::EXACT);
    REQUIRE(name_len == strlen("name1"));
    REQUIRE(memcmp(name, "name1", name_len) == 0);
    REQUIRE(value_len == strlen("value1"));
    REQUIRE(memcmp(value, "value1", value_len) == 0);

    // Insert an oversized item. The previous item should be evicted.
    dt.insert_entry("", UINT32_MAX, "", UINT32_MAX); // This should not cause a buffer over run
    REQUIRE(dt.size() == 0);
    REQUIRE(dt.maximum_size() == 4096);
    REQUIRE(dt.is_empty());
    REQUIRE(dt.count() == 0);
  }
}

// Return a 110 character string.
std::string
get_long_string(std::string_view prefix)
{
  return std::string(prefix) + std::string("0123456789"
                                           "0123456789"
                                           "0123456789"
                                           "0123456789"
                                           "0123456789"
                                           "0123456789"
                                           "0123456789"
                                           "0123456789"
                                           "0123456789"
                                           "0123456789"
                                           "0123456789");
}

TEST_CASE("XpackDynamicTableStorage", "[xpack]")
{
  constexpr uint16_t       MAX_SIZE = 100;
  XpackDynamicTableStorage storage{MAX_SIZE};

  // First write.
  auto const name1   = get_long_string("name1");
  auto const value1  = get_long_string("value1");
  auto const offset1 = storage.write(name1.data(), 25, value1.data(), 25);
  REQUIRE(offset1 == 0);
  char const *name  = nullptr;
  char const *value = nullptr;
  storage.read(offset1, &name, 25, &value, 25);
  REQUIRE(memcmp(name, name1.data(), 25) == 0);
  REQUIRE(memcmp(value, value1.data(), 25) == 0);

  // Second write.
  auto const name2   = get_long_string("name2");
  auto const value2  = get_long_string("value2");
  auto const offset2 = storage.write(name2.data(), 25, value2.data(), 25);
  REQUIRE(offset2 == 50);
  storage.read(offset2, &name, 25, &value, 25);
  REQUIRE(memcmp(name, name2.data(), 25) == 0);
  REQUIRE(memcmp(value, value2.data(), 25) == 0);

  // Third write - exceed size and enter into the overwrite threshold.
  auto const name3   = get_long_string("name3");
  auto const value3  = get_long_string("value3");
  auto const offset3 = storage.write(name3.data(), 25, value3.data(), 25);
  REQUIRE(offset3 == 100);
  storage.read(offset3, &name, 25, &value, 25);
  REQUIRE(memcmp(name, name3.data(), 25) == 0);
  REQUIRE(memcmp(value, value3.data(), 25) == 0);

  // Note that the offset will now wrap back around to 0 since we've exceeded MAX_SIZE.
  auto const name4   = get_long_string("name4");
  auto const value4  = get_long_string("value4");
  auto const offset4 = storage.write(name4.data(), 25, value4.data(), 25);
  REQUIRE(offset4 == 0);
  storage.read(offset4, &name, 25, &value, 25);
  REQUIRE(memcmp(name, name4.data(), 25) == 0);
  REQUIRE(memcmp(value, value4.data(), 25) == 0);

  // Test expanding capacity. Note that we start at offset2 since the data at
  // offset1 will be overwritten.
  uint32_t reoffset2 = 0, reoffset3 = 0, reoffset4 = 0;
  {
    ExpandCapacityContext context{storage, 200};
    reoffset2 = context.copy_field(offset2, 50);
    // Note that the offsets will now be shifted, starting from 0 now.
    REQUIRE(reoffset2 == 0);
    reoffset3 = context.copy_field(offset3, 50);
    REQUIRE(reoffset3 == 50);
    reoffset4 = context.copy_field(offset4, 50);
    REQUIRE(reoffset4 == 100);
  } // context goes out of scope and finishes the expansion phase.

  storage.read(reoffset2, &name, 25, &value, 25);
  REQUIRE(memcmp(name, name2.data(), 25) == 0);
  REQUIRE(memcmp(value, value2.data(), 25) == 0);
  storage.read(reoffset3, &name, 25, &value, 25);
  REQUIRE(memcmp(name, name3.data(), 25) == 0);
  REQUIRE(memcmp(value, value3.data(), 25) == 0);
  storage.read(reoffset4, &name, 25, &value, 25);
  REQUIRE(memcmp(name, name4.data(), 25) == 0);
  REQUIRE(memcmp(value, value4.data(), 25) == 0);
}

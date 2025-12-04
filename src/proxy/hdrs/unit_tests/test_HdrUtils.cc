/** @file

   Catch-based tests for HdrsUtils.cc

   @section license License

   Licensed to the Apache Software Foundation (ASF) under one or more contributor license agreements.
   See the NOTICE file distributed with this work for additional information regarding copyright
   ownership.  The ASF licenses this file to you under the Apache License, Version 2.0 (the
   "License"); you may not use this file except in compliance with the License.  You may obtain a
   copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software distributed under the License
   is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
   or implied. See the License for the specific language governing permissions and limitations under
   the License.
 */

#include <string>
#include <cstring>
#include <cctype>
#include <bitset>
#include <initializer_list>
#include <new>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include "proxy/hdrs/HdrHeap.h"
#include "proxy/hdrs/MIME.h"
#include "proxy/hdrs/HdrUtils.h"

// Parameterized test for HdrCsvIter parsing.
TEST_CASE("HdrCsvIter", "[proxy][hdrutils]")
{
  constexpr bool COMBINE_DUPLICATES = true;

  // Structure for parameterized HdrCsvIter tests.
  struct CsvIterTestCase {
    const char                   *description;
    const char                   *header_text;
    const char                   *field_name;
    std::vector<std::string_view> expected_values;
    bool                          combine_dups; // Parameter for get_first()
  };

  // Test cases for HdrCsvIter parsing.
  // clang-format off
  static const std::vector<CsvIterTestCase> csv_iter_test_cases = {
    // Basic CSV parsing tests
    {"single value",
     "One: alpha\r\n\r\n",
     "One",
     {"alpha"},
     COMBINE_DUPLICATES},

    {"two values",
     "Two: alpha, bravo\r\n\r\n",
     "Two",
     {"alpha", "bravo"},
     COMBINE_DUPLICATES},

    {"quoted values and escaping",
     "Three: zwoop, \"A,B\" , , phil  , \"unterminated\r\n\r\n",
     "Three",
     {"zwoop", "A,B", "phil", "unterminated"},
     COMBINE_DUPLICATES},

    {"escaped quotes passed through",
     "Four: itchi, \"ni, \\\"san\" , \"\" , \"\r\n\r\n",
     "Four",
     {"itchi", "ni, \\\"san"},
     COMBINE_DUPLICATES},

    {"duplicate fields combined",
     "Five: alpha, bravo, charlie\r\nFive: delta, echo\r\n\r\n",
     "Five",
     {"alpha", "bravo", "charlie", "delta", "echo"},
     COMBINE_DUPLICATES},

    {"duplicate fields not combined",
     "Five: alpha, bravo, charlie\r\nFive: delta, echo\r\n\r\n",
     "Five",
     {"alpha", "bravo", "charlie"},
     !COMBINE_DUPLICATES},

    // Cache-Control specific tests
    {"Cache-Control: basic max-age and public",
     "Cache-Control: max-age=30, public\r\n\r\n",
     "Cache-Control",
     {"max-age=30", "public"},
     COMBINE_DUPLICATES},

    {"Cache-Control: extension directives with values",
     "Cache-Control: stale-if-error=1, stale-while-revalidate=60, no-cache\r\n\r\n",
     "Cache-Control",
     {"stale-if-error=1", "stale-while-revalidate=60", "no-cache"},
     COMBINE_DUPLICATES},

    {"Cache-Control: mixed directives",
     "Cache-Control: public, max-age=300, s-maxage=600\r\n\r\n",
     "Cache-Control",
     {"public", "max-age=300", "s-maxage=600"},
     COMBINE_DUPLICATES},

    {"Cache-Control: semicolon separator treated as single value",
     "Cache-Control: public; max-age=30\r\n\r\n",
     "Cache-Control",
     {"public; max-age=30"},
     COMBINE_DUPLICATES},

    {"Cache-Control: empty value",
     "Cache-Control: \r\n\r\n",
     "Cache-Control",
     {},
     COMBINE_DUPLICATES},
  };
  // clang-format on
  auto test_case = GENERATE(from_range(csv_iter_test_cases));

  CAPTURE(test_case.description, test_case.header_text);

  HdrHeap    *heap = new_HdrHeap(HdrHeap::DEFAULT_SIZE + 64);
  MIMEParser  parser;
  char const *real_s = test_case.header_text;
  char const *real_e = test_case.header_text + strlen(test_case.header_text);
  MIMEHdr     mime;

  mime.create(heap);
  mime_parser_init(&parser);

  auto result = mime_parser_parse(&parser, heap, mime.m_mime, &real_s, real_e, false, true, false);
  REQUIRE(ParseResult::DONE == result);

  HdrCsvIter iter;
  MIMEField *field = mime.field_find(test_case.field_name);
  REQUIRE(field != nullptr);

  if (test_case.expected_values.empty()) {
    auto value = iter.get_first(field, test_case.combine_dups);
    REQUIRE(value.empty());
  } else {
    auto value = iter.get_first(field, test_case.combine_dups);
    REQUIRE(value == test_case.expected_values[0]);

    for (size_t i = 1; i < test_case.expected_values.size(); ++i) {
      value = iter.get_next();
      REQUIRE(value == test_case.expected_values[i]);
    }

    // After all expected values, the next should be empty.
    value = iter.get_next();
    REQUIRE(value.empty());
  }

  heap->destroy();
}

TEST_CASE("HdrUtils 2", "[proxy][hdrutils]")
{
  // Test empty field.
  static constexpr swoc::TextView text{"Host: example.one\r\n"
                                       "Connection: keep-alive\r\n"
                                       "Vary:\r\n"
                                       "After: value\r\n"
                                       "\r\n"};
  static constexpr swoc::TextView connection_tag{"Connection"};
  static constexpr swoc::TextView vary_tag{"Vary"};
  static constexpr swoc::TextView after_tag{"After"};

  char buff[text.size() + 1];

  HdrHeap    *heap = new_HdrHeap(HdrHeap::DEFAULT_SIZE + 64);
  MIMEParser  parser;
  char const *real_s = text.data();
  char const *real_e = text.data_end();
  MIMEHdr     mime;

  mime.create(heap);
  mime_parser_init(&parser);
  auto result = mime_parser_parse(&parser, heap, mime.m_mime, &real_s, real_e, false, true, false);
  REQUIRE(ParseResult::DONE == result);

  MIMEField *field{mime.field_find(connection_tag)};
  REQUIRE(mime_hdr_fields_count(mime.m_mime) == 4);
  REQUIRE(field != nullptr);
  field = mime.field_find(vary_tag);
  REQUIRE(field != nullptr);
  REQUIRE(field->m_len_value == 0);
  field = mime.field_find(after_tag);
  REQUIRE(field != nullptr);

  int  idx   = 0;
  int  skip  = 0;
  auto parse = mime_hdr_print(mime.m_mime, buff, static_cast<int>(sizeof(buff)), &idx, &skip);
  REQUIRE(parse != 0);
  REQUIRE(idx == static_cast<int>(text.size()));
  REQUIRE(0 == memcmp(swoc::TextView(buff, idx), text));
  heap->destroy();
};

TEST_CASE("HdrUtils 3", "[proxy][hdrutils]")
{
  // Test empty field.
  static constexpr swoc::TextView text{"Host: example.one\r\n"
                                       "Connection: keep-alive\r\n"
                                       "Before: value\r\n"
                                       "Vary: \r\n"
                                       "\r\n"};
  static constexpr swoc::TextView connection_tag{"Connection"};
  static constexpr swoc::TextView vary_tag{"Vary"};
  static constexpr swoc::TextView before_tag{"Before"};

  char buff[text.size() + 1];

  HdrHeap    *heap = new_HdrHeap(HdrHeap::DEFAULT_SIZE + 64);
  MIMEParser  parser;
  char const *real_s = text.data();
  char const *real_e = text.data_end();
  MIMEHdr     mime;

  mime.create(heap);
  mime_parser_init(&parser);
  auto result = mime_parser_parse(&parser, heap, mime.m_mime, &real_s, real_e, false, true, false);
  REQUIRE(ParseResult::DONE == result);

  MIMEField *field{mime.field_find(connection_tag)};
  REQUIRE(mime_hdr_fields_count(mime.m_mime) == 4);
  REQUIRE(field != nullptr);
  field = mime.field_find(vary_tag);
  REQUIRE(field != nullptr);
  REQUIRE(field->m_len_value == 0);
  field = mime.field_find(before_tag);
  REQUIRE(field != nullptr);

  int  idx   = 0;
  int  skip  = 0;
  auto parse = mime_hdr_print(mime.m_mime, buff, static_cast<int>(sizeof(buff)), &idx, &skip);
  REQUIRE(parse != 0);
  REQUIRE(idx == static_cast<int>(text.size()));
  REQUIRE(0 == memcmp(swoc::TextView(buff, idx), text));
  heap->destroy();
};

// Test that malformed Cache-Control directives are properly ignored during cooking.
// All malformed directives should result in mask == 0.
TEST_CASE("Cache-Control Malformed Cooking", "[proxy][hdrutils]")
{
  struct MalformedCCTestCase {
    const char *description;
    const char *header_text;
  };

  // clang-format off
  // These tests align with cache-tests.fyi/#cc-parse
  static const std::vector<MalformedCCTestCase> malformed_cc_test_cases = {
    // Separator issues
    {"semicolon separator (should be comma)",
     "Cache-Control: public; max-age=30\r\n\r\n"},

    // Space around equals (cc-parse: max-age with space before/after =)
    {"space before equals sign",
     "Cache-Control: max-age =300\r\n\r\n"},

    {"space after equals sign",
     "Cache-Control: max-age= 300\r\n\r\n"},

    {"space both before and after equals sign",
     "Cache-Control: max-age = 300\r\n\r\n"},

    // Quoted values (cc-parse: single-quoted max-age)
    {"single quotes around value",
     "Cache-Control: max-age='300'\r\n\r\n"},

    {"double quotes around value",
     "Cache-Control: max-age=\"300\"\r\n\r\n"},

    // s-maxage variants
    {"s-maxage with space before equals",
     "Cache-Control: s-maxage =600\r\n\r\n"},

    {"s-maxage with space after equals",
     "Cache-Control: s-maxage= 600\r\n\r\n"},

    // Invalid numeric values (cc-parse: decimal max-age)
    {"decimal value in max-age (1.5)",
     "Cache-Control: max-age=1.5\r\n\r\n"},

    {"decimal value in max-age (3600.0)",
     "Cache-Control: max-age=3600.0\r\n\r\n"},

    {"decimal value starting with dot (.5)",
     "Cache-Control: max-age=.5\r\n\r\n"},

    {"decimal value in s-maxage",
     "Cache-Control: s-maxage=1.5\r\n\r\n"},

    // Leading and trailing alpha characters
    {"leading alpha in max-age value",
     "Cache-Control: max-age=a300\r\n\r\n"},

    {"trailing alpha in max-age value",
     "Cache-Control: max-age=300a\r\n\r\n"},

    {"leading alpha in s-maxage value",
     "Cache-Control: s-maxage=a600\r\n\r\n"},

    {"trailing alpha in s-maxage value",
     "Cache-Control: s-maxage=600a\r\n\r\n"},

    // Empty and missing values
    {"empty max-age value alone",
     "Cache-Control: max-age=\r\n\r\n"},
  };
  // clang-format on

  auto test_case = GENERATE(from_range(malformed_cc_test_cases));

  CAPTURE(test_case.description, test_case.header_text);

  HdrHeap    *heap = new_HdrHeap(HdrHeap::DEFAULT_SIZE + 64);
  MIMEParser  parser;
  char const *real_s = test_case.header_text;
  char const *real_e = test_case.header_text + strlen(test_case.header_text);
  MIMEHdr     mime;

  mime.create(heap);
  mime_parser_init(&parser);

  auto result = mime_parser_parse(&parser, heap, mime.m_mime, &real_s, real_e, false, true, false);
  REQUIRE(ParseResult::DONE == result);

  mime.m_mime->recompute_cooked_stuff();

  // All malformed directives should result in mask == 0.
  auto mask = mime.get_cooked_cc_mask();
  REQUIRE(mask == 0);

  heap->destroy();
}

// Test that properly formed Cache-Control directives are correctly cooked.
TEST_CASE("Cache-Control Valid Cooking", "[proxy][hdrutils]")
{
  struct ValidCCTestCase {
    const char *description;
    const char *header_text;
    uint32_t    expected_mask;
    int32_t     expected_max_age;
    int32_t     expected_s_maxage;
    int32_t     expected_max_stale;
    int32_t     expected_min_fresh;
  };

  // Use 0 to indicate "don't care" for integer values (mask determines which are valid).
  // clang-format off
  static const std::vector<ValidCCTestCase> valid_cc_test_cases = {
    // Basic directives without values
    {"public only",
     "Cache-Control: public\r\n\r\n",
     MIME_COOKED_MASK_CC_PUBLIC,
     0, 0, 0, 0},

    {"private only",
     "Cache-Control: private\r\n\r\n",
     MIME_COOKED_MASK_CC_PRIVATE,
     0, 0, 0, 0},

    {"no-cache only",
     "Cache-Control: no-cache\r\n\r\n",
     MIME_COOKED_MASK_CC_NO_CACHE,
     0, 0, 0, 0},

    {"no-store only",
     "Cache-Control: no-store\r\n\r\n",
     MIME_COOKED_MASK_CC_NO_STORE,
     0, 0, 0, 0},

    {"no-transform only",
     "Cache-Control: no-transform\r\n\r\n",
     MIME_COOKED_MASK_CC_NO_TRANSFORM,
     0, 0, 0, 0},

    {"must-revalidate only",
     "Cache-Control: must-revalidate\r\n\r\n",
     MIME_COOKED_MASK_CC_MUST_REVALIDATE,
     0, 0, 0, 0},

    {"proxy-revalidate only",
     "Cache-Control: proxy-revalidate\r\n\r\n",
     MIME_COOKED_MASK_CC_PROXY_REVALIDATE,
     0, 0, 0, 0},

    {"only-if-cached only",
     "Cache-Control: only-if-cached\r\n\r\n",
     MIME_COOKED_MASK_CC_ONLY_IF_CACHED,
     0, 0, 0, 0},

    // Directives with values
    {"max-age=0",
     "Cache-Control: max-age=0\r\n\r\n",
     MIME_COOKED_MASK_CC_MAX_AGE,
     0, 0, 0, 0},

    {"max-age=300",
     "Cache-Control: max-age=300\r\n\r\n",
     MIME_COOKED_MASK_CC_MAX_AGE,
     300, 0, 0, 0},

    {"max-age=86400",
     "Cache-Control: max-age=86400\r\n\r\n",
     MIME_COOKED_MASK_CC_MAX_AGE,
     86400, 0, 0, 0},

    {"s-maxage=600",
     "Cache-Control: s-maxage=600\r\n\r\n",
     MIME_COOKED_MASK_CC_S_MAXAGE,
     0, 600, 0, 0},

    {"max-stale=100",
     "Cache-Control: max-stale=100\r\n\r\n",
     MIME_COOKED_MASK_CC_MAX_STALE,
     0, 0, 100, 0},

    {"min-fresh=60",
     "Cache-Control: min-fresh=60\r\n\r\n",
     MIME_COOKED_MASK_CC_MIN_FRESH,
     0, 0, 0, 60},

    // Multiple directives
    {"max-age and public",
     "Cache-Control: max-age=300, public\r\n\r\n",
     MIME_COOKED_MASK_CC_MAX_AGE | MIME_COOKED_MASK_CC_PUBLIC,
     300, 0, 0, 0},

    {"public and max-age (reversed order)",
     "Cache-Control: public, max-age=300\r\n\r\n",
     MIME_COOKED_MASK_CC_MAX_AGE | MIME_COOKED_MASK_CC_PUBLIC,
     300, 0, 0, 0},

    {"max-age and s-maxage",
     "Cache-Control: max-age=300, s-maxage=600\r\n\r\n",
     MIME_COOKED_MASK_CC_MAX_AGE | MIME_COOKED_MASK_CC_S_MAXAGE,
     300, 600, 0, 0},

    {"private and no-cache",
     "Cache-Control: private, no-cache\r\n\r\n",
     MIME_COOKED_MASK_CC_PRIVATE | MIME_COOKED_MASK_CC_NO_CACHE,
     0, 0, 0, 0},

    {"no-store and no-cache",
     "Cache-Control: no-store, no-cache\r\n\r\n",
     MIME_COOKED_MASK_CC_NO_STORE | MIME_COOKED_MASK_CC_NO_CACHE,
     0, 0, 0, 0},

    {"must-revalidate and proxy-revalidate",
     "Cache-Control: must-revalidate, proxy-revalidate\r\n\r\n",
     MIME_COOKED_MASK_CC_MUST_REVALIDATE | MIME_COOKED_MASK_CC_PROXY_REVALIDATE,
     0, 0, 0, 0},

    {"complex: public, max-age, s-maxage, must-revalidate",
     "Cache-Control: public, max-age=300, s-maxage=600, must-revalidate\r\n\r\n",
     MIME_COOKED_MASK_CC_PUBLIC | MIME_COOKED_MASK_CC_MAX_AGE |
       MIME_COOKED_MASK_CC_S_MAXAGE | MIME_COOKED_MASK_CC_MUST_REVALIDATE,
     300, 600, 0, 0},

    {"all request directives: max-age, max-stale, min-fresh, no-cache, no-store, no-transform, only-if-cached",
     "Cache-Control: max-age=100, max-stale=200, min-fresh=50, no-cache, no-store, no-transform, only-if-cached\r\n\r\n",
     MIME_COOKED_MASK_CC_MAX_AGE | MIME_COOKED_MASK_CC_MAX_STALE | MIME_COOKED_MASK_CC_MIN_FRESH |
       MIME_COOKED_MASK_CC_NO_CACHE | MIME_COOKED_MASK_CC_NO_STORE |
       MIME_COOKED_MASK_CC_NO_TRANSFORM | MIME_COOKED_MASK_CC_ONLY_IF_CACHED,
     100, 0, 200, 50},

    // Edge cases - whitespace
    {"extra whitespace around directive",
     "Cache-Control:   max-age=300  \r\n\r\n",
     MIME_COOKED_MASK_CC_MAX_AGE,
     300, 0, 0, 0},

    {"extra whitespace between directives",
     "Cache-Control: max-age=300 ,  public\r\n\r\n",
     MIME_COOKED_MASK_CC_MAX_AGE | MIME_COOKED_MASK_CC_PUBLIC,
     300, 0, 0, 0},

    {"tab character in header value",
     "Cache-Control:\tmax-age=300\r\n\r\n",
     MIME_COOKED_MASK_CC_MAX_AGE,
     300, 0, 0, 0},

    // Edge cases - unknown directives
    {"unknown directive ignored, known directive parsed",
     "Cache-Control: unknown-directive, max-age=300\r\n\r\n",
     MIME_COOKED_MASK_CC_MAX_AGE,
     300, 0, 0, 0},

    {"unknown directive with value ignored",
     "Cache-Control: unknown=value, public\r\n\r\n",
     MIME_COOKED_MASK_CC_PUBLIC,
     0, 0, 0, 0},

    // Edge cases - numeric values (cc-parse: 0000 max-age, large max-age)
    {"max-age with leading zeros (cc-parse: 0000 max-age)",
     "Cache-Control: max-age=0000\r\n\r\n",
     MIME_COOKED_MASK_CC_MAX_AGE,
     0, 0, 0, 0},

    {"max-age with leading zeros and value",
     "Cache-Control: max-age=00300\r\n\r\n",
     MIME_COOKED_MASK_CC_MAX_AGE,
     300, 0, 0, 0},

    {"large max-age value",
     "Cache-Control: max-age=999999999\r\n\r\n",
     MIME_COOKED_MASK_CC_MAX_AGE,
     999999999, 0, 0, 0},

    // Edge cases - negative values should be parsed (behavior per implementation)
    {"negative max-age value",
     "Cache-Control: max-age=-1\r\n\r\n",
     MIME_COOKED_MASK_CC_MAX_AGE,
     -1, 0, 0, 0},
  };
  // clang-format on

  auto test_case = GENERATE(from_range(valid_cc_test_cases));

  CAPTURE(test_case.description, test_case.header_text);

  HdrHeap    *heap = new_HdrHeap(HdrHeap::DEFAULT_SIZE + 64);
  MIMEParser  parser;
  char const *real_s = test_case.header_text;
  char const *real_e = test_case.header_text + strlen(test_case.header_text);
  MIMEHdr     mime;

  mime.create(heap);
  mime_parser_init(&parser);

  auto result = mime_parser_parse(&parser, heap, mime.m_mime, &real_s, real_e, false, true, false);
  REQUIRE(ParseResult::DONE == result);

  mime.m_mime->recompute_cooked_stuff();

  auto mask = mime.get_cooked_cc_mask();
  REQUIRE(mask == test_case.expected_mask);

  if (test_case.expected_mask & MIME_COOKED_MASK_CC_MAX_AGE) {
    REQUIRE(mime.get_cooked_cc_max_age() == test_case.expected_max_age);
  }
  if (test_case.expected_mask & MIME_COOKED_MASK_CC_S_MAXAGE) {
    REQUIRE(mime.get_cooked_cc_s_maxage() == test_case.expected_s_maxage);
  }
  if (test_case.expected_mask & MIME_COOKED_MASK_CC_MAX_STALE) {
    REQUIRE(mime.get_cooked_cc_max_stale() == test_case.expected_max_stale);
  }
  if (test_case.expected_mask & MIME_COOKED_MASK_CC_MIN_FRESH) {
    REQUIRE(mime.get_cooked_cc_min_fresh() == test_case.expected_min_fresh);
  }

  heap->destroy();
}

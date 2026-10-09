/** @file

  Catch-based tests for Encoding.h.

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

#include <array>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <cstdio>
#include <string_view>
#include <vector>

#include <tscore/Encoding.h>

#include <catch2/generators/catch_generators_range.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace Encoding;

namespace
{
constexpr bool TS_HTML_ESCAPE_USE_ATTRIBUTE_MODE = true;

struct HtmlDestinationTestCase {
  static constexpr bool USE_NULL_DESTINATION = true;
  static constexpr bool EXPECT_SUCCESS       = true;

  std::string_view description;
  size_t           destination_size;
  bool             use_null_destination;
  bool             expect_success;
};
} // namespace

TEST_CASE("Encoding pure escapify url", "[pure_esc_url]")
{
  char input[][32] = {
    " ",
    "%",
    "% ",
    "%20",
  };
  const char *expected[] = {
    "%20",
    "%25",
    "%25%20",
    "%2520",
  };
  char output[128];
  int  output_len;

  int n = sizeof(input) / sizeof(input[0]);
  for (int i = 0; i < n; ++i) {
    Encoding::pure_escapify_url(nullptr, input[i], std::strlen(input[i]), &output_len, output, 128);
    CHECK(std::string_view(output) == expected[i]);
  }
}

TEST_CASE("Encoding escapify url without a terminator", "[esc_url_unterminated]")
{
  // The source is a counted string, not a C string, so nothing may be read past len_in.
  // Sized exactly so that a read past the end is caught by a sanitizer.
  constexpr std::string_view src{"abcdef"};

  std::vector<char> unterminated(src.begin(), src.end());

  char output[128];
  int  output_len;

  REQUIRE(Encoding::pure_escapify_url(nullptr, unterminated.data(), unterminated.size(), &output_len, output, sizeof(output)) !=
          nullptr);
  CHECK(output_len == static_cast<int>(src.size()));
  CHECK(std::string_view(output, output_len) == src);
  CHECK(output[output_len] == '\0');

  REQUIRE(Encoding::escapify_url(nullptr, unterminated.data(), unterminated.size(), &output_len, output, sizeof(output)) !=
          nullptr);
  CHECK(output_len == static_cast<int>(src.size()));
  CHECK(std::string_view(output, output_len) == src);
  CHECK(output[output_len] == '\0');
}

TEST_CASE("Encoding escapify url", "[esc_url]")
{
  char input[][32] = {
    " ",
    "%",
    "% ",
    "%20",
  };
  const char *expected[] = {
    "%20",
    "%25",
    "%25%20",
    "%20",
  };
  char output[128];
  int  output_len;

  int n = sizeof(input) / sizeof(input[0]);
  for (int i = 0; i < n; ++i) {
    Encoding::escapify_url(nullptr, input[i], std::strlen(input[i]), &output_len, output, 128);
    CHECK(std::string_view(output) == expected[i]);
  }
}

TEST_CASE("Encoding HTML escape validates destination capacity", "[html_escape]")
{
  constexpr std::string_view input{"<&"};
  constexpr std::string_view expected{"&lt;&amp;"};
  using TestCase = HtmlDestinationTestCase;

  // clang-format off
  static constexpr TestCase test_cases[] = {
    {
      "rejects an undersized destination: too small for null terminator",
      expected.size(),
      !TestCase::USE_NULL_DESTINATION,
      !TestCase::EXPECT_SUCCESS,
    },
    {
      "accepts the exact required destination size",
      expected.size() + 1,
      !TestCase::USE_NULL_DESTINATION,
      TestCase::EXPECT_SUCCESS,
    },
    {
      "rejects a null destination",
      0,
      TestCase::USE_NULL_DESTINATION,
      !TestCase::EXPECT_SUCCESS,
    },
  };
  // clang-format on

  auto test = GENERATE(from_range(test_cases));
  CAPTURE(test.description, test.destination_size, test.use_null_destination, test.expect_success);

  std::array<char, expected.size() + 1> output;
  output.fill('x');
  char  *destination   = test.use_null_destination ? nullptr : output.data();
  size_t output_length = input.size();

  CHECK(Encoding::html_escape(input, destination, test.destination_size, &output_length, !TS_HTML_ESCAPE_USE_ATTRIBUTE_MODE) ==
        test.expect_success);
  if (test.expect_success) {
    CHECK(output_length == expected.size());
    CHECK(std::string_view{output.data(), output_length} == expected);
  } else {
    CHECK(output_length == 0);
    CHECK(output.front() == 'x');
  }
}

TEST_CASE("Encoding HTML escape", "[html_escape]")
{
  struct TestCase {
    std::string_view description;
    std::string_view input;
    std::string_view expected;
    bool             use_attribute_mode;
  };

  // clang-format off
  static constexpr TestCase test_cases[] = {
    {
      "escapes every ampersand",
      "Fish & chips & salsa",
      "Fish &amp; chips &amp; salsa",
      !TS_HTML_ESCAPE_USE_ATTRIBUTE_MODE,
    },
    {
      "escapes every no-break space",
      "one\xC2\xA0two\xC2\xA0three",
      "one&nbsp;two&nbsp;three",
      !TS_HTML_ESCAPE_USE_ATTRIBUTE_MODE,
    },
    {
      "escapes every less-than sign",
      "<<tag",
      "&lt;&lt;tag",
      !TS_HTML_ESCAPE_USE_ATTRIBUTE_MODE,
    },
    {
      "escapes every greater-than sign",
      "tag>>",
      "tag&gt;&gt;",
      !TS_HTML_ESCAPE_USE_ATTRIBUTE_MODE,
    },
    {
      "preserves double quotes outside attribute mode",
      "\"one\" \"two\"",
      "\"one\" \"two\"",
      !TS_HTML_ESCAPE_USE_ATTRIBUTE_MODE,
    },
    {
      "escapes every double quote in attribute mode",
      "\"one\" \"two\"",
      "&quot;one&quot; &quot;two&quot;",
      TS_HTML_ESCAPE_USE_ATTRIBUTE_MODE,
    },
    {
      "preserves apostrophes",
      "it's 'quoted'",
      "it's 'quoted'",
      !TS_HTML_ESCAPE_USE_ATTRIBUTE_MODE,
    },
    {
      "does not re-escape generated references",
      "&lt;\xC2\xA0",
      "&amp;lt;&nbsp;",
      !TS_HTML_ESCAPE_USE_ATTRIBUTE_MODE,
    },
    {
      "preserves other UTF-8 and incomplete sequences",
      "caf\xC3\xA9\xC2",
      "caf\xC3\xA9\xC2",
      !TS_HTML_ESCAPE_USE_ATTRIBUTE_MODE,
    },
  };
  // clang-format on

  auto test = GENERATE(from_range(test_cases));
  CAPTURE(test.description, test.input, test.expected, test.use_attribute_mode);

  std::vector<char> output;
  size_t            output_length = 0;

  output.resize(test.expected.size() + 1);

  REQUIRE(Encoding::html_escape(test.input, output.data(), output.size(), &output_length, test.use_attribute_mode));
  CHECK(output_length == test.expected.size());
  CHECK(std::string_view{output.data(), output_length} == test.expected);
  CHECK(output[output_length] == '\0');
}

TEST_CASE("Encoding HTML unescape validates destination capacity", "[html_unescape]")
{
  constexpr std::string_view input{"&lt;&amp;"};
  constexpr std::string_view expected{"<&"};
  using TestCase = HtmlDestinationTestCase;

  // clang-format off
  static constexpr TestCase test_cases[] = {
    {
      "rejects an undersized destination: too small for null terminator",
      expected.size(),
      !TestCase::USE_NULL_DESTINATION,
      !TestCase::EXPECT_SUCCESS,
    },
    {
      "accepts the exact required destination size",
      expected.size() + 1,
      !TestCase::USE_NULL_DESTINATION,
      TestCase::EXPECT_SUCCESS,
    },
    {
      "rejects a null destination",
      0,
      TestCase::USE_NULL_DESTINATION,
      !TestCase::EXPECT_SUCCESS,
    },
  };
  // clang-format on

  auto test = GENERATE(from_range(test_cases));
  CAPTURE(test.description, test.destination_size, test.use_null_destination, test.expect_success);

  std::array<char, expected.size() + 1> output;
  output.fill('x');
  char  *destination   = test.use_null_destination ? nullptr : output.data();
  size_t output_length = input.size();

  CHECK(Encoding::html_unescape(input, destination, test.destination_size, &output_length) == test.expect_success);
  if (test.expect_success) {
    CHECK(output_length == expected.size());
    CHECK(std::string_view{output.data(), output_length} == expected);
  } else {
    CHECK(output_length == 0);
    CHECK(output.front() == 'x');
  }
}

TEST_CASE("Encoding HTML unescape", "[html_unescape]")
{
  struct TestCase {
    std::string_view description;
    std::string_view input;
    std::string_view expected;
  };

  // clang-format off
  static constexpr TestCase test_cases[] = {
    {
      "unescapes every ampersand reference",
      "Fish &amp; chips &amp; salsa",
      "Fish & chips & salsa",
    },
    {
      "unescapes every no-break space reference",
      "one&nbsp;two&nbsp;three",
      "one\xC2\xA0two\xC2\xA0three",
    },
    {
      "unescapes every less-than reference",
      "&lt;&lt;tag",
      "<<tag",
    },
    {
      "unescapes every greater-than reference",
      "tag&gt;&gt;",
      "tag>>",
    },
    {
      "unescapes every double-quote reference",
      "&quot;one&quot; &quot;two&quot;",
      "\"one\" \"two\"",
    },
    {
      "preserves apostrophes",
      "it's 'quoted'",
      "it's 'quoted'",
    },
    {
      "unescapes one layer of nested references",
      "&amp;lt;&nbsp;",
      "&lt;\xC2\xA0",
    },
    {
      "preserves unknown and incomplete references",
      "&copy; &amp &AMP; &bogus;",
      "&copy; &amp &AMP; &bogus;",
    },
    {
      "preserves other UTF-8 and incomplete sequences",
      "caf\xC3\xA9\xC2",
      "caf\xC3\xA9\xC2",
    },
  };
  // clang-format on

  auto test = GENERATE(from_range(test_cases));
  CAPTURE(test.description, test.input, test.expected);

  std::vector<char> output;
  size_t            output_length = 0;

  output.resize(test.expected.size() + 1);

  REQUIRE(Encoding::html_unescape(test.input, output.data(), output.size(), &output_length));
  CHECK(output_length == test.expected.size());
  CHECK(std::string_view{output.data(), output_length} == test.expected);
  CHECK(output[output_length] == '\0');
}

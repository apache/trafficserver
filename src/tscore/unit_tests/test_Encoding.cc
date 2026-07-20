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

#include <string_view>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <cstdio>

#include <tscore/Encoding.h>

#include <catch2/catch_test_macros.hpp>

using namespace Encoding;

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

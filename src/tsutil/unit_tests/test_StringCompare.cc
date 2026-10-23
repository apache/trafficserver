/** @file

    Unit tests for StringCompare.h.

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

#include <catch2/catch_test_macros.hpp>

#include <string_view>
#include <tsutil/StringCompare.h>

TEST_CASE("iequals", "[STE]")
{
  SECTION("equal")
  {
    REQUIRE(ts::iequals("Accept-Encoding", "Accept-Encoding"));
    REQUIRE(ts::iequals("", ""));
  }

  SECTION("differing case")
  {
    REQUIRE(ts::iequals("ACCEPT-ENCODING", "accept-encoding"));
    REQUIRE(ts::iequals("Accept-Encoding", "aCCePt-eNCoDiNG"));
  }

  SECTION("prefix is not equal")
  {
    REQUIRE_FALSE(ts::iequals("Accept", "Accept-Encoding"));
    REQUIRE_FALSE(ts::iequals("Accept-Encoding", "Accept"));
    REQUIRE_FALSE(ts::iequals("", "Accept-Encoding"));
    REQUIRE_FALSE(ts::iequals("Accept-Encoding", ""));
  }

  SECTION("suffix is not equal")
  {
    REQUIRE_FALSE(ts::iequals("Encoding", "Accept-Encoding"));
    REQUIRE_FALSE(ts::iequals("Accept-Encoding", "Encoding"));
  }

  SECTION("same length, differing content")
  {
    REQUIRE_FALSE(ts::iequals("gzip", "zstd"));
    REQUIRE_FALSE(ts::iequals("Accept-Encoding", "Accept-Language"));
  }

  SECTION("length is honored over NUL termination")
  {
    // Callers build views from a pointer and a length; the terminator must not decide the result.
    static constexpr char raw[] = "Accept-Encoding-and-more";

    REQUIRE(ts::iequals(std::string_view{raw, 15}, "Accept-Encoding"));
    REQUIRE_FALSE(ts::iequals(std::string_view{raw, 16}, "Accept-Encoding"));
  }
}

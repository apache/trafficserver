/** @file

    Unit tests for ts_iso8601.h.

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

#include <chrono>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include "tsutil/ts_iso8601.h"

using namespace std::chrono;

namespace
{
// Formats a time given as milliseconds since the epoch, returning an empty string on failure.
std::string
format(int64_t epoch_ms)
{
  char   buf[ts::ISO8601_TIMESTAMP_LEN + 1];
  size_t n = ts::format_iso8601_utc(system_clock::time_point{milliseconds{epoch_ms}}, buf, sizeof(buf));

  return std::string(buf, n);
}
} // namespace

TEST_CASE("ISO 8601 UTC timestamps", "[iso8601]")
{
  SECTION("epoch")
  {
    REQUIRE(format(0) == "1970-01-01T00:00:00.000Z");
  }

  SECTION("milliseconds keep leading zeros")
  {
    REQUIRE(format(1'791'399'662'007) == "2026-10-07T19:01:02.007Z");
    REQUIRE(format(1'791'399'662'070) == "2026-10-07T19:01:02.070Z");
    REQUIRE(format(1'791'399'662'999) == "2026-10-07T19:01:02.999Z");
  }

  SECTION("year boundary")
  {
    REQUIRE(format(1'798'761'599'999) == "2026-12-31T23:59:59.999Z");
    REQUIRE(format(1'798'761'600'000) == "2027-01-01T00:00:00.000Z");
  }

  SECTION("before the epoch")
  {
    REQUIRE(format(-1) == "1969-12-31T23:59:59.999Z");
  }

  SECTION("years are zero padded to four digits")
  {
    REQUIRE(format(-30'627'458'703'211) == "0999-06-15T12:34:56.789Z");
  }

  SECTION("years outside 0000-9999 are rejected")
  {
    REQUIRE(format(-62'167'219'200'000) == "0000-01-01T00:00:00.000Z");
    REQUIRE(format(-62'167'219'200'001).empty());
    REQUIRE(format(253'402'300'799'999) == "9999-12-31T23:59:59.999Z");
    REQUIRE(format(253'402'300'800'000).empty());
  }

  SECTION("NUL terminated")
  {
    char buf[ts::ISO8601_TIMESTAMP_LEN + 1];

    REQUIRE(ts::format_iso8601_utc(system_clock::time_point{}, buf, sizeof(buf)) == ts::ISO8601_TIMESTAMP_LEN);
    REQUIRE(buf[ts::ISO8601_TIMESTAMP_LEN] == '\0');
  }

  SECTION("buffer too small")
  {
    char buf[ts::ISO8601_TIMESTAMP_LEN];

    REQUIRE(ts::format_iso8601_utc(system_clock::time_point{}, buf, sizeof(buf)) == 0);
  }
}

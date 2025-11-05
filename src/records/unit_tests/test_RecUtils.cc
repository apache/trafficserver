/** @file

   Catch-based tests for RecUtils.cc

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

#include "records/RecordsConfig.h"
#include "../P_RecUtils.h"
#include "test_Diags.h"

TEST_CASE("recordRangeCheck via RecordValidityCheck", "[librecords][RecUtils]")
{
  SECTION("valid ranges")
  {
    // Basic range checks
    REQUIRE(RecordValidityCheck("0", RECC_INT, "[0-1]"));
    REQUIRE(RecordValidityCheck("1", RECC_INT, "[0-1]"));
    REQUIRE(RecordValidityCheck("5", RECC_INT, "[0-10]"));
    REQUIRE(RecordValidityCheck("10", RECC_INT, "[0-10]"));

    // Larger ranges
    REQUIRE(RecordValidityCheck("100", RECC_INT, "[0-255]"));
    REQUIRE(RecordValidityCheck("255", RECC_INT, "[0-255]"));
    REQUIRE(RecordValidityCheck("1024", RECC_INT, "[1-2048]"));
  }

  SECTION("boundary conditions")
  {
    // Lower boundary
    REQUIRE(RecordValidityCheck("0", RECC_INT, "[0-100]"));
    REQUIRE(RecordValidityCheck("1", RECC_INT, "[1-100]"));

    // Upper boundary
    REQUIRE(RecordValidityCheck("100", RECC_INT, "[0-100]"));
    REQUIRE(RecordValidityCheck("99", RECC_INT, "[0-99]"));

    // Single value range
    REQUIRE(RecordValidityCheck("5", RECC_INT, "[5-5]"));
  }

  SECTION("out of range values")
  {
    // Below lower bound
    REQUIRE_FALSE(RecordValidityCheck("-1", RECC_INT, "[0-10]"));
    REQUIRE_FALSE(RecordValidityCheck("0", RECC_INT, "[1-10]"));

    // Above upper bound
    REQUIRE_FALSE(RecordValidityCheck("11", RECC_INT, "[0-10]"));
    REQUIRE_FALSE(RecordValidityCheck("256", RECC_INT, "[0-255]"));

    // Way out of bounds
    REQUIRE_FALSE(RecordValidityCheck("1000", RECC_INT, "[0-10]"));
    REQUIRE_FALSE(RecordValidityCheck("-1000", RECC_INT, "[0-10]"));
  }

  SECTION("invalid input formats")
  {
    // Non-numeric values (should fail parse_int validation)
    REQUIRE_FALSE(RecordValidityCheck("abc", RECC_INT, "[0-10]"));
    REQUIRE_FALSE(RecordValidityCheck("12abc", RECC_INT, "[0-100]"));
    REQUIRE_FALSE(RecordValidityCheck("abc12", RECC_INT, "[0-100]"));
    REQUIRE_FALSE(RecordValidityCheck("1.5", RECC_INT, "[0-10]"));

    // Empty string
    REQUIRE_FALSE(RecordValidityCheck("", RECC_INT, "[0-10]"));

    // Whitespace
    REQUIRE_FALSE(RecordValidityCheck(" 5", RECC_INT, "[0-10]"));
    REQUIRE_FALSE(RecordValidityCheck("5 ", RECC_INT, "[0-10]"));
    REQUIRE_FALSE(RecordValidityCheck(" 5 ", RECC_INT, "[0-10]"));
  }

  SECTION("negative ranges not supported")
  {
    // Patterns with negative numbers should be rejected
    // (ambiguous parsing with dash as separator vs negative sign)
    REQUIRE_FALSE(RecordValidityCheck("-5", RECC_INT, "[-10-0]"));
    REQUIRE_FALSE(RecordValidityCheck("0", RECC_INT, "[-10-10]"));
    REQUIRE_FALSE(RecordValidityCheck("-1", RECC_INT, "[-5--1]"));

    // Even if value is in range, negative pattern should fail
    REQUIRE_FALSE(RecordValidityCheck("5", RECC_INT, "[-10-20]"));
  }

  SECTION("invalid pattern formats")
  {
    // Missing brackets
    REQUIRE_FALSE(RecordValidityCheck("5", RECC_INT, "0-10"));
    REQUIRE_FALSE(RecordValidityCheck("5", RECC_INT, "[0-10"));
    REQUIRE_FALSE(RecordValidityCheck("5", RECC_INT, "0-10]"));

    // No bracket at all
    REQUIRE_FALSE(RecordValidityCheck("5", RECC_INT, "invalid"));

    // Invalid range format (no dash)
    REQUIRE_FALSE(RecordValidityCheck("5", RECC_INT, "[010]"));

    // Non-numeric range
    REQUIRE_FALSE(RecordValidityCheck("5", RECC_INT, "[a-z]"));
  }

  SECTION("edge cases from actual ATS config")
  {
    // From RecordsConfig.cc examples
    REQUIRE(RecordValidityCheck("0", RECC_INT, "[0-1]"));
    REQUIRE(RecordValidityCheck("1", RECC_INT, "[0-1]"));
    REQUIRE(RecordValidityCheck("2", RECC_INT, "[0-2]"));
    REQUIRE(RecordValidityCheck("3", RECC_INT, "[0-3]"));
    REQUIRE(RecordValidityCheck("256", RECC_INT, "[1-256]"));

    // Common boolean patterns
    REQUIRE(RecordValidityCheck("0", RECC_INT, "[0-1]"));       // false
    REQUIRE(RecordValidityCheck("1", RECC_INT, "[0-1]"));       // true
    REQUIRE_FALSE(RecordValidityCheck("2", RECC_INT, "[0-1]")); // invalid bool
  }

  SECTION("std::from_chars advantages - strict parsing")
  {
    // These would pass with atoi("123abc") -> 123
    // But fail with from_chars (correct behavior)
    REQUIRE_FALSE(RecordValidityCheck("5extra", RECC_INT, "[0-10]"));
    REQUIRE_FALSE(RecordValidityCheck("10garbage", RECC_INT, "[0-100]"));
    REQUIRE_FALSE(RecordValidityCheck("0x10", RECC_INT, "[0-100]")); // hex not supported in this context
  }

  SECTION("zero handling")
  {
    // Ensure "0" is properly distinguished from parse errors
    // (atoi returns 0 for both "0" and parse errors)
    REQUIRE(RecordValidityCheck("0", RECC_INT, "[0-10]"));
    REQUIRE_FALSE(RecordValidityCheck("invalid", RECC_INT, "[0-10]")); // Also would be 0 with atoi
  }

  SECTION("large numbers")
  {
    // Test with realistic config values
    REQUIRE(RecordValidityCheck("65535", RECC_INT, "[1-65535]")); // max uint16
    REQUIRE(RecordValidityCheck("8080", RECC_INT, "[1-65535]"));  // typical port
    REQUIRE(RecordValidityCheck("32768", RECC_INT, "[1024-65535]"));

    // Out of typical ranges
    REQUIRE_FALSE(RecordValidityCheck("65536", RECC_INT, "[1-65535]"));
    REQUIRE_FALSE(RecordValidityCheck("100000", RECC_INT, "[1-65535]"));
  }
}

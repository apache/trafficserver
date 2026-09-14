/** @file

  Catch based unit tests for connection tracking configuration.

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

#include "iocore/net/ConnectionTracker.h"

#include <catch2/catch_test_macros.hpp>

#include <array>

TEST_CASE("Connection tracker server match conversion", "[libinknet][ConnectionTracker]")
{
  auto const &converter = ConnectionTracker::SERVER_MATCH_CONV;
  auto        match     = ConnectionTracker::MATCH_IP;

  REQUIRE(converter.store_int != nullptr);
  REQUIRE(converter.load_int != nullptr);

  SECTION("valid values round trip")
  {
    static constexpr std::array valid_values{
      ConnectionTracker::MATCH_IP,
      ConnectionTracker::MATCH_PORT,
      ConnectionTracker::MATCH_HOST,
      ConnectionTracker::MATCH_BOTH,
    };

    for (auto const expected : valid_values) {
      converter.store_int(&match, static_cast<MgmtInt>(expected));

      CHECK(match == expected);
      CHECK(converter.load_int(&match) == static_cast<MgmtInt>(expected));
    }
  }

  SECTION("invalid values are clamped")
  {
    converter.store_int(&match, -1);
    CHECK(match == ConnectionTracker::MATCH_IP);

    converter.store_int(&match, 95);
    CHECK(match == ConnectionTracker::MATCH_BOTH);
  }
}

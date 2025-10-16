/** @file

  Unit tests for Parent Selection hash algorithm configuration

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

#include "proxy/ParentSelection.h"
#include <catch2/catch_test_macros.hpp>

// Helper function to test parseHashAlgorithm (normally static, exposed here for testing)
extern ParentHashAlgorithm parseHashAlgorithm(std::string_view name);

TEST_CASE("parseHashAlgorithm - Valid inputs", "[ParentSelection]")
{
  REQUIRE(parseHashAlgorithm("siphash24") == ParentHashAlgorithm::SIPHASH24);
  REQUIRE(parseHashAlgorithm("siphash13") == ParentHashAlgorithm::SIPHASH13);
  REQUIRE(parseHashAlgorithm("wyhash") == ParentHashAlgorithm::WYHASH);
}

TEST_CASE("parseHashAlgorithm - Invalid inputs fallback to default", "[ParentSelection]")
{
  REQUIRE(parseHashAlgorithm("invalid") == ParentHashAlgorithm::SIPHASH24);
  REQUIRE(parseHashAlgorithm("") == ParentHashAlgorithm::SIPHASH24);
  REQUIRE(parseHashAlgorithm("SIPHASH24") == ParentHashAlgorithm::SIPHASH24); // case sensitive
  REQUIRE(parseHashAlgorithm("xxh3") == ParentHashAlgorithm::SIPHASH24);      // not yet implemented
  REQUIRE(parseHashAlgorithm("md5") == ParentHashAlgorithm::SIPHASH24);
}

TEST_CASE("parseHashAlgorithm - Case sensitivity", "[ParentSelection]")
{
  // Should be case-sensitive - uppercase should fall back to default
  REQUIRE(parseHashAlgorithm("WYHASH") == ParentHashAlgorithm::SIPHASH24);
  REQUIRE(parseHashAlgorithm("SipHash24") == ParentHashAlgorithm::SIPHASH24);
  REQUIRE(parseHashAlgorithm("Wyhash") == ParentHashAlgorithm::SIPHASH24);
}

TEST_CASE("ParentHashAlgorithm - Backward compatibility", "[ParentSelection]")
{
  // Verify default is siphash24 for backward compatibility
  REQUIRE(parseHashAlgorithm("siphash24") == ParentHashAlgorithm::SIPHASH24);

  // Verify enum default value is SIPHASH24
  ParentHashAlgorithm default_algo = ParentHashAlgorithm::SIPHASH24;
  REQUIRE(static_cast<int>(default_algo) == 0);

  // Verify that unrecognized values fall back to siphash24
  REQUIRE(parseHashAlgorithm("unknown") == ParentHashAlgorithm::SIPHASH24);
}

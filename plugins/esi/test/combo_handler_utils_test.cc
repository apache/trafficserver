/** @file

  Unit tests for combo_handler::parse_cache_control_value().

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

#include <limits>

#include <catch2/catch_test_macros.hpp>

#include "combo_handler_utils.h"

using combo_handler::CacheControlValue;
using combo_handler::parse_cache_control_value;

TEST_CASE("parse_cache_control_value parses max-age digits")
{
  auto p = parse_cache_control_value("max-age=300");
  REQUIRE(p.has_max_age);
  REQUIRE(p.max_age == 300u);
  REQUIRE_FALSE(p.is_private);
  REQUIRE_FALSE(p.is_immutable);
}

TEST_CASE("parse_cache_control_value honors max-age=0")
{
  // max-age=0 is a valid "must revalidate" directive; the parser must
  // expose it so the caller can drive the combined response down to 0
  // rather than treating it as if the directive were absent.
  auto p = parse_cache_control_value("max-age=0");
  REQUIRE(p.has_max_age);
  REQUIRE(p.max_age == 0u);
}

TEST_CASE("parse_cache_control_value rejects max-age with no digits")
{
  // "max-age=" or "max-age=foo" must not masquerade as max-age=0. If
  // they did, callers that honor zero would force the combined
  // response to no-cache on every garbage upstream value.
  {
    auto p = parse_cache_control_value("max-age=");
    REQUIRE_FALSE(p.has_max_age);
  }
  {
    auto p = parse_cache_control_value("max-age=foo");
    REQUIRE_FALSE(p.has_max_age);
  }
  {
    auto p = parse_cache_control_value("max-age");
    REQUIRE_FALSE(p.has_max_age);
  }
}

TEST_CASE("parse_cache_control_value clamps overflow to UINT_MAX")
{
  // Pathological upstream values must not be misread as a small TTL.
  // The parser clamps overflow to UINT_MAX so the min-merge in the
  // caller never selects this object as the minimum.
  auto p = parse_cache_control_value("max-age=99999999999999999999");
  REQUIRE(p.has_max_age);
  REQUIRE(p.max_age == std::numeric_limits<unsigned>::max());
}

TEST_CASE("parse_cache_control_value tolerates whitespace around '='")
{
  auto p = parse_cache_control_value("max-age = 42");
  REQUIRE(p.has_max_age);
  REQUIRE(p.max_age == 42u);
}

TEST_CASE("parse_cache_control_value is case-insensitive on the token")
{
  {
    auto p = parse_cache_control_value("MAX-AGE=300");
    REQUIRE(p.has_max_age);
    REQUIRE(p.max_age == 300u);
  }
  {
    auto p = parse_cache_control_value("Max-Age=7");
    REQUIRE(p.has_max_age);
    REQUIRE(p.max_age == 7u);
  }
}

TEST_CASE("parse_cache_control_value recognizes private")
{
  {
    auto p = parse_cache_control_value("private");
    REQUIRE(p.is_private);
    REQUIRE_FALSE(p.has_max_age);
    REQUIRE_FALSE(p.is_immutable);
  }
  {
    auto p = parse_cache_control_value("Private");
    REQUIRE(p.is_private);
  }
  {
    auto p = parse_cache_control_value("PRIVATE");
    REQUIRE(p.is_private);
  }
}

TEST_CASE("parse_cache_control_value recognizes immutable")
{
  {
    auto p = parse_cache_control_value("immutable");
    REQUIRE(p.is_immutable);
    REQUIRE_FALSE(p.has_max_age);
    REQUIRE_FALSE(p.is_private);
  }
  {
    auto p = parse_cache_control_value("Immutable");
    REQUIRE(p.is_immutable);
  }
}

TEST_CASE("parse_cache_control_value enforces directive boundary for private")
{
  // A longer token that merely starts with "private" must not be treated as
  // the private directive. Without a boundary check, "privatee" would flip
  // the whole combined response to private.
  {
    auto p = parse_cache_control_value("privatee");
    REQUIRE_FALSE(p.is_private);
    REQUIRE_FALSE(p.has_max_age);
    REQUIRE_FALSE(p.is_immutable);
  }
  {
    auto p = parse_cache_control_value("private-cache");
    REQUIRE_FALSE(p.is_private);
  }
  // The directive may be followed by whitespace or by '=' (field-name form,
  // e.g. private="set-cookie"); both are valid boundaries.
  {
    auto p = parse_cache_control_value("private ");
    REQUIRE(p.is_private);
  }
  {
    auto p = parse_cache_control_value(R"(private="set-cookie")");
    REQUIRE(p.is_private);
  }
}

TEST_CASE("parse_cache_control_value enforces directive boundary for immutable")
{
  {
    auto p = parse_cache_control_value("immutableX");
    REQUIRE_FALSE(p.is_immutable);
    REQUIRE_FALSE(p.has_max_age);
    REQUIRE_FALSE(p.is_private);
  }
  // immutable takes no value, so '=' is not a valid boundary for it.
  {
    auto p = parse_cache_control_value("immutable=1");
    REQUIRE_FALSE(p.is_immutable);
  }
  {
    auto p = parse_cache_control_value("immutable ");
    REQUIRE(p.is_immutable);
  }
}

TEST_CASE("parse_cache_control_value ignores unrelated tokens")
{
  // Cache-Control directives the combo_handler does not aggregate
  // (no-cache, no-store, public, must-revalidate, ...) should all
  // come back with every flag false.
  auto p = parse_cache_control_value("no-cache");
  REQUIRE_FALSE(p.has_max_age);
  REQUIRE_FALSE(p.is_private);
  REQUIRE_FALSE(p.is_immutable);
}

TEST_CASE("parse_cache_control_value handles empty input")
{
  auto p = parse_cache_control_value("");
  REQUIRE_FALSE(p.has_max_age);
  REQUIRE_FALSE(p.is_private);
  REQUIRE_FALSE(p.is_immutable);
}

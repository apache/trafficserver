/** @file

  Catch-based tests for LogHeaderFallback parsing helpers.

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

#include <catch2/catch_test_macros.hpp>

#include "LogHeaderFallback.h"

using LogHeaderFallback::ParseResult;

namespace
{
ParseResult
require_parse(std::string_view symbol)
{
  std::string error;
  auto        parsed = LogHeaderFallback::parse(symbol, error);

  INFO(error);
  REQUIRE(parsed.has_value());

  return std::move(*parsed);
}
} // namespace

TEST_CASE("Header fallback detection ignores quoted separators", "[LogHeaderFallback]")
{
  REQUIRE_FALSE(LogHeaderFallback::has_fallback("{field}cqh"));
  REQUIRE(LogHeaderFallback::has_fallback("{field}cqh??{other}pqh"));
  REQUIRE(LogHeaderFallback::has_fallback("{field}cqh??\"default??value\""));
  REQUIRE_FALSE(LogHeaderFallback::has_fallback("\"default??value\""));
}

TEST_CASE("Header fallback parsing preserves containers and slices", "[LogHeaderFallback]")
{
  auto parsed = require_parse(" {x-primary-id}cqh[0:8] ?? {x-secondary-id}epqh[0:16] ");

  REQUIRE(parsed.header_fields.size() == 2);
  REQUIRE_FALSE(parsed.fallback_default.has_value());

  CHECK(parsed.header_fields[0].name == "x-primary-id");
  CHECK(parsed.header_fields[0].container == LogField::CQH);
  CHECK(parsed.header_fields[0].slice.m_enable);
  CHECK(parsed.header_fields[0].slice.m_start == 0);
  CHECK(parsed.header_fields[0].slice.m_end == 8);

  CHECK(parsed.header_fields[1].name == "x-secondary-id");
  CHECK(parsed.header_fields[1].container == LogField::EPQH);
  CHECK(parsed.header_fields[1].slice.m_enable);
  CHECK(parsed.header_fields[1].slice.m_start == 0);
  CHECK(parsed.header_fields[1].slice.m_end == 16);
}

TEST_CASE("Header fallback parsing supports quoted defaults", "[LogHeaderFallback]")
{
  auto parsed = require_parse("{x-primary-id}cqh??{x-secondary-id}cqh??\"missing\\\"id\\\\tail\"");

  REQUIRE(parsed.header_fields.size() == 2);
  REQUIRE(parsed.fallback_default.has_value());
  CHECK(parsed.fallback_default == std::optional<std::string>{"missing\"id\\tail"});
}

TEST_CASE("Header fallback parsing rejects malformed chains", "[LogHeaderFallback]")
{
  std::string error;

  auto parsed = LogHeaderFallback::parse("{x-primary-id}cqh??\"missing\"??{x-secondary-id}cqh", error);
  REQUIRE_FALSE(parsed.has_value());
  CHECK(error.find("default literal must be the final term") != std::string::npos);

  parsed = LogHeaderFallback::parse("{x-primary-id}record??{x-secondary-id}cqh", error);
  REQUIRE_FALSE(parsed.has_value());
  CHECK(error.find("record is not a supported header container") != std::string::npos);

  parsed = LogHeaderFallback::parse("{x-primary-id}cqh????{x-secondary-id}cqh", error);
  REQUIRE_FALSE(parsed.has_value());
  CHECK(error.find("empty candidate") != std::string::npos);
}

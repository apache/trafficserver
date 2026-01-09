/** @file
 *
 * Unit tests for XDebug plugin utility functions.
 *
 *  Licensed to the Apache Software Foundation (ASF) under one
 *  or more contributor license agreements.  See the NOTICE file
 *  distributed with this work for additional information
 *  regarding copyright ownership.  The ASF licenses this file
 *  to you under the Apache License, Version 2.0 (the
 *  "License"); you may not use this file except in compliance
 *  with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include "xdebug_utils.h"
#include "xdebug_types.h"
#include "xdebug_escape.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <sstream>
#include <vector>

TEST_CASE("xdebug::parse_probe_full_json_field_value basic functionality", "[xdebug][utils]")
{
  xdebug::BodyEncoding_t encoding;

  SECTION("Basic probe-full-json without suffix")
  {
    REQUIRE(xdebug::parse_probe_full_json_field_value("probe-full-json", encoding));
    REQUIRE(encoding == xdebug::BodyEncoding_t::AUTO);
  }

  SECTION("Case insensitive matching")
  {
    REQUIRE(xdebug::parse_probe_full_json_field_value("PROBE-FULL-JSON", encoding));
    REQUIRE(encoding == xdebug::BodyEncoding_t::AUTO);

    REQUIRE(xdebug::parse_probe_full_json_field_value("Probe-Full-Json", encoding));
    REQUIRE(encoding == xdebug::BodyEncoding_t::AUTO);
  }

  SECTION("With whitespace around value")
  {
    REQUIRE(xdebug::parse_probe_full_json_field_value("  probe-full-json  ", encoding));
    REQUIRE(encoding == xdebug::BodyEncoding_t::AUTO);

    REQUIRE(xdebug::parse_probe_full_json_field_value("\t\nprobe-full-json\r\n ", encoding));
    REQUIRE(encoding == xdebug::BodyEncoding_t::AUTO);
  }
}

TEST_CASE("xdebug::parse_probe_full_json_field_value with valid suffixes", "[xdebug][utils]")
{
  xdebug::BodyEncoding_t encoding;

  SECTION("hex suffix")
  {
    REQUIRE(xdebug::parse_probe_full_json_field_value("probe-full-json=hex", encoding));
    REQUIRE(encoding == xdebug::BodyEncoding_t::HEX);
  }

  SECTION("escape suffix")
  {
    REQUIRE(xdebug::parse_probe_full_json_field_value("probe-full-json=escape", encoding));
    REQUIRE(encoding == xdebug::BodyEncoding_t::ESCAPE);
  }

  SECTION("nobody suffix")
  {
    REQUIRE(xdebug::parse_probe_full_json_field_value("probe-full-json=nobody", encoding));
    REQUIRE(encoding == xdebug::BodyEncoding_t::OMIT_BODY);
  }

  SECTION("Suffixes with whitespace")
  {
    REQUIRE(xdebug::parse_probe_full_json_field_value("probe-full-json = hex", encoding));
    REQUIRE(encoding == xdebug::BodyEncoding_t::HEX);

    REQUIRE(xdebug::parse_probe_full_json_field_value("probe-full-json= escape ", encoding));
    REQUIRE(encoding == xdebug::BodyEncoding_t::ESCAPE);

    REQUIRE(xdebug::parse_probe_full_json_field_value("  probe-full-json  =  nobody  ", encoding));
    REQUIRE(encoding == xdebug::BodyEncoding_t::OMIT_BODY);
  }

  SECTION("Case insensitive suffixes")
  {
    REQUIRE(xdebug::parse_probe_full_json_field_value("probe-full-json=HEX", encoding));
    REQUIRE(encoding == xdebug::BodyEncoding_t::HEX);

    REQUIRE(xdebug::parse_probe_full_json_field_value("probe-full-json=ESCAPE", encoding));
    REQUIRE(encoding == xdebug::BodyEncoding_t::ESCAPE);

    REQUIRE(xdebug::parse_probe_full_json_field_value("probe-full-json=Nobody", encoding));
    REQUIRE(encoding == xdebug::BodyEncoding_t::OMIT_BODY);
  }
}

TEST_CASE("xdebug::parse_probe_full_json_field_value invalid cases", "[xdebug][utils]")
{
  xdebug::BodyEncoding_t encoding;

  SECTION("Not probe-full-json")
  {
    REQUIRE_FALSE(xdebug::parse_probe_full_json_field_value("probe", encoding));
    REQUIRE_FALSE(xdebug::parse_probe_full_json_field_value("full-json", encoding));
    REQUIRE_FALSE(xdebug::parse_probe_full_json_field_value("probe-json", encoding));
    REQUIRE_FALSE(xdebug::parse_probe_full_json_field_value("x-cache", encoding));
    REQUIRE_FALSE(xdebug::parse_probe_full_json_field_value("", encoding));
  }

  SECTION("Invalid suffixes")
  {
    REQUIRE_FALSE(xdebug::parse_probe_full_json_field_value("probe-full-json=invalid", encoding));
    REQUIRE_FALSE(xdebug::parse_probe_full_json_field_value("probe-full-json=base64", encoding));
    REQUIRE_FALSE(xdebug::parse_probe_full_json_field_value("probe-full-json=json", encoding));
    REQUIRE_FALSE(xdebug::parse_probe_full_json_field_value("probe-full-json=none", encoding));
  }

  SECTION("Malformed syntax")
  {
    REQUIRE_FALSE(xdebug::parse_probe_full_json_field_value("probe-full-json=", encoding));
    REQUIRE_FALSE(xdebug::parse_probe_full_json_field_value("probe-full-json==hex", encoding));
    REQUIRE_FALSE(xdebug::parse_probe_full_json_field_value("probe-full-json hex", encoding)); // Missing =
    REQUIRE_FALSE(xdebug::parse_probe_full_json_field_value("probe-full-json+hex", encoding)); // Wrong separator
  }

  SECTION("Partial matches")
  {
    REQUIRE_FALSE(xdebug::parse_probe_full_json_field_value("probe-full", encoding));
    REQUIRE_FALSE(xdebug::parse_probe_full_json_field_value("probe-full-js", encoding));
    REQUIRE_FALSE(xdebug::parse_probe_full_json_field_value("robe-full-json", encoding));
  }
}

TEST_CASE("xdebug::is_textual_content_type functionality", "[xdebug][utils]")
{
  SECTION("Text content types")
  {
    REQUIRE(xdebug::is_textual_content_type("text/html"));
    REQUIRE(xdebug::is_textual_content_type("text/plain"));
    REQUIRE(xdebug::is_textual_content_type("text/css"));
    REQUIRE(xdebug::is_textual_content_type("text/javascript"));
    REQUIRE(xdebug::is_textual_content_type("text/xml"));
  }

  SECTION("JSON content types")
  {
    REQUIRE(xdebug::is_textual_content_type("application/json"));
    REQUIRE(xdebug::is_textual_content_type("application/ld+json"));
    REQUIRE(xdebug::is_textual_content_type("application/vnd.api+json"));
  }

  SECTION("XML content types")
  {
    REQUIRE(xdebug::is_textual_content_type("application/xml"));
    REQUIRE(xdebug::is_textual_content_type("application/rss+xml"));
    REQUIRE(xdebug::is_textual_content_type("application/atom+xml"));
  }

  SECTION("Other textual types")
  {
    REQUIRE(xdebug::is_textual_content_type("application/javascript"));
    REQUIRE(xdebug::is_textual_content_type("text/csv"));
    REQUIRE(xdebug::is_textual_content_type("text/html; charset=utf-8"));
  }

  SECTION("Non-textual content types")
  {
    REQUIRE_FALSE(xdebug::is_textual_content_type("application/octet-stream"));
    REQUIRE_FALSE(xdebug::is_textual_content_type("image/jpeg"));
    REQUIRE_FALSE(xdebug::is_textual_content_type("video/mp4"));
    REQUIRE_FALSE(xdebug::is_textual_content_type("audio/mpeg"));
    REQUIRE_FALSE(xdebug::is_textual_content_type("application/pdf"));
    REQUIRE_FALSE(xdebug::is_textual_content_type("application/zip"));
  }

  SECTION("Case insensitive matching")
  {
    REQUIRE(xdebug::is_textual_content_type("TEXT/HTML"));
    REQUIRE(xdebug::is_textual_content_type("Application/JSON"));
    REQUIRE(xdebug::is_textual_content_type("Application/XML"));
  }

  SECTION("Edge cases")
  {
    REQUIRE_FALSE(xdebug::is_textual_content_type(""));
    REQUIRE_FALSE(xdebug::is_textual_content_type("invalid"));
    REQUIRE(xdebug::is_textual_content_type("contains-json-somewhere"));
    REQUIRE(xdebug::is_textual_content_type("has-xml-in-name"));
  }
}

// Helper function to process a string through EscapeCharForJson.
static std::string
escape_string(std::string_view input, bool full_json)
{
  xdebug::EscapeCharForJson escaper(full_json);
  std::stringstream         ss;
  for (char c : input) {
    ss << escaper(c);
  }
  return ss.str();
}

struct EscapeTestCase {
  std::string description;
  bool        full_json;
  std::string input;
  std::string expected;
};

TEST_CASE("xdebug::EscapeCharForJson escaping", "[xdebug][headers]")
{
  // clang-format off
  static std::vector<EscapeTestCase> const tests = {
    // Single quotes are NOT escaped in either mode.
    {"full JSON: single quotes are not escaped",
         xdebug::FULL_JSON,
         R"('self')",
         R"('self')"},

    {"full JSON: CSP header with multiple single-quoted directives",
         xdebug::FULL_JSON,
         R"(child-src blob: 'self'; connect-src 'self' 'unsafe-inline')",
         R"(child-src blob: 'self'; connect-src 'self' 'unsafe-inline')"},

    {"legacy: single quotes are not escaped",
         !xdebug::FULL_JSON,
         R"('self')",
         R"('self')"},

    {"legacy: CSP header with multiple single-quoted directives",
         !xdebug::FULL_JSON,
         R"(child-src blob: 'self'; connect-src 'self' 'unsafe-inline')",
         R"(child-src blob: 'self'; connect-src 'self' 'unsafe-inline')"},

    // Common escapes work the same in both modes.
    {"full JSON: double quotes are escaped",
         xdebug::FULL_JSON,
         R"(say "hello")",
         R"(say \"hello\")"},

    {"legacy: double quotes are escaped",
         !xdebug::FULL_JSON,
         R"(say "hello")",
         R"(say \"hello\")"},

    {"full JSON: backslashes are escaped",
         xdebug::FULL_JSON,
         R"(path\to\file)",
         R"(path\\to\\file)"},

    {"legacy: backslashes are escaped",
         !xdebug::FULL_JSON,
         R"(path\to\file)",
         R"(path\\to\\file)"},

    {"full JSON: tab characters are escaped",
         xdebug::FULL_JSON,
         "line1\tline2",
         R"(line1\tline2)"},

    {"full JSON: backspace characters are escaped",
         xdebug::FULL_JSON,
         "a\bb",
         R"(a\bb)"},

    {"full JSON: form feed characters are escaped",
         xdebug::FULL_JSON,
         "a\fb",
         R"(a\fb)"},

    {"full JSON: plain text passes through unchanged",
         xdebug::FULL_JSON,
         R"(hello world)",
         R"(hello world)"},

    {"legacy: plain text passes through unchanged",
         !xdebug::FULL_JSON,
         R"(hello world)",
         R"(hello world)"},
  };
  // clang-format on

  auto const &test = GENERATE_REF(from_range(tests));
  CAPTURE(test.description, test.full_json, test.input, test.expected);

  std::string result = escape_string(test.input, test.full_json);
  REQUIRE(result == test.expected);
}

TEST_CASE("xdebug::EscapeCharForJson backup calculation", "[xdebug][headers]")
{
  struct BackupTestCase {
    std::string description;
    bool        full_json;
    std::size_t expected_backup;
  };

  // clang-format off
  static std::vector<BackupTestCase> const tests = {
    {R"(full JSON uses "," separator (backup = 2))",
         xdebug::FULL_JSON,
         2},

    {R"(legacy uses "',\n\t'" separator (backup = 4))",
         !xdebug::FULL_JSON,
         4},
  };
  // clang-format on

  auto const &test = GENERATE_REF(from_range(tests));
  CAPTURE(test.description, test.full_json, test.expected_backup);

  REQUIRE(xdebug::EscapeCharForJson::backup(test.full_json) == test.expected_backup);
}

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
#include <catch2/catch_test_macros.hpp>

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

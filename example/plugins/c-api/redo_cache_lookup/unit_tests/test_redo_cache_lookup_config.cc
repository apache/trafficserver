/** @file

  Tests for redo_cache_lookup plugin configuration parsing.

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

#include <cstring>

#include "redo_cache_lookup_config.h"

TEST_CASE("redo_cache_lookup fallback option is copied", "[redo_cache_lookup]")
{
  char        plugin_name[]  = "redo_cache_lookup.so";
  char        fallback_opt[] = "--fallback";
  char        fallback_url[] = "http://example.test/fallback";
  const char *argv[]         = {plugin_name, fallback_opt, fallback_url};

  auto parsed = redo_cache_lookup::parse_fallback_url(3, argv);

  REQUIRE(parsed.has_value());

  std::memset(fallback_url, 'x', sizeof(fallback_url) - 1);

  REQUIRE(*parsed == "http://example.test/fallback");
}

TEST_CASE("redo_cache_lookup fallback option accepts short form", "[redo_cache_lookup]")
{
  char        plugin_name[]  = "redo_cache_lookup.so";
  char        fallback_opt[] = "-f";
  char        fallback_url[] = "http://example.test/short";
  const char *argv[]         = {plugin_name, fallback_opt, fallback_url};

  auto parsed = redo_cache_lookup::parse_fallback_url(3, argv);

  REQUIRE(parsed == "http://example.test/short");
}

TEST_CASE("redo_cache_lookup fallback option is required", "[redo_cache_lookup]")
{
  char        plugin_name[] = "redo_cache_lookup.so";
  const char *argv[]        = {plugin_name};

  auto parsed = redo_cache_lookup::parse_fallback_url(1, argv);

  REQUIRE_FALSE(parsed.has_value());
}

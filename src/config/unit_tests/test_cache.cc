/** @file

  Unit tests for cache configuration parsing and marshalling.

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

#include "config/cache.h"

#include <catch2/catch_test_macros.hpp>

using namespace config;

namespace
{

constexpr char LEGACY_CONFIG[] = R"(
# Specific rules precede general rules.
dest_domain=example.com suffix=php action=never-cache
dest_domain=example.com scheme=https revalidate=6h cache-responses-to-cookies=0
url_regex="^https?://example.com/assets/" time=08:00-14:00 action=ignore-no-cache
)";

constexpr char YAML_CONFIG[] = R"(
cache:
  - match:
      dest_domain: example.com
      suffix: php
    action:
      cache: never
  - match:
      dest_domain: example.com
      scheme: https
    action:
      revalidate: 6h
      cache_responses_to_cookies: 0
  - action:
      ignore_client_no_cache: true
)";

} // namespace

TEST_CASE("CacheConfigParser parses legacy cache.config", "[cache][config][legacy]")
{
  CacheConfigParser parser;
  auto              result = parser.parse_content(LEGACY_CONFIG, "cache.config");

  REQUIRE(result.ok());
  REQUIRE(result.value.size() == 3);

  CHECK(result.value[0].match.dest_domain == "example.com");
  CHECK(result.value[0].match.suffix == "php");
  REQUIRE(result.value[0].action.cache);
  CHECK(*result.value[0].action.cache == CacheMode::NEVER);

  CHECK(result.value[1].match.scheme == "https");
  CHECK(result.value[1].action.revalidate == "6h");
  CHECK(result.value[1].action.cache_responses_to_cookies == 0);

  CHECK(result.value[2].match.url_regex == "^https?://example.com/assets/");
  CHECK(result.value[2].match.time == "08:00-14:00");
  CHECK(result.value[2].action.ignore_no_cache == true);
}

TEST_CASE("CacheConfigParser parses cache.yaml", "[cache][config][yaml]")
{
  CacheConfigParser parser;
  auto              result = parser.parse_content(YAML_CONFIG, "cache.yaml");

  REQUIRE(result.ok());
  REQUIRE(result.value.size() == 3);

  CHECK(result.value[0].match.dest_domain == "example.com");
  REQUIRE(result.value[0].action.cache);
  CHECK(*result.value[0].action.cache == CacheMode::NEVER);
  CHECK(result.value[1].action.revalidate == "6h");
  CHECK(result.value[1].action.cache_responses_to_cookies == 0);
  CHECK(result.value[2].match.empty());
  CHECK(result.value[2].action.ignore_client_no_cache == true);
}

TEST_CASE("CacheConfigParser converts all legacy fields", "[cache][config][legacy]")
{
  CacheConfigParser parser;
  auto              result =
    parser.parse_content("DEST_HOST=example.com port=443 scheme=https prefix=/assets suffix=js method=GET time=08:00-14:00 "
                         "src_ip=192.0.2.1 iport=8443 tag=internal internal=FALSE action=STANDARD-CACHE\n"
                         "dest_ip=192.0.2.2 action=ignore-client-no-cache\n"
                         "host_regex=.*[.]example[.]com pin-in-cache=15m\n"
                         "dest_domain=example.net ttl-in-cache=1d2h\n"
                         "url_regex=example[.]org action=ignore-server-no-cache\n",
                         "cache.config");

  REQUIRE(result.ok());
  REQUIRE(result.value.size() == 5);

  CacheRule const &full = result.value[0];
  CHECK(full.match.dest_host == "example.com");
  CHECK(full.match.port == "443");
  CHECK(full.match.scheme == "https");
  CHECK(full.match.prefix == "/assets");
  CHECK(full.match.suffix == "js");
  CHECK(full.match.method == "GET");
  CHECK(full.match.time == "08:00-14:00");
  CHECK(full.match.src_ip == "192.0.2.1");
  CHECK(full.match.incoming_port == "8443");
  CHECK(full.match.tag == "internal");
  CHECK(full.match.internal == false);
  CHECK(full.action.cache == CacheMode::STANDARD);
  CHECK(result.value[1].action.ignore_client_no_cache == true);
  CHECK(result.value[2].action.pin_in_cache == "15m");
  CHECK(result.value[3].action.ttl_in_cache == "1d2h");
  CHECK(result.value[4].action.ignore_server_no_cache == true);
}

TEST_CASE("CacheConfigMarshaller round trips legacy rules through YAML", "[cache][config][marshaller]")
{
  CacheConfigParser     parser;
  CacheConfigMarshaller marshaller;
  auto                  legacy = parser.parse_content(LEGACY_CONFIG, "cache.config");

  REQUIRE(legacy.ok());

  std::string const yaml = marshaller.to_yaml(legacy.value);
  CHECK(yaml.find("cache:") != std::string::npos);
  CHECK(yaml.find("cache: never") != std::string::npos);
  CHECK(yaml.find("revalidate: 6h") != std::string::npos);
  CHECK(yaml.find("cache_responses_to_cookies: 0") != std::string::npos);
  CHECK(yaml.find("ignore_no_cache: true") != std::string::npos);

  auto reparsed = parser.parse_content(yaml, "cache.yaml");
  REQUIRE(reparsed.ok());
  REQUIRE(reparsed.value.size() == legacy.value.size());
  CHECK(reparsed.value[0].match.dest_domain == legacy.value[0].match.dest_domain);
  CHECK(reparsed.value[1].action.revalidate == legacy.value[1].action.revalidate);
  CHECK(reparsed.value[2].match.url_regex == legacy.value[2].match.url_regex);
}

TEST_CASE("CacheConfigParser permits empty and match-all YAML", "[cache][config][edge]")
{
  CacheConfigParser parser;

  auto empty = parser.parse_content("", "cache.yaml");
  REQUIRE(empty.ok());
  CHECK(empty.value.empty());

  auto match_all = parser.parse_content(R"(
cache:
  - action:
      revalidate: 30m
)",
                                        "cache.yaml");
  REQUIRE(match_all.ok());
  REQUIRE(match_all.value.size() == 1);
  CHECK(match_all.value[0].match.empty());
}

TEST_CASE("CacheConfigParser rejects invalid rules", "[cache][config][edge]")
{
  CacheConfigParser parser;

  auto multiple_matches = parser.parse_content(R"(
cache:
  - match:
      dest_domain: example.com
      url_regex: example
    action:
      cache: never
)",
                                               "cache.yaml");
  CHECK_FALSE(multiple_matches.ok());

  auto conflicting_actions = parser.parse_content(R"(
cache:
  - match:
      dest_domain: example.com
    action:
      cache: never
      ttl_in_cache: 1h
)",
                                                  "cache.yaml");
  CHECK_FALSE(conflicting_actions.ok());

  auto ineffective_action = parser.parse_content(R"(
cache:
  - action:
      ignore_no_cache: false
)",
                                                 "cache.yaml");
  CHECK_FALSE(ineffective_action.ok());

  auto duplicate_action = parser.parse_content(R"(
cache:
  - match:
      dest_domain: example.com
    action:
      cache: never
      cache: standard
)",
                                               "cache.yaml");
  CHECK_FALSE(duplicate_action.ok());

  auto invalid_legacy = parser.parse_content("dest_domain=example.com action=bogus\n", "cache.config");
  CHECK_FALSE(invalid_legacy.ok());
}

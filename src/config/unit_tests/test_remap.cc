/** @file

  Unit tests for shared remap configuration parsing and marshalling.

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

#include "config/remap.h"

#include <filesystem>
#include <fstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

using namespace config;

namespace
{

class TempFile
{
public:
  TempFile(std::string const &filename, std::string const &content)
  {
    _path = std::filesystem::temp_directory_path() / filename;
    std::ofstream ofs(_path);
    ofs << content;
  }

  ~TempFile() { std::filesystem::remove(_path); }

  std::string
  path() const
  {
    return _path.string();
  }

private:
  std::filesystem::path _path;
};

ConfigResult<RemapConfig>
parse_file(std::string const &content, std::string const &filename)
{
  TempFile    file(filename, content);
  RemapParser parser;
  return parser.parse(file.path());
}

} // namespace

TEST_CASE("RemapParser converts legacy remap.config directives and rule options", "[remap][legacy][parser]")
{
  static constexpr char LEGACY_CONFIG[] = R"REMAP(
.definefilter deny_write @method=PUT @method=DELETE @action=deny @src_ip=10.0.0.0/8
.activatefilter deny_write
map http://example.com// http://origin.example.com edge-tag @plugin=conf_remap.so @pparam=proxy.config.foo=1 @plugin=hdr.so @pparam=a @pparam=b @strategy=my_strategy @mapid=42 @volume=3,4 @method=GET @src_ip=192.0.2.10 @src_ip=~198.51.100.0/24 @internal
map_with_referer http://refer.example.com http://origin.example.com http://deny.example.com (.*[.])?allowed[.]com ~evil[.]example
.include remap-extra.yaml
)REMAP";

  auto result = parse_file(LEGACY_CONFIG, "remap.config");

  REQUIRE(result.ok());
  REQUIRE(result.value["remap"]);
  REQUIRE(result.value["remap"].IsSequence());
  REQUIRE(result.value["remap"].size() == 5);

  SECTION("Named filter directive")
  {
    auto filter = result.value["remap"][0]["define_filter"]["deny_write"];
    REQUIRE(filter);
    CHECK(filter["method"].IsSequence());
    CHECK(filter["method"][0].as<std::string>() == "PUT");
    CHECK(filter["method"][1].as<std::string>() == "DELETE");
    CHECK(filter["action"].as<std::string>() == "deny");
    CHECK(filter["src_ip"].as<std::string>() == "10.0.0.0/8");
  }

  SECTION("Main mapping rule")
  {
    auto rule = result.value["remap"][2];
    REQUIRE(rule);
    CHECK(rule["type"].as<std::string>() == "map");
    CHECK(rule["unique"].as<bool>() == true);
    CHECK(rule["from"]["url"].as<std::string>() == "http://example.com");
    CHECK(rule["to"]["url"].as<std::string>() == "http://origin.example.com");
    CHECK(rule["tag"].as<std::string>() == "edge-tag");
    CHECK(rule["strategy"].as<std::string>() == "my_strategy");
    CHECK(rule["mapid"].as<unsigned int>() == 42);
    CHECK(rule["volume"].IsSequence());
    CHECK(rule["volume"][0].as<int>() == 3);
    CHECK(rule["volume"][1].as<int>() == 4);

    auto acl = rule["acl_filter"];
    REQUIRE(acl);
    CHECK(acl["method"].as<std::string>() == "GET");
    CHECK(acl["src_ip"].as<std::string>() == "192.0.2.10");
    CHECK(acl["src_ip_invert"].as<std::string>() == "198.51.100.0/24");
    CHECK(acl["internal"].as<bool>() == true);

    auto plugins = rule["plugins"];
    REQUIRE(plugins);
    REQUIRE(plugins.IsSequence());
    REQUIRE(plugins.size() == 2);
    CHECK(plugins[0]["name"].as<std::string>() == "conf_remap.so");
    CHECK(plugins[0]["params"][0].as<std::string>() == "proxy.config.foo=1");
    CHECK(plugins[1]["name"].as<std::string>() == "hdr.so");
    CHECK(plugins[1]["params"][0].as<std::string>() == "a");
    CHECK(plugins[1]["params"][1].as<std::string>() == "b");
  }

  SECTION("map_with_referer conversion")
  {
    auto rule = result.value["remap"][3];
    REQUIRE(rule["type"].as<std::string>() == "map_with_referer");
    CHECK(rule["redirect"]["url"].as<std::string>() == "http://deny.example.com");
    REQUIRE(rule["redirect"]["regex"].IsSequence());
    CHECK(rule["redirect"]["regex"][0].as<std::string>() == "(.*[.])?allowed[.]com");
    CHECK(rule["redirect"]["regex"][1].as<std::string>() == "~evil[.]example");
  }
}

TEST_CASE("RemapParser round-trips legacy remap config through YAML", "[remap][legacy][roundtrip]")
{
  static constexpr char LEGACY_CONFIG[] = R"REMAP(
map http://www.example.com http://127.0.0.1:8080 @volume=4 @mapid=7 @plugin=conf_remap.so @pparam=foo=bar
)REMAP";

  RemapParser     parser;
  RemapMarshaller marshaller;

  auto legacy = parser.parse_content(LEGACY_CONFIG, "remap.config");
  REQUIRE(legacy.ok());

  std::string yaml = marshaller.to_yaml(legacy.value);
  CHECK(yaml.find("volume: 4") != std::string::npos);
  CHECK(yaml.find("mapid: 7") != std::string::npos);
  CHECK(yaml.find("name: conf_remap.so") != std::string::npos);

  auto round_trip = parser.parse_content(yaml, "remap.yaml");
  REQUIRE(round_trip.ok());
  REQUIRE(round_trip.value["remap"].IsSequence());
  REQUIRE(round_trip.value["remap"].size() == 1);

  auto rule = round_trip.value["remap"][0];
  CHECK(rule["volume"].as<int>() == 4);
  CHECK(rule["mapid"].as<unsigned int>() == 7);
  CHECK(rule["plugins"][0]["params"][0].as<std::string>() == "foo=bar");
}

TEST_CASE("RemapParser reports errors for malformed legacy directives", "[remap][legacy][error]")
{
  static constexpr char LEGACY_CONFIG[] = R"REMAP(
.definefilter broken
)REMAP";

  auto result = parse_file(LEGACY_CONFIG, "remap.config");

  REQUIRE_FALSE(result.ok());
  REQUIRE_FALSE(result.errata.empty());
  CHECK(std::string(result.errata.front().text()).find("directive \".definefilter\" must have filter parameter(s)") !=
        std::string::npos);
}

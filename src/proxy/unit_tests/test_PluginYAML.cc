/** @file

    Unit tests for plugin.yaml parsing

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
#include <filesystem>
#include <fstream>
#include <string>

#include "proxy/Plugin.h"

namespace
{
class TempYAML
{
public:
  explicit TempYAML(const std::string &content)
  {
    _path = std::filesystem::temp_directory_path() / "test_plugin_yaml.yaml";
    std::ofstream f(_path);
    f << content;
  }

  ~TempYAML() { std::filesystem::remove(_path); }

  const char *
  path() const
  {
    return _path.c_str();
  }

private:
  std::filesystem::path _path;
};
} // namespace

TEST_CASE("parse_plugin_yaml - minimal valid config", "[plugin_yaml]")
{
  TempYAML yaml(R"(
plugins:
  - path: stats_over_http.so
)");

  auto result = parse_plugin_yaml(yaml.path());

  REQUIRE(result.ok());
  REQUIRE(result.value.size() == 1);
  CHECK(result.value[0].path == "stats_over_http.so");
  CHECK(result.value[0].enabled == true);
  CHECK(result.value[0].load_order == -1);
  CHECK(result.value[0].params.empty());
  CHECK(result.value[0].config_literal.empty());
}

TEST_CASE("parse_plugin_yaml - all fields populated", "[plugin_yaml]")
{
  TempYAML yaml(R"(
plugins:
  - path: abuse.so
    enabled: true
    load_order: 100
    params:
      - etc/trafficserver/abuse.config
      - --verbose
      - --debug
)");

  auto result = parse_plugin_yaml(yaml.path());

  REQUIRE(result.ok());
  REQUIRE(result.value.size() == 1);
  CHECK(result.value[0].path == "abuse.so");
  CHECK(result.value[0].enabled == true);
  CHECK(result.value[0].load_order == 100);
  REQUIRE(result.value[0].params.size() == 3);
  CHECK(result.value[0].params[0] == "etc/trafficserver/abuse.config");
  CHECK(result.value[0].params[1] == "--verbose");
  CHECK(result.value[0].params[2] == "--debug");
}

TEST_CASE("parse_plugin_yaml - enabled false", "[plugin_yaml]")
{
  TempYAML yaml(R"(
plugins:
  - path: debug_plugin.so
    enabled: false
  - path: stats_over_http.so
)");

  auto result = parse_plugin_yaml(yaml.path());

  REQUIRE(result.ok());
  REQUIRE(result.value.size() == 2);
  CHECK(result.value[0].enabled == false);
  CHECK(result.value[1].enabled == true);
}

TEST_CASE("parse_plugin_yaml - load_order sorting", "[plugin_yaml]")
{
  TempYAML yaml(R"(
plugins:
  - path: third.so
    load_order: 300
  - path: first.so
    load_order: 100
  - path: second.so
    load_order: 200
)");

  auto result = parse_plugin_yaml(yaml.path());

  REQUIRE(result.ok());
  REQUIRE(result.value.size() == 3);
  CHECK(result.value[0].path == "first.so");
  CHECK(result.value[1].path == "second.so");
  CHECK(result.value[2].path == "third.so");
}

TEST_CASE("parse_plugin_yaml - ordered before unordered", "[plugin_yaml]")
{
  TempYAML yaml(R"(
plugins:
  - path: unordered_first.so
  - path: unordered_second.so
  - path: ordered.so
    load_order: 50
)");

  auto result = parse_plugin_yaml(yaml.path());

  REQUIRE(result.ok());
  REQUIRE(result.value.size() == 3);
  CHECK(result.value[0].path == "ordered.so");
  CHECK(result.value[1].path == "unordered_first.so");
  CHECK(result.value[2].path == "unordered_second.so");
}

TEST_CASE("parse_plugin_yaml - stable sort preserves sequence order on ties", "[plugin_yaml]")
{
  TempYAML yaml(R"(
plugins:
  - path: b.so
    load_order: 100
  - path: a.so
    load_order: 100
  - path: c.so
    load_order: 100
)");

  auto result = parse_plugin_yaml(yaml.path());

  REQUIRE(result.ok());
  REQUIRE(result.value.size() == 3);
  CHECK(result.value[0].path == "b.so");
  CHECK(result.value[1].path == "a.so");
  CHECK(result.value[2].path == "c.so");
}

TEST_CASE("parse_plugin_yaml - inline config literal (scalar)", "[plugin_yaml]")
{
  TempYAML yaml(R"(
plugins:
  - path: header_rewrite.so
    config: |
      cond %{SEND_RESPONSE_HDR_HOOK}
         set-header X-Debug "true"
)");

  auto result = parse_plugin_yaml(yaml.path());

  REQUIRE(result.ok());
  REQUIRE(result.value.size() == 1);
  CHECK(result.value[0].path == "header_rewrite.so");
  CHECK(result.value[0].config_literal.find("set-header X-Debug") != std::string::npos);
}

TEST_CASE("parse_plugin_yaml - inline config rejects structured YAML mapping", "[plugin_yaml]")
{
  TempYAML yaml(R"(
plugins:
  - path: txn_box.so
    config:
      when: proxy-req
      do:
        - set-header:
            name: X-Forwarded-For
            value: inbound-addr-remote
)");

  auto result = parse_plugin_yaml(yaml.path());

  REQUIRE_FALSE(result.ok());
}

TEST_CASE("parse_plugin_yaml - inline config rejects structured YAML sequence", "[plugin_yaml]")
{
  TempYAML yaml(R"(
plugins:
  - path: custom.so
    config:
      - rule1
      - rule2
      - rule3
)");

  auto result = parse_plugin_yaml(yaml.path());

  REQUIRE_FALSE(result.ok());
}

TEST_CASE("parse_plugin_yaml - missing path field", "[plugin_yaml]")
{
  TempYAML yaml(R"(
plugins:
  - enabled: true
)");

  auto result = parse_plugin_yaml(yaml.path());

  REQUIRE_FALSE(result.ok());
  CHECK(std::string(result.errata.front().text()).find("missing required 'path' field") != std::string::npos);
}

TEST_CASE("parse_plugin_yaml - missing plugins key", "[plugin_yaml]")
{
  TempYAML yaml(R"(
something_else:
  - path: foo.so
)");

  auto result = parse_plugin_yaml(yaml.path());

  REQUIRE_FALSE(result.ok());
  CHECK(std::string(result.errata.front().text()).find("missing or invalid 'plugins' sequence") != std::string::npos);
}

TEST_CASE("parse_plugin_yaml - invalid YAML syntax", "[plugin_yaml]")
{
  TempYAML yaml("plugins:\n  - path: foo.so\n  bad indent here\n");

  auto result = parse_plugin_yaml(yaml.path());

  REQUIRE_FALSE(result.ok());
  CHECK(std::string(result.errata.front().text()).find("failed to parse") != std::string::npos);
}

TEST_CASE("parse_plugin_yaml - empty plugins list", "[plugin_yaml]")
{
  TempYAML yaml(R"(
plugins: []
)");

  auto result = parse_plugin_yaml(yaml.path());

  REQUIRE(result.ok());
  CHECK(result.value.empty());
}

TEST_CASE("parse_plugin_yaml - multiple plugins mixed features", "[plugin_yaml]")
{
  TempYAML yaml(R"(
plugins:
  - path: stats_over_http.so

  - path: abuse.so
    params:
      - etc/trafficserver/abuse.config

  - path: header_rewrite.so
    params:
      - etc/trafficserver/header_rewrite.config

  - path: experimental.so
    enabled: false
    params:
      - --verbose
)");

  auto result = parse_plugin_yaml(yaml.path());

  REQUIRE(result.ok());
  REQUIRE(result.value.size() == 4);

  CHECK(result.value[0].path == "stats_over_http.so");
  CHECK(result.value[0].params.empty());

  CHECK(result.value[1].path == "abuse.so");
  REQUIRE(result.value[1].params.size() == 1);
  CHECK(result.value[1].params[0] == "etc/trafficserver/abuse.config");

  CHECK(result.value[2].path == "header_rewrite.so");
  REQUIRE(result.value[2].params.size() == 1);
  CHECK(result.value[2].params[0] == "etc/trafficserver/header_rewrite.config");

  CHECK(result.value[3].path == "experimental.so");
  CHECK(result.value[3].enabled == false);
}

TEST_CASE("parse_plugin_yaml - nonexistent file", "[plugin_yaml]")
{
  auto result = parse_plugin_yaml("/tmp/nonexistent_plugin_yaml_test.yaml");

  REQUIRE_FALSE(result.ok());
  CHECK(std::string(result.errata.front().text()).find("failed to parse") != std::string::npos);
}

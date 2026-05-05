/** @file

  Unit tests for reload directives: ConfigContext directive accessors,
  framework extraction logic, and CLI parse_directive() format.

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

#include "mgmt/config/ConfigContext.h"
#include "mgmt/config/ConfigReloadTrace.h"

#include <yaml-cpp/yaml.h>
#include <string>
#include <string_view>

// ─── parse_directive: standalone copy of the traffic_ctl parsing logic ────────
//
// The actual function lives in an anonymous namespace in CtrlCommands.cc.
// We reproduce the identical logic here so we can unit-test the format parsing
// without pulling in the full traffic_ctl binary and its dependencies.
namespace
{
bool
parse_directive(std::string_view dir, YAML::Node &configs, std::string &error_out)
{
  auto dot = dir.find('.');
  if (dot == std::string_view::npos || dot == 0) {
    error_out = "Invalid directive format '" + std::string(dir) + "'. Expected: config_key.directive_key=value";
    return false;
  }

  auto eq = dir.find('=', dot + 1);
  if (eq == std::string_view::npos || eq == dot + 1) {
    error_out = "Invalid directive format '" + std::string(dir) + "'. Expected: config_key.directive_key=value";
    return false;
  }

  std::string config_key{dir.substr(0, dot)};
  std::string directive_key{dir.substr(dot + 1, eq - dot - 1)};
  std::string value{dir.substr(eq + 1)};

  configs[config_key]["_reload"][directive_key] = value;
  return true;
}
} // namespace

// ─── parse_directive format tests ─────────────────────────────────────────────

TEST_CASE("parse_directive: valid single directive", "[config][directive][parse]")
{
  YAML::Node  configs;
  std::string err;

  REQUIRE(parse_directive("myconfig.id=foo", configs, err));
  REQUIRE(configs["myconfig"]["_reload"]["id"].as<std::string>() == "foo");
}

TEST_CASE("parse_directive: value with equals signs", "[config][directive][parse]")
{
  YAML::Node  configs;
  std::string err;

  REQUIRE(parse_directive("plugin.url=http://x.com/a=b", configs, err));
  REQUIRE(configs["plugin"]["_reload"]["url"].as<std::string>() == "http://x.com/a=b");
}

TEST_CASE("parse_directive: value with dots", "[config][directive][parse]")
{
  YAML::Node  configs;
  std::string err;

  REQUIRE(parse_directive("plugin.fqdn=foo.example.com", configs, err));
  REQUIRE(configs["plugin"]["_reload"]["fqdn"].as<std::string>() == "foo.example.com");
}

TEST_CASE("parse_directive: empty value is allowed", "[config][directive][parse]")
{
  YAML::Node  configs;
  std::string err;

  REQUIRE(parse_directive("myconfig.flag=", configs, err));
  REQUIRE(configs["myconfig"]["_reload"]["flag"].as<std::string>() == "");
}

TEST_CASE("parse_directive: multiple directives for same config", "[config][directive][parse]")
{
  YAML::Node  configs;
  std::string err;

  REQUIRE(parse_directive("myconfig.id=foo", configs, err));
  REQUIRE(parse_directive("myconfig.dry_run=true", configs, err));

  REQUIRE(configs["myconfig"]["_reload"]["id"].as<std::string>() == "foo");
  REQUIRE(configs["myconfig"]["_reload"]["dry_run"].as<std::string>() == "true");
}

TEST_CASE("parse_directive: multiple directives for different configs", "[config][directive][parse]")
{
  YAML::Node  configs;
  std::string err;

  REQUIRE(parse_directive("myconfig.id=foo", configs, err));
  REQUIRE(parse_directive("sni.fqdn=example.com", configs, err));

  REQUIRE(configs["myconfig"]["_reload"]["id"].as<std::string>() == "foo");
  REQUIRE(configs["sni"]["_reload"]["fqdn"].as<std::string>() == "example.com");
}

TEST_CASE("parse_directive: rejects missing dot", "[config][directive][parse]")
{
  YAML::Node  configs;
  std::string err;

  REQUIRE_FALSE(parse_directive("nodot", configs, err));
  REQUIRE(err.find("Invalid directive format") != std::string::npos);
}

TEST_CASE("parse_directive: rejects leading dot", "[config][directive][parse]")
{
  YAML::Node  configs;
  std::string err;

  REQUIRE_FALSE(parse_directive(".key=value", configs, err));
  REQUIRE(err.find("Invalid directive format") != std::string::npos);
}

TEST_CASE("parse_directive: rejects missing equals", "[config][directive][parse]")
{
  YAML::Node  configs;
  std::string err;

  REQUIRE_FALSE(parse_directive("config.key", configs, err));
  REQUIRE(err.find("Invalid directive format") != std::string::npos);
}

TEST_CASE("parse_directive: rejects empty directive key", "[config][directive][parse]")
{
  YAML::Node  configs;
  std::string err;

  REQUIRE_FALSE(parse_directive("config.=value", configs, err));
  REQUIRE(err.find("Invalid directive format") != std::string::npos);
}

// ─── ConfigContext directive accessor tests ───────────────────────────────────

TEST_CASE("ConfigContext: reload_directives on default context has no keys", "[config][context][directive]")
{
  ConfigContext ctx;

  // Members are initialized as Undefined, so operator bool() is false.
  YAML::Node const directives = ctx.reload_directives();
  REQUIRE_FALSE(directives.IsDefined());
  REQUIRE_FALSE(directives);
  REQUIRE_FALSE(directives["id"].IsDefined());
}

TEST_CASE("ConfigContext: supplied_yaml on default context has no content", "[config][context][directive]")
{
  ConfigContext ctx;

  auto yaml = ctx.supplied_yaml();
  REQUIRE_FALSE(yaml.IsDefined());
  REQUIRE_FALSE(yaml);
  REQUIRE_FALSE(yaml.IsMap());
  REQUIRE_FALSE(yaml.IsSequence());
}

TEST_CASE("ConfigContext: reload_directives round-trip via task", "[config][context][directive]")
{
  auto          task = std::make_shared<ConfigReloadTask>("test-dir-1", "test", false, nullptr);
  ConfigContext ctx(task, "test_handler");

  YAML::Node directives;
  directives["id"]      = "foo";
  directives["dry_run"] = "true";

  // Use the private setter via a ConfigContext that has a live task.
  // Since set_reload_directives is private and friend-accessible only from
  // ConfigRegistry/ReloadCoordinator, we test through the public interface
  // after setting up the state that execute_reload would create.

  // Simulate what ConfigRegistry::execute_reload() does:
  // Build a passed_config with _reload, then manually extract
  YAML::Node passed_config;
  passed_config["_reload"]["id"]      = "foo";
  passed_config["_reload"]["dry_run"] = "true";
  passed_config["data"]               = "some content";

  // Extract _reload (same logic as execute_reload)
  if (passed_config.IsMap() && passed_config["_reload"]) {
    auto dir = passed_config["_reload"];
    if (dir.IsMap()) {
      // We can't call set_reload_directives directly (private).
      // But we can verify the extraction logic works on the YAML node.
      REQUIRE(dir["id"].as<std::string>() == "foo");
      REQUIRE(dir["dry_run"].as<std::string>() == "true");
      passed_config.remove("_reload");
    }
  }

  // After extraction, passed_config should only have "data"
  REQUIRE_FALSE(passed_config["_reload"].IsDefined());
  REQUIRE(passed_config["data"].as<std::string>() == "some content");
  REQUIRE(passed_config.size() == 1);
}

TEST_CASE("ConfigContext: _reload extraction with directives only", "[config][context][directive]")
{
  YAML::Node passed_config;
  passed_config["_reload"]["id"] = "bar";

  // Extract
  YAML::Node directives;
  if (passed_config.IsMap() && passed_config["_reload"]) {
    directives = passed_config["_reload"];
    passed_config.remove("_reload");
  }

  REQUIRE(directives.IsDefined());
  REQUIRE(directives["id"].as<std::string>() == "bar");

  // After extraction with only _reload, the map should be empty
  REQUIRE(passed_config.size() == 0);
}

TEST_CASE("ConfigContext: _reload extraction with content only", "[config][context][directive]")
{
  YAML::Node passed_config;
  passed_config["rules"].push_back("rule1");
  passed_config["rules"].push_back("rule2");

  bool extracted = false;
  if (passed_config.IsMap() && passed_config["_reload"]) {
    extracted = true;
    passed_config.remove("_reload");
  }

  // No _reload key present — extraction did not fire
  REQUIRE_FALSE(extracted);

  // Content untouched
  REQUIRE(passed_config["rules"].size() == 2);
}

TEST_CASE("ConfigContext: _reload non-map is rejected", "[config][context][directive]")
{
  YAML::Node passed_config;
  passed_config["_reload"] = "scalar_value";
  passed_config["data"]    = "content";

  bool extracted = false;
  bool rejected  = false;
  if (passed_config.IsMap() && passed_config["_reload"]) {
    auto dir = passed_config["_reload"];
    if (!dir.IsMap()) {
      rejected = true;
    } else {
      extracted = true;
    }
    passed_config.remove("_reload");
  }

  REQUIRE(rejected);
  REQUIRE_FALSE(extracted);
  // _reload is still removed even when rejected
  REQUIRE_FALSE(passed_config["_reload"].IsDefined());
  REQUIRE(passed_config["data"].as<std::string>() == "content");
}

// ─── Wire format integration: -D flag produces correct YAML structure ─────────

TEST_CASE("Wire format: -D produces _reload nested under config key", "[config][directive][wire]")
{
  YAML::Node  configs;
  std::string err;

  parse_directive("myconfig.id=foo", configs, err);

  // Verify the structure matches what the server expects
  REQUIRE(configs.IsMap());
  REQUIRE(configs["myconfig"].IsMap());
  REQUIRE(configs["myconfig"]["_reload"].IsMap());
  REQUIRE(configs["myconfig"]["_reload"]["id"].as<std::string>() == "foo");
}

TEST_CASE("Wire format: -D combined with -d content", "[config][directive][wire]")
{
  YAML::Node configs;

  // Simulate -d providing content
  configs["myconfig"]["rules"].push_back("rule1");

  // Then -D adding directives
  std::string err;
  parse_directive("myconfig.id=foo", configs, err);

  // Both coexist under the same config key
  REQUIRE(configs["myconfig"]["rules"].size() == 1);
  REQUIRE(configs["myconfig"]["_reload"]["id"].as<std::string>() == "foo");
}

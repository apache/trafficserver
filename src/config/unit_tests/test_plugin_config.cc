/** @file

  Unit tests for plugin.config parser and marshaller.

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

#include <yaml-cpp/yaml.h>

#include "config/plugin_config.h"

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

} // namespace

TEST_CASE("plugin_config parser - basic active plugins", "[plugin_config]")
{
  TempFile tf("plugin_basic.config", "stats_over_http.so _stats\nheader_rewrite.so /etc/trafficserver/rewrite.conf\n");
  auto     result = config::PluginConfigParser{}.parse(tf.path());

  REQUIRE(result.ok());
  REQUIRE(result.value.size() == 2);

  CHECK(result.value[0].path == "stats_over_http.so");
  CHECK(result.value[0].enabled == true);
  REQUIRE(result.value[0].args.size() == 1);
  CHECK(result.value[0].args[0] == "_stats");

  CHECK(result.value[1].path == "header_rewrite.so");
  CHECK(result.value[1].enabled == true);
  REQUIRE(result.value[1].args.size() == 1);
  CHECK(result.value[1].args[0] == "/etc/trafficserver/rewrite.conf");
}

TEST_CASE("plugin_config parser - commented lines become disabled", "[plugin_config]")
{
  TempFile tf("plugin_commented.config", "stats_over_http.so\n# cache_promote.so --policy=lru\nxdebug.so\n");
  auto     result = config::PluginConfigParser{}.parse(tf.path());

  REQUIRE(result.ok());
  REQUIRE(result.value.size() == 3);

  CHECK(result.value[0].path == "stats_over_http.so");
  CHECK(result.value[0].enabled == true);

  CHECK(result.value[1].path == "cache_promote.so");
  CHECK(result.value[1].enabled == false);
  REQUIRE(result.value[1].args.size() == 1);
  CHECK(result.value[1].args[0] == "--policy=lru");

  CHECK(result.value[2].path == "xdebug.so");
  CHECK(result.value[2].enabled == true);
}

TEST_CASE("plugin_config parser - pure comment lines are skipped", "[plugin_config]")
{
  TempFile tf("plugin_pure_comments.config", "# This is just a comment\n# Another comment line\nstats_over_http.so\n");
  auto     result = config::PluginConfigParser{}.parse(tf.path());

  REQUIRE(result.ok());
  REQUIRE(result.value.size() == 1);
  CHECK(result.value[0].path == "stats_over_http.so");
}

TEST_CASE("plugin_config parser - blank lines", "[plugin_config]")
{
  TempFile tf("plugin_blanks.config", "\n\nstats_over_http.so\n\nxdebug.so\n\n");
  auto     result = config::PluginConfigParser{}.parse(tf.path());

  REQUIRE(result.ok());
  REQUIRE(result.value.size() == 2);
}

TEST_CASE("plugin_config parser - quoted arguments", "[plugin_config]")
{
  TempFile tf("plugin_quoted.config", "my_plugin.so \"arg with spaces\" second_arg\n");
  auto     result = config::PluginConfigParser{}.parse(tf.path());

  REQUIRE(result.ok());
  REQUIRE(result.value.size() == 1);
  REQUIRE(result.value[0].args.size() == 2);
  CHECK(result.value[0].args[0] == "arg with spaces");
  CHECK(result.value[0].args[1] == "second_arg");
}

TEST_CASE("plugin_config parser - plugin with no args", "[plugin_config]")
{
  TempFile tf("plugin_noargs.config", "xdebug.so\n");
  auto     result = config::PluginConfigParser{}.parse(tf.path());

  REQUIRE(result.ok());
  REQUIRE(result.value.size() == 1);
  CHECK(result.value[0].path == "xdebug.so");
  CHECK(result.value[0].args.empty());
}

TEST_CASE("plugin_config parser - dollar record references preserved", "[plugin_config]")
{
  TempFile tf("plugin_dollar.config", "my_plugin.so $proxy.config.http.server_ports\n");
  auto     result = config::PluginConfigParser{}.parse(tf.path());

  REQUIRE(result.ok());
  REQUIRE(result.value.size() == 1);
  REQUIRE(result.value[0].args.size() == 1);
  CHECK(result.value[0].args[0] == "$proxy.config.http.server_ports");
}

TEST_CASE("plugin_config parser - nonexistent file", "[plugin_config]")
{
  auto result = config::PluginConfigParser{}.parse("/nonexistent/path/plugin.config");

  CHECK(!result.ok());
  CHECK(result.file_not_found == true);
}

TEST_CASE("plugin_config marshaller - basic output", "[plugin_config]")
{
  config::PluginConfigData data = {
    {"stats_over_http.so", {"_stats"},                        true },
    {"cache_promote.so",   {"--policy=lru", "--buckets=100"}, false},
    {"xdebug.so",          {},                                true },
  };

  auto yaml = config::PluginConfigMarshaller{}.to_yaml(data);

  YAML::Node root = YAML::Load(yaml);
  REQUIRE(root["plugins"]);
  REQUIRE(root["plugins"].IsSequence());
  REQUIRE(root["plugins"].size() == 3);

  CHECK(root["plugins"][0]["path"].as<std::string>() == "stats_over_http.so");
  CHECK_FALSE(root["plugins"][0]["enabled"]);

  CHECK(root["plugins"][1]["path"].as<std::string>() == "cache_promote.so");
  CHECK(root["plugins"][1]["enabled"].as<bool>() == false);
  CHECK(root["plugins"][1]["params"].size() == 2);

  CHECK(root["plugins"][2]["path"].as<std::string>() == "xdebug.so");
  CHECK_FALSE(root["plugins"][2]["params"]);
}

TEST_CASE("plugin_config round-trip parse then marshal", "[plugin_config]")
{
  TempFile tf("plugin_roundtrip.config",
              "stats_over_http.so _stats\n# cache_promote.so --policy=lru\nheader_rewrite.so rewrite.conf\n");
  auto     result = config::PluginConfigParser{}.parse(tf.path());

  REQUIRE(result.ok());

  auto yaml = config::PluginConfigMarshaller{}.to_yaml(result.value);

  YAML::Node root = YAML::Load(yaml);
  REQUIRE(root["plugins"].size() == 3);

  CHECK(root["plugins"][0]["path"].as<std::string>() == "stats_over_http.so");
  CHECK_FALSE(root["plugins"][0]["enabled"]);

  CHECK(root["plugins"][1]["path"].as<std::string>() == "cache_promote.so");
  CHECK(root["plugins"][1]["enabled"].as<bool>() == false);

  CHECK(root["plugins"][2]["path"].as<std::string>() == "header_rewrite.so");
  CHECK_FALSE(root["plugins"][2]["enabled"]);
}

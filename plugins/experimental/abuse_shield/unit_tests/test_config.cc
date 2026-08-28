/** @file

  Unit tests for abuse_shield plugin configuration and rate-limited IP policy.

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one or more contributor license
  agreements.  See the NOTICE file distributed with this work for additional information regarding
  copyright ownership.  The ASF licenses this file to you under the Apache License, Version 2.0
  (the "License"); you may not use this file except in compliance with the License.  You may obtain
  a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software distributed under the License
  is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
  or implied. See the License for the specific language governing permissions and limitations under
  the License.
*/

#include "config.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>

void
TSError(const char *fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  std::vfprintf(stderr, fmt, args);
  va_end(args);
  std::fputc('\n', stderr);
}

namespace
{

class TempConfig
{
public:
  TempConfig()
  {
    auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    dir_        = std::filesystem::temp_directory_path() / ("abuse_shield_config_test_" + std::to_string(suffix));
    std::filesystem::create_directories(dir_);
  }

  ~TempConfig()
  {
    std::error_code ec;
    std::filesystem::remove_all(dir_, ec);
  }

  std::filesystem::path
  write(const std::string &name, const std::string &content) const
  {
    auto          path = dir_ / name;
    std::ofstream file(path);
    REQUIRE(file.good());
    file << content;
    REQUIRE(file.good());
    return path;
  }

private:
  std::filesystem::path dir_;
};

std::string
yaml_path(const std::filesystem::path &path)
{
  return "\"" + path.string() + "\"";
}

} // namespace

using namespace abuse_shield;

TEST_CASE("Config parses rate-limited IP lists and selects metric-specific tiers", "[abuse_shield][config]")
{
  TempConfig files;

  auto trusted         = files.write("trusted.yaml", R"(
trusted_ips:
  - 198.51.100.8
)");
  auto rate_limited    = files.write("rate_limited.yaml", R"(
rate_limited_ips:
  - 203.0.113.0/24
)");
  auto h2_rate_limited = files.write("h2_rate_limited.yaml", R"(
rate_limited_ips:
  - 2001:db8:1::/48
)");

  std::ostringstream config_yaml;
  config_yaml << R"(
global:
  trusted_ips_file: )"
              << yaml_path(trusted) << R"(

rules:
  - name: "ordinary_req"
    filter:
      max_req_rate: 50
    action: [log, block]

  - name: "rate_limited_req"
    filter:
      max_req_rate: 500
      req_burst_multiplier: 2.0
      rate_limited_ips_file: )"
              << yaml_path(rate_limited) << R"(
    action: [log]

  - name: "ordinary_conn"
    filter:
      max_conn_rate: 10
    action: [log, block]

  - name: "rate_limited_h2"
    filter:
      max_h2_error_rate: 25
      rate_limited_ips_file: )"
              << yaml_path(h2_rate_limited) << R"(
    action: [log]

enabled: true
)";
  auto config_path = files.write("abuse_shield.yaml", config_yaml.str());

  auto config = Config::parse(config_path.string());
  REQUIRE(config);

  std::string error_msg;
  REQUIRE(config->validate(error_msg));
  REQUIRE(config->rules().size() == 4);

  swoc::IPAddr trusted_ip{"198.51.100.8"};
  swoc::IPAddr rate_limited_ip{"203.0.113.10"};
  swoc::IPAddr normal_ip{"192.0.2.10"};
  swoc::IPAddr rate_limited_h2_ip{"2001:db8:1::1"};

  CHECK(config->is_trusted(trusted_ip));
  CHECK(config->is_rate_limited_for_metric(rate_limited_ip, RateMetric::REQUEST));
  CHECK_FALSE(config->is_rate_limited_for_metric(rate_limited_ip, RateMetric::CONNECTION));
  CHECK(config->is_rate_limited_for_metric(rate_limited_h2_ip, RateMetric::H2_ERROR));

  CHECK_FALSE(config->rule_applies_to_ip(config->rules()[0], rate_limited_ip));
  CHECK(config->rule_applies_to_ip(config->rules()[1], rate_limited_ip));
  CHECK(config->rule_applies_to_ip(config->rules()[2], rate_limited_ip));

  CHECK(config->rule_applies_to_ip(config->rules()[0], normal_ip));
}

TEST_CASE("Config excludes ordinary combined rules when any used metric is rate-limited", "[abuse_shield][config]")
{
  TempConfig files;

  auto rate_limited = files.write("rate_limited.yaml", R"(
rate_limited_ips:
  - 203.0.113.0/24
)");

  std::ostringstream config_yaml;
  config_yaml << R"(
rules:
  - name: "ordinary_combined"
    filter:
      max_req_rate: 10
      max_conn_rate: 5
    action: [log, block]

  - name: "rate_limited_req"
    filter:
      max_req_rate: 100
      rate_limited_ips_file: )"
              << yaml_path(rate_limited) << R"(
    action: [log]

  - name: "ordinary_conn_only"
    filter:
      max_conn_rate: 20
    action: [log, block]

enabled: true
)";
  auto config_path = files.write("abuse_shield.yaml", config_yaml.str());

  auto config = Config::parse(config_path.string());
  REQUIRE(config);

  swoc::IPAddr rate_limited_ip{"203.0.113.10"};
  swoc::IPAddr normal_ip{"192.0.2.10"};

  CHECK_FALSE(config->rule_applies_to_ip(config->rules()[0], rate_limited_ip));
  CHECK(config->rule_applies_to_ip(config->rules()[2], rate_limited_ip));

  CHECK(config->rule_applies_to_ip(config->rules()[1], rate_limited_ip));
  CHECK(config->rule_applies_to_ip(config->rules()[0], normal_ip));
}

TEST_CASE("Config accepts trusted_ips as a legacy rate-limited IP list key", "[abuse_shield][config]")
{
  TempConfig files;

  auto rate_limited = files.write("rate_limited.yaml", R"(
trusted_ips:
  - 203.0.113.0/24
)");

  std::ostringstream config_yaml;
  config_yaml << R"(
rules:
  - name: "rate_limited_req"
    filter:
      max_req_rate: 100
      rate_limited_ips_file: )"
              << yaml_path(rate_limited) << R"(
    action: [log]

enabled: true
)";
  auto config_path = files.write("abuse_shield.yaml", config_yaml.str());

  auto config = Config::parse(config_path.string());
  REQUIRE(config);

  swoc::IPAddr rate_limited_ip{"203.0.113.10"};
  CHECK(config->is_rate_limited_for_metric(rate_limited_ip, RateMetric::REQUEST));
}

TEST_CASE("Config rejects rate-limited IP files that do not use an accepted IP list key", "[abuse_shield][config]")
{
  TempConfig files;

  auto rate_limited = files.write("rate_limited.yaml", R"(
unexpected_ips:
  - 203.0.113.0/24
)");

  std::ostringstream config_yaml;
  config_yaml << R"(
rules:
  - name: "rate_limited_req"
    filter:
      max_req_rate: 100
      rate_limited_ips_file: )"
              << yaml_path(rate_limited) << R"(
    action: [log]

enabled: true
)";
  auto config_path = files.write("abuse_shield.yaml", config_yaml.str());

  CHECK_FALSE(Config::parse(config_path.string()));
}

TEST_CASE("Config parses and canonicalizes ClientHello fingerprint filters", "[abuse_shield][config][fingerprint]")
{
  TempConfig files;
  auto       config_path = files.write("abuse_shield.yaml", R"(
global:
  fingerprint_registry: test.jax.registry

rules:
  - name: "blocked_tls_clients"
    filter:
      fingerprints:
        ja3:
          - "238BCEBDFA16AA0BE417A7F7A80063A9"
          - "99c071c5a5e14cc2527c9e8e0dde4a50"
        JA4:
          - "T13D1516H2_8DAAF6152771_02713D6AF862"
    action: [log, close]
)");

  auto config = Config::parse(config_path.string());
  REQUIRE(config);
  REQUIRE(config->rules().size() == 1);
  CHECK(config->has_fingerprint_rules());
  CHECK(config->fingerprint_registry() == "test.jax.registry");
  CHECK(config->fingerprint_methods().contains("JA3"));
  CHECK(config->fingerprint_methods().contains("JA4"));

  const auto &fingerprints = config->rules()[0].filter.fingerprints;
  CHECK(fingerprints.at("JA3").contains("238bcebdfa16aa0be417a7f7a80063a9"));
  CHECK(fingerprints.at("JA3").contains("99c071c5a5e14cc2527c9e8e0dde4a50"));
  CHECK(fingerprints.at("JA4").contains("t13d1516h2_8daaf6152771_02713d6af862"));
}

TEST_CASE("Config accepts private methods and rejects malformed fingerprints", "[abuse_shield][config][fingerprint]")
{
  TempConfig files;

  auto internal        = files.write("internal.yaml", R"(
global:
  fingerprint_registry: test.jax.registry

rules:
  - name: "internal_method"
    filter:
      fingerprints:
        JA_INTERNAL:
          - "company-specific-value"
    action: [close]
)");
  auto internal_config = Config::parse(internal.string());
  REQUIRE(internal_config);
  CHECK(internal_config->fingerprint_methods().contains("JA_INTERNAL"));
  CHECK(internal_config->rules()[0].filter.fingerprints.at("JA_INTERNAL").contains("company-specific-value"));

  auto malformed = files.write("malformed.yaml", R"(
global:
  fingerprint_registry: test.jax.registry

rules:
  - name: "bad_ja3"
    filter:
      fingerprints:
        JA3:
          - "not-a-ja3-hash"
    action: [close]
)");
  CHECK_FALSE(Config::parse(malformed.string()));

  auto empty = files.write("empty.yaml", R"(
global:
  fingerprint_registry: test.jax.registry

rules:
  - name: "empty_ja3"
    filter:
      fingerprints:
        JA3: []
    action: [close]
)");
  CHECK_FALSE(Config::parse(empty.string()));
}

TEST_CASE("Config requires a registry for fingerprint rules", "[abuse_shield][config][fingerprint]")
{
  TempConfig files;
  auto       config_path = files.write("abuse_shield.yaml", R"(
rules:
  - name: "blocked_tls_clients"
    filter:
      fingerprints:
        JA3:
          - "238bcebdfa16aa0be417a7f7a80063a9"
    action: [close]
)");

  auto config = Config::parse(config_path.string());
  REQUIRE(config);

  std::string error_msg;
  CHECK_FALSE(config->validate(error_msg));
  CHECK(error_msg == "global.fingerprint_registry is required when fingerprint rules are configured");
}

TEST_CASE("Config rejects malformed security settings", "[abuse_shield][config]")
{
  TempConfig files;

  auto scalar_action = files.write("scalar_action.yaml", R"(
rules:
  - name: bad_action
    filter:
      max_req_rate: 10
    action: block
)");
  CHECK_FALSE(Config::parse(scalar_action.string()));

  auto unknown_action = files.write("unknown_action.yaml", R"(
rules:
  - name: bad_action
    filter:
      max_req_rate: 10
    action: [downgrade]
)");
  CHECK_FALSE(Config::parse(unknown_action.string()));

  auto invalid_type = files.write("invalid_type.yaml", R"(
global:
  ip_tracking:
    slots: not-a-number
rules:
  - name: request_rate
    filter:
      max_req_rate: 10
    action: [log]
)");
  CHECK_FALSE(Config::parse(invalid_type.string()));

  auto invalid_range = files.write("invalid_range.yaml", R"(
global:
  ip_tracking:
    slots: 0
  blocking:
    duration_seconds: -1
  log_interval_sec: -1
rules:
  - name: request_rate
    filter:
      max_req_rate: 10
    action: [log]
)");
  auto config        = Config::parse(invalid_range.string());
  REQUIRE(config);
  std::string error;
  CHECK_FALSE(config->validate(error));
}

TEST_CASE("Config converts large block durations without overflow", "[abuse_shield][config][blocking]")
{
  TempConfig files;
  auto       config_path = files.write("large-duration.yaml", R"(
global:
  blocking:
    duration_seconds: 2147483647
rules:
  - name: request_rate
    filter:
      max_req_rate: 10
    action: [block]
)");

  auto config = Config::parse(config_path.string());
  REQUIRE(config);
  CHECK(config->block_duration_ms() == 2147483647000ULL);
}

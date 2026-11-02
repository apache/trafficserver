/** @file

  Unit tests for the NextHopStrategyFactory.

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

  @section details Details

  Unit testing the NextHopStrategy factory.

 */

#include <catch2/catch_test_macros.hpp> /* catch unit-test framework */
#include <catch2/generators/catch_generators.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream> /* ofstream */
#include <memory>
#include <stdexcept>
#include <unistd.h>
#include <utime.h>
#include <yaml-cpp/yaml.h>

#include "records/RecCore.h"
#include "tscore/ink_platform.h"
#include "tscore/Layout.h"
#include "proxy/http/remap/NextHopSelectionStrategy.h"
#include "proxy/http/remap/NextHopStrategyFactory.h"
#include "proxy/http/remap/NextHopConsistentHash.h"

namespace
{
std::filesystem::path
make_temp_dir(std::string const &name)
{
  const auto temp_dir = std::filesystem::temp_directory_path();
  const auto prefix   = name + "-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());

  for (unsigned int attempt = 0; attempt < 100; ++attempt) {
    auto dir = temp_dir / (prefix + "-" + std::to_string(attempt));
    if (std::filesystem::create_directory(dir)) {
      return dir;
    }
  }
  throw std::runtime_error("unable to create a temporary directory in " + temp_dir.string());
}

// Copy the strategy.yaml fixture into a new temporary directory, with its '#include' replaced by include_target.
std::filesystem::path
write_strategy_with_include(std::string const &include_target)
{
  const auto    dir = make_temp_dir("nh_strategy_include_test");
  std::ofstream out(dir / "strategy.yaml");
  std::ifstream in(TS_SRC_DIR "/strategy.yaml");

  if (!in || !out) {
    throw std::runtime_error("unable to copy " TS_SRC_DIR "/strategy.yaml into " + dir.string());
  }
  out << "#include " << include_target << "\n";
  for (std::string line; std::getline(in, line);) {
    if (!line.starts_with("#include")) {
      out << line << "\n";
    }
  }
  return dir;
}
} // namespace

// '#include' directives in strategy files resolve against the configuration
// directory, so point it at a temporary directory outside the install prefix
// containing the test fixtures. The install prefix holds copies for the legacy
// prefix-relative fallback, and an invalid decoy at <prefix>/hosts.yaml that
// must lose to the configuration directory's hosts.yaml.
static std::filesystem::path config_root;
static std::filesystem::path config_dir;

struct ConfigDirListener : Catch::EventListenerBase {
  using EventListenerBase::EventListenerBase;

  void
  testRunStarting(Catch::TestRunInfo const & /* testRunInfo ATS_UNUSED */) override
  {
    config_root = make_temp_dir("nh_strategy_prefix_test");
    config_dir  = make_temp_dir("nh_strategy_config_test");
    setenv("PROXY_CONFIG_CONFIG_DIR", config_dir.c_str(), 1);
    Layout::create(config_root.string());
    if (Layout::get()->prefix != config_root.string()) {
      throw std::runtime_error("the install prefix is " + Layout::get()->prefix + ", not " + config_root.string());
    }
    if (std::filesystem::path(RecConfigReadConfigDir()) != config_dir) {
      throw std::runtime_error("the configuration directory is " + RecConfigReadConfigDir() + ", not " + config_dir.string());
    }

    std::filesystem::copy_file(TS_SRC_DIR "/hosts.yaml", config_dir / "hosts.yaml");
    std::filesystem::create_directories(config_root / "etc" / "trafficserver");
    std::filesystem::copy_file(TS_SRC_DIR "/hosts.yaml", config_root / "etc" / "trafficserver" / "hosts.yaml");
    std::filesystem::create_directories(config_root / "var");
    std::filesystem::copy_file(TS_SRC_DIR "/hosts.yaml", config_root / "var" / "hosts.yaml");
    std::ofstream(config_dir / "lib") << "# not a directory\n";
    std::filesystem::create_directories(config_root / "lib");
    std::filesystem::copy_file(TS_SRC_DIR "/hosts.yaml", config_root / "lib" / "hosts.yaml");
    std::filesystem::create_directories(config_dir / "locked");
    std::filesystem::copy_file(TS_SRC_DIR "/hosts.yaml", config_dir / "locked" / "hosts.yaml");
    std::filesystem::create_directories(config_root / "locked");
    std::filesystem::copy_file(TS_SRC_DIR "/hosts.yaml", config_root / "locked" / "hosts.yaml");
    std::ofstream(config_root / "hosts.yaml") << "# decoy: must never be included\nhosts: [\n";
  }

  void
  testRunEnded(Catch::TestRunStats const & /* testRunStats ATS_UNUSED */) override
  {
    unsetenv("PROXY_CONFIG_CONFIG_DIR");
    std::filesystem::remove_all(config_dir);
    std::filesystem::remove_all(config_root);
  }
};

CATCH_REGISTER_LISTENER(ConfigDirListener);

SCENARIO("factory resolves '#include' paths from a strategy outside the configuration directory", "[loadConfig]")
{
  // hosts.yaml is never next to the strategy. A bare 'hosts.yaml' must load the configuration directory's copy,
  // not the invalid decoy at <prefix>/hosts.yaml. The other relative targets exist only under the prefix; for
  // 'lib/hosts.yaml', <config>/lib is a regular file, so the configuration-directory lookup fails with ENOTDIR.
  const std::string include_target =
    GENERATE(std::string{TS_SRC_DIR "/hosts.yaml"}, std::string{"hosts.yaml"}, std::string{"etc/trafficserver/hosts.yaml"},
             std::string{"./etc/trafficserver/hosts.yaml"}, std::string{"var/hosts.yaml"}, std::string{"lib/hosts.yaml"});
  CAPTURE(include_target);

  const auto             dir = write_strategy_with_include(include_target);
  NextHopStrategyFactory nhf((dir / "strategy.yaml").c_str());
  std::filesystem::remove_all(dir);

  REQUIRE(nhf.strategies_loaded == true);
  REQUIRE(nhf.strategyInstance("strategy-1") != nullptr);
  REQUIRE(nhf.strategyInstance("strategy-2") != nullptr);
}

SCENARIO("factory does not fall back to the install prefix for an include it cannot access", "[loadConfig]")
{
  if (geteuid() == 0) {
    SKIP("root bypasses the directory permissions this scenario relies on");
  }

  // <config>/locked/hosts.yaml exists but its directory cannot be searched, so the lookup fails with EACCES. The
  // load must fail rather than silently use <prefix>/locked/hosts.yaml.
  const auto locked = config_dir / "locked";
  const auto dir    = write_strategy_with_include("locked/hosts.yaml");

  std::filesystem::permissions(locked, std::filesystem::perms::none);
  NextHopStrategyFactory nhf((dir / "strategy.yaml").c_str());
  std::filesystem::permissions(locked, std::filesystem::perms::owner_all);
  std::filesystem::remove_all(dir);

  REQUIRE(nhf.strategies_loaded == false);
  REQUIRE(nhf.strategyInstance("strategy-1") == nullptr);
}

SCENARIO("factory tests loading yaml configs", "[loadConfig]")
{
  GIVEN("Loading a strategies.yaml with a bare '#include' directive.")
  {
    REQUIRE_NOTHROW(YAML::LoadFile(TS_SRC_DIR "/include-missing-strategy.yaml"));
    NextHopStrategyFactory nhf(TS_SRC_DIR "/include-missing-strategy.yaml");

    THEN("the bare directive is rejected and no strategies load")
    {
      REQUIRE(nhf.strategies_loaded == false);
      REQUIRE(nhf.strategyInstance("strategy-1") == nullptr);
    }
  }

  GIVEN("Loading a strategies.yaml whose '#include' names a missing file.")
  {
    REQUIRE_NOTHROW(YAML::LoadFile(TS_SRC_DIR "/include-missing-file-strategy.yaml"));
    NextHopStrategyFactory nhf(TS_SRC_DIR "/include-missing-file-strategy.yaml");

    THEN("the nested load error is reported and no strategies load")
    {
      REQUIRE(nhf.strategies_loaded == false);
      REQUIRE(nhf.strategyInstance("strategy-1") == nullptr);
    }
  }

  GIVEN("Loading a strategies.yaml whose '#include' path exceeds the path length limit.")
  {
    const auto strategy_file = config_root / "long-include-strategy.yaml";
    {
      std::ofstream out(strategy_file);
      std::ifstream in(TS_SRC_DIR "/strategy.yaml");

      out << "#include " << std::string(PATH_NAME_MAX, 'a') << "\n";
      for (std::string line; std::getline(in, line);) {
        if (line.rfind("#include", 0) != 0) {
          out << line << "\n";
        }
      }
    }
    NextHopStrategyFactory nhf(strategy_file.c_str());

    THEN("the include is rejected as a load error instead of aborting, and no strategies load")
    {
      REQUIRE(nhf.strategies_loaded == false);
      REQUIRE(nhf.strategyInstance("strategy-1") == nullptr);
    }
  }

  GIVEN("Loading the strategy.yaml with included 'hosts.yaml'.")
  {
    NextHopStrategyFactory nhf(TS_SRC_DIR "/strategy.yaml");

    WHEN("the two files are loaded.")
    {
      THEN("there are two strategies defined in the config, 'strategy-1' and 'strategy-2'")
      {
        REQUIRE(nhf.strategies_loaded == true);
        REQUIRE(nhf.strategyInstance("strategy-1") != nullptr);
        REQUIRE(nhf.strategyInstance("strategy-2") != nullptr);
        REQUIRE(nhf.strategyInstance("notthere") == nullptr);
      }
    }

    WHEN("'strategy-1' details are checked.")
    {
      THEN("Expect that these results for 'strategy-1'")
      {
        NextHopSelectionStrategy *const strategy = nhf.strategyInstance("strategy-1");
        REQUIRE(strategy != nullptr);
        CHECK(strategy->parent_is_proxy == true);
        CHECK(strategy->max_simple_retries == 1);
        CHECK(strategy->policy_type == NHPolicyType::CONSISTENT_HASH);

        // down cast here using the stored pointer so that I can verify the hash_key was set
        // properly.
        NextHopConsistentHash *const ptr = static_cast<NextHopConsistentHash *>(strategy);
        REQUIRE(ptr != nullptr);
        CHECK(ptr->hash_key == NHHashKeyType::CACHE_HASH_KEY);

        CHECK(strategy->go_direct == false);
        CHECK(strategy->scheme == NHSchemeType::HTTP);
        CHECK(strategy->ring_mode == NHRingMode::EXHAUST_RING);
        CHECK(strategy->groups == 2);
        std::shared_ptr<HostRecord> h = strategy->host_groups[0][0];
        CHECK(h != nullptr);
        for (unsigned int i = 0; i < strategy->groups; i++) {
          CHECK(strategy->host_groups[i].size() == 2);
          for (unsigned int j = 0; j < strategy->host_groups[i].size(); j++) {
            h = strategy->host_groups[i][j];
            switch (i) {
            case 0:
              switch (j) {
              case 0:
                CHECK(h->hostname == "p1.foo.com");
                CHECK(h->protocols[0]->scheme == NHSchemeType::HTTP);
                CHECK(h->protocols[0]->port == 80);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.1.1:80");
                CHECK(h->protocols[1]->scheme == NHSchemeType::HTTPS);
                CHECK(h->protocols[1]->port == 443);
                CHECK(h->protocols[1]->health_check_url == "https://192.168.1.1:443");
                CHECK(h->weight == 1.5);
                CHECK(h->available == true);
                break;
              case 1:
                CHECK(h->hostname == "p2.foo.com");
                CHECK(h->protocols[0]->scheme == NHSchemeType::HTTP);
                CHECK(h->protocols[0]->port == 80);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.1.2:80");
                CHECK(h->weight == 1.5);
                CHECK(h->available == true);
                break;
              }
              break;
            case 1:
              switch (j) {
              case 0:
                CHECK(h->hostname == "p3.foo.com");
                CHECK(h->protocols[0]->scheme == NHSchemeType::HTTP);
                CHECK(h->protocols[0]->port == 8080);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.1.3:8080");
                CHECK(h->protocols[1]->scheme == NHSchemeType::HTTPS);
                CHECK(h->protocols[1]->port == 8443);
                CHECK(h->protocols[1]->health_check_url == "https://192.168.1.3:8443");
                CHECK(h->weight == 0.5);
                CHECK(h->available == true);
                break;
              case 1:
                CHECK(h->hostname == "p4.foo.com");
                CHECK(h->protocols[0]->scheme == NHSchemeType::HTTP);
                CHECK(h->protocols[0]->port == 8080);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.1.4:8080");
                CHECK(h->protocols[1]->scheme == NHSchemeType::HTTPS);
                CHECK(h->protocols[1]->port == 8443);
                CHECK(h->protocols[1]->health_check_url == "https://192.168.1.4:8443");
                CHECK(h->weight == 1.5);
                CHECK(h->available == true);
                break;
              }
              break;
            }
          }
        }
        CHECK(strategy->resp_codes.contains(404));
        CHECK(strategy->resp_codes.contains(503));
        CHECK(!strategy->resp_codes.contains(604));
      }
    }

    WHEN("'strategy-2' details are checked.")
    {
      THEN("Expect that these results for 'strategy-2'")
      {
        NextHopSelectionStrategy *const strategy = nhf.strategyInstance("strategy-2");
        REQUIRE(strategy != nullptr);
        CHECK(strategy->policy_type == NHPolicyType::RR_STRICT);
        CHECK(strategy->go_direct == true);
        CHECK(strategy->scheme == NHSchemeType::HTTP);
        CHECK(strategy->ring_mode == NHRingMode::EXHAUST_RING);
        CHECK(strategy->groups == 2);
        std::shared_ptr<HostRecord> h = strategy->host_groups[0][0];
        CHECK(h != nullptr);
        for (unsigned int i = 0; i < strategy->groups; i++) {
          CHECK(strategy->host_groups[i].size() == 2);
          for (unsigned int j = 0; j < strategy->host_groups[i].size(); j++) {
            h = strategy->host_groups[i][j];
            switch (i) {
            case 0:
              switch (j) {
              case 0:
                CHECK(h->hostname == "p1.foo.com");
                CHECK(h->protocols[0]->scheme == NHSchemeType::HTTP);
                CHECK(h->protocols[0]->port == 80);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.1.1:80");
                CHECK(h->protocols[1]->scheme == NHSchemeType::HTTPS);
                CHECK(h->protocols[1]->port == 443);
                CHECK(h->protocols[1]->health_check_url == "https://192.168.1.1:443");
                CHECK(h->weight == 1.5);
                break;
              case 1:
                CHECK(h->hostname == "p2.foo.com");
                CHECK(h->protocols[0]->scheme == NHSchemeType::HTTP);
                CHECK(h->protocols[0]->port == 80);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.1.2:80");
                CHECK(h->weight == 1.5);
                break;
              }
              break;
            case 1:
              switch (j) {
              case 0:
                CHECK(h->hostname == "p3.foo.com");
                CHECK(h->protocols[0]->scheme == NHSchemeType::HTTP);
                CHECK(h->protocols[0]->port == 8080);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.1.3:8080");
                CHECK(h->protocols[1]->scheme == NHSchemeType::HTTPS);
                CHECK(h->protocols[1]->port == 8443);
                CHECK(h->protocols[1]->health_check_url == "https://192.168.1.3:8443");
                CHECK(h->weight == 0.5);
                break;
              case 1:
                CHECK(h->hostname == "p4.foo.com");
                CHECK(h->protocols[0]->scheme == NHSchemeType::HTTP);
                CHECK(h->protocols[0]->port == 8080);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.1.4:8080");
                CHECK(h->protocols[1]->scheme == NHSchemeType::HTTPS);
                CHECK(h->protocols[1]->port == 8443);
                CHECK(h->protocols[1]->health_check_url == "https://192.168.1.4:8443");
                CHECK(h->weight == 1.5);
                break;
              }
              break;
            }
          }
        }
        CHECK(strategy->resp_codes.contains(404));
        CHECK(strategy->resp_codes.contains(503));
        CHECK(!strategy->resp_codes.contains(604));
      }
    }
  }

  GIVEN("loading a yaml config, simple-strategy.yaml ")
  {
    NextHopStrategyFactory nhf(TS_SRC_DIR "/simple-strategy.yaml");

    WHEN("loading the single file")
    {
      THEN("loading the simple-strategy.yaml")
      {
        REQUIRE(nhf.strategies_loaded == true);
        REQUIRE(nhf.strategyInstance("strategy-3") != nullptr);
        REQUIRE(nhf.strategyInstance("strategy-4") != nullptr);
      }
    }

    WHEN("'strategy-3' details are checked.")
    {
      THEN("Expect that these results for 'strategy-3'")
      {
        NextHopSelectionStrategy *const strategy = nhf.strategyInstance("strategy-3");
        REQUIRE(strategy != nullptr);
        CHECK(strategy->policy_type == NHPolicyType::RR_IP);
        CHECK(strategy->go_direct == true);
        CHECK(strategy->scheme == NHSchemeType::HTTPS);
        CHECK(strategy->ring_mode == NHRingMode::EXHAUST_RING);
        CHECK(strategy->groups == 2);
        std::shared_ptr<HostRecord> h = strategy->host_groups[0][0];
        CHECK(h != nullptr);
        for (unsigned int i = 0; i < strategy->groups; i++) {
          CHECK(strategy->host_groups[i].size() == 2);
          for (unsigned int j = 0; j < strategy->host_groups[i].size(); j++) {
            h = strategy->host_groups[i][j];
            switch (i) {
            case 0:
              switch (j) {
              case 0:
                CHECK(h->hostname == "p1.foo.com");
                CHECK(h->protocols[0]->scheme == NHSchemeType::HTTP);
                CHECK(h->protocols[0]->port == 80);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.1.1:80");
                CHECK(h->protocols[1]->scheme == NHSchemeType::HTTPS);
                CHECK(h->protocols[1]->port == 443);
                CHECK(h->protocols[1]->health_check_url == "https://192.168.1.1:443");
                CHECK(h->weight == 1.0);
                break;
              case 1:
                CHECK(h->hostname == "p2.foo.com");
                CHECK(h->protocols[0]->scheme == NHSchemeType::HTTP);
                CHECK(h->protocols[0]->port == 80);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.1.2:80");
                CHECK(h->protocols[1]->scheme == NHSchemeType::HTTPS);
                CHECK(h->protocols[1]->port == 443);
                CHECK(h->protocols[1]->health_check_url == "https://192.168.1.2:443");
                CHECK(h->weight == 1.0);
                break;
              }
              break;
            case 1:
              switch (j) {
              case 0:
                CHECK(h->hostname == "s1.bar.com");
                CHECK(h->protocols[0]->scheme == NHSchemeType::HTTP);
                CHECK(h->protocols[0]->port == 80);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.2.1:80");
                CHECK(h->protocols[1]->scheme == NHSchemeType::HTTPS);
                CHECK(h->protocols[1]->port == 443);
                CHECK(h->protocols[1]->health_check_url == "https://192.168.2.1:443");
                CHECK(h->weight == 1.0);
                break;
              case 1:
                CHECK(h->hostname == "s2.bar.com");
                CHECK(h->protocols[0]->scheme == NHSchemeType::HTTP);
                CHECK(h->protocols[0]->port == 80);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.2.2:80");
                CHECK(h->protocols[1]->scheme == NHSchemeType::HTTPS);
                CHECK(h->protocols[1]->port == 443);
                CHECK(h->protocols[1]->health_check_url == "https://192.168.2.2:443");
                CHECK(h->weight == 1.0);
                break;
              }
              break;
            }
          }
        }
        CHECK(strategy->resp_codes.contains(404));
        CHECK(strategy->resp_codes.contains(503));
        CHECK(!strategy->resp_codes.contains(604));
      }
    }

    WHEN("'strategy-4' details are checked.")
    {
      THEN("Expect that these results for 'strategy-4'")
      {
        NextHopSelectionStrategy *const strategy = nhf.strategyInstance("strategy-4");
        REQUIRE(strategy != nullptr);
        CHECK(strategy->policy_type == NHPolicyType::RR_LATCHED);
        CHECK(strategy->go_direct == true);
        CHECK(strategy->scheme == NHSchemeType::HTTP);
        CHECK(strategy->ring_mode == NHRingMode::ALTERNATE_RING);
        CHECK(strategy->groups == 1);
        std::shared_ptr<HostRecord> h = strategy->host_groups[0][0];
        CHECK(h != nullptr);
        for (unsigned int i = 0; i < strategy->groups; i++) {
          CHECK(strategy->host_groups[i].size() == 2);
          for (unsigned int j = 0; j < strategy->host_groups[i].size(); j++) {
            h = strategy->host_groups[i][j];
            switch (i) {
            case 0:
              switch (j) {
              case 0:
                CHECK(h->hostname == "p3.foo.com");
                CHECK(h->protocols[0]->scheme == NHSchemeType::HTTP);
                CHECK(h->protocols[0]->port == 80);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.1.3:80");
                CHECK(h->protocols[1]->scheme == NHSchemeType::HTTPS);
                CHECK(h->protocols[1]->port == 443);
                CHECK(h->protocols[1]->health_check_url == "https://192.168.1.3:443");
                CHECK(h->weight == 1.0);
                break;
              case 1:
                CHECK(h->hostname == "p4.foo.com");
                CHECK(h->protocols[0]->scheme == NHSchemeType::HTTP);
                CHECK(h->protocols[0]->port == 80);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.1.4:80");
                CHECK(h->protocols[1]->scheme == NHSchemeType::HTTPS);
                CHECK(h->protocols[1]->port == 443);
                CHECK(h->protocols[1]->health_check_url == "https://192.168.1.4:443");
                CHECK(h->weight == 1.0);
                break;
              }
              break;
            }
          }
        }
        CHECK(strategy->resp_codes.contains(404));
        CHECK(strategy->resp_codes.contains(503));
        CHECK(!strategy->resp_codes.contains(604));
      }
    }
  }

  GIVEN("loading a yaml config combining hosts and strategies into one file, combined.yaml")
  {
    NextHopStrategyFactory nhf(TS_SRC_DIR "/combined.yaml");

    WHEN("loading the single file")
    {
      THEN("loading the single file when there is only one strategy, 'mid-tier-east'")
      {
        REQUIRE(nhf.strategies_loaded == true);
        REQUIRE(nhf.strategyInstance("mid-tier-east") != nullptr);
        REQUIRE(nhf.strategyInstance("notthere") == nullptr);
      }
    }

    WHEN("the strategy 'mid-tier-north' details are checked.")
    {
      THEN("expect the following details.")
      {
        NextHopSelectionStrategy *const strategy = nhf.strategyInstance("mid-tier-north");
        REQUIRE(strategy != nullptr);
        CHECK(strategy->parent_is_proxy == false);
        CHECK(strategy->max_simple_retries == 2);
        CHECK(strategy->policy_type == NHPolicyType::RR_IP);
        CHECK(strategy->go_direct == true);
        CHECK(strategy->scheme == NHSchemeType::HTTP);
        CHECK(strategy->ring_mode == NHRingMode::EXHAUST_RING);
        CHECK(strategy->groups == 2);
        CHECK(strategy->resp_codes.contains(404));
        CHECK(strategy->resp_codes.contains(402));
        CHECK(!strategy->resp_codes.contains(604));
        CHECK(strategy->health_checks.active == true);
        CHECK(strategy->health_checks.passive == true);
        std::shared_ptr<HostRecord> h = strategy->host_groups[0][0];
        CHECK(h != nullptr);
        for (unsigned int i = 0; i < strategy->groups; i++) {
          CHECK(strategy->host_groups[i].size() == 2);
          for (unsigned int j = 0; j < strategy->host_groups[i].size(); j++) {
            h = strategy->host_groups[i][j];
            switch (i) {
            case 0:
              switch (j) {
              case 0:
                CHECK(h->hostname == "p1.foo.com");
                CHECK(h->protocols[0]->scheme == NHSchemeType::HTTP);
                CHECK(h->protocols[0]->port == 80);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.1.1:80");
                CHECK(h->protocols[1]->scheme == NHSchemeType::HTTPS);
                CHECK(h->protocols[1]->port == 443);
                CHECK(h->protocols[1]->health_check_url == "https://192.168.1.1:443");
                CHECK(h->weight == 0.5);
                break;
              case 1:
                CHECK(h->hostname == "p2.foo.com");
                CHECK(h->protocols[0]->scheme == NHSchemeType::HTTP);
                CHECK(h->protocols[0]->port == 80);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.1.2:80");
                CHECK(h->weight == 0.5);
                break;
              }
              break;
            case 1:
              switch (j) {
              case 0:
                CHECK(h->hostname == "s1.bar.com");
                CHECK(h->protocols[0]->scheme == NHSchemeType::HTTP);
                CHECK(h->protocols[0]->port == 8080);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.2.1:8080");
                CHECK(h->protocols[1]->scheme == NHSchemeType::HTTPS);
                CHECK(h->protocols[1]->port == 8443);
                CHECK(h->protocols[1]->health_check_url == "https://192.168.2.1:8443");
                CHECK(h->weight == 2.0);
                break;
              case 1:
                CHECK(h->hostname == "s2.bar.com");
                CHECK(h->protocols[0]->scheme == NHSchemeType::HTTP);
                CHECK(h->protocols[0]->port == 8080);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.2.2:8080");
                CHECK(h->protocols[1]->scheme == NHSchemeType::HTTPS);
                CHECK(h->protocols[1]->port == 8443);
                CHECK(h->protocols[1]->health_check_url == "https://192.168.2.2:8443");
                CHECK(h->weight == 1.0);
                break;
              }
              break;
            }
          }
        }
        CHECK(strategy->resp_codes.contains(404));
        CHECK(strategy->resp_codes.contains(403));
        CHECK(!strategy->resp_codes.contains(604));
      }
    }

    WHEN("the strategy 'mid-tier-south' details are checked.")
    {
      THEN("expect the following results.")
      {
        NextHopSelectionStrategy *const strategy = nhf.strategyInstance("mid-tier-south");
        REQUIRE(strategy != nullptr);
        CHECK(strategy->policy_type == NHPolicyType::RR_LATCHED);
        CHECK(strategy->parent_is_proxy == false);
        CHECK(strategy->ignore_self_detect == false);
        CHECK(strategy->max_simple_retries == 2);
        CHECK(strategy->go_direct == false);
        CHECK(strategy->scheme == NHSchemeType::HTTP);
        CHECK(strategy->ring_mode == NHRingMode::ALTERNATE_RING);
        CHECK(strategy->groups == 2);
        CHECK(strategy->resp_codes.contains(404));
        CHECK(strategy->resp_codes.contains(502));
        CHECK(!strategy->resp_codes.contains(604));
        CHECK(strategy->health_checks.active == true);
        CHECK(strategy->health_checks.passive == true);
        std::shared_ptr<HostRecord> h = strategy->host_groups[0][0];
        CHECK(h != nullptr);
        for (unsigned int i = 0; i < strategy->groups; i++) {
          CHECK(strategy->host_groups[i].size() == 2);
          for (unsigned int j = 0; j < strategy->host_groups[i].size(); j++) {
            h = strategy->host_groups[i][j];
            switch (i) {
            case 0:
              switch (j) {
              case 0:
                CHECK(h->hostname == "p1.foo.com");
                CHECK(h->protocols[0]->scheme == NHSchemeType::HTTP);
                CHECK(h->protocols[0]->port == 80);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.1.1:80");
                CHECK(h->protocols[1]->scheme == NHSchemeType::HTTPS);
                CHECK(h->protocols[1]->port == 443);
                CHECK(h->protocols[1]->health_check_url == "https://192.168.1.1:443");
                CHECK(h->weight == 0.5);
                break;
              case 1:
                CHECK(h->hostname == "p2.foo.com");
                CHECK(h->protocols[0]->scheme == NHSchemeType::HTTP);
                CHECK(h->protocols[0]->port == 80);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.1.2:80");
                CHECK(h->weight == 0.5);
                break;
              }
              break;
            case 1:
              switch (j) {
              case 0:
                CHECK(h->hostname == "s1.bar.com");
                CHECK(h->protocols[0]->scheme == NHSchemeType::HTTP);
                CHECK(h->protocols[0]->port == 8080);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.2.1:8080");
                CHECK(h->protocols[1]->scheme == NHSchemeType::HTTPS);
                CHECK(h->protocols[1]->port == 8443);
                CHECK(h->protocols[1]->health_check_url == "https://192.168.2.1:8443");
                CHECK(h->weight == 2.0);
                break;
              case 1:
                CHECK(h->hostname == "s2.bar.com");
                CHECK(h->protocols[0]->scheme == NHSchemeType::HTTP);
                CHECK(h->protocols[0]->port == 8080);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.2.2:8080");
                CHECK(h->protocols[1]->scheme == NHSchemeType::HTTPS);
                CHECK(h->protocols[1]->port == 8443);
                CHECK(h->protocols[1]->health_check_url == "https://192.168.2.2:8443");
                CHECK(h->weight == 1.0);
                break;
              }
              break;
            }
          }
        }
        CHECK(strategy->resp_codes.contains(404));
        CHECK(strategy->resp_codes.contains(503));
        CHECK(!strategy->resp_codes.contains(604));
      }
    }

    WHEN("the strategy 'mid-tier-east' details are checked.")
    {
      THEN("expect the following results.")
      {
        NextHopSelectionStrategy *const strategy = nhf.strategyInstance("mid-tier-east");
        REQUIRE(strategy != nullptr);
        CHECK(strategy->policy_type == NHPolicyType::FIRST_LIVE);
        CHECK(strategy->parent_is_proxy == false);
        CHECK(strategy->ignore_self_detect == true);
        CHECK(strategy->max_simple_retries == 2);
        CHECK(strategy->go_direct == false);
        CHECK(strategy->scheme == NHSchemeType::HTTPS);
        CHECK(strategy->ring_mode == NHRingMode::ALTERNATE_RING);
        CHECK(strategy->groups == 2);
        CHECK(strategy->resp_codes.contains(404));
        CHECK(strategy->resp_codes.contains(502));
        CHECK(!strategy->resp_codes.contains(604));
        CHECK(strategy->health_checks.active == false);
        CHECK(strategy->health_checks.passive == true);
        std::shared_ptr<HostRecord> h = strategy->host_groups[0][0];
        CHECK(h != nullptr);
        for (unsigned int i = 0; i < strategy->groups; i++) {
          CHECK(strategy->host_groups[i].size() == 2);
          for (unsigned int j = 0; j < strategy->host_groups[i].size(); j++) {
            h = strategy->host_groups[i][j];
            switch (i) {
            case 0:
              switch (j) {
              case 0:
                CHECK(h->hostname == "p1.foo.com");
                CHECK(h->protocols[0]->scheme == NHSchemeType::HTTP);
                CHECK(h->protocols[0]->port == 80);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.1.1:80");
                CHECK(h->protocols[1]->scheme == NHSchemeType::HTTPS);
                CHECK(h->protocols[1]->port == 443);
                CHECK(h->protocols[1]->health_check_url == "https://192.168.1.1:443");
                CHECK(h->weight == 0.5);
                break;
              case 1:
                CHECK(h->hostname == "p2.foo.com");
                CHECK(h->protocols[0]->scheme == NHSchemeType::HTTP);
                CHECK(h->protocols[0]->port == 80);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.1.2:80");
                CHECK(h->weight == 0.5);
                break;
              }
              break;
            case 1:
              switch (j) {
              case 0:
                CHECK(h->hostname == "s1.bar.com");
                CHECK(h->protocols[0]->scheme == NHSchemeType::HTTP);
                CHECK(h->protocols[0]->port == 8080);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.2.1:8080");
                CHECK(h->protocols[1]->scheme == NHSchemeType::HTTPS);
                CHECK(h->protocols[1]->port == 8443);
                CHECK(h->protocols[1]->health_check_url == "https://192.168.2.1:8443");
                CHECK(h->weight == 2.0);
                break;
              case 1:
                CHECK(h->hostname == "s2.bar.com");
                CHECK(h->protocols[0]->scheme == NHSchemeType::HTTP);
                CHECK(h->protocols[0]->port == 8080);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.2.2:8080");
                CHECK(h->protocols[1]->scheme == NHSchemeType::HTTPS);
                CHECK(h->protocols[1]->port == 8443);
                CHECK(h->protocols[1]->health_check_url == "https://192.168.2.2:8443");
                CHECK(h->weight == 1.0);
                break;
              }
              break;
            }
          }
        }
        CHECK(strategy->resp_codes.contains(404));
        CHECK(strategy->resp_codes.contains(503));
        CHECK(!strategy->resp_codes.contains(604));
      }
    }

    WHEN("the strategy 'mid-tier-west' details are checked.")
    {
      THEN("expect the following results.")
      {
        NextHopSelectionStrategy *const strategy = nhf.strategyInstance("mid-tier-west");
        REQUIRE(strategy != nullptr);
        CHECK(strategy->policy_type == NHPolicyType::RR_STRICT);
        CHECK(strategy->go_direct == true);
        CHECK(strategy->scheme == NHSchemeType::HTTPS);
        CHECK(strategy->parent_is_proxy == false);
        CHECK(strategy->max_simple_retries == 2);
        CHECK(strategy->ring_mode == NHRingMode::EXHAUST_RING);
        CHECK(strategy->groups == 2);
        CHECK(strategy->resp_codes.contains(404));
        CHECK(strategy->resp_codes.contains(502));
        CHECK(!strategy->resp_codes.contains(604));
        CHECK(strategy->health_checks.active == true);
        CHECK(strategy->health_checks.passive == false);
        std::shared_ptr<HostRecord> h = strategy->host_groups[0][0];
        CHECK(h != nullptr);
        for (unsigned int i = 0; i < strategy->groups; i++) {
          CHECK(strategy->host_groups[i].size() == 2);
          for (unsigned int j = 0; j < strategy->host_groups[i].size(); j++) {
            h = strategy->host_groups[i][j];
            switch (i) {
            case 0:
              switch (j) {
              case 0:
                CHECK(h->hostname == "p1.foo.com");
                CHECK(h->protocols[0]->scheme == NHSchemeType::HTTP);
                CHECK(h->protocols[0]->port == 80);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.1.1:80");
                CHECK(h->protocols[1]->scheme == NHSchemeType::HTTPS);
                CHECK(h->protocols[1]->port == 443);
                CHECK(h->protocols[1]->health_check_url == "https://192.168.1.1:443");
                CHECK(h->weight == 0.5);
                break;
              case 1:
                CHECK(h->hostname == "p2.foo.com");
                CHECK(h->protocols[0]->scheme == NHSchemeType::HTTP);
                CHECK(h->protocols[0]->port == 80);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.1.2:80");
                CHECK(h->weight == 0.5);
                break;
              }
              break;
            case 1:
              switch (j) {
              case 0:
                CHECK(h->hostname == "s1.bar.com");
                CHECK(h->protocols[0]->scheme == NHSchemeType::HTTP);
                CHECK(h->protocols[0]->port == 8080);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.2.1:8080");
                CHECK(h->protocols[1]->scheme == NHSchemeType::HTTPS);
                CHECK(h->protocols[1]->port == 8443);
                CHECK(h->protocols[1]->health_check_url == "https://192.168.2.1:8443");
                CHECK(h->weight == 2.0);
                break;
              case 1:
                CHECK(h->hostname == "s2.bar.com");
                CHECK(h->protocols[0]->scheme == NHSchemeType::HTTP);
                CHECK(h->protocols[0]->port == 8080);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.2.2:8080");
                CHECK(h->protocols[1]->scheme == NHSchemeType::HTTPS);
                CHECK(h->protocols[1]->port == 8443);
                CHECK(h->protocols[1]->health_check_url == "https://192.168.2.2:8443");
                CHECK(h->weight == 1.0);
                break;
              }
              break;
            }
          }
        }
        CHECK(strategy->resp_codes.contains(404));
        CHECK(strategy->resp_codes.contains(503));
        CHECK(!strategy->resp_codes.contains(604));
      }
    }

    WHEN("the strategy 'mid-tier-midwest' details are checked.")
    {
      THEN("expect the following results.")
      {
        NextHopSelectionStrategy *const strategy = nhf.strategyInstance("mid-tier-midwest");
        REQUIRE(strategy != nullptr);
        CHECK(strategy->policy_type == NHPolicyType::CONSISTENT_HASH);
        CHECK(strategy->parent_is_proxy == false);
        CHECK(strategy->max_simple_retries == 2);

        // I need to down cast here using the stored pointer so that I can verify that
        // the hash_key was set properly.
        NextHopConsistentHash *const ptr = static_cast<NextHopConsistentHash *>(strategy);
        REQUIRE(ptr != nullptr);
        CHECK(ptr->hash_key == NHHashKeyType::URL_HASH_KEY);

        CHECK(strategy->go_direct == true);
        CHECK(strategy->scheme == NHSchemeType::HTTPS);
        CHECK(strategy->ring_mode == NHRingMode::EXHAUST_RING);
        CHECK(strategy->groups == 2);
        CHECK(strategy->resp_codes.contains(404));
        CHECK(strategy->resp_codes.contains(502));
        CHECK(!strategy->resp_codes.contains(604));
        CHECK(strategy->health_checks.active == true);
        CHECK(strategy->health_checks.passive == false);
        std::shared_ptr<HostRecord> h = strategy->host_groups[0][0];
        CHECK(h != nullptr);
        for (unsigned int i = 0; i < strategy->groups; i++) {
          CHECK(strategy->host_groups[i].size() == 2);
          for (unsigned int j = 0; j < strategy->host_groups[i].size(); j++) {
            h = strategy->host_groups[i][j];
            switch (i) {
            case 0:
              switch (j) {
              case 0:
                CHECK(h->hostname == "p1.foo.com");
                CHECK(h->protocols[0]->scheme == NHSchemeType::HTTP);
                CHECK(h->protocols[0]->port == 80);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.1.1:80");
                CHECK(h->protocols[1]->scheme == NHSchemeType::HTTPS);
                CHECK(h->protocols[1]->port == 443);
                CHECK(h->protocols[1]->health_check_url == "https://192.168.1.1:443");
                CHECK(h->weight == 0.5);
                break;
              case 1:
                CHECK(h->hostname == "p2.foo.com");
                CHECK(h->protocols[0]->scheme == NHSchemeType::HTTP);
                CHECK(h->protocols[0]->port == 80);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.1.2:80");
                CHECK(h->weight == 0.5);
                break;
              }
              break;
            case 1:
              switch (j) {
              case 0:
                CHECK(h->hostname == "s1.bar.com");
                CHECK(h->protocols[0]->scheme == NHSchemeType::HTTP);
                CHECK(h->protocols[0]->port == 8080);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.2.1:8080");
                CHECK(h->protocols[1]->scheme == NHSchemeType::HTTPS);
                CHECK(h->protocols[1]->port == 8443);
                CHECK(h->protocols[1]->health_check_url == "https://192.168.2.1:8443");
                CHECK(h->weight == 2.0);
                break;
              case 1:
                CHECK(h->hostname == "s2.bar.com");
                CHECK(h->protocols[0]->scheme == NHSchemeType::HTTP);
                CHECK(h->protocols[0]->port == 8080);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.2.2:8080");
                CHECK(h->protocols[1]->scheme == NHSchemeType::HTTPS);
                CHECK(h->protocols[1]->port == 8443);
                CHECK(h->protocols[1]->health_check_url == "https://192.168.2.2:8443");
                CHECK(h->weight == 1.0);
                break;
              }
              break;
            }
          }
        }
        CHECK(strategy->resp_codes.contains(404));
        CHECK(strategy->resp_codes.contains(503));
        CHECK(!strategy->resp_codes.contains(604));
      }
    }
  }
}

SCENARIO("factory tests loading yaml configs from a directory", "[loadConfig]")
{
  GIVEN("Loading the strategies using a directory of 'yaml' files")
  {
    NextHopStrategyFactory nhf(TS_SRC_DIR "/strategies-dir");

    WHEN("the two files are loaded.")
    {
      THEN("there are two strategies defined in the config, 'strategy-1' and 'strategy-2'")
      {
        REQUIRE(nhf.strategies_loaded == true);
        REQUIRE(nhf.strategyInstance("mid-tier-north") != nullptr);
        REQUIRE(nhf.strategyInstance("mid-tier-south") != nullptr);
      }
    }

    WHEN("the strategy 'mid-tier-north' details are checked.")
    {
      THEN("expect the following results.")
      {
        NextHopSelectionStrategy *const strategy = nhf.strategyInstance("mid-tier-north");
        REQUIRE(strategy != nullptr);
        CHECK(strategy->parent_is_proxy == false);
        CHECK(strategy->max_simple_retries == 2);
        CHECK(strategy->policy_type == NHPolicyType::RR_IP);
        CHECK(strategy->go_direct == true);
        CHECK(strategy->scheme == NHSchemeType::HTTP);
        CHECK(strategy->ring_mode == NHRingMode::EXHAUST_RING);
        CHECK(strategy->groups == 2);
        CHECK(strategy->resp_codes.contains(404));
        CHECK(strategy->resp_codes.contains(502));
        CHECK(!strategy->resp_codes.contains(604));
        CHECK(strategy->health_checks.active == true);
        CHECK(strategy->health_checks.passive == true);
        std::shared_ptr<HostRecord> h = strategy->host_groups[0][0];
        CHECK(h != nullptr);
        for (unsigned int i = 0; i < strategy->groups; i++) {
          CHECK(strategy->host_groups[i].size() == 2);
          for (unsigned int j = 0; j < strategy->host_groups[i].size(); j++) {
            h = strategy->host_groups[i][j];
            switch (i) {
            case 0:
              switch (j) {
              case 0:
                CHECK(h->hostname == "p1.foo.com");
                CHECK(h->protocols[0]->scheme == NHSchemeType::HTTP);
                CHECK(h->protocols[0]->port == 80);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.1.1:80");
                CHECK(h->protocols[1]->scheme == NHSchemeType::HTTPS);
                CHECK(h->protocols[1]->port == 443);
                CHECK(h->protocols[1]->health_check_url == "https://192.168.1.1:443");
                CHECK(h->weight == 0.5);
                break;
              case 1:
                CHECK(h->hostname == "p2.foo.com");
                CHECK(h->protocols[0]->scheme == NHSchemeType::HTTP);
                CHECK(h->protocols[0]->port == 80);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.1.2:80");
                CHECK(h->weight == 0.5);
                break;
              }
              break;
            case 1:
              switch (j) {
              case 0:
                CHECK(h->hostname == "p3.foo.com");
                CHECK(h->protocols[0]->scheme == NHSchemeType::HTTP);
                CHECK(h->protocols[0]->port == 8080);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.1.3:8080");
                CHECK(h->protocols[1]->scheme == NHSchemeType::HTTPS);
                CHECK(h->protocols[1]->port == 8443);
                CHECK(h->protocols[1]->health_check_url == "https://192.168.1.3:8443");
                CHECK(h->weight == 0.5);
                break;
              case 1:
                CHECK(h->hostname == "p4.foo.com");
                CHECK(h->protocols[0]->scheme == NHSchemeType::HTTP);
                CHECK(h->protocols[0]->port == 8080);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.1.4:8080");
                CHECK(h->protocols[1]->scheme == NHSchemeType::HTTPS);
                CHECK(h->protocols[1]->port == 8443);
                CHECK(h->protocols[1]->health_check_url == "https://192.168.1.4:8443");
                CHECK(h->weight == 0.5);
                break;
              }
              break;
            }
          }
        }
        CHECK(strategy->resp_codes.contains(404));
        CHECK(strategy->resp_codes.contains(503));
        CHECK(!strategy->resp_codes.contains(604));
        CHECK(!strategy->markdown_codes.contains(405));
        CHECK(!strategy->markdown_codes.contains(502));
        CHECK(!strategy->markdown_codes.contains(503));
      }
    }

    WHEN("the strategy 'mid-tier-south' details are checked.")
    {
      THEN("expect the following results.")
      {
        NextHopSelectionStrategy *const strategy = nhf.strategyInstance("mid-tier-south");
        REQUIRE(strategy != nullptr);
        CHECK(strategy->policy_type == NHPolicyType::RR_LATCHED);
        CHECK(strategy->parent_is_proxy == false);
        CHECK(strategy->ignore_self_detect == false);
        CHECK(strategy->max_simple_retries == 2);
        CHECK(strategy->go_direct == false);
        CHECK(strategy->scheme == NHSchemeType::HTTP);
        CHECK(strategy->ring_mode == NHRingMode::ALTERNATE_RING);
        CHECK(strategy->groups == 2);
        CHECK(strategy->resp_codes.contains(404));
        CHECK(strategy->resp_codes.contains(502));
        CHECK(!strategy->resp_codes.contains(604));
        CHECK(strategy->health_checks.active == true);
        CHECK(strategy->health_checks.passive == true);
        std::shared_ptr<HostRecord> h = strategy->host_groups[0][0];
        CHECK(h != nullptr);
        for (unsigned int i = 0; i < strategy->groups; i++) {
          CHECK(strategy->host_groups[i].size() == 2);
          for (unsigned int j = 0; j < strategy->host_groups[i].size(); j++) {
            h = strategy->host_groups[i][j];
            switch (i) {
            case 0:
              switch (j) {
              case 0:
                CHECK(h->hostname == "p1.foo.com");
                CHECK(h->protocols[0]->scheme == NHSchemeType::HTTP);
                CHECK(h->protocols[0]->port == 80);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.1.1:80");
                CHECK(h->protocols[1]->scheme == NHSchemeType::HTTPS);
                CHECK(h->protocols[1]->port == 443);
                CHECK(h->protocols[1]->health_check_url == "https://192.168.1.1:443");
                CHECK(h->weight == 0.5);
                break;
              case 1:
                CHECK(h->hostname == "p2.foo.com");
                CHECK(h->protocols[0]->scheme == NHSchemeType::HTTP);
                CHECK(h->protocols[0]->port == 80);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.1.2:80");
                CHECK(h->weight == 0.5);
                break;
              }
              break;
            case 1:
              switch (j) {
              case 0:
                CHECK(h->hostname == "p3.foo.com");
                CHECK(h->protocols[0]->scheme == NHSchemeType::HTTP);
                CHECK(h->protocols[0]->port == 8080);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.1.3:8080");
                CHECK(h->protocols[1]->scheme == NHSchemeType::HTTPS);
                CHECK(h->protocols[1]->port == 8443);
                CHECK(h->protocols[1]->health_check_url == "https://192.168.1.3:8443");
                CHECK(h->weight == 0.5);
                break;
              case 1:
                CHECK(h->hostname == "p4.foo.com");
                CHECK(h->protocols[0]->scheme == NHSchemeType::HTTP);
                CHECK(h->protocols[0]->port == 8080);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.1.4:8080");
                CHECK(h->protocols[1]->scheme == NHSchemeType::HTTPS);
                CHECK(h->protocols[1]->port == 8443);
                CHECK(h->protocols[1]->health_check_url == "https://192.168.1.4:8443");
                CHECK(h->weight == 0.5);
                break;
              }
              break;
            }
          }
        }
        CHECK(strategy->resp_codes.contains(404));
        CHECK(strategy->resp_codes.contains(503));
        CHECK(!strategy->resp_codes.contains(604));
      }
    }
  }
}

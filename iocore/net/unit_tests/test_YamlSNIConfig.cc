/** @file

  Catch based unit tests for YamlSNIConfig

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

#ifndef LIBINKNET_UNIT_TEST_DIR
#error please set LIBINKNET_UNIT_TEST_DIR
#endif

#define _STR(s)  #s
#define _XSTR(s) _STR(s)

#include <string>
#include <sstream>
#include <algorithm>

#include "swoc/bwf_base.h"
#include "catch.hpp"

#include "YamlSNIConfig.h"

TEST_CASE("YamlSNIConfig sets port ranges appropriately")
{
  YamlSNIConfig conf{};
  ts::Errata zret{conf.loader(_XSTR(LIBINKNET_UNIT_TEST_DIR) "/sni_conf_test.yaml")};
  if (!zret.isOK()) {
    std::stringstream errorstream;
    errorstream << zret;
    FAIL(errorstream.str());
  }
  REQUIRE(zret.isOK());
  REQUIRE(conf.items.size() == 4);

  SECTION("If no ports were specified, ports should be empty.")
  {
    const auto &item{conf.items[0]};
    CHECK(item.port_ranges.size() == 0);
  }

  SECTION("If one port range was specified, ports should contain that port range.")
  {
    SECTION("Ports 1-433.")
    {
      const auto &item{conf.items[1]};
      REQUIRE(item.port_ranges.size() == 1);
      const auto [min, max]{item.port_ranges[0]};
      CHECK(min == 1);
      CHECK(max == 433);
    }
    SECTION("Ports 8080-65535.")
    {
      const auto &item{conf.items[2]};
      REQUIRE(item.port_ranges.size() == 1);
      const auto [min, max]{item.port_ranges[0]};
      CHECK(min == 8080);
      CHECK(max == 65535);
    }
    SECTION("Ports 433-433.")
    {
      const auto &item{conf.items[3]};
      REQUIRE(item.port_ranges.size() == 1);
      const auto [min, max]{item.port_ranges[0]};
      CHECK(min == 433);
      CHECK(max == 433);
    }
  }

  SECTION("If a port was specified, it should not interfere with the fqdn.")
  {
    const auto &item{conf.items[1]};
    CHECK(item.fqdn == "someport.com");
  }
}

TEST_CASE("YamlConfig handles bad ports appropriately.")
{
  YamlSNIConfig conf{};

  std::string port_str{GENERATE("0-1", "65535-65536", "8080-433", "yowzers-1", "1-yowzers2")};

  std::string filepath;
  swoc::bwprint(filepath, "{}/sni_conf_test_bad_port_{}.yaml", _XSTR(LIBINKNET_UNIT_TEST_DIR), port_str);

  ts::Errata zret{conf.loader(filepath)};
  std::stringstream errorstream;
  errorstream << zret;

  std::string expected;
  swoc::bwprint(expected, "1 [1]: yaml-cpp: error at line 2, column 9: bad port range: {}\n", port_str);
  CHECK(errorstream.str() == expected);
}

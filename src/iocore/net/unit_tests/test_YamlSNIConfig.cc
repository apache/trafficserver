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
#include <netinet/in.h>
#include "catch.hpp"

#include "iocore/net/YamlSNIConfig.h"

static void
check_port_range(const YamlSNIConfig::Item &item, in_port_t min_expected, in_port_t max_expected)
{
  CHECK(item.inbound_port_ranges.at(0).min() == min_expected);
  CHECK(item.inbound_port_ranges.at(0).max() == max_expected);
}

TEST_CASE("YamlSNIConfig sets port ranges appropriately")
{
  YamlSNIConfig conf{};
  swoc::Errata  zret{conf.loader(_XSTR(LIBINKNET_UNIT_TEST_DIR) "/sni_conf_test.yaml")};
  if (!zret.is_ok()) {
    std::stringstream errorstream;
    errorstream << zret;
    FAIL(errorstream.str());
  }
  REQUIRE(zret.is_ok());
  REQUIRE(conf.items.size() == 10);

  SECTION("If no ports were specified, port range should contain all ports.")
  {
    check_port_range(conf.items[0], 1, 65535);
  }

  SECTION("If one port range was specified, port range should match.")
  {
    SECTION("Ports 1-433.")
    {
      check_port_range(conf.items[1], 1, 433);
    }
    SECTION("Ports 8080-65535.")
    {
      check_port_range(conf.items[2], 8080, 65535);
    }
    SECTION("Port 433.")
    {
      check_port_range(conf.items[3], 433, 433);
    }
  }

  SECTION("If a port range was specified, it should not interfere with the fqdn.")
  {
    auto const &item{conf.items[1]};
    CHECK(item.fqdn == "someport.com");
  }

  SECTION("If no port range was specified, it should not interfere with the fqdn.")
  {
    auto const &item{conf.items[0]};
    CHECK(item.fqdn == "allports.com");
  }

  SECTION("If multiple port ranges were specified, all of them should be checked.")
  {
    auto const &item{conf.items[1]};
    CHECK(item.inbound_port_ranges.at(1).min() == 480);
    CHECK(item.inbound_port_ranges.at(1).max() == 488);
  }

  SECTION("If one port range was specified, "
          "there should only be one port range.")
  {
    CHECK(conf.items[2].inbound_port_ranges.size() == 1);
  }
}

TEST_CASE("YamlConfig handles bad ports appropriately.")
{
  YamlSNIConfig conf{};

  std::string port_str{GENERATE("0-1", "65535-65536", "8080-433", "yowzers-1", "1-yowzers2", "3-")};

  std::string filepath;
  swoc::bwprint(filepath, "{}/sni_conf_test_bad_port_{}.yaml", _XSTR(LIBINKNET_UNIT_TEST_DIR), port_str);

  swoc::Errata      zret{conf.loader(filepath)};
  std::stringstream errorstream;
  errorstream << zret;

  std::string expected;
  swoc::bwprint(expected, "Error: exception - yaml-cpp: error at line 20, column 5: bad port range: {}\n", port_str);
  CHECK(errorstream.str() == expected);
}

/** @file

  Unit Tests for parsing pre-warm tunnel destinations

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

#include "proxy/http/PreWarmManager.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

TEST_CASE("PreWarm parse_authority", "[prewarm]")
{
  std::string fqdn;
  int32_t     port = 0;

  SECTION("host and port")
  {
    REQUIRE(PreWarm::parse_authority(fqdn, port, "origin.example.com:8443"));
    CHECK(fqdn == "origin.example.com");
    CHECK(port == 8443);
  }

  SECTION("host without a port")
  {
    REQUIRE(PreWarm::parse_authority(fqdn, port, "origin.example.com"));
    CHECK(fqdn == "origin.example.com");
    CHECK(port == -1);
  }

  SECTION("IPv4 address and port")
  {
    REQUIRE(PreWarm::parse_authority(fqdn, port, "127.0.0.1:443"));
    CHECK(fqdn == "127.0.0.1");
    CHECK(port == 443);
  }

  SECTION("IPv6 address in brackets")
  {
    REQUIRE(PreWarm::parse_authority(fqdn, port, "[::1]:443"));
    CHECK(fqdn == "::1");
    CHECK(port == 443);

    REQUIRE(PreWarm::parse_authority(fqdn, port, "[::1]:8443"));
    CHECK(fqdn == "::1");
    CHECK(port == 8443);

    REQUIRE(PreWarm::parse_authority(fqdn, port, "[2001:4860::1]:443"));
    CHECK(fqdn == "2001:4860::1");
    CHECK(port == 443);

    REQUIRE(PreWarm::parse_authority(fqdn, port, "[::1]"));
    CHECK(fqdn == "::1");
    CHECK(port == -1);
  }

  SECTION("highest port")
  {
    REQUIRE(PreWarm::parse_authority(fqdn, port, "origin.example.com:65535"));
    CHECK(port == 65535);
  }

  SECTION("no host, or no fixed port, to pre-warm")
  {
    for (std::string_view bad : {
           "origin.example.com:{inbound_local_port}",
           "origin.example.com:{proxy_protocol_port}",
           "$1.example.com:$2", // Rejected for the $2 port.
           "origin.example.com:",
           "origin.example.com:443x",
           "origin.example.com:0",
           "origin.example.com:65536",
           "origin.example.com:99999999999",
           "origin.example.com:18446744073709552059", // 2^64 + 443: svtou clamps, so this is not port 443.
           ":443",
           "",
         }) {
      CAPTURE(bad);
      fqdn = "unchanged";
      port = 7;
      CHECK_FALSE(PreWarm::parse_authority(fqdn, port, bad));
      CHECK(fqdn == "unchanged");
      CHECK(port == 7);
    }
  }
}

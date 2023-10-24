/** @file

  Catch based unit tests for SSLSNIConfig

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

#include "iocore/net/SSLSNIConfig.h"

#include "catch.hpp"

#include <cstring>

TEST_CASE("Test SSLSNIConfig")
{
  SNIConfigParams params;
  REQUIRE(params.initialize(_XSTR(LIBINKNET_UNIT_TEST_DIR) "/sni_conf_test.yaml"));

  SECTION("The config does not match any SNIs for someport.com:577")
  {
    auto const &actions{params.get({"someport.com", std::strlen("someport.com")}, 577)};
    CHECK(!actions.first);
  }

  SECTION("The config does not match any SNIs for someport.com:808")
  {
    auto const &actions{params.get({"someport.com", std::strlen("someport.com")}, 808)};
    CHECK(!actions.first);
  }

  SECTION("The config does not match any SNIs for oneport.com:1")
  {
    auto const &actions{params.get({"oneport.com", std::strlen("oneport.com")}, 1)};
    CHECK(!actions.first);
  }

  SECTION("The config does match an SNI for oneport.com:433")
  {
    auto const &actions{params.get({"oneport.com", std::strlen("oneport.com")}, 433)};
    REQUIRE(actions.first);
    REQUIRE(actions.first->size() == 2);
  }

  SECTION("The config matches an SNI for allports.com")
  {
    auto const &actions{params.get({"allports.com", std::strlen("allports.com")}, 1)};
    REQUIRE(actions.first);
    REQUIRE(actions.first->size() == 2);
  }

  SECTION("The config matches an SNI for someport.com:1")
  {
    auto const &actions{params.get({"someport.com", std::strlen("someport.com")}, 1)};
    REQUIRE(actions.first);
    REQUIRE(actions.first->size() == 3);
  }

  SECTION("The config matches an SNI for someport.com:433")
  {
    auto const &actions{params.get({"someport.com", std::strlen("someport.com")}, 433)};
    REQUIRE(actions.first);
    REQUIRE(actions.first->size() == 3);
  }

  SECTION("The config matches an SNI for someport:8080")
  {
    auto const &actions{params.get({"someport.com", std::strlen("someport.com")}, 8080)};
    REQUIRE(actions.first);
    REQUIRE(actions.first->size() == 2);
  }

  SECTION("The config matches an SNI for someport:65535")
  {
    auto const &actions{params.get({"someport.com", std::strlen("someport.com")}, 65535)};
    REQUIRE(actions.first);
    REQUIRE(actions.first->size() == 2);
  }

  SECTION("The config matches an SNI for someport:482")
  {
    auto const &actions{params.get({"someport.com", std::strlen("someport.com")}, 482)};
    REQUIRE(actions.first);
    REQUIRE(actions.first->size() == 3);
  }

  SECTION("Matching order")
  {
    std::string_view target = "foo.bar.com";
    auto const &actions{params.get(target, 443)};
    REQUIRE(actions.first);
    REQUIRE(actions.first->size() == 5); ///< three H2 config + early data + fqdn
  }
}

TEST_CASE("SNIConfig reconfigure callback is invoked")
{
  int result{0};
  auto set_result{[&result]() { result = 42; }};
  SNIConfig::set_on_reconfigure_callback(set_result);
  SNIConfig::reconfigure();
  CHECK(result == 42);
}

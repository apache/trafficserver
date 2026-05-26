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
#include "tscore/ink_inet.h"

TEST_CASE("Test SSLSNIConfig")
{
  SNIConfigParams params;
  REQUIRE(params.initialize(_XSTR(LIBINKNET_UNIT_TEST_DIR) "/sni_conf_test.yaml"));

  SECTION("The config does not match any SNIs for someport.com:577")
  {
    auto const &actions{params.get("someport.com", 577)};
    CHECK(!actions.first);
  }

  SECTION("The config does not match any SNIs for someport.com:808")
  {
    auto const &actions{params.get("someport.com", 808)};
    CHECK(!actions.first);
  }

  SECTION("The config does not match any SNIs for oneport.com:1")
  {
    auto const &actions{params.get("oneport.com", 1)};
    CHECK(!actions.first);
  }

  SECTION("The config does match an SNI for oneport.com:433")
  {
    auto const &actions{params.get("oneport.com", 433)};
    REQUIRE(actions.first);
    REQUIRE(actions.first->size() == 2);
  }

  SECTION("The config matches an SNI for allports.com")
  {
    auto const &actions{params.get("allports.com", 1)};
    REQUIRE(actions.first);
    REQUIRE(actions.first->size() == 2);
  }

  SECTION("The config matches an SNI for someport.com:1")
  {
    auto const &actions{params.get("someport.com", 1)};
    REQUIRE(actions.first);
    REQUIRE(actions.first->size() == 3);
  }

  SECTION("The config matches an SNI for someport.com:433")
  {
    auto const &actions{params.get("someport.com", 433)};
    REQUIRE(actions.first);
    REQUIRE(actions.first->size() == 3);
  }

  SECTION("The config matches an SNI for someport:8080")
  {
    auto const &actions{params.get("someport.com", 8080)};
    REQUIRE(actions.first);
    REQUIRE(actions.first->size() == 2);
  }

  SECTION("The config matches an SNI for someport:65535")
  {
    auto const &actions{params.get("someport.com", 65535)};
    REQUIRE(actions.first);
    REQUIRE(actions.first->size() == 2);
  }

  SECTION("The config matches an SNI for someport:482")
  {
    auto const &actions{params.get("someport.com", 482)};
    REQUIRE(actions.first);
    REQUIRE(actions.first->size() == 3);
  }

  SECTION("The config matches an SNI for tickets.com")
  {
    auto const &actions{params.get("tickets.com", 443)};
    REQUIRE(actions.first);
    REQUIRE(actions.first->size() == 4); ///< ticket enabled + ticket number + early data + fqdn
  }

  SECTION("Matching order")
  {
    auto const &actions{params.get("foo.bar.com", 443)};
    REQUIRE(actions.first);
    REQUIRE(actions.first->size() == 5); ///< three H2 config + early data + fqdn
  }

  SECTION("Test mixed-case")
  {
    auto const &actions{params.get("SoMePoRt.CoM", 65535)};
    REQUIRE(actions.first);
    REQUIRE(actions.first->size() == 2);
  }

  SECTION("Test mixed-case with wildcard in yaml config")
  {
    auto const &actions{params.get("AnYtHiNg.BaR.CoM", 443)};
    REQUIRE(actions.first);
    REQUIRE(actions.first->size() == 4);
    // verify the capture group
    REQUIRE(actions.second._fqdn_wildcard_captured_groups->at(0) == "AnYtHiNg");
  }

  SECTION("Test mixed-case in yaml config")
  {
    auto const &actions{params.get("mixedcase.foo.com", 31337)};
    REQUIRE(actions.first);
    REQUIRE(actions.first->size() == 4);
  }

  SECTION("Test mixed-case glob in yaml config")
  {
    auto const &actions{params.get("FoO.mixedcase.com", 443)};
    REQUIRE(actions.first);
    REQUIRE(actions.first->size() == 3);
    // verify the capture group
    REQUIRE(actions.second._fqdn_wildcard_captured_groups->at(0) == "FoO");
  }

  SECTION("Test empty SNI does not match")
  {
    auto const &actions{params.get("", 443)};
    CHECK(!actions.first);
  }

  SECTION("Test SNI with special characters does not match")
  {
    auto const &actions{params.get("some$port.com", 443)};
    CHECK(!actions.first);
  }

  SECTION("Test with invalid glob in the middle in yaml config (e.g. cat.*.com) does not match")
  {
    auto const &actions{params.get("cat.dog.com", 443)};
    REQUIRE(!actions.first);
  }

  SECTION("Test with invalid glob in the middle in yaml config (e.g. cat.*.com) does an exact match")
  {
    auto const &actions{params.get("cat.*.com", 443)};
    REQUIRE(actions.first);
    REQUIRE(actions.first->size() == 2);
  }

  SECTION("Wildcard fqdn does not match when the input has trailing content past the wildcarded suffix")
  {
    // *.bar.com must not match foo.bar.com.extra.com -- the regex must
    // consume the entire SNI, not just a prefix of it.
    auto const &actions{params.get("foo.bar.com.extra.com", 443)};
    CHECK(!actions.first);
  }

  SECTION("get_property_config matches an exact fqdn")
  {
    CHECK(params.get_property_config("foo.bar.com") != nullptr);
  }

  SECTION("get_property_config matches a wildcard fqdn")
  {
    CHECK(params.get_property_config("baz.bar.com") != nullptr);
  }

  SECTION("get_property_config does not match when the input has trailing content past an exact fqdn")
  {
    // foo.bar.com is stored as a regex in next_hop_list; the lookup must
    // not accept foo.bar.com.extra.com as a match.
    CHECK(params.get_property_config("foo.bar.com.extra.com") == nullptr);
  }

  SECTION("get_property_config does not match when the input has trailing content past a wildcard fqdn")
  {
    CHECK(params.get_property_config("baz.bar.com.extra.com") == nullptr);
  }

  SECTION("get_property_config does not match when the input has leading content before an exact fqdn")
  {
    // allports.com is stored as a regex in next_hop_list; RE_ANCHORED at
    // compile time should keep the lookup from matching prefix.allports.com.
    // Use allports.com (which has no wildcard sibling in the test config)
    // so the lookup cannot legitimately match via *.something.
    CHECK(params.get_property_config("prefix.allports.com") == nullptr);
  }

  SECTION("get_property_config does not match when the input has trailing content past an exact-only fqdn")
  {
    // Mirror of the prefix case: allports.com has no wildcard sibling, so
    // this isolates the exact-entry path in next_hop_list.
    CHECK(params.get_property_config("allports.com.extra.com") == nullptr);
  }

  SECTION("get for an exact fqdn does not match when the input has leading content")
  {
    // Exact fqdns live in sni_action_map (hash-keyed), so a prefixed
    // input must not produce a hit.
    auto const &actions{params.get("prefix.allports.com", 1)};
    CHECK(!actions.first);
  }

  SECTION("get for an exact-only fqdn does not match when the input has trailing content")
  {
    // Mirror of the prefix case for the hash-keyed path.
    auto const &actions{params.get("allports.com.extra.com", 1)};
    CHECK(!actions.first);
  }
}

TEST_CASE("SNIConfig reconfigure callback is invoked")
{
  int  result{0};
  auto set_result{[&result]() { result = 42; }};
  SNIConfig::set_on_reconfigure_callback(set_result);
  SNIConfig::reconfigure();
  CHECK(result == 42);
}

TEST_CASE("SNIConfig handles high-bit bytes while normalizing server names")
{
  constexpr auto HIGH_ORDER_BIT = static_cast<char>(0x80);

  YamlSNIConfig::Item item;
  item.fqdn = "High";
  item.fqdn.push_back(HIGH_ORDER_BIT);
  item.fqdn.append(".Example.Com");
  item.inbound_port_ranges.emplace_back(1, ts::MAX_PORT_VALUE);

  SNIConfigParams params;
  params.yaml_sni.items.push_back(item);
  REQUIRE(params.load_sni_config());

  std::string servername{"hIGH"};
  servername.push_back(HIGH_ORDER_BIT);
  servername.append(".eXAMPLE.cOM");

  auto const &actions{params.get(servername, 443)};
  REQUIRE(actions.first);
  CHECK(actions.first->size() == 2);
}

static IpEndpoint
make_endpoint(const char *ip_str)
{
  IpEndpoint ep;
  ats_ip_pton(ip_str, &ep.sa);
  return ep;
}

TEST_CASE("SNI_IpAllow TestClientSNIAction")
{
  SNIConfigParams params;
  REQUIRE(params.initialize(_XSTR(LIBINKNET_UNIT_TEST_DIR) "/sni_conf_test.yaml"));

  SECTION("Entry with ip_allow always triggers regardless of client IP")
  {
    auto const &actions{params.get("ipallow.example.com", 443)};
    REQUIRE(actions.first);

    auto blocked_ep = make_endpoint("172.16.0.1");
    int  policy     = 2;
    bool triggered  = false;
    for (auto &&item : *actions.first) {
      triggered |= item->TestClientSNIAction("ipallow.example.com", blocked_ep, policy);
    }
    CHECK(triggered);

    auto allowed_ep = make_endpoint("192.168.1.50");
    triggered       = false;
    for (auto &&item : *actions.first) {
      triggered |= item->TestClientSNIAction("ipallow.example.com", allowed_ep, policy);
    }
    CHECK(triggered);
  }

  SECTION("Entry without ip_allow does not trigger TestClientSNIAction for any IP")
  {
    auto const &actions{params.get("noipallow.example.com", 443)};
    REQUIRE(actions.first);

    auto any_ep    = make_endpoint("203.0.113.1");
    int  policy    = 2;
    bool triggered = false;
    for (auto &&item : *actions.first) {
      triggered |= item->TestClientSNIAction("noipallow.example.com", any_ep, policy);
    }
    CHECK_FALSE(triggered);
  }
}

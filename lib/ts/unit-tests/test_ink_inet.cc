/** @file

    ink_inet unit tests.

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

#include <ts/TextView.h>
#include <ts/ink_inet.h>
#include <catch.hpp>
#include <iostream>

TEST_CASE("ink_inet", "[libts][inet][ink_inet]")
{
  // Use TextView because string_view(nullptr) fails. Gah.
  struct ip_parse_spec {
    ts::TextView hostspec;
    ts::TextView host;
    ts::TextView port;
    ts::TextView rest;
  };

  constexpr ip_parse_spec names[] = {
    {{"::"}, {"::"}, {nullptr}, {nullptr}},
    {{"[::1]:99"}, {"::1"}, {"99"}, {nullptr}},
    {{"127.0.0.1:8080"}, {"127.0.0.1"}, {"8080"}, {nullptr}},
    {{"127.0.0.1:8080-Bob"}, {"127.0.0.1"}, {"8080"}, {"-Bob"}},
    {{"127.0.0.1:"}, {"127.0.0.1"}, {nullptr}, {":"}},
    {{"foo.example.com"}, {"foo.example.com"}, {nullptr}, {nullptr}},
    {{"foo.example.com:99"}, {"foo.example.com"}, {"99"}, {nullptr}},
    {{"ffee::24c3:3349:3cee:0143"}, {"ffee::24c3:3349:3cee:0143"}, {nullptr}, {nullptr}},
    {{"fe80:88b5:4a:20c:29ff:feae:1c33:8080"}, {"fe80:88b5:4a:20c:29ff:feae:1c33:8080"}, {nullptr}, {nullptr}},
    {{"[ffee::24c3:3349:3cee:0143]"}, {"ffee::24c3:3349:3cee:0143"}, {nullptr}, {nullptr}},
    {{"[ffee::24c3:3349:3cee:0143]:80"}, {"ffee::24c3:3349:3cee:0143"}, {"80"}, {nullptr}},
    {{"[ffee::24c3:3349:3cee:0143]:8080x"}, {"ffee::24c3:3349:3cee:0143"}, {"8080"}, {"x"}},
  };

  for (auto const &s : names) {
    ts::string_view host, port, rest;

    REQUIRE(0 == ats_ip_parse(s.hostspec, &host, &port, &rest));
    REQUIRE(s.host == host);
    REQUIRE(s.port == port);
    REQUIRE(s.rest == rest);
  }
}

TEST_CASE("ats_ip_pton", "[libts][inet][ink_inet]")
{
  IpEndpoint ep;
  IpAddr addr;

  REQUIRE(0 == ats_ip_pton("76.14.64.156", &ep.sa));
  REQUIRE(0 == addr.load("76.14.64.156"));
  REQUIRE(addr.family() == ep.family());

  switch (addr.family()) {
  case AF_INET:
    REQUIRE(ep.sin.sin_addr.s_addr == addr._addr._ip4);
    break;
  case AF_INET6:
    REQUIRE(0 == memcmp(&ep.sin6.sin6_addr, &addr._addr._ip6, sizeof(in6_addr)));
    break;
  default:;
  }
}

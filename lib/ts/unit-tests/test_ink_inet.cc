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
#include <ts/apidefs.h>
#include <ts/ink_inet.h>
#include <ts/BufferWriter.h>

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
  IpAddr lower, upper;

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

  REQUIRE(TS_SUCCESS != addr.load("Evil Dave Rulz!"));

  REQUIRE(TS_SUCCESS == ats_ip_range_parse("1.1.1.1-2.2.2.2"_sv, lower, upper));
  REQUIRE(TS_SUCCESS != ats_ip_range_parse("172.16.39.0/", lower, upper));
  REQUIRE(TS_SUCCESS == ats_ip_range_parse("172.16.39.0/24", lower, upper));
  REQUIRE(TS_SUCCESS != ats_ip_range_parse("172.16.39.0-", lower, upper));
  REQUIRE(TS_SUCCESS != ats_ip_range_parse("172.16.39.0/35", lower, upper));
  REQUIRE(TS_SUCCESS != ats_ip_range_parse("172.16.39.0/-20", lower, upper));
  REQUIRE(TS_SUCCESS != ats_ip_range_parse("Thanks, Persia! You're the best.", lower, upper));

  addr.load("172.16.39.0");
  REQUIRE(addr == lower);
  addr.load("172.16.39.255");
  REQUIRE(addr == upper);

  REQUIRE(TS_SUCCESS == ats_ip_range_parse("10.169.243.105/23", lower, upper));
  addr.load("10.169.242.0");
  REQUIRE(lower == addr);
  addr.load("10.169.243.255");
  REQUIRE(upper == addr);

  REQUIRE(TS_SUCCESS == ats_ip_range_parse("192.168.99.22", lower, upper));
  REQUIRE(lower == upper);
  REQUIRE(lower != IpAddr{INADDR_ANY});

  REQUIRE(TS_SUCCESS == ats_ip_range_parse("0/0", lower, upper));
  REQUIRE(lower == IpAddr{INADDR_ANY});
  REQUIRE(upper == IpAddr{INADDR_BROADCAST});

  REQUIRE(TS_SUCCESS == ats_ip_range_parse("c600::-d900::"_sv, lower, upper));
  REQUIRE(TS_SUCCESS == ats_ip_range_parse("1300::/96", lower, upper));
  REQUIRE(TS_SUCCESS != ats_ip_range_parse("ffee::24c3:3349:3cee:0143/", lower, upper));

  REQUIRE(TS_SUCCESS == ats_ip_range_parse("ffee:1337:beef:dead:24c3:3349:3cee:0143/80", lower, upper));
  addr.load("ffee:1337:beef:dead:24c3::"_sv);
  REQUIRE(lower == addr);
  addr.load("ffee:1337:beef:dead:24c3:FFFF:FFFF:FFFF"_sv);
  REQUIRE(upper == addr);

  REQUIRE(TS_SUCCESS == ats_ip_range_parse("ffee:1337:beef:dead:24c3:3349:3cee:0143/57", lower, upper));
  addr.load("ffee:1337:beef:de80::"_sv);
  REQUIRE(lower == addr);
  addr.load("ffee:1337:beef:deff:FFFF:FFFF:FFFF:FFFF"_sv);
  REQUIRE(upper == addr);

  REQUIRE(TS_SUCCESS == ats_ip_range_parse("ffee::24c3:3349:3cee:0143", lower, upper));
  REQUIRE(lower == upper);

  REQUIRE(TS_SUCCESS == ats_ip_range_parse("::/0", lower, upper));
  REQUIRE(lower._addr._u64[0] == 0);
  REQUIRE(lower._addr._u64[1] == 0);
  REQUIRE(upper._addr._u64[0] == ~static_cast<uint64_t>(0));
  REQUIRE(upper._addr._u64[1] == static_cast<uint64_t>(-1));

  REQUIRE(TS_SUCCESS == ats_ip_range_parse("c000::/32", lower, upper));
  addr.load("c000::");
  REQUIRE(addr == lower);
  addr.load("c000::ffff:ffff:ffff:ffff:ffff:ffff");
  REQUIRE(addr == upper);
}

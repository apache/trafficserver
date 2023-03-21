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

#include "swoc/TextView.h"
#include "swoc/BufferWriter.h"
#include "swoc/bwf_base.h"
#include "swoc/swoc_ip.h"
#include "swoc/bwf_ip.h"
#include "tscore/ink_inet.h"
#include <catch.hpp>
#include <iostream>
#include "ts/apidefs.h"
#include "tscore/ink_inet.h"

using namespace std::literals;

TEST_CASE("ink_inet", "[libts][inet][ink_inet]")
{
  // Use TextView because string_view(nullptr) fails. Gah.
  struct ip_parse_spec {
    swoc::TextView hostspec;
    swoc::TextView host;
    swoc::TextView port;
    swoc::TextView rest;
  };

  constexpr ip_parse_spec names[] = {
    {{"::"},                                   {"::"},                                   {nullptr}, {nullptr}},
    {{"[::1]:99"},                             {"::1"},                                  {"99"},    {nullptr}},
    {{"127.0.0.1:8080"},                       {"127.0.0.1"},                            {"8080"},  {nullptr}},
    {{"127.0.0.1:8080-Bob"},                   {"127.0.0.1"},                            {"8080"},  {"-Bob"} },
    {{"127.0.0.1:"},                           {"127.0.0.1"},                            {nullptr}, {":"}    },
    {{"foo.example.com"},                      {"foo.example.com"},                      {nullptr}, {nullptr}},
    {{"foo.example.com:99"},                   {"foo.example.com"},                      {"99"},    {nullptr}},
    {{"ffee::24c3:3349:3cee:0143"},            {"ffee::24c3:3349:3cee:0143"},            {nullptr}, {nullptr}},
    {{"fe80:88b5:4a:20c:29ff:feae:1c33:8080"}, {"fe80:88b5:4a:20c:29ff:feae:1c33:8080"}, {nullptr}, {nullptr}},
    {{"[ffee::24c3:3349:3cee:0143]"},          {"ffee::24c3:3349:3cee:0143"},            {nullptr}, {nullptr}},
    {{"[ffee::24c3:3349:3cee:0143]:80"},       {"ffee::24c3:3349:3cee:0143"},            {"80"},    {nullptr}},
    {{"[ffee::24c3:3349:3cee:0143]:8080x"},    {"ffee::24c3:3349:3cee:0143"},            {"8080"},  {"x"}    },
  };

  for (auto const &s : names) {
    std::string_view host, port, rest;

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

  REQUIRE(TS_SUCCESS == ats_ip_range_parse("1.1.1.1-2.2.2.2"sv, lower, upper));
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

  REQUIRE(swoc::IPAddr(addr) == swoc::IPAddr("172.16.39.255"));

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

  REQUIRE(TS_SUCCESS == ats_ip_range_parse("c600::-d900::"sv, lower, upper));
  REQUIRE(TS_SUCCESS == ats_ip_range_parse("1300::/96", lower, upper));
  REQUIRE(TS_SUCCESS != ats_ip_range_parse("ffee::24c3:3349:3cee:0143/", lower, upper));

  REQUIRE(TS_SUCCESS == ats_ip_range_parse("ffee:1337:beef:dead:24c3:3349:3cee:0143/80", lower, upper));
  addr.load("ffee:1337:beef:dead:24c3::"sv);
  REQUIRE(lower == addr);
  addr.load("ffee:1337:beef:dead:24c3:FFFF:FFFF:FFFF"sv);
  REQUIRE(upper == addr);

  REQUIRE(TS_SUCCESS == ats_ip_range_parse("ffee:1337:beef:dead:24c3:3349:3cee:0143/57", lower, upper));
  addr.load("ffee:1337:beef:de80::"sv);
  REQUIRE(lower == addr);
  addr.load("ffee:1337:beef:deff:FFFF:FFFF:FFFF:FFFF"sv);
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

TEST_CASE("inet formatting", "[libts][ink_inet][bwformat]")
{
  IpEndpoint ep;
  std::string_view addr_1{"[ffee::24c3:3349:3cee:143]:8080"};
  std::string_view addr_2{"172.17.99.231:23995"};
  std::string_view addr_3{"[1337:ded:BEEF::]:53874"};
  std::string_view addr_4{"[1337::ded:BEEF]:53874"};
  std::string_view addr_5{"[1337:0:0:ded:BEEF:0:0:956]:53874"};
  std::string_view addr_6{"[1337:0:0:ded:BEEF:0:0:0]:53874"};
  std::string_view addr_7{"172.19.3.105:4951"};
  std::string_view addr_null{"[::]:53874"};
  swoc::LocalBufferWriter<1024> w;

  REQUIRE(0 == ats_ip_pton(addr_1, &ep.sa));
  w.print("{}", swoc::IPEndpoint(ep));
  REQUIRE(w.view() == addr_1);
  w.clear().print("{::p}", ep);
  REQUIRE(w.view() == "8080");
  w.clear().print("{::a}", ep);
  REQUIRE(w.view() == addr_1.substr(1, 24)); // check the brackets are dropped.
  w.clear().print("[{::a}]", ep);
  REQUIRE(w.view() == addr_1.substr(0, 26)); // check the brackets are dropped.
  w.clear().print("[{0::a}]:{0::p}", ep);
  REQUIRE(w.view() == addr_1); // check the brackets are dropped.
  w.clear().print("{::=a}", ep);
  REQUIRE(w.view() == "ffee:0000:0000:0000:24c3:3349:3cee:0143");
  w.clear().print("{:: =a}", ep);
  REQUIRE(w.view() == "ffee:   0:   0:   0:24c3:3349:3cee: 143");
  ep.setToLoopback(AF_INET6);
  w.clear().print("{::a}", ep);
  REQUIRE(w.view() == "::1");
  REQUIRE(0 == ats_ip_pton(addr_3, &ep.sa));
  w.clear().print("{::a}", ep);
  REQUIRE(w.view() == "1337:ded:beef::");
  REQUIRE(0 == ats_ip_pton(addr_4, &ep.sa));
  w.clear().print("{::a}", ep);
  REQUIRE(w.view() == "1337::ded:beef");

  REQUIRE(0 == ats_ip_pton(addr_5, &ep.sa));
  w.clear().print("{:X:a}", ep);
  REQUIRE(w.view() == "1337::DED:BEEF:0:0:956");

  REQUIRE(0 == ats_ip_pton(addr_6, &ep.sa));
  w.clear().print("{::a}", ep);
  REQUIRE(w.view() == "1337:0:0:ded:beef::");

  REQUIRE(0 == ats_ip_pton(addr_null, &ep.sa));
  w.clear().print("{::a}", ep);
  REQUIRE(w.view() == "::");

  REQUIRE(0 == ats_ip_pton(addr_2, &ep.sa));
  w.clear().print("{::a}", ep);
  REQUIRE(w.view() == addr_2.substr(0, 13));
  w.clear().print("{0::a}", ep);
  REQUIRE(w.view() == addr_2.substr(0, 13));
  w.clear().print("{::ap}", ep);
  REQUIRE(w.view() == addr_2);
  w.clear().print("{::f}", ep);
  REQUIRE(w.view() == IP_PROTO_TAG_IPV4);
  w.clear().print("{::fpa}", ep);
  REQUIRE(w.view() == "172.17.99.231:23995 ipv4");
  w.clear().print("{0::a} .. {0::p}", ep);
  REQUIRE(w.view() == "172.17.99.231 .. 23995");
  w.clear().print("<+> {0::a} <+> {0::p}", ep);
  REQUIRE(w.view() == "<+> 172.17.99.231 <+> 23995");
  w.clear().print("<+> {0::a} <+> {0::p} <+>", ep);
  REQUIRE(w.view() == "<+> 172.17.99.231 <+> 23995 <+>");
  w.clear().print("{:: =a}", ep);
  REQUIRE(w.view() == "172. 17. 99.231");
  w.clear().print("{::=a}", ep);
  REQUIRE(w.view() == "172.017.099.231");
  w.clear().print("{}", swoc::bwf::As_Hex(ats_ip4_addr_cast(ep)));
  REQUIRE(w.view() == "ac1163e7");
  w.clear().print("{:#X}", swoc::bwf::As_Hex(ats_ip4_addr_cast(ep)));
  REQUIRE(w.view() == "0XAC1163E7");

  // Documentation examples
  REQUIRE(0 == ats_ip_pton(addr_7, &ep.sa));
  w.clear().print("To {}", ep);
  REQUIRE(w.view() == "To 172.19.3.105:4951");
  w.clear().print("To {0::a} on port {0::p}", ep); // no need to pass the argument twice.
  REQUIRE(w.view() == "To 172.19.3.105 on port 4951");
  w.clear().print("To {::=}", ep);
  REQUIRE(w.view() == "To 172.019.003.105:04951");
  w.clear().print("{::a}", ep);
  REQUIRE(w.view() == "172.19.3.105");
  w.clear().print("{::=a}", ep);
  REQUIRE(w.view() == "172.019.003.105");
  w.clear().print("{::0=a}", ep);
  REQUIRE(w.view() == "172.019.003.105");
  w.clear().print("{:: =a}", ep);
  REQUIRE(w.view() == "172. 19.  3.105");
  w.clear().print("{:>20:a}", ep);
  REQUIRE(w.view() == "        172.19.3.105");
  w.clear().print("{:>20:=a}", ep);
  REQUIRE(w.view() == "     172.019.003.105");
  w.clear().print("{:>20: =a}", ep);
  REQUIRE(w.view() == "     172. 19.  3.105");
  w.clear().print("{:<20:a}", ep);
  REQUIRE(w.view() == "172.19.3.105        ");

  w.clear().print("{:p}", reinterpret_cast<sockaddr const *>(0x1337beef));
  REQUIRE(w.view() == "0x1337beef");

  ats_ip_pton(addr_1, &ep.sa);
  w.clear().print("{}", swoc::bwf::As_Hex(ats_ip6_addr_cast(ep)));
  REQUIRE(w.view() == "ffee00000000000024c333493cee0143");
}

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

TEST_CASE("inet formatting", "[libts][ink_inet][bwformat]")
{
  IpEndpoint ep;
  ts::string_view addr_1{"[ffee::24c3:3349:3cee:143]:8080"};
  ts::string_view addr_2{"172.17.99.231:23995"};
  ts::string_view addr_3{"[1337:ded:BEEF::]:53874"};
  ts::string_view addr_4{"[1337::ded:BEEF]:53874"};
  ts::string_view addr_5{"[1337:0:0:ded:BEEF:0:0:956]:53874"};
  ts::string_view addr_6{"[1337:0:0:ded:BEEF:0:0:0]:53874"};
  ts::string_view addr_7{"172.19.3.105:49951"};
  ts::string_view addr_null{"[::]:53874"};
  ts::LocalBufferWriter<1024> w;

  REQUIRE(0 == ats_ip_pton(addr_1, &ep.sa));
  w.print("{}", ep);
  REQUIRE(w.view() == addr_1);
  w.reset().print("{::p}", ep);
  REQUIRE(w.view() == "8080");
  w.reset().print("{::a}", ep);
  REQUIRE(w.view() == addr_1.substr(1, 24)); // check the brackets are dropped.
  w.reset().print("[{::a}]", ep);
  REQUIRE(w.view() == addr_1.substr(0, 26)); // check the brackets are dropped.
  w.reset().print("[{0::a}]:{0::p}", ep);
  REQUIRE(w.view() == addr_1); // check the brackets are dropped.
  w.reset().print("{::^a}", ep);
  REQUIRE(w.view() == "ffee:0000:0000:0000:24c3:3349:3cee:0143");
  w.reset().print("{:: ^a}", ep);
  REQUIRE(w.view() == "ffee:   0:   0:   0:24c3:3349:3cee: 143");
  ep.setToLoopback(AF_INET6);
  w.reset().print("{::a}", ep);
  REQUIRE(w.view() == "::1");
  REQUIRE(0 == ats_ip_pton(addr_3, &ep.sa));
  w.reset().print("{::a}", ep);
  REQUIRE(w.view() == "1337:ded:beef::");
  REQUIRE(0 == ats_ip_pton(addr_4, &ep.sa));
  w.reset().print("{::a}", ep);
  REQUIRE(w.view() == "1337::ded:beef");

  REQUIRE(0 == ats_ip_pton(addr_5, &ep.sa));
  w.reset().print("{:X:a}", ep);
  REQUIRE(w.view() == "1337::DED:BEEF:0:0:956");

  REQUIRE(0 == ats_ip_pton(addr_6, &ep.sa));
  w.reset().print("{::a}", ep);
  REQUIRE(w.view() == "1337:0:0:ded:beef::");

  REQUIRE(0 == ats_ip_pton(addr_null, &ep.sa));
  w.reset().print("{::a}", ep);
  REQUIRE(w.view() == "::");

  REQUIRE(0 == ats_ip_pton(addr_2, &ep.sa));
  w.reset().print("{::a}", ep);
  REQUIRE(w.view() == addr_2.substr(0, 13));
  w.reset().print("{::ap}", ep);
  REQUIRE(w.view() == addr_2);
  w.reset().print("{::f}", ep);
  REQUIRE(w.view() == IP_PROTO_TAG_IPV4);
  w.reset().print("{::fpa}", ep);
  REQUIRE(w.view() == "172.17.99.231:23995 ipv4");
  w.reset().print("{:: ^a}", ep);
  REQUIRE(w.view() == "172. 17. 99.231");
  w.reset().print("{::^a}", ep);
  REQUIRE(w.view() == "172.017.099.231");

  // Documentation examples
  REQUIRE(0 == ats_ip_pton(addr_7, &ep.sa));
  w.reset().print("Connecting to {}", ep);
  REQUIRE(w.view() == "Connecting to 172.19.3.105:49951");
  w.reset().print("{::a}", ep);
  REQUIRE(w.view() == "172.19.3.105");
  w.reset().print("{::^a}", ep);
  REQUIRE(w.view() == "172.019.003.105");
  w.reset().print("{::0^a}", ep);
  REQUIRE(w.view() == "172.019.003.105");
  w.reset().print("{:: ^a}", ep);
  REQUIRE(w.view() == "172. 19.  3.105");
  w.reset().print("{:>20:a}", ep);
  REQUIRE(w.view() == "        172.19.3.105");
  w.reset().print("{:>20:^a}", ep);
  REQUIRE(w.view() == "     172.019.003.105");
  w.reset().print("{:>20: ^a}", ep);
  REQUIRE(w.view() == "     172. 19.  3.105");
  w.reset().print("{:<20:a}", ep);
  REQUIRE(w.view() == "172.19.3.105        ");

  w.reset().print("{:p}", reinterpret_cast<sockaddr const *>(0x1337beef));
  REQUIRE(w.view() == "0x1337beef");
}

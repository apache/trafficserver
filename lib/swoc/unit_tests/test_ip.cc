// SPDX-License-Identifier: Apache-2.0
// Copyright 2014 Network Geographics
/** @file

    IP address support testing.
*/

#include "catch.hpp"

#include <set>
#include <iostream>

#include "swoc/TextView.h"
#include "swoc/swoc_ip.h"
#include "swoc/bwf_ip.h"
#include "swoc/bwf_std.h"
#include "swoc/Lexicon.h"

using namespace std::literals;
using namespace swoc::literals;
using swoc::TextView;
using swoc::IPEndpoint;

using swoc::IP4Addr;
using swoc::IP4Range;

using swoc::IP6Addr;
using swoc::IP6Range;

using swoc::IPAddr;
using swoc::IPRange;

using swoc::IPMask;

using swoc::IP4Net;
using swoc::IP6Net;

using swoc::IPSpace;
using swoc::IPRangeSet;

namespace {
std::string bws;

template <typename P>
void
dump(IPSpace<P> const &space) {
  for (auto &&[r, p] : space) {
    std::cout << bwprint(bws, "{} : {}\n", r, p);
  }
}
} // namespace

TEST_CASE("Basic IP", "[libswoc][ip]") {
  IPEndpoint ep;

  // Use TextView because string_view(nullptr) fails. Gah.
  struct ip_parse_spec {
    TextView hostspec;
    TextView host;
    TextView port;
    TextView rest;
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
    {{"[ffee::24c3:3349:3cee:0143]:8080x"},    {"ffee::24c3:3349:3cee:0143"},            {"8080"},  {"x"}    }
  };

  for (auto const &s : names) {
    std::string_view host, port, rest;

    REQUIRE(IPEndpoint::tokenize(s.hostspec, &host, &port, &rest) == true);
    REQUIRE(s.host == host);
    REQUIRE(s.port == port);
    REQUIRE(s.rest == rest);
  }

  IP4Addr alpha{"172.96.12.134"};
  CHECK(alpha == IP4Addr{"172.96.12.134"});
  CHECK(alpha == IPAddr{IPEndpoint{"172.96.12.134:80"}});
  CHECK(alpha == IPAddr{IPEndpoint{"172.96.12.134"}});
  REQUIRE(alpha[1] == 96);
  REQUIRE(alpha[2] == 12);
  REQUIRE(alpha[3] == 134);

  // Alternate forms - inet_aton compabitility. Note in truncated forms, the last value is for
  // all remaining octets, those are not zero filled as in IPv6.
  CHECK(alpha.load("172.96.12"));
  REQUIRE(alpha[0] == 172);
  REQUIRE(alpha[2] == 0);
  REQUIRE(alpha[3] == 12);
  CHECK_FALSE(alpha.load("172.96.71117"));
  CHECK(alpha.load("172.96.3136"));
  REQUIRE(alpha[0] == 172);
  REQUIRE(alpha[2] == 0xC);
  REQUIRE(alpha[3] == 0x40);
  CHECK(alpha.load("172.12586118"));
  REQUIRE(alpha[0] == 172);
  REQUIRE(alpha[1] == 192);
  REQUIRE(alpha[2] == 12);
  REQUIRE(alpha[3] == 134);
  CHECK(alpha.load("172.0xD00D56"));
  REQUIRE(alpha[0] == 172);
  REQUIRE(alpha[1] == 0xD0);
  REQUIRE(alpha[2] == 0x0D);
  REQUIRE(alpha[3] == 0x56);
  CHECK_FALSE(alpha.load("192.172.3."));
  CHECK(alpha.load("192.0xAC.014.135"));
  REQUIRE(alpha[0] == 192);
  REQUIRE(alpha[1] == 172);
  REQUIRE(alpha[2] == 12);
  REQUIRE(alpha[3] == 135);

  CHECK(IP6Addr().load("ffee:1f2d:c587:24c3:9128:3349:3cee:143"));

  IP4Addr lo{"127.0.0.1"};
  CHECK(lo.is_loopback());
  CHECK_FALSE(lo.is_any());
  CHECK_FALSE(lo.is_multicast());
  CHECK_FALSE(lo.is_link_local());
  CHECK(lo[0] == 0x7F);

  IP4Addr any{"0.0.0.0"};
  REQUIRE_FALSE(any.is_loopback());
  REQUIRE(any.is_any());
  REQUIRE_FALSE(any.is_link_local());
  REQUIRE(any == IP4Addr("0"));

  IP4Addr mc{"238.11.55.99"};
  CHECK_FALSE(mc.is_loopback());
  CHECK_FALSE(mc.is_any());
  CHECK_FALSE(mc.is_link_local());
  CHECK(mc.is_multicast());

  IP4Addr ll4{"169.254.55.99"};
  CHECK_FALSE(ll4.is_loopback());
  CHECK_FALSE(ll4.is_any());
  CHECK(ll4.is_link_local());
  CHECK_FALSE(ll4.is_multicast());
  CHECK(swoc::ip::is_link_local_host_order(ll4.host_order()));
  CHECK_FALSE(swoc::ip::is_link_local_network_order(ll4.host_order()));

  CHECK(swoc::ip::is_private_host_order(0xC0A8BADC));
  CHECK_FALSE(swoc::ip::is_private_network_order(0xC0A8BADC));
  CHECK_FALSE(swoc::ip::is_private_host_order(0xDCBA8C0));
  CHECK(swoc::ip::is_private_network_order(0xDCBA8C0));

  CHECK(IP4Addr(INADDR_LOOPBACK).is_loopback());

  IP6Addr lo6{"::1"};
  REQUIRE(lo6.is_loopback());
  REQUIRE_FALSE(lo6.is_any());
  REQUIRE_FALSE(lo6.is_multicast());
  REQUIRE_FALSE(lo.is_link_local());

  IP6Addr any6{"::"};
  REQUIRE_FALSE(any6.is_loopback());
  REQUIRE(any6.is_any());
  REQUIRE_FALSE(lo.is_link_local());

  IP6Addr multi6{"FF02::19"};
  REQUIRE(multi6.is_loopback() == false);
  REQUIRE(multi6.is_multicast() == true);
  REQUIRE(lo.is_link_local() == false);
  REQUIRE(IPAddr(multi6).is_multicast());

  IP6Addr ll{"FE80::56"};
  REQUIRE(ll.is_link_local() == true);
  REQUIRE(ll.is_multicast() == false);
  REQUIRE(IPAddr(ll).is_link_local() == true);

  // Do a bit of IPv6 testing.
  IP6Addr a6_null;
  IP6Addr a6_1{"fe80:88b5:4a:20c:29ff:feae:5587:1c33"};
  IP6Addr a6_2{"fe80:88b5:4a:20c:29ff:feae:5587:1c34"};
  IP6Addr a6_3{"de80:88b5:4a:20c:29ff:feae:5587:1c35"};

  REQUIRE(a6_1 != a6_null);
  REQUIRE(a6_1 != a6_2);
  REQUIRE(a6_1 < a6_2);
  REQUIRE(a6_2 > a6_1);
  ++a6_1;
  REQUIRE(a6_1 == a6_2);
  ++a6_1;
  REQUIRE(a6_1 != a6_2);
  REQUIRE(a6_1 > a6_2);

  REQUIRE(a6_3 != a6_2);
  REQUIRE(a6_3 < a6_2);
  REQUIRE(a6_2 > a6_3);

  REQUIRE(-1 == a6_3.cmp(a6_2));
  REQUIRE(0 == a6_2.cmp(a6_2));
  REQUIRE(1 == a6_1.cmp(a6_2));

  REQUIRE(a6_1[0] == 0xFE);
  REQUIRE(a6_1[1] == 0x80);
  REQUIRE(a6_2[3] == 0xB5);
  REQUIRE(a6_3[11] == 0xAE);
  REQUIRE(a6_3[14] == 0x1C);
  REQUIRE(a6_2[15] == 0x34);

  REQUIRE(a6_1.host_order() != a6_2.host_order());

  a6_1.copy_to(&ep.sa);
  REQUIRE(a6_1 == IP6Addr(ep.ip6()));
  REQUIRE(IPAddr(a6_1) == &ep.sa);
  REQUIRE(IPAddr(a6_2) != &ep.sa);
  a6_2.copy_to(&ep.sa6);
  REQUIRE(a6_2 == IP6Addr(&ep.sa6));
  REQUIRE(a6_1 != IP6Addr(ep.ip6()));
  in6_addr in6;
  a6_1.network_order(in6);
  REQUIRE(a6_1 == IP6Addr(in6));
  a6_1.network_order(ep.sa6.sin6_addr);
  REQUIRE(a6_1 == IP6Addr(ep.ip6()));
  in6 = a6_2.network_order();
  REQUIRE(a6_2.host_order() != in6);
  REQUIRE(a6_2.network_order() == in6);
  REQUIRE(a6_2 == IP6Addr(in6));
  a6_2.host_order(in6);
  REQUIRE(a6_2.network_order() != in6);
  REQUIRE(a6_2.host_order() == in6);
  REQUIRE(in6.s6_addr[0] == 0x34);
  REQUIRE(in6.s6_addr[6] == 0xff);
  REQUIRE(in6.s6_addr[13] == 0x88);

  // Little bit of IP4 address arithmetic / comparison testing.
  IP4Addr a4_null;
  IP4Addr a4_1{"172.28.56.33"};
  IP4Addr a4_2{"172.28.56.34"};
  IP4Addr a4_3{"170.28.56.35"};
  IP4Addr a4_loopback{"127.0.0.1"_tv};
  IP4Addr ip4_loopback{INADDR_LOOPBACK};

  REQUIRE(a4_loopback == ip4_loopback);
  REQUIRE(a4_loopback.is_loopback() == true);
  REQUIRE(ip4_loopback.is_loopback() == true);
  CHECK(a4_2.is_private());
  CHECK_FALSE(a4_3.is_private());

  REQUIRE(a4_1 != a4_null);
  REQUIRE(a4_1 != a4_2);
  REQUIRE(a4_1 < a4_2);
  REQUIRE(a4_2 > a4_1);
  ++a4_1;
  REQUIRE(a4_1 == a4_2);
  ++a4_1;
  REQUIRE(a4_1 != a4_2);
  REQUIRE(a4_1 > a4_2);
  REQUIRE(a4_3 != a4_2);
  REQUIRE(a4_3 < a4_2);
  REQUIRE(a4_2 > a4_3);

  REQUIRE(IPAddr(a4_1) > IPAddr(a4_2));
  REQUIRE(IPAddr(a4_1) >= IPAddr(a4_2));
  REQUIRE(false == (IPAddr(a4_1) < IPAddr(a4_2)));
  REQUIRE(IPAddr(a6_2) < IPAddr(a6_1));
  REQUIRE(IPAddr(a6_2) <= IPAddr(a6_1));
  REQUIRE(false == (IPAddr(a6_2) > IPAddr(a6_1)));
  REQUIRE(IPAddr(a4_3) == IPAddr(a4_3));
  REQUIRE(IPAddr(a4_3) <= IPAddr(a4_3));
  REQUIRE(IPAddr(a4_3) >= IPAddr(a4_3));
  REQUIRE(IPAddr(a4_3) < IPAddr(a6_3));
  REQUIRE(IPAddr{} < IPAddr(a4_3));
  REQUIRE(IPAddr{} == IPAddr{});

  REQUIRE(IPAddr(a4_3).cmp(IPAddr(a6_3)) == -1);
  REQUIRE(IPAddr{}.cmp(IPAddr(a4_3)) == -1);
  REQUIRE(IPAddr{}.cmp(IPAddr{}) == 0);
  REQUIRE(IPAddr(a6_3).cmp(IPAddr(a4_3)) == 1);
  REQUIRE(IPAddr{a4_3}.cmp(IPAddr{}) == 1);

  // For this data, the bytes should be in IPv6 network order.
  static const std::tuple<TextView, bool, IP6Addr::raw_type> ipv6_ex[] = {
    {"::",                                 true,  {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {"::1",                                true,  {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01}},
    {":::",                                false, {}                                                                                              },
    {"fe80::20c:29ff:feae:5587:1c33",
     true,                                        {0xFE, 0x80, 0x00, 0x00, 0x00, 0x00, 0x02, 0x0C, 0x29, 0xFF, 0xFE, 0xAE, 0x55, 0x87, 0x1C, 0x33}},
    {"fe80:20c:29ff:feae:5587::1c33",
     true,                                        {0xFE, 0x80, 0x02, 0x0C, 0x29, 0xFF, 0xFE, 0xAE, 0x55, 0x87, 0x00, 0x00, 0x00, 0x00, 0x1C, 0x33}},
    {"fe80:20c:29ff:feae:5587:1c33::",
     true,                                        {0xFE, 0x80, 0x02, 0x0C, 0x29, 0xFF, 0xFE, 0xAE, 0x55, 0x87, 0x1c, 0x33, 0x00, 0x00, 0x00, 0x00}},
    {"::fe80:20c:29ff:feae:5587:1c33",
     true,                                        {0x00, 0x00, 0x00, 0x00, 0xFE, 0x80, 0x02, 0x0C, 0x29, 0xFF, 0xFE, 0xAE, 0x55, 0x87, 0x1c, 0x33}},
    {":fe80:20c:29ff:feae:5587:4A43:1c33", false, {}                                                                                              },
    {"fe80:20c::29ff:feae:5587::1c33",     false, {}                                                                                              }
  };

  for (auto const &item : ipv6_ex) {
    auto &&[text, result, data]{item};
    IP6Addr addr;
    REQUIRE(result == addr.load(text));
    if (result) {
      union {
        in6_addr _inet;
        IP6Addr::raw_type _raw;
      } ar;
      ar._inet = addr.network_order();
      REQUIRE(ar._raw == data);
    }
  }

  IPRange r;
  IP4Range r4;
  IP6Range r6;

  REQUIRE(r4.load("10.242.129.0-10.242.129.127") == true);
  REQUIRE(r4.min() == IP4Addr("10.242.129.0"));
  REQUIRE(r4.max() == IP4Addr("10.242.129.127"));
  REQUIRE(r4.load("10.242.129.0/25") == true);
  REQUIRE(r4.min() == IP4Addr("10.242.129.0"));
  REQUIRE(r4.max() == IP4Addr("10.242.129.127"));
  REQUIRE(r4.load("2.2.2.2") == true);
  REQUIRE(r4.min() == IP4Addr("2.2.2.2"));
  REQUIRE(r4.max() == IP4Addr("2.2.2.2"));
  REQUIRE(r4.load("2.2.2.2.2") == false);
  REQUIRE(r4.load("2.2.2.2-fe80:20c::29ff:feae:5587::1c33") == false);
  CHECK(r4.load("0xC0A83801"));
  REQUIRE(r4 == IP4Addr("192.168.56.1"));

  // A few special cases.
  static constexpr TextView all_4_txt{"0/0"};
  static constexpr TextView all_6_txt{"::/0"};

  CHECK(r4.load(all_4_txt));
  CHECK(r.load(all_4_txt));
  REQUIRE(r.ip4() == r4);
  REQUIRE(r4.min() == IP4Addr::MIN);
  REQUIRE(r4.max() == IP4Addr::MAX);
  CHECK(r.load(all_6_txt));
  CHECK(r6.load(all_6_txt));
  REQUIRE(r.ip6() == r6);
  REQUIRE(r6.min() == IP6Addr::MIN);
  REQUIRE(r6.max() == IP6Addr::MAX);
  CHECK_FALSE(r6.load("2.2.2.2-fe80:20c::29ff:feae:5587::1c33"));
  CHECK_FALSE(r.load("2.2.2.2-fe80:20c::29ff:feae:5587::1c33"));

  ep.set_to_any(AF_INET);
  REQUIRE(ep.is_loopback() == false);
  REQUIRE(ep.is_any() == true);
  REQUIRE(ep.raw_addr().length() == sizeof(in_addr_t));
  ep.set_to_loopback(AF_INET6);
  REQUIRE(ep.is_loopback() == true);
  REQUIRE(ep.is_any() == false);
  REQUIRE(ep.raw_addr().length() == sizeof(in6_addr));

  ep.set_to_any(AF_INET6);
  REQUIRE(ep.is_loopback() == false);
  REQUIRE(ep.is_any() == true);
  CHECK(ep.ip4() == nullptr);
  IP6Addr a6{ep.ip6()};
  REQUIRE(a6.is_loopback() == false);
  REQUIRE(a6.is_any() == true);

  ep.set_to_loopback(AF_INET);
  REQUIRE(ep.is_loopback() == true);
  REQUIRE(ep.is_any() == false);
  CHECK(ep.ip6() == nullptr);
  IP4Addr a4{ep.ip4()};
  REQUIRE(a4.is_loopback() == true);
  REQUIRE(a4.is_any() == false);

  CHECK_FALSE(IP6Addr("1337:0:0:ded:BEEF:0:0:0").is_mapped_ip4());
  CHECK_FALSE(IP6Addr("1337:0:0:ded:BEEF::").is_mapped_ip4());
  CHECK(IP6Addr("::FFFF:C0A8:381F").is_mapped_ip4());
  CHECK_FALSE(IP6Addr("FFFF:C0A8:381F::").is_mapped_ip4());
  CHECK_FALSE(IP6Addr("::C0A8:381F").is_mapped_ip4());
  CHECK(IP6Addr(a4_2).is_mapped_ip4());
};

TEST_CASE("IP Net and Mask", "[libswoc][ip][ipnet]") {
  IP4Addr a24{"255.255.255.0"};
  REQUIRE(IP4Addr::MAX == IPMask(32).as_ip4());
  REQUIRE(IP4Addr::MIN == IPMask(0).as_ip4());
  REQUIRE(IPMask(24).as_ip4() == a24);

  SECTION("addr as mask") {
    swoc::IP4Net n1{"10.0.0.0/255.255.0.0"};
    CHECK_FALSE(n1.empty());
    REQUIRE(n1.mask().width() == 16);

    swoc::IP6Net n2{"BEEF:1337:dead::/FFFF:FFFF:FFFF:C000::"};
    CHECK_FALSE(n2.empty());
    REQUIRE(n2.mask().width() == 50);

    swoc::IPNet n3{"10.0.0.0/255.255.0.0"};
    CHECK_FALSE(n3.empty());
    REQUIRE(n3.mask().width() == 16);

    swoc::IPNet n4{"BEEF:1337:dead::/FFFF:FFFF:FFFF:C000::"};
    CHECK_FALSE(n4.empty());
    REQUIRE(n4.mask().width() == 50);

    swoc::IPNet n5{"BEEF:1337:dead::/FFFF:FFFF:FFFF:000C::"};
    REQUIRE(n5.empty()); // mask address isn't a valid mask.
  }

  swoc::IP4Net n1{"0/1"};
  auto nr1 = n1.as_range();
  REQUIRE(nr1.min() == IP4Addr::MIN);
  REQUIRE(nr1.max() == IP4Addr("127.255.255.255"));

  IP4Addr a{"8.8.8.8"};
  swoc::IP4Net n4{a, IPMask{32}};
  auto nr4 = n4.as_range();
  REQUIRE(nr4.min() == a);
  REQUIRE(nr4.max() == a);

  swoc::IP4Net n0{"0/0"};
  auto nr0 = n0.as_range();
  REQUIRE(nr0.min() == IP4Addr::MIN);
  REQUIRE(nr0.max() == IP4Addr::MAX);

  swoc::IPMask m128{128};
  REQUIRE(m128.as_ip6() == IP6Addr::MAX);
  swoc::IPMask m0{0};
  REQUIRE(m0.as_ip6() == IP6Addr::MIN);

  IP6Addr a6{"12:34:56:78:9A:BC:DE:FF"};
  REQUIRE(a6 == (a6 | IPMask(128))); // Host network, should be unchanged.
  REQUIRE(IP6Addr::MAX == (a6 | IPMask(0)));
  REQUIRE(IP6Addr::MIN == (a6 & IPMask(0)));

  IP6Addr a6_2{"2001:1f2d:c587:24c3:9128:3349:3cee:143"_tv};
  swoc::IPMask mask{127};
  CHECK(a6_2 == (a6_2 | mask));
  CHECK(a6_2 != (a6_2 & mask));
  CHECK(a6_2 == (a6_2 & swoc::IPMask(128))); // should always be a no-op.

  IP6Net n6_1{a6_2, IPMask(96)};
  CHECK(n6_1.min() == IP6Addr("2001:1f2d:c587:24c3:9128:3349::"));

  swoc::IP6Addr a6_3{"2001:1f2d:c587:24c4::"};
  CHECK(a6_3 == (a6_3 & swoc::IPMask{64}));
  CHECK(a6_3 == (a6_3 & swoc::IPMask{62}));
  CHECK(a6_3 != (a6_3 & swoc::IPMask{61}));

  REQUIRE(IPMask(1) == IPMask::mask_for(IP4Addr("0x80.0.0.0")));
  REQUIRE(IPMask(2) == IPMask::mask_for(IP4Addr("0xC0.0.0.0")));
  REQUIRE(IPMask(27) == IPMask::mask_for(IP4Addr("0xFF.0xFF.0xFF.0xE0")));
  REQUIRE(IPMask(55) == IPMask::mask_for(IP6Addr("1337:dead:beef:CA00::")));
  REQUIRE(IPMask(91) == IPMask::mask_for(IP6Addr("1337:dead:beef:CA00:24c3:3ce0::")));

  IP4Addr b1{"192.168.56.24"};
  REQUIRE((b1 & IPMask(24)) == IP4Addr("192.168.56.0"));
  IP6Addr b2{"1337:dead:beef:CA00:24c3:3ce0:9120:143"};
  REQUIRE((b2 & IPMask(32)) == IP6Addr("1337:dead::"));
  REQUIRE((b2 & IPMask(64)) == IP6Addr("1337:dead:beef:CA00::"));
  REQUIRE((b2 & IPMask(96)) == IP6Addr("1337:dead:beef:CA00:24c3:3ce0::"));
  // do it again with generic address.
  IPAddr b3{"192.168.56.24"};
  REQUIRE((b3 & IPMask(24)) == IP4Addr("192.168.56.0"));
  IPAddr b4{"1337:dead:beef:CA00:24c3:3ce0:9120:143"};
  REQUIRE((b4 & IPMask(32)) == IP6Addr("1337:dead::"));
  REQUIRE((b4 & IPMask(64)) == IP6Addr("1337:dead:beef:CA00::"));
  REQUIRE((b4 & IPMask(96)) == IP6Addr("1337:dead:beef:CA00:24c3:3ce0::"));

  IP4Addr c1{"192.168.56.24"};
  REQUIRE((c1 | IPMask(24)) == IP4Addr("192.168.56.255"));
  REQUIRE((c1 | IPMask(15)) == IP4Addr("192.169.255.255"));
  REQUIRE((c1 | IPMask(7)) == IP4Addr("193.255.255.255"));
  IP6Addr c2{"1337:dead:beef:CA00:24c3:3ce0:9120:143"};
  REQUIRE((c2 | IPMask(96)) == IP6Addr("1337:dead:beef:CA00:24c3:3ce0:FFFF:FFFF"));
  REQUIRE((c2 | IPMask(64)) == IP6Addr("1337:dead:beef:CA00:FFFF:FFFF:FFFF:FFFF"));
  REQUIRE((c2 | IPMask(32)) == IP6Addr("1337:dead:FFFF:FFFF:FFFF:FFFF:FFFF:FFFF"));
  // do it again with generic address.
  IPAddr c3{"192.168.56.24"};
  REQUIRE((c3 | IPMask(24)) == IP4Addr("192.168.56.255"));
  REQUIRE((c3 | IPMask(15)) == IP4Addr("192.169.255.255"));
  REQUIRE((c3 | IPMask(7)) == IP4Addr("193.255.255.255"));
  IPAddr c4{"1337:dead:beef:CA00:24c3:3ce0:9120:143"};
  REQUIRE((c4 | IPMask(96)) == IP6Addr("1337:dead:beef:CA00:24c3:3ce0:FFFF:FFFF"));
  REQUIRE((c4 | IPMask(64)) == IP6Addr("1337:dead:beef:CA00:FFFF:FFFF:FFFF:FFFF"));
  REQUIRE((c4 | IPMask(32)) == IP6Addr("1337:dead:FFFF:FFFF:FFFF:FFFF:FFFF:FFFF"));
}

TEST_CASE("IP Formatting", "[libswoc][ip][bwformat]") {
  IPEndpoint ep;
  std::string_view addr_1{"[ffee::24c3:3349:3cee:143]:8080"};
  std::string_view addr_2{"172.17.99.231:23995"};
  std::string_view addr_3{"[1337:ded:BEEF::]:53874"};
  std::string_view addr_4{"[1337::ded:BEEF]:53874"};
  std::string_view addr_5{"[1337:0:0:ded:BEEF:0:0:956]:53874"};
  std::string_view addr_6{"[1337:0:0:ded:BEEF:0:0:0]:53874"};
  std::string_view addr_7{"172.19.3.105:4951"};
  std::string_view addr_8{"[1337:0:0:ded:BEEF:0:0:0]"};
  std::string_view addr_9{"1337:0:0:ded:BEEF:0:0:0"};
  std::string_view addr_A{"172.19.3.105"};
  std::string_view addr_null{"[::]:53874"};
  std::string_view localhost{"[::1]:8080"};
  swoc::LocalBufferWriter<1024> w;

  REQUIRE(ep.parse(addr_null) == true);
  w.clear().print("{::a}", ep);
  REQUIRE(w.view() == "::");

  ep.set_to_loopback(AF_INET6);
  w.clear().print("{::a}", ep);
  REQUIRE(w.view() == "::1");

  REQUIRE(ep.parse(addr_1) == true);
  w.clear().print("{}", ep);
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

  // Verify @c IPEndpoint will parse without the port.
  REQUIRE(ep.parse(addr_8) == true);
  REQUIRE(ep.network_order_port() == 0);
  REQUIRE(ep.parse(addr_9) == true);
  REQUIRE(ep.network_order_port() == 0);
  REQUIRE(ep.parse(addr_A) == true);
  REQUIRE(ep.network_order_port() == 0);

  REQUIRE(ep.parse(addr_2) == true);
  w.clear().print("{::a}", ep);
  REQUIRE(w.view() == addr_2.substr(0, 13));
  w.clear().print("{0::a}", ep);
  REQUIRE(w.view() == addr_2.substr(0, 13));
  w.clear().print("{::ap}", ep);
  REQUIRE(w.view() == addr_2);
  w.clear().print("{::f}", ep);
  REQUIRE(w.view() == "ipv4");
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
  w.clear().print("{:x:a}", ep);
  REQUIRE(w.view() == "ac.11.63.e7");
  auto a4 = IP4Addr(ep.ip4());
  w.clear().print("{:x}", a4);
  REQUIRE(w.view() == "ac.11.63.e7");

  REQUIRE(ep.parse(addr_3) == true);
  w.clear().print("{::a}", ep);
  REQUIRE(w.view() == "1337:ded:beef::"_tv);

  REQUIRE(ep.parse(addr_4) == true);
  w.clear().print("{::a}", ep);
  REQUIRE(w.view() == "1337::ded:beef"_tv);

  REQUIRE(ep.parse(addr_5) == true);
  w.clear().print("{:X:a}", ep);
  REQUIRE(w.view() == "1337::DED:BEEF:0:0:956");

  REQUIRE(ep.parse(addr_6) == true);
  w.clear().print("{::a}", ep);
  REQUIRE(w.view() == "1337:0:0:ded:beef::");

  // Documentation examples
  REQUIRE(ep.parse(addr_7) == true);
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

  REQUIRE(ep.parse(localhost) == true);
  w.clear().print("{}", ep);
  REQUIRE(w.view() == localhost);
  w.clear().print("{::p}", ep);
  REQUIRE(w.view() == "8080");
  w.clear().print("{::a}", ep);
  REQUIRE(w.view() == localhost.substr(1, 3)); // check the brackets are dropped.
  w.clear().print("[{::a}]", ep);
  REQUIRE(w.view() == localhost.substr(0, 5));
  w.clear().print("[{0::a}]:{0::p}", ep);
  REQUIRE(w.view() == localhost); // check the brackets are dropped.
  w.clear().print("{::=a}", ep);
  REQUIRE(w.view() == "0000:0000:0000:0000:0000:0000:0000:0001");
  w.clear().print("{:: =a}", ep);
  REQUIRE(w.view() == "   0:   0:   0:   0:   0:   0:   0:   1");

  std::string_view r_1{"10.1.0.0-10.1.0.127"};
  std::string_view r_2{"10.2.0.1-10.2.0.127"}; // not a network - bad start
  std::string_view r_3{"10.3.0.0-10.3.0.126"}; // not a network - bad end
  std::string_view r_4{"10.4.1.1-10.4.1.1"};   // singleton
  std::string_view r_5{"10.20.30.40- 50.60.70.80"};
  std::string_view r_6{"10.20.30.40 -50.60.70.80"};
  std::string_view r_7{"10.20.30.40 - 50.60.70.80"};

  IPRange r;

  r.load(r_1);
  w.clear().print("{}", r);
  REQUIRE(w.view() == r_1);
  w.clear().print("{::c}", r);
  REQUIRE(w.view() == "10.1.0.0/25");

  r.load(r_2);
  w.clear().print("{}", r);
  REQUIRE(w.view() == r_2);
  w.clear().print("{::c}", r);
  REQUIRE(w.view() == r_2);

  r.load(r_3);
  w.clear().print("{}", r);
  REQUIRE(w.view() == r_3);
  w.clear().print("{::c}", r);
  REQUIRE(w.view() == r_3);

  r.load(r_4);
  w.clear().print("{}", r);
  REQUIRE(w.view() == r_4);
  w.clear().print("{::c}", r);
  REQUIRE(w.view() == "10.4.1.1");

  REQUIRE(r.load(r_5));
  REQUIRE(r.load(r_6));
  REQUIRE(r.load(r_7));
}

TEST_CASE("IP ranges and networks", "[libswoc][ip][net][range]") {
  swoc::IP4Range r_0;
  swoc::IP4Range r_1{"1.1.1.0-1.1.1.9"};
  swoc::IP4Range r_2{"1.1.2.0-1.1.2.97"};
  swoc::IP4Range r_3{"1.1.0.0-1.2.0.0"};
  swoc::IP4Range r_4{"10.33.45.19-10.33.45.76"};
  swoc::IP6Range r_5{"2001:1f2d:c587:24c3:9128:3349:3cee:143-ffee:1f2d:c587:24c3:9128:3349:3cFF:FFFF"_tv};

  CHECK(r_0.empty());
  CHECK_FALSE(r_1.empty());

  // Verify a family specific range only works with the same family range.
  TextView r4_txt{"10.33.45.19-10.33.45.76"};
  TextView r6_txt{"2001:1f2d:c587:24c3:9128:3349:3cee:143-ffee:1f2d:c587:24c3:9128:3349:3cFF:FFFF"};
  IP4Range rr4;
  IP6Range rr6;
  CHECK(rr4.load(r4_txt));
  CHECK_FALSE(rr4.load(r6_txt));
  CHECK_FALSE(rr6.load(r4_txt));
  CHECK(rr6.load(r6_txt));

  std::array<swoc::IP4Net, 7> r_4_nets = {
    {"10.33.45.19/32"_tv, "10.33.45.20/30"_tv, "10.33.45.24/29"_tv, "10.33.45.32/27"_tv, "10.33.45.64/29"_tv, "10.33.45.72/30"_tv,
     "10.33.45.76/32"_tv}
  };
  auto r4_net = r_4_nets.begin();
  for (auto net : r_4.networks()) {
    REQUIRE(r4_net != r_4_nets.end());
    CHECK(*r4_net == net);
    ++r4_net;
  }

  // Let's try that again, with @c IPRange instead.
  r4_net = r_4_nets.begin();
  for (auto const &net : IPRange{r_4}.networks()) {
    REQUIRE(r4_net != r_4_nets.end());
    CHECK(*r4_net == net);
    ++r4_net;
  }

  std::array<swoc::IP6Net, 130> r_5_nets = {
    {{IP6Addr{"2001:1f2d:c587:24c3:9128:3349:3cee:143"}, IPMask{128}},
     {IP6Addr{"2001:1f2d:c587:24c3:9128:3349:3cee:144"}, IPMask{126}},
     {IP6Addr{"2001:1f2d:c587:24c3:9128:3349:3cee:148"}, IPMask{125}},
     {IP6Addr{"2001:1f2d:c587:24c3:9128:3349:3cee:150"}, IPMask{124}},
     {IP6Addr{"2001:1f2d:c587:24c3:9128:3349:3cee:160"}, IPMask{123}},
     {IP6Addr{"2001:1f2d:c587:24c3:9128:3349:3cee:180"}, IPMask{121}},
     {IP6Addr{"2001:1f2d:c587:24c3:9128:3349:3cee:200"}, IPMask{119}},
     {IP6Addr{"2001:1f2d:c587:24c3:9128:3349:3cee:400"}, IPMask{118}},
     {IP6Addr{"2001:1f2d:c587:24c3:9128:3349:3cee:800"}, IPMask{117}},
     {IP6Addr{"2001:1f2d:c587:24c3:9128:3349:3cee:1000"}, IPMask{116}},
     {IP6Addr{"2001:1f2d:c587:24c3:9128:3349:3cee:2000"}, IPMask{115}},
     {IP6Addr{"2001:1f2d:c587:24c3:9128:3349:3cee:4000"}, IPMask{114}},
     {IP6Addr{"2001:1f2d:c587:24c3:9128:3349:3cee:8000"}, IPMask{113}},
     {IP6Addr{"2001:1f2d:c587:24c3:9128:3349:3cef:0"}, IPMask{112}},
     {IP6Addr{"2001:1f2d:c587:24c3:9128:3349:3cf0:0"}, IPMask{108}},
     {IP6Addr{"2001:1f2d:c587:24c3:9128:3349:3d00:0"}, IPMask{104}},
     {IP6Addr{"2001:1f2d:c587:24c3:9128:3349:3e00:0"}, IPMask{103}},
     {IP6Addr{"2001:1f2d:c587:24c3:9128:3349:4000:0"}, IPMask{98}},
     {IP6Addr{"2001:1f2d:c587:24c3:9128:3349:8000:0"}, IPMask{97}},
     {IP6Addr{"2001:1f2d:c587:24c3:9128:334a::"}, IPMask{95}},
     {IP6Addr{"2001:1f2d:c587:24c3:9128:334c::"}, IPMask{94}},
     {IP6Addr{"2001:1f2d:c587:24c3:9128:3350::"}, IPMask{92}},
     {IP6Addr{"2001:1f2d:c587:24c3:9128:3360::"}, IPMask{91}},
     {IP6Addr{"2001:1f2d:c587:24c3:9128:3380::"}, IPMask{89}},
     {IP6Addr{"2001:1f2d:c587:24c3:9128:3400::"}, IPMask{86}},
     {IP6Addr{"2001:1f2d:c587:24c3:9128:3800::"}, IPMask{85}},
     {IP6Addr{"2001:1f2d:c587:24c3:9128:4000::"}, IPMask{82}},
     {IP6Addr{"2001:1f2d:c587:24c3:9128:8000::"}, IPMask{81}},
     {IP6Addr{"2001:1f2d:c587:24c3:9129::"}, IPMask{80}},
     {IP6Addr{"2001:1f2d:c587:24c3:912a::"}, IPMask{79}},
     {IP6Addr{"2001:1f2d:c587:24c3:912c::"}, IPMask{78}},
     {IP6Addr{"2001:1f2d:c587:24c3:9130::"}, IPMask{76}},
     {IP6Addr{"2001:1f2d:c587:24c3:9140::"}, IPMask{74}},
     {IP6Addr{"2001:1f2d:c587:24c3:9180::"}, IPMask{73}},
     {IP6Addr{"2001:1f2d:c587:24c3:9200::"}, IPMask{71}},
     {IP6Addr{"2001:1f2d:c587:24c3:9400::"}, IPMask{70}},
     {IP6Addr{"2001:1f2d:c587:24c3:9800::"}, IPMask{69}},
     {IP6Addr{"2001:1f2d:c587:24c3:a000::"}, IPMask{67}},
     {IP6Addr{"2001:1f2d:c587:24c3:c000::"}, IPMask{66}},
     {IP6Addr{"2001:1f2d:c587:24c4::"}, IPMask{62}},
     {IP6Addr{"2001:1f2d:c587:24c8::"}, IPMask{61}},
     {IP6Addr{"2001:1f2d:c587:24d0::"}, IPMask{60}},
     {IP6Addr{"2001:1f2d:c587:24e0::"}, IPMask{59}},
     {IP6Addr{"2001:1f2d:c587:2500::"}, IPMask{56}},
     {IP6Addr{"2001:1f2d:c587:2600::"}, IPMask{55}},
     {IP6Addr{"2001:1f2d:c587:2800::"}, IPMask{53}},
     {IP6Addr{"2001:1f2d:c587:3000::"}, IPMask{52}},
     {IP6Addr{"2001:1f2d:c587:4000::"}, IPMask{50}},
     {IP6Addr{"2001:1f2d:c587:8000::"}, IPMask{49}},
     {IP6Addr{"2001:1f2d:c588::"}, IPMask{45}},
     {IP6Addr{"2001:1f2d:c590::"}, IPMask{44}},
     {IP6Addr{"2001:1f2d:c5a0::"}, IPMask{43}},
     {IP6Addr{"2001:1f2d:c5c0::"}, IPMask{42}},
     {IP6Addr{"2001:1f2d:c600::"}, IPMask{39}},
     {IP6Addr{"2001:1f2d:c800::"}, IPMask{37}},
     {IP6Addr{"2001:1f2d:d000::"}, IPMask{36}},
     {IP6Addr{"2001:1f2d:e000::"}, IPMask{35}},
     {IP6Addr{"2001:1f2e::"}, IPMask{31}},
     {IP6Addr{"2001:1f30::"}, IPMask{28}},
     {IP6Addr{"2001:1f40::"}, IPMask{26}},
     {IP6Addr{"2001:1f80::"}, IPMask{25}},
     {IP6Addr{"2001:2000::"}, IPMask{19}},
     {IP6Addr{"2001:4000::"}, IPMask{18}},
     {IP6Addr{"2001:8000::"}, IPMask{17}},
     {IP6Addr{"2002::"}, IPMask{15}},
     {IP6Addr{"2004::"}, IPMask{14}},
     {IP6Addr{"2008::"}, IPMask{13}},
     {IP6Addr{"2010::"}, IPMask{12}},
     {IP6Addr{"2020::"}, IPMask{11}},
     {IP6Addr{"2040::"}, IPMask{10}},
     {IP6Addr{"2080::"}, IPMask{9}},
     {IP6Addr{"2100::"}, IPMask{8}},
     {IP6Addr{"2200::"}, IPMask{7}},
     {IP6Addr{"2400::"}, IPMask{6}},
     {IP6Addr{"2800::"}, IPMask{5}},
     {IP6Addr{"3000::"}, IPMask{4}},
     {IP6Addr{"4000::"}, IPMask{2}},
     {IP6Addr{"8000::"}, IPMask{2}},
     {IP6Addr{"c000::"}, IPMask{3}},
     {IP6Addr{"e000::"}, IPMask{4}},
     {IP6Addr{"f000::"}, IPMask{5}},
     {IP6Addr{"f800::"}, IPMask{6}},
     {IP6Addr{"fc00::"}, IPMask{7}},
     {IP6Addr{"fe00::"}, IPMask{8}},
     {IP6Addr{"ff00::"}, IPMask{9}},
     {IP6Addr{"ff80::"}, IPMask{10}},
     {IP6Addr{"ffc0::"}, IPMask{11}},
     {IP6Addr{"ffe0::"}, IPMask{13}},
     {IP6Addr{"ffe8::"}, IPMask{14}},
     {IP6Addr{"ffec::"}, IPMask{15}},
     {IP6Addr{"ffee::"}, IPMask{20}},
     {IP6Addr{"ffee:1000::"}, IPMask{21}},
     {IP6Addr{"ffee:1800::"}, IPMask{22}},
     {IP6Addr{"ffee:1c00::"}, IPMask{23}},
     {IP6Addr{"ffee:1e00::"}, IPMask{24}},
     {IP6Addr{"ffee:1f00::"}, IPMask{27}},
     {IP6Addr{"ffee:1f20::"}, IPMask{29}},
     {IP6Addr{"ffee:1f28::"}, IPMask{30}},
     {IP6Addr{"ffee:1f2c::"}, IPMask{32}},
     {IP6Addr{"ffee:1f2d::"}, IPMask{33}},
     {IP6Addr{"ffee:1f2d:8000::"}, IPMask{34}},
     {IP6Addr{"ffee:1f2d:c000::"}, IPMask{38}},
     {IP6Addr{"ffee:1f2d:c400::"}, IPMask{40}},
     {IP6Addr{"ffee:1f2d:c500::"}, IPMask{41}},
     {IP6Addr{"ffee:1f2d:c580::"}, IPMask{46}},
     {IP6Addr{"ffee:1f2d:c584::"}, IPMask{47}},
     {IP6Addr{"ffee:1f2d:c586::"}, IPMask{48}},
     {IP6Addr{"ffee:1f2d:c587::"}, IPMask{51}},
     {IP6Addr{"ffee:1f2d:c587:2000::"}, IPMask{54}},
     {IP6Addr{"ffee:1f2d:c587:2400::"}, IPMask{57}},
     {IP6Addr{"ffee:1f2d:c587:2480::"}, IPMask{58}},
     {IP6Addr{"ffee:1f2d:c587:24c0::"}, IPMask{63}},
     {IP6Addr{"ffee:1f2d:c587:24c2::"}, IPMask{64}},
     {IP6Addr{"ffee:1f2d:c587:24c3::"}, IPMask{65}},
     {IP6Addr{"ffee:1f2d:c587:24c3:8000::"}, IPMask{68}},
     {IP6Addr{"ffee:1f2d:c587:24c3:9000::"}, IPMask{72}},
     {IP6Addr{"ffee:1f2d:c587:24c3:9100::"}, IPMask{75}},
     {IP6Addr{"ffee:1f2d:c587:24c3:9120::"}, IPMask{77}},
     {IP6Addr{"ffee:1f2d:c587:24c3:9128::"}, IPMask{83}},
     {IP6Addr{"ffee:1f2d:c587:24c3:9128:2000::"}, IPMask{84}},
     {IP6Addr{"ffee:1f2d:c587:24c3:9128:3000::"}, IPMask{87}},
     {IP6Addr{"ffee:1f2d:c587:24c3:9128:3200::"}, IPMask{88}},
     {IP6Addr{"ffee:1f2d:c587:24c3:9128:3300::"}, IPMask{90}},
     {IP6Addr{"ffee:1f2d:c587:24c3:9128:3340::"}, IPMask{93}},
     {IP6Addr{"ffee:1f2d:c587:24c3:9128:3348::"}, IPMask{96}},
     {IP6Addr{"ffee:1f2d:c587:24c3:9128:3349::"}, IPMask{99}},
     {IP6Addr{"ffee:1f2d:c587:24c3:9128:3349:2000:0"}, IPMask{100}},
     {IP6Addr{"ffee:1f2d:c587:24c3:9128:3349:3000:0"}, IPMask{101}},
     {IP6Addr{"ffee:1f2d:c587:24c3:9128:3349:3800:0"}, IPMask{102}},
     {IP6Addr{"ffee:1f2d:c587:24c3:9128:3349:3c00:0"}, IPMask{104}}}
  };

  auto r5_net = r_5_nets.begin();
  for (auto const &[a, m] : r_5.networks()) {
    REQUIRE(r5_net != r_5_nets.end());
    CHECK(*r5_net == swoc::IP6Net{a, m});
    ++r5_net;
  }

  // Try it again, using @c IPNet.
  r5_net = r_5_nets.begin();
  for (auto const &[a, m] : IPRange{r_5}.networks()) {
    REQUIRE(r5_net != r_5_nets.end());
    CHECK(*r5_net == swoc::IPNet{a, m});
    ++r5_net;
  }
}

TEST_CASE("IP Space Int", "[libswoc][ip][ipspace]") {
  using uint_space = swoc::IPSpace<unsigned>;
  uint_space space;

  REQUIRE(space.count() == 0);

  space.mark(
    IPRange{
      {IP4Addr("172.16.0.0"), IP4Addr("172.16.0.255")}
  },
    1);
  auto result = space.find(IPAddr{"172.16.0.97"});
  REQUIRE(result != space.end());
  REQUIRE(std::get<1>(*result) == 1);

  result = space.find(IPAddr{"172.17.0.97"});
  REQUIRE(result == space.end());

  space.mark(IPRange{"172.16.0.12-172.16.0.25"_tv}, 2);

  result = space.find(IPAddr{"172.16.0.21"});
  REQUIRE(result != space.end());
  REQUIRE(std::get<1>(*result) == 2);
  REQUIRE(space.count() == 3);

  space.clear();
  auto BF = [](unsigned &lhs, unsigned rhs) -> bool {
    lhs |= rhs;
    return true;
  };

  swoc::IP4Range r_1{"1.1.1.0-1.1.1.9"};
  swoc::IP4Range r_2{"1.1.2.0-1.1.2.97"};
  swoc::IP4Range r_3{"1.1.0.0-1.2.0.0"};

  // Compiler check - make sure both of these work.
  REQUIRE(r_1.min() == IP4Addr("1.1.1.0"_tv));
  REQUIRE(r_1.max() == IPAddr("1.1.1.9"_tv));

  space.blend(r_1, 0x1, BF);
  REQUIRE(space.count() == 1);
  REQUIRE(space.end() == space.find(r_2.min()));
  REQUIRE(space.end() != space.find(r_1.min()));
  REQUIRE(space.end() != space.find(r_1.max()));
  REQUIRE(space.end() != space.find(IP4Addr{"1.1.1.7"}));
  CHECK(0x1 == std::get<1>(*space.find(IP4Addr{"1.1.1.7"})));

  space.blend(r_2, 0x2, BF);
  REQUIRE(space.count() == 2);
  REQUIRE(space.end() != space.find(r_1.min()));
  auto spot = space.find(r_2.min());
  REQUIRE(spot != space.end());
  REQUIRE(std::get<1>(*spot) == 0x2);
  spot = space.find(r_2.max());
  REQUIRE(spot != space.end());
  REQUIRE(std::get<1>(*spot) == 0x2);

  space.blend(r_3, 0x4, BF);
  REQUIRE(space.count() == 5);
  spot = space.find(r_2.min());
  REQUIRE(spot != space.end());
  REQUIRE(std::get<1>(*spot) == 0x6);

  spot = space.find(r_3.min());
  REQUIRE(spot != space.end());
  REQUIRE(std::get<1>(*spot) == 0x4);

  spot = space.find(r_1.max());
  REQUIRE(spot != space.end());
  REQUIRE(std::get<1>(*spot) == 0x5);

  space.blend(IPRange{r_2.min(), r_3.max()}, 0x6, BF);
  REQUIRE(space.count() == 4);

  std::array<std::tuple<TextView, int>, 9> ranges = {
    {{"100.0.0.0-100.0.0.255", 0},
     {"100.0.1.0-100.0.1.255", 1},
     {"100.0.2.0-100.0.2.255", 2},
     {"100.0.3.0-100.0.3.255", 3},
     {"100.0.4.0-100.0.4.255", 4},
     {"100.0.5.0-100.0.5.255", 5},
     {"100.0.6.0-100.0.6.255", 6},
     {"100.0.0.0-100.0.0.255", 31},
     {"100.0.1.0-100.0.1.255", 30}}
  };

  space.clear();
  for (auto &&[text, value] : ranges) {
    IPRange range{text};
    space.mark(IPRange{text}, value);
  }

  CHECK(7 == space.count());
  // Make sure all of these addresses yield the same result.
  CHECK(space.end() != space.find(IP4Addr{"100.0.4.16"}));
  CHECK(space.end() != space.find(IPAddr{"100.0.4.16"}));
  CHECK(space.end() != space.find(IPAddr{IPEndpoint{"100.0.4.16:80"}}));
  // same for negative result
  CHECK(space.end() == space.find(IP4Addr{"10.0.4.16"}));
  CHECK(space.end() == space.find(IPAddr{"10.0.4.16"}));
  CHECK(space.end() == space.find(IPAddr{IPEndpoint{"10.0.4.16:80"}}));

  std::array<std::tuple<TextView, int>, 3> r_clear = {
    {{"2.2.2.2-2.2.2.40", 0}, {"2.2.2.50-2.2.2.60", 1}, {"2.2.2.70-2.2.2.100", 2}}
  };
  space.clear();
  for (auto &&[text, value] : r_clear) {
    IPRange range{text};
    space.mark(IPRange{text}, value);
  }
  CHECK(space.count() == 3);
  space.erase(IPRange{"2.2.2.35-2.2.2.75"});
  CHECK(space.count() == 2);
  {
    spot          = space.begin();
    auto [r0, p0] = *spot;
    auto [r2, p2] = *++spot;
    CHECK(r0 == IPRange{"2.2.2.2-2.2.2.34"});
    CHECK(p0 == 0);
    CHECK(r2 == IPRange{"2.2.2.76-2.2.2.100"});
    CHECK(p2 == 2);
  }

  // This is about testing repeated colorings of the same addresses, which happens quite a
  // bit in normal network datasets. In fact, the test dataset is based on such a dataset
  // and its use.
  auto b2 = [](unsigned &lhs, unsigned const &rhs) {
    lhs = rhs;
    return true;
  };
  std::array<std::tuple<TextView, unsigned>, 31> r2 = {
    {
     {"2001:4998:58:400::1/128", 1} // 1
      ,
     {"2001:4998:58:400::2/128", 1},
     {"2001:4998:58:400::3/128", 1},
     {"2001:4998:58:400::4/128", 1},
     {"2001:4998:58:400::5/128", 1},
     {"2001:4998:58:400::6/128", 1},
     {"2001:4998:58:400::7/128", 1},
     {"2001:4998:58:400::8/128", 1},
     {"2001:4998:58:400::9/128", 1},
     {"2001:4998:58:400::A/127", 1},
     {"2001:4998:58:400::10/127", 1} // 2
      ,
     {"2001:4998:58:400::12/127", 1},
     {"2001:4998:58:400::14/127", 1},
     {"2001:4998:58:400::16/127", 1},
     {"2001:4998:58:400::18/127", 1},
     {"2001:4998:58:400::1a/127", 1},
     {"2001:4998:58:400::1c/127", 1},
     {"2001:4998:58:400::1e/127", 1},
     {"2001:4998:58:400::20/127", 1},
     {"2001:4998:58:400::22/127", 1},
     {"2001:4998:58:400::24/127", 1},
     {"2001:4998:58:400::26/127", 1},
     {"2001:4998:58:400::2a/127", 1} // 3
      ,
     {"2001:4998:58:400::2c/127", 1},
     {"2001:4998:58:400::2e/127", 1},
     {"2001:4998:58:400::30/127", 1},
     {"2001:4998:58:400::140/127", 1} // 4
      ,
     {"2001:4998:58:400::142/127", 1},
     {"2001:4998:58:400::146/127", 1} // 5
      ,
     {"2001:4998:58:400::148/127", 1},
     {"2001:4998:58:400::150/127", 1} // 6
    }
  };

  space.clear();
  // Start with basic blending.
  for (auto &&[text, value] : r2) {
    IPRange range{text};
    space.blend(IPRange{text}, value, b2);
    REQUIRE(space.end() != space.find(range.min()));
    REQUIRE(space.end() != space.find(range.max()));
  }
  CHECK(6 == space.count());
  // Do the exact same networks again, should not change the range count.
  for (auto &&[text, value] : r2) {
    IPRange range{text};
    space.blend(IPRange{text}, value, b2);
    REQUIRE(space.end() != space.find(range.min()));
    REQUIRE(space.end() != space.find(range.max()));
  }
  CHECK(6 == space.count());
  // Verify that earlier ranges are still valid after the double blend.
  for (auto &&[text, value] : r2) {
    IPRange range{text};
    REQUIRE(space.end() != space.find(range.min()));
    REQUIRE(space.end() != space.find(range.max()));
  }
  // Color the non-intersecting range between ranges 1 and 2, verify coalesce.
  space.blend(IPRange{"2001:4998:58:400::C/126"_tv}, 1, b2);
  CHECK(5 == space.count());
  // Verify all the data is in the ranges.
  for (auto &&[text, value] : r2) {
    IPRange range{text};
    REQUIRE(space.end() != space.find(range.min()));
    REQUIRE(space.end() != space.find(range.max()));
  }

  // Check some syntax.
  {
    auto a      = IPAddr{"2001:4998:58:400::1E"};
    auto [r, p] = *space.find(a);
    REQUIRE_FALSE(r.empty());
    REQUIRE(p == 1);
  }
  {
    auto [r, p] = *space.find(IPAddr{"2001:4997:58:400::1E"});
    REQUIRE(r.empty());
  }

  space.clear();
  // Test a mix
  unsigned idx = 0;
  std::array<TextView, 6> mix_r{"1.1.1.1-1.1.1.111",
                                "2.2.2.2-2.2.2.222",
                                "3.3.3.3-3.255.255.255",
                                "1:2:3:4:5:6:7:8-1:2:3:4:5:6:7:ffff",
                                "11:2:3:4:5:6:7:8-11:2:3:4:5:6:7:ffff",
                                "111:2:3:4:5:6:7:8-111:2:3:4:5:6:7:ffff"};
  for (auto &&r : mix_r) {
    space.mark(IPRange(r), idx);
    ++idx;
  }

  idx = 0;
  std::string s;
  for (auto [r, p] : space) {
    REQUIRE(!r.empty());
    REQUIRE(p == idx);
    swoc::LocalBufferWriter<64> dbg;
    bwformat(dbg, swoc::bwf::Spec::DEFAULT, r);
    bwprint(s, "{}", r);
    REQUIRE(s == mix_r[idx]);
    ++idx;
  }
}

TEST_CASE("IPSpace bitset", "[libswoc][ipspace][bitset]") {
  using PAYLOAD = std::bitset<32>;
  using Space   = swoc::IPSpace<PAYLOAD>;

  std::array<std::tuple<TextView, std::initializer_list<unsigned>>, 6> ranges = {
    {{"172.28.56.12-172.28.56.99"_tv, {0, 2, 3}},
     {"10.10.35.0/24"_tv, {1, 2}},
     {"192.168.56.0/25"_tv, {10, 12, 31}},
     {"1337::ded:beef-1337::ded:ceef"_tv, {4, 5, 6, 7}},
     {"ffee:1f2d:c587:24c3:9128:3349:3cee:143-ffee:1f2d:c587:24c3:9128:3349:3cFF:FFFF"_tv, {9, 10, 18}},
     {"10.12.148.0/23"_tv, {1, 2, 17}}}
  };

  Space space;

  for (auto &&[text, bit_list] : ranges) {
    PAYLOAD bits;
    for (auto bit : bit_list) {
      bits[bit] = true;
    }
    space.mark(IPRange{text}, bits);
  }
  REQUIRE(space.count() == ranges.size());

  // Check that if an IPv4 lookup misses, it doesn't pass on to the first IPv6
  auto [r1, p1] = *(space.find(IP4Addr{"172.28.56.100"}));
  REQUIRE(true == r1.empty());
  auto [r2, p2] = *(space.find(IPAddr{"172.28.56.100"}));
  REQUIRE(true == r2.empty());
}

TEST_CASE("IPSpace docJJ", "[libswoc][ipspace][docJJ]") {
  using PAYLOAD = std::bitset<32>;
  using Space   = swoc::IPSpace<PAYLOAD>;
  // Add the bits in @rhs to the range.
  auto blender = [](PAYLOAD &lhs, PAYLOAD const &rhs) -> bool {
    lhs |= rhs;
    return true;
  };
  // Add bit @a idx iff bits are already set.
  auto additive = [](PAYLOAD &lhs, unsigned idx) -> bool {
    if (!lhs.any()) {
      return false;
    }
    lhs[idx] = true;
    return true;
  };

  auto make_bits = [](std::initializer_list<unsigned> idx) -> PAYLOAD {
    PAYLOAD bits;
    for (auto bit : idx) {
      bits[bit] = true;
    }
    return bits;
  };

  std::array<std::tuple<TextView, PAYLOAD>, 9> ranges = {
    {{"100.0.0.0-100.0.0.255", make_bits({0})},
     {"100.0.1.0-100.0.1.255", make_bits({1})},
     {"100.0.2.0-100.0.2.255", make_bits({2})},
     {"100.0.3.0-100.0.3.255", make_bits({3})},
     {"100.0.4.0-100.0.4.255", make_bits({4})},
     {"100.0.5.0-100.0.5.255", make_bits({5})},
     {"100.0.6.0-100.0.6.255", make_bits({6})},
     {"100.0.0.0-100.0.0.255", make_bits({31})},
     {"100.0.1.0-100.0.1.255", make_bits({30})}}
  };

  static const std::array<PAYLOAD, 7> results = {make_bits({0, 31}), make_bits({1, 30}), make_bits({2}), make_bits({3}),
                                                 make_bits({4}),     make_bits({5}),     make_bits({6})};

  Space space;

  for (auto &&[text, bit_list] : ranges) {
    space.blend(IPRange{text}, bit_list, blender);
  }

  // Check iteration - verify forward and reverse iteration yield the correct number of ranges
  // and the range payloads match what is expected.
  REQUIRE(space.count() == results.size());

  unsigned idx;

  idx = 0;
  for (auto const &[range, bits] : space) {
    CHECK(bits == results[idx]);
    ++idx;
  }

  idx = 0;
  for (auto spot = space.begin(); spot != space.end() && idx < results.size(); ++spot) {
    auto const &[range, bits]{*spot};
    CHECK(bits == results[idx]);
    ++idx;
  }

  idx = results.size();
  for (auto spot = space.end(); spot != space.begin();) {
    auto const &[range, bits]{*--spot};
    REQUIRE(idx > 0);
    --idx;
    CHECK(bits == results[idx]);
  }

  // Check iterator copying.
  idx = 0;
  Space::iterator iter;
  IPRange range;
  PAYLOAD bits;
  for (auto spot = space.begin(); spot != space.end(); ++spot, ++idx) {
    std::tie(range, bits) = spot->tuple();
    CHECK(bits == results[idx]);
  }

  // This blend should change only existing ranges, not add range.
  space.blend(IPRange{"99.128.0.0-100.0.1.255"}, 27, additive);
  REQUIRE(space.count() == results.size()); // no more ranges.
  // Verify first two ranges modified, but not the next.
  REQUIRE(std::get<1>(*(space.find(IP4Addr{"100.0.0.37"}))) == make_bits({0, 27, 31}));
  REQUIRE(std::get<1>(*(space.find(IP4Addr{"100.0.1.37"}))) == make_bits({1, 27, 30}));
  REQUIRE(std::get<1>(*(space.find(IP4Addr{"100.0.2.37"}))) == make_bits({2}));

  space.blend(IPRange{"100.10.1.1-100.10.2.2"}, make_bits({15}), blender);
  REQUIRE(space.count() == results.size() + 1);
  // Color in empty range - should not add range.
  space.blend(IPRange{"100.8.10.25"}, 27, additive);
  REQUIRE(space.count() == results.size() + 1);
}

TEST_CASE("IPSpace Edge", "[libswoc][ipspace][edge]") {
  struct Thing {
    unsigned _n;
    Thing(Thing const &)            = delete; // No copy.
    Thing &operator=(Thing const &) = delete; // No self assignment.
    bool
    operator==(Thing const &that) const {
      return _n == that._n;
    }
  };
  using Space = IPSpace<Thing>;
  Space space;

  IP4Addr a1{"192.168.99.99"};
  if (auto [r, p] = *(space.find(a1)); !r.empty()) {
    REQUIRE(false); // Checking this syntax doesn't copy the payload.
  }

  auto const &cspace = space;
  if (auto [r, p] = *(cspace.find(a1)); !r.empty()) {
    Thing const &cp = p;
    static_assert(std::is_const_v<typeof(cp)>, "Payload was expected to be const.");
    REQUIRE(false); // Checking this syntax doesn't copy the payload.
  }
  if (auto [r, p] = *(cspace.find(a1)); !r.empty()) {
    static_assert(std::is_const_v<typeof(p)>, "Payload was expected to be const.");
    REQUIRE(false); // Checking this syntax doesn't copy the payload.
  }

  auto spot = cspace.find(a1);
  static_assert(std::is_same_v<Space::const_iterator, decltype(spot)>);
  auto &v1 = *spot;
  auto &p1 = get<1>(v1);

  if (auto &&[r, p] = *(cspace.find(a1)); !r.empty()) {
    static_assert(std::is_same_v<swoc::IPRangeView const &, decltype(r)>);
    IPRange rr = r;
    swoc::IPRangeView rvv{r};
    swoc::IPRangeView rv = r;
    REQUIRE(rv == rr);
  }
}

TEST_CASE("IPSpace Uthira", "[libswoc][ipspace][uthira]") {
  struct Data {
    TextView _pod;
    int _rack = 0;
    int _code = 0;

    bool
    operator==(Data const &that) const {
      return _pod == that._pod && _rack == that._rack && _code == that._code;
    }
  };
  auto pod_blender = [](Data &data, TextView const &p) {
    data._pod = p;
    return true;
  };
  auto rack_blender = [](Data &data, int r) {
    data._rack = r;
    return true;
  };
  auto code_blender = [](Data &data, int c) {
    data._code = c;
    return true;
  };
  swoc::IPSpace<Data> space;
  // This is overkill, but no reason to not slam the code.
  // For the original bug that triggered this testing, only the first line is actually necessary
  // to cause the problem.
  TextView content = R"(10.215.88.12-10.215.88.12,pdb,9
    10.215.88.13-10.215.88.13,pdb,9
    10.215.88.0-10.215.88.1,pdb,9
    10.215.88.2-10.215.88.3,pdb,9
    10.215.88.4-10.215.88.5,pdb,9
    10.215.88.6-10.215.88.7,pdb,9
    10.215.88.8-10.215.88.9,pdb,9
    10.215.88.10-10.215.88.11,pdb,9
    10.214.128.0-10.214.128.63,pda,1
    10.214.128.64-10.214.128.127,pda,1
    10.214.128.128-10.214.128.191,pda,1
    10.214.128.192-10.214.128.255,pda,1
    10.214.129.0-10.214.129.63,pda,1
    10.214.129.64-10.214.129.127,pda,1
    10.214.129.128-10.214.129.191,pda,1
    10.214.129.192-10.214.129.255,pda,1
    10.214.130.0-10.214.130.63,pda,1
    10.214.130.64-10.214.130.127,pda,1
    10.214.130.128-10.214.130.191,pda,1
    10.214.130.192-10.214.130.255,pda,1
    10.214.131.0-10.214.131.63,pda,1
    10.214.131.64-10.214.131.127,pda,1
    10.214.131.128-10.214.131.191,pda,1
    10.214.131.192-10.214.131.255,pda,1
    10.214.132.0-10.214.132.63,pda,1
    10.214.132.64-10.214.132.127,pda,1
    10.214.132.128-10.214.132.191,pda,1
    10.214.132.192-10.214.132.255,pda,1
    10.214.133.0-10.214.133.63,pda,1
    10.214.133.64-10.214.133.127,pda,1
    10.214.133.128-10.214.133.191,pda,1
    10.214.133.192-10.214.133.255,pda,1
    10.214.134.0-10.214.134.63,pda,1
    10.214.134.64-10.214.134.127,pda,1
    10.214.134.128-10.214.134.191,pda,1
    10.214.134.192-10.214.134.255,pda,1
    10.214.135.0-10.214.135.63,pda,1
    10.214.135.64-10.214.135.127,pda,1
    10.214.135.128-10.214.135.191,pda,1
    10.214.135.192-10.214.135.255,pda,1
    10.214.140.0-10.214.140.63,pda,1
    10.214.140.64-10.214.140.127,pda,1
    10.214.140.128-10.214.140.191,pda,1
    10.214.140.192-10.214.140.255,pda,1
    10.214.141.0-10.214.141.63,pda,1
    10.214.141.64-10.214.141.127,pda,1
    10.214.141.128-10.214.141.191,pda,1
    10.214.141.192-10.214.141.255,pda,1
    10.214.145.0-10.214.145.63,pda,1
    10.214.145.64-10.214.145.127,pda,1
    10.214.145.128-10.214.145.191,pda,1
    10.214.145.192-10.214.145.255,pda,1
    10.214.146.0-10.214.146.63,pda,1
    10.214.146.64-10.214.146.127,pda,1
    10.214.146.128-10.214.146.191,pda,1
    10.214.146.192-10.214.146.255,pda,1
    10.214.147.0-10.214.147.63,pda,1
    10.214.147.64-10.214.147.127,pda,1
    10.214.147.128-10.214.147.191,pda,1
    10.214.147.192-10.214.147.255,pda,1
    10.214.152.0-10.214.152.63,pda,1
    10.214.152.64-10.214.152.127,pda,1
    10.214.152.128-10.214.152.191,pda,1
    10.214.152.192-10.214.152.255,pda,1
    10.214.153.0-10.214.153.63,pda,1
    10.214.153.64-10.214.153.127,pda,1
    10.214.153.128-10.214.153.191,pda,1
    10.214.153.192-10.214.153.255,pda,1
    10.214.154.0-10.214.154.63,pda,1
    10.214.154.64-10.214.154.127,pda,1
    10.214.154.128-10.214.154.191,pda,1
    10.214.154.192-10.214.154.255,pda,1
    10.214.155.0-10.214.155.63,pda,1
    10.214.155.64-10.214.155.127,pda,1
    10.214.155.128-10.214.155.191,pda,1
    10.214.155.192-10.214.155.255,pda,1
    10.214.156.0-10.214.156.63,pda,1
    10.214.156.64-10.214.156.127,pda,1
    10.214.156.128-10.214.156.191,pda,1
    10.214.156.192-10.214.156.255,pda,1
    10.214.157.0-10.214.157.63,pda,1
    10.214.157.64-10.214.157.127,pda,1
    10.214.157.128-10.214.157.191,pda,1
    10.214.157.192-10.214.157.255,pda,1
    10.214.158.0-10.214.158.63,pda,1
    10.214.158.64-10.214.158.127,pda,1
    10.214.158.128-10.214.158.191,pda,1
    10.214.158.192-10.214.158.255,pda,1
    10.214.164.0-10.214.164.63,pda,1
    10.214.164.64-10.214.164.127,pda,1
    10.214.167.0-10.214.167.63,pda,1
    10.214.167.64-10.214.167.127,pda,1
    10.214.167.128-10.214.167.191,pda,1
    10.214.167.192-10.214.167.255,pda,1
    10.214.168.0-10.214.168.63,pda,1
    10.214.168.64-10.214.168.127,pda,1
    10.214.168.128-10.214.168.191,pda,1
    10.214.168.192-10.214.168.255,pda,1
    10.214.169.0-10.214.169.63,pda,1
    10.214.169.64-10.214.169.127,pda,1
    10.214.169.128-10.214.169.191,pda,1
    10.214.169.192-10.214.169.255,pda,1
    10.214.172.0-10.214.172.63,pda,1
    10.214.172.64-10.214.172.127,pda,1
    10.214.172.128-10.214.172.191,pda,1
    10.214.172.192-10.214.172.255,pda,1
    10.214.173.0-10.214.173.63,pda,1
    10.214.173.64-10.214.173.127,pda,1
    10.214.173.128-10.214.173.191,pda,1
    10.214.173.192-10.214.173.255,pda,1
    10.214.219.128-10.214.219.191,pda,1
    10.214.219.192-10.214.219.255,pda,1
    10.214.245.0-10.214.245.63,pda,1
    10.214.245.64-10.214.245.127,pda,1
    10.215.64.0-10.215.64.63,pda,1
    10.215.64.64-10.215.64.127,pda,1
    10.215.64.128-10.215.64.191,pda,1
    10.215.64.192-10.215.64.255,pda,1
    10.215.65.128-10.215.65.191,pda,1
    10.215.65.192-10.215.65.255,pda,1
    10.215.66.0-10.215.66.63,pda,1
    10.215.66.64-10.215.66.127,pda,1
    10.215.66.128-10.215.66.191,pda,1
    10.215.66.192-10.215.66.255,pda,1
    10.215.67.0-10.215.67.63,pda,1
    10.215.67.64-10.215.67.127,pda,1
    10.215.71.0-10.215.71.63,pda,1
    10.215.71.64-10.215.71.127,pda,1
    10.215.71.128-10.215.71.191,pda,1
    10.215.71.192-10.215.71.255,pda,1
    10.215.72.0-10.215.72.63,pda,1
    10.215.72.64-10.215.72.127,pda,1
    10.215.72.128-10.215.72.191,pda,1
    10.215.72.192-10.215.72.255,pda,1
    10.215.80.0-10.215.80.63,pda,1
    10.215.80.64-10.215.80.127,pda,1
    10.215.80.128-10.215.80.191,pda,1
    10.215.80.192-10.215.80.255,pda,1
    10.215.81.0-10.215.81.63,pda,1
    10.215.81.64-10.215.81.127,pda,1
    10.215.81.128-10.215.81.191,pda,1
    10.215.81.192-10.215.81.255,pda,1
    10.215.82.0-10.215.82.63,pda,1
    10.215.82.64-10.215.82.127,pda,1
    10.215.82.128-10.215.82.191,pda,1
    10.215.82.192-10.215.82.255,pda,1
    10.215.84.0-10.215.84.63,pda,1
    10.215.84.64-10.215.84.127,pda,1
    10.215.84.128-10.215.84.191,pda,1
    10.215.84.192-10.215.84.255,pda,1
    10.215.88.64-10.215.88.127,pdb,1
    10.215.88.128-10.215.88.191,pdb,1
    10.215.88.192-10.215.88.255,pdb,1
    10.215.89.0-10.215.89.63,pdb,1
    10.215.89.64-10.215.89.127,pdb,1
    10.215.89.128-10.215.89.191,pdb,1
    10.215.89.192-10.215.89.255,pdb,1
    10.215.90.0-10.215.90.63,pdb,1
    10.215.90.64-10.215.90.127,pdb,1
    10.215.90.128-10.215.90.191,pdb,1
    10.215.100.0-10.215.100.63,pda,1
    10.215.132.0-10.215.132.63,pda,1
    10.215.132.64-10.215.132.127,pda,1
    10.215.132.128-10.215.132.191,pda,1
    10.215.132.192-10.215.132.255,pda,1
    10.215.133.0-10.215.133.63,pda,1
    10.215.133.64-10.215.133.127,pda,1
    10.215.133.128-10.215.133.191,pda,1
    10.215.133.192-10.215.133.255,pda,1
    10.215.134.0-10.215.134.63,pda,1
    10.215.134.64-10.215.134.127,pda,1
    10.215.134.128-10.215.134.191,pda,1
    10.215.134.192-10.215.134.255,pda,1
    10.215.135.0-10.215.135.63,pda,1
    10.215.135.64-10.215.135.127,pda,1
    10.215.135.128-10.215.135.191,pda,1
    10.215.135.192-10.215.135.255,pda,1
    10.215.136.0-10.215.136.63,pda,1
    10.215.136.64-10.215.136.127,pda,1
    10.215.136.128-10.215.136.191,pda,1
    10.215.136.192-10.215.136.255,pda,1
    10.215.137.0-10.215.137.63,pda,1
    10.215.137.64-10.215.137.127,pda,1
    10.215.137.128-10.215.137.191,pda,1
    10.215.137.192-10.215.137.255,pda,1
    10.215.138.0-10.215.138.63,pda,1
    10.215.138.64-10.215.138.127,pda,1
    10.215.138.128-10.215.138.191,pda,1
    10.215.138.192-10.215.138.255,pda,1
    10.215.139.0-10.215.139.63,pda,1
    10.215.139.64-10.215.139.127,pda,1
    10.215.139.128-10.215.139.191,pda,1
    10.215.139.192-10.215.139.255,pda,1
    10.215.144.0-10.215.144.63,pda,1
    10.215.144.64-10.215.144.127,pda,1
    10.215.144.128-10.215.144.191,pda,1
    10.215.144.192-10.215.144.255,pda,1
    10.215.145.0-10.215.145.63,pda,1
    10.215.145.64-10.215.145.127,pda,1
    10.215.145.128-10.215.145.191,pda,1
    10.215.145.192-10.215.145.255,pda,1
    10.215.146.0-10.215.146.63,pda,1
    10.215.146.64-10.215.146.127,pda,1
    10.215.146.128-10.215.146.191,pda,1
    10.215.146.192-10.215.146.255,pda,1
    10.215.147.0-10.215.147.63,pda,1
    10.215.147.64-10.215.147.127,pda,1
    10.215.147.128-10.215.147.191,pda,1
    10.215.147.192-10.215.147.255,pda,1
    10.215.166.0-10.215.166.63,pda,1
    10.215.166.64-10.215.166.127,pda,1
    10.215.166.128-10.215.166.191,pda,1
    10.215.166.192-10.215.166.255,pda,1
    10.215.167.0-10.215.167.63,pda,1
    10.215.167.64-10.215.167.127,pda,1
    10.215.167.128-10.215.167.191,pda,1
    10.215.167.192-10.215.167.255,pda,1
    10.215.170.0-10.215.170.63,pda,1
    10.215.170.64-10.215.170.127,pda,1
    10.215.170.128-10.215.170.191,pda,1
    10.215.170.192-10.215.170.255,pda,1
    10.215.171.0-10.215.171.63,pda,1
    10.215.171.64-10.215.171.127,pda,1
    10.215.171.128-10.215.171.191,pda,1
    10.215.171.192-10.215.171.255,pda,1
    10.215.172.0-10.215.172.63,pda,1
    10.215.172.64-10.215.172.127,pda,1
    10.215.172.128-10.215.172.191,pda,1
    10.215.172.192-10.215.172.255,pda,1
    10.215.173.0-10.215.173.63,pda,1
    10.215.173.64-10.215.173.127,pda,1
    10.215.173.128-10.215.173.191,pda,1
    10.215.173.192-10.215.173.255,pda,1
    10.215.174.0-10.215.174.63,pda,1
    10.215.174.64-10.215.174.127,pda,1
    10.215.174.128-10.215.174.191,pda,1
    10.215.174.192-10.215.174.255,pda,1
    10.215.178.0-10.215.178.63,pda,1
    10.215.178.64-10.215.178.127,pda,1
    10.215.178.128-10.215.178.191,pda,1
    10.215.178.192-10.215.178.255,pda,1
    10.215.179.0-10.215.179.63,pda,1
    10.215.179.64-10.215.179.127,pda,1
    10.215.179.128-10.215.179.191,pda,1
    10.215.179.192-10.215.179.255,pda,1
    10.215.192.0-10.215.192.63,pda,1
    10.215.192.64-10.215.192.127,pda,1
    10.215.192.128-10.215.192.191,pda,1
    10.215.192.192-10.215.192.255,pda,1
    10.215.193.0-10.215.193.63,pda,1
    10.215.193.64-10.215.193.127,pda,1
    10.215.193.128-10.215.193.191,pda,1
    10.215.193.192-10.215.193.255,pda,1
    10.215.194.0-10.215.194.63,pda,1
    10.215.194.64-10.215.194.127,pda,1
    10.215.194.128-10.215.194.191,pda,1
    10.215.194.192-10.215.194.255,pda,1
    10.215.195.0-10.215.195.63,pda,1
    10.215.195.64-10.215.195.127,pda,1
    10.215.195.128-10.215.195.191,pda,1
    10.215.195.192-10.215.195.255,pda,1
    10.215.196.0-10.215.196.63,pda,1
    10.215.196.64-10.215.196.127,pda,1
    10.215.196.128-10.215.196.191,pda,1
    10.215.196.192-10.215.196.255,pda,1
    10.215.197.0-10.215.197.63,pda,1
    10.215.197.64-10.215.197.127,pda,1
    10.215.197.128-10.215.197.191,pda,1
    10.215.197.192-10.215.197.255,pda,1
    10.215.198.0-10.215.198.63,pda,1
    10.215.198.64-10.215.198.127,pda,1
    10.215.198.128-10.215.198.191,pda,1
    10.215.198.192-10.215.198.255,pda,1
    10.215.199.0-10.215.199.63,pda,1
    10.215.199.64-10.215.199.127,pda,1
    10.215.199.128-10.215.199.191,pda,1
    10.215.199.192-10.215.199.255,pda,1
    10.215.200.0-10.215.200.63,pda,1
    10.215.200.64-10.215.200.127,pda,1
    10.215.200.128-10.215.200.191,pda,1
    10.215.200.192-10.215.200.255,pda,1
    10.215.201.0-10.215.201.63,pda,1
    10.215.201.64-10.215.201.127,pda,1
    10.215.201.128-10.215.201.191,pda,1
    10.215.201.192-10.215.201.255,pda,1
    10.215.202.0-10.215.202.63,pda,1
    10.215.202.64-10.215.202.127,pda,1
    10.215.202.128-10.215.202.191,pda,1
    10.215.202.192-10.215.202.255,pda,1
    10.215.203.0-10.215.203.63,pda,1
    10.215.203.64-10.215.203.127,pda,1
    10.215.203.128-10.215.203.191,pda,1
    10.215.203.192-10.215.203.255,pda,1
    10.215.204.0-10.215.204.63,pda,1
    10.215.204.64-10.215.204.127,pda,1
    10.215.204.128-10.215.204.191,pda,1
    10.215.204.192-10.215.204.255,pda,1
    10.215.205.0-10.215.205.63,pda,1
    10.215.205.64-10.215.205.127,pda,1
    10.215.205.128-10.215.205.191,pda,1
    10.215.205.192-10.215.205.255,pda,1
    10.215.206.0-10.215.206.63,pda,1
    10.215.206.64-10.215.206.127,pda,1
    10.215.206.128-10.215.206.191,pda,1
    10.215.206.192-10.215.206.255,pda,1
    10.215.207.0-10.215.207.63,pda,1
    10.215.207.64-10.215.207.127,pda,1
    10.215.207.128-10.215.207.191,pda,1
    10.215.207.192-10.215.207.255,pda,1
    10.215.208.0-10.215.208.63,pda,1
    10.215.208.64-10.215.208.127,pda,1
    10.215.208.128-10.215.208.191,pda,1
    10.215.208.192-10.215.208.255,pda,1
    10.215.209.0-10.215.209.63,pda,1
    10.215.209.64-10.215.209.127,pda,1
    10.215.209.128-10.215.209.191,pda,1
    10.215.209.192-10.215.209.255,pda,1
    10.215.210.0-10.215.210.63,pda,1
    10.215.210.64-10.215.210.127,pda,1
    10.215.210.128-10.215.210.191,pda,1
    10.215.210.192-10.215.210.255,pda,1
    10.215.211.0-10.215.211.63,pda,1
    10.215.211.64-10.215.211.127,pda,1
    10.215.211.128-10.215.211.191,pda,1
    10.215.211.192-10.215.211.255,pda,1
    10.215.212.0-10.215.212.63,pda,1
    10.215.212.64-10.215.212.127,pda,1
    10.215.212.128-10.215.212.191,pda,1
    10.215.212.192-10.215.212.255,pda,1
    10.215.213.0-10.215.213.63,pda,1
    10.215.213.64-10.215.213.127,pda,1
    10.215.213.128-10.215.213.191,pda,1
    10.215.213.192-10.215.213.255,pda,1
    10.215.214.0-10.215.214.63,pda,1
    10.215.214.64-10.215.214.127,pda,1
    10.215.214.128-10.215.214.191,pda,1
    10.215.214.192-10.215.214.255,pda,1
    10.215.215.0-10.215.215.63,pda,1
    10.215.215.64-10.215.215.127,pda,1
    10.215.215.128-10.215.215.191,pda,1
    10.215.215.192-10.215.215.255,pda,1
    10.215.216.0-10.215.216.63,pda,1
    10.215.216.64-10.215.216.127,pda,1
    10.215.216.128-10.215.216.191,pda,1
    10.215.216.192-10.215.216.255,pda,1
    10.215.217.0-10.215.217.63,pda,1
    10.215.217.64-10.215.217.127,pda,1
    10.215.217.128-10.215.217.191,pda,1
    10.215.217.192-10.215.217.255,pda,1
    10.215.218.0-10.215.218.63,pda,1
    10.215.218.64-10.215.218.127,pda,1
    10.215.218.128-10.215.218.191,pda,1
    10.215.218.192-10.215.218.255,pda,1
    10.215.219.0-10.215.219.63,pda,1
    10.215.219.64-10.215.219.127,pda,1
    10.215.219.128-10.215.219.191,pda,1
    10.215.219.192-10.215.219.255,pda,1
    10.215.220.0-10.215.220.63,pda,1
    10.215.220.64-10.215.220.127,pda,1
    10.215.220.128-10.215.220.191,pda,1
    10.215.220.192-10.215.220.255,pda,1
    10.215.221.0-10.215.221.63,pda,1
    10.215.221.64-10.215.221.127,pda,1
    10.215.221.128-10.215.221.191,pda,1
    10.215.221.192-10.215.221.255,pda,1
    10.215.222.0-10.215.222.63,pda,1
    10.215.222.64-10.215.222.127,pda,1
    10.215.222.128-10.215.222.191,pda,1
    10.215.222.192-10.215.222.255,pda,1
    10.215.223.0-10.215.223.63,pda,1
    10.215.223.64-10.215.223.127,pda,1
    10.215.223.128-10.215.223.191,pda,1
    10.215.223.192-10.215.223.255,pda,1
    10.215.224.0-10.215.224.63,pda,1
    10.215.224.64-10.215.224.127,pda,1
    10.215.224.128-10.215.224.191,pda,1
    10.215.224.192-10.215.224.255,pda,1
    10.215.225.0-10.215.225.63,pda,1
    10.215.225.64-10.215.225.127,pda,1
    10.215.225.128-10.215.225.191,pda,1
    10.215.225.192-10.215.225.255,pda,1
    10.215.226.0-10.215.226.63,pda,1
    10.215.226.64-10.215.226.127,pda,1
    10.215.226.128-10.215.226.191,pda,1
    10.215.226.192-10.215.226.255,pda,1
    10.215.227.0-10.215.227.63,pda,1
    10.215.227.64-10.215.227.127,pda,1
    10.215.227.128-10.215.227.191,pda,1
    10.215.227.192-10.215.227.255,pda,1
    10.215.228.0-10.215.228.63,pda,1
    10.215.228.64-10.215.228.127,pda,1
    10.215.228.128-10.215.228.191,pda,1
    10.215.228.192-10.215.228.255,pda,1
    10.215.229.0-10.215.229.63,pda,1
    10.215.229.64-10.215.229.127,pda,1
    10.215.229.128-10.215.229.191,pda,1
    10.215.229.192-10.215.229.255,pda,1
    10.215.230.0-10.215.230.63,pda,1
    10.215.230.64-10.215.230.127,pda,1
    10.215.230.128-10.215.230.191,pda,1
    10.215.230.192-10.215.230.255,pda,1
    10.215.231.0-10.215.231.63,pda,1
    10.215.231.64-10.215.231.127,pda,1
    10.215.231.128-10.215.231.191,pda,1
    10.215.231.192-10.215.231.255,pda,1
    10.215.232.0-10.215.232.63,pda,1
    10.215.232.64-10.215.232.127,pda,1
    10.215.232.128-10.215.232.191,pda,1
    10.215.232.192-10.215.232.255,pda,1
    10.215.233.0-10.215.233.63,pda,1
    10.215.233.64-10.215.233.127,pda,1
    10.215.233.128-10.215.233.191,pda,1
    10.215.233.192-10.215.233.255,pda,1
    10.215.234.0-10.215.234.63,pda,1
    10.215.234.64-10.215.234.127,pda,1
    10.215.234.128-10.215.234.191,pda,1
    10.215.234.192-10.215.234.255,pda,1
    10.215.235.0-10.215.235.63,pda,1
    10.215.235.64-10.215.235.127,pda,1
    10.215.235.128-10.215.235.191,pda,1
    10.215.235.192-10.215.235.255,pda,1
    10.215.236.0-10.215.236.63,pda,1
    10.215.236.64-10.215.236.127,pda,1
    10.215.236.128-10.215.236.191,pda,1
    10.215.236.192-10.215.236.255,pda,1
    10.215.237.0-10.215.237.63,pda,1
    10.215.237.64-10.215.237.127,pda,1
    10.215.237.128-10.215.237.191,pda,1
    10.215.237.192-10.215.237.255,pda,1
    10.215.238.0-10.215.238.63,pda,1
    10.215.238.64-10.215.238.127,pda,1
    10.215.238.128-10.215.238.191,pda,1
    10.215.238.192-10.215.238.255,pda,1
    10.215.239.0-10.215.239.63,pda,1
    10.215.239.64-10.215.239.127,pda,1
    10.215.239.128-10.215.239.191,pda,1
    10.215.239.192-10.215.239.255,pda,1
    10.215.240.0-10.215.240.63,pda,1
    10.215.240.64-10.215.240.127,pda,1
    10.215.240.128-10.215.240.191,pda,1
    10.215.240.192-10.215.240.255,pda,1
    10.215.241.0-10.215.241.63,pda,1
    10.215.241.64-10.215.241.127,pda,1
    10.215.241.128-10.215.241.191,pda,1
    10.215.241.192-10.215.241.255,pda,1
    10.215.242.0-10.215.242.63,pda,1
    10.215.242.64-10.215.242.127,pda,1
    10.215.242.128-10.215.242.191,pda,1
    10.215.242.192-10.215.242.255,pda,1
    10.215.243.0-10.215.243.63,pda,1
    10.215.243.64-10.215.243.127,pda,1
    10.215.243.128-10.215.243.191,pda,1
    10.215.243.192-10.215.243.255,pda,1
    10.215.244.0-10.215.244.63,pda,1
    10.215.244.64-10.215.244.127,pda,1
    10.215.244.128-10.215.244.191,pda,1
    10.215.244.192-10.215.244.255,pda,1
    10.215.245.0-10.215.245.63,pda,1
    10.215.245.64-10.215.245.127,pda,1
    10.215.245.128-10.215.245.191,pda,1
    10.215.245.192-10.215.245.255,pda,1
    10.215.246.0-10.215.246.63,pda,1
    10.215.246.64-10.215.246.127,pda,1
    10.215.246.128-10.215.246.191,pda,1
    10.215.246.192-10.215.246.255,pda,1
    10.215.247.0-10.215.247.63,pda,1
    10.215.247.64-10.215.247.127,pda,1
    10.215.247.128-10.215.247.191,pda,1
    10.215.247.192-10.215.247.255,pda,1
    10.215.248.0-10.215.248.63,pda,1
    10.215.248.64-10.215.248.127,pda,1
    10.215.248.128-10.215.248.191,pda,1
    10.215.248.192-10.215.248.255,pda,1
    10.215.249.0-10.215.249.63,pda,1
    10.215.249.64-10.215.249.127,pda,1
    10.215.249.128-10.215.249.191,pda,1
    10.215.249.192-10.215.249.255,pda,1
    10.215.250.0-10.215.250.63,pda,1
    10.215.250.64-10.215.250.127,pda,1
    10.215.250.128-10.215.250.191,pda,1
    10.215.250.192-10.215.250.255,pda,1
    10.215.251.0-10.215.251.63,pda,1
    10.215.251.64-10.215.251.127,pda,1
    10.215.251.128-10.215.251.191,pda,1
    10.215.251.192-10.215.251.255,pda,1
    10.215.252.0-10.215.252.63,pda,1
    10.215.252.64-10.215.252.127,pda,1
    10.215.252.128-10.215.252.191,pda,1
    10.215.252.192-10.215.252.255,pda,1
    10.215.253.0-10.215.253.63,pda,1
    10.215.253.64-10.215.253.127,pda,1
    10.215.253.128-10.215.253.191,pda,1
    10.215.253.192-10.215.253.255,pda,1
    10.215.254.0-10.215.254.63,pda,1
    10.215.254.64-10.215.254.127,pda,1
    10.215.254.128-10.215.254.191,pda,1
    10.215.254.192-10.215.254.255,pda,1
    10.215.255.0-10.215.255.63,pda,1
    10.215.255.64-10.215.255.127,pda,1
    10.215.255.128-10.215.255.191,pda,1
    10.215.255.192-10.215.255.255,pda,1
    10.214.164.128-10.214.164.255,pda,1
    10.214.219.0-10.214.219.127,pda,1
    10.214.245.128-10.214.245.255,pda,1
    10.215.65.0-10.215.65.127,pda,1
    10.215.67.128-10.215.67.255,pda,1
    10.215.73.0-10.215.73.127,pda,1
    10.215.73.128-10.215.73.255,pda,1
    10.215.78.0-10.215.78.127,pda,1
    10.215.78.128-10.215.78.255,pda,1
    10.215.79.0-10.215.79.127,pda,1
    10.215.79.128-10.215.79.255,pda,1
    10.214.136.0-10.214.136.255,pda,1
    10.214.137.0-10.214.137.255,pda,1
    10.214.138.0-10.214.138.255,pda,1
    10.214.139.0-10.214.139.255,pda,1
    10.214.142.0-10.214.142.255,pda,1
    10.214.143.0-10.214.143.255,pda,1
    10.214.144.0-10.214.144.255,pda,1
    10.214.159.0-10.214.159.255,pda,1
    10.214.160.0-10.214.160.255,pda,1
    10.214.161.0-10.214.161.255,pda,1
    10.214.162.0-10.214.162.255,pda,1
    10.214.163.0-10.214.163.255,pda,1
    10.214.165.0-10.214.165.255,pda,1
    10.214.166.0-10.214.166.255,pda,1
    10.214.170.0-10.214.170.255,pda,1
    10.214.171.0-10.214.171.255,pda,1
    10.214.218.0-10.214.218.255,pda,1
    10.214.244.0-10.214.244.255,pda,1
    10.215.70.0-10.215.70.255,pda,1
    10.215.83.0-10.215.83.255,pda,1
    10.215.85.0-10.215.85.255,pda,1
    10.215.101.0-10.215.101.255,pda,1
    10.215.104.0-10.215.104.255,pda,1
    10.215.164.0-10.215.164.255,pda,1
    10.215.165.0-10.215.165.255,pda,1
    10.215.175.0-10.215.175.255,pda,1
    10.214.148.0-10.214.149.255,pda,1
    10.214.150.0-10.214.151.255,pda,1
    10.214.174.0-10.214.175.255,pda,1
    10.214.216.0-10.214.217.255,pda,1
    10.214.246.0-10.214.247.255,pda,1
    10.215.68.0-10.215.69.255,pda,1
    10.215.74.0-10.215.75.255,pda,1
    10.215.76.0-10.215.77.255,pda,1
    10.215.96.0-10.215.97.255,pda,1
    10.215.98.0-10.215.99.255,pda,1
    10.215.102.0-10.215.103.255,pda,1
    10.215.140.0-10.215.141.255,pda,1
    10.215.142.0-10.215.143.255,pda,1
    10.215.148.0-10.215.149.255,pda,1
    10.215.150.0-10.215.151.255,pda,1
    10.215.152.0-10.215.153.255,pda,1
    10.215.154.0-10.215.155.255,pda,1
    10.215.168.0-10.215.169.255,pda,1
    10.215.176.0-10.215.177.255,pda,1
    10.214.220.0-10.214.223.255,pda,1
    10.214.240.0-10.214.243.255,pda,1
    10.215.108.0-10.215.111.255,pda,1
    10.215.128.0-10.215.131.255,pda,1
    10.215.156.0-10.215.159.255,pda,1
    10.215.160.0-10.215.163.255,pda,1
    10.215.180.0-10.215.183.255,pda,1
    10.214.208.0-10.214.215.255,pda,1
    10.214.248.0-10.214.255.255,pda,1
    10.215.184.0-10.215.191.255,pda,1
    10.214.176.0-10.214.191.255,pda,1
    10.214.192.0-10.214.207.255,pda,1
    10.214.224.0-10.214.239.255,pda,1
    10.215.112.0-10.215.127.255,pda,1
    10.215.32.0-10.215.63.255,pda,9
    10.214.0.0-10.214.127.255,pda,9
    )";

  // Need to have the working ranges covered first, before they're blended.
  space.blend(IP4Range{"10.214.0.0/15"}, 1, code_blender);
  // Now blend the working ranges over the base range.
  while (content) {
    auto line = content.take_prefix_at('\n').trim_if(&isspace);
    if (line.empty()) {
      continue;
    }
    IP4Range range{line.take_prefix_at(',')};
    auto pod = line.take_prefix_at(',');
    int r    = swoc::svtoi(line.take_prefix_at(','));
    space.blend(range, pod, pod_blender);
    space.blend(range, r, rack_blender);
    if (space.count() > 2) {
      auto spot     = space.begin();
      auto [r1, p1] = *++spot;
      auto [r2, p2] = *++spot;
      REQUIRE(r1.max() < r2.min()); // This is supposed to be an invariant! Make sure.
      auto back       = space.end();
      auto [br1, bp1] = *--back;
      auto [br2, bp2] = *--back;
      REQUIRE(br2.max() < br1.min()); // This is supposed to be an invariant! Make sure.
    }
  }

  // Do some range intersection checks.
}

TEST_CASE("IPSpace skew overlap blend", "[libswoc][ipspace][blend][skew]") {
  std::string buff;
  enum class Pod { INVALID, zio, zaz, zlz };
  swoc::Lexicon<Pod> PodNames{
    {{Pod::zio, "zio"}, {Pod::zaz, "zaz"}, {Pod::zlz, "zlz"}},
    "-1"
  };

  struct Data {
    int _state   = 0;
    int _country = -1;
    int _rack    = 0;
    Pod _pod     = Pod::INVALID;
    int _code    = 0;

    bool
    operator==(Data const &that) const {
      return _pod == that._pod && _rack == that._rack && _code == that._code && _state == that._state && _country == that._country;
    }
  };

  using Src_1  = std::tuple<int, Pod, int>; // rack, pod, code
  using Src_2  = std::tuple<int, int>;      // state, country.
  auto blend_1 = [](Data &data, Src_1 const &src) {
    std::tie(data._rack, data._pod, data._code) = src;
    return true;
  };
  [[maybe_unused]] auto blend_2 = [](Data &data, Src_2 const &src) {
    std::tie(data._state, data._country) = src;
    return true;
  };
  swoc::IPSpace<Data> space;
  space.blend(IPRange("14.6.128.0-14.6.191.255"), Src_2{32, 231}, blend_2);
  space.blend(IPRange("14.6.192.0-14.6.223.255"), Src_2{32, 231}, blend_2);
  REQUIRE(space.count() == 1);
  space.blend(IPRange("14.6.160.0-14.6.160.1"), Src_1{1, Pod::zaz, 1}, blend_1);
  REQUIRE(space.count() == 3);
  space.blend(IPRange("14.6.160.64-14.6.160.95"), Src_1{1, Pod::zio, 1}, blend_1);
  space.blend(IPRange("14.6.160.96-14.6.160.127"), Src_1{1, Pod::zlz, 1}, blend_1);
  space.blend(IPRange("14.6.160.128-14.6.160.255"), Src_1{1, Pod::zlz, 1}, blend_1);
  space.blend(IPRange("14.6.0.0-14.6.127.255"), Src_2{32, 231}, blend_2);

  std::array<std::tuple<IPRange, Data>, 6> results = {
    {{IPRange("14.6.0.0-14.6.159.255"), Data{32, 231, 0, Pod::INVALID, 0}},
     {IPRange("14.6.160.0-14.6.160.1"), Data{32, 231, 1, Pod::zaz, 1}},
     {IPRange("14.6.160.2-14.6.160.63"), Data{32, 231, 0, Pod::INVALID, 0}},
     {IPRange("14.6.160.64-14.6.160.95"), Data{32, 231, 1, Pod::zio, 1}},
     {IPRange("14.6.160.96-14.6.160.255"), Data{32, 231, 1, Pod::zlz, 1}},
     {IPRange("14.6.161.0-14.6.223.255"), Data{32, 231, 0, Pod::INVALID, 0}}}
  };
  REQUIRE(space.count() == results.size());
  unsigned idx = 0;
  for (auto const &v : space) {
    REQUIRE(v == results[idx]);
    ++idx;
  }
}

TEST_CASE("IPSpace fill", "[libswoc][ipspace][fill]") {
  using PAYLOAD = unsigned;
  using Space   = swoc::IPSpace<PAYLOAD>;

  std::array<std::tuple<TextView, unsigned>, 6> ranges{
    {{"172.28.56.12-172.28.56.99"_tv, 1},
     {"10.10.35.0/24"_tv, 2},
     {"192.168.56.0/25"_tv, 3},
     {"1337::ded:beef-1337::ded:ceef"_tv, 4},
     {"ffee:1f2d:c587:24c3:9128:3349:3cee:143-ffee:1f2d:c587:24c3:9128:3349:3cFF:FFFF"_tv, 5},
     {"10.12.148.0/23"_tv, 6}}
  };

  Space space;

  for (auto &&[text, v] : ranges) {
    space.fill(IPRange{text}, v);
  }
  REQUIRE(space.count() == ranges.size());

  auto [r1, p1] = *(space.find(IP4Addr{"172.28.56.100"}));
  REQUIRE(r1.empty());
  auto [r2, p2] = *(space.find(IPAddr{"172.28.56.87"}));
  REQUIRE_FALSE(r2.empty());

  space.fill(IPRange{"10.0.0.0/8"}, 7);
  REQUIRE(space.count() == ranges.size() + 3);
  space.fill(IPRange{"9.0.0.0-11.255.255.255"}, 7);
  REQUIRE(space.count() == ranges.size() + 3);

  {
    auto [r, p] = *(space.find(IPAddr{"10.99.88.77"}));
    REQUIRE(false == r.empty());
    REQUIRE(p == 7);
  }

  {
    auto [r, p] = *(space.find(IPAddr{"10.10.35.35"}));
    REQUIRE(false == r.empty());
    REQUIRE(p == 2);
  }

  {
    auto [r, p] = *(space.find(IPAddr{"192.168.56.56"}));
    REQUIRE(false == r.empty());
    REQUIRE(p == 3);
  }

  {
    auto [r, p] = *(space.find(IPAddr{"11.11.11.11"}));
    REQUIRE(false == r.empty());
    REQUIRE(p == 7);
  }

  space.fill(IPRange{"192.168.56.0-192.168.56.199"}, 8);
  REQUIRE(space.count() == ranges.size() + 4);
  {
    auto [r, p] = *(space.find(IPAddr{"192.168.55.255"}));
    REQUIRE(true == r.empty());
  }
  {
    auto [r, p] = *(space.find(IPAddr{"192.168.56.0"}));
    REQUIRE(false == r.empty());
    REQUIRE(p == 3);
  }
  {
    auto [r, p] = *(space.find(IPAddr{"192.168.56.128"}));
    REQUIRE(false == r.empty());
    REQUIRE(p == 8);
  }

  space.fill(IPRange{"0.0.0.0/0"}, 0);
  {
    auto [r, p] = *(space.find(IPAddr{"192.168.55.255"}));
    REQUIRE(false == r.empty());
    REQUIRE(p == 0);
  }
}

TEST_CASE("IPSpace intersect", "[libswoc][ipspace][intersect]") {
  std::string dbg;
  using PAYLOAD = unsigned;
  using Space   = swoc::IPSpace<PAYLOAD>;

  std::array<std::tuple<TextView, unsigned>, 7> ranges{
    {{"172.28.56.12-172.28.56.99"_tv, 1},
     {"10.10.35.0/24"_tv, 2},
     {"192.168.56.0/25"_tv, 3},
     {"10.12.148.0/23"_tv, 6},
     {"10.14.56.0/24"_tv, 9},
     {"192.168.57.0/25"_tv, 7},
     {"192.168.58.0/25"_tv, 5}}
  };

  Space space;

  for (auto &&[text, v] : ranges) {
    space.fill(IPRange{text}, v);
  }

  {
    IPRange r{"172.0.0.0/16"};
    auto &&[begin, end] = space.intersection(r);
    REQUIRE(begin == end);
  }
  {
    IPRange r{"172.0.0.0/8"};
    auto &&[begin, end] = space.intersection(r);
    REQUIRE(std::distance(begin, end) == 1);
  }
  {
    IPRange r{"10.0.0.0/8"};
    auto &&[begin, end] = space.intersection(r);
    REQUIRE(std::distance(begin, end) == 3);
  }
  {
    IPRange r{"10.10.35.17-10.12.148.7"};
    auto &&[begin, end] = space.intersection(r);
    REQUIRE(std::distance(begin, end) == 2);
  }
  {
    IPRange r{"10.10.35.0-10.14.56.0"};
    auto &&[begin, end] = space.intersection(r);
    REQUIRE(std::distance(begin, end) == 3);
  }
  {
    IPRange r{"10.13.0.0-10.15.148.7"}; // past the end
    auto &&[begin, end] = space.intersection(r);
    REQUIRE(std::distance(begin, end) == 1);
  }
  {
    IPRange r{"10.13.0.0-10.14.55.127"}; // inside a gap.
    auto &&[begin, end] = space.intersection(r);
    REQUIRE(begin == end);
  }
  {
    IPRange r{"192.168.56.127-192.168.67.35"}; // include last range.
    auto &&[begin, end] = space.intersection(r);
    REQUIRE(std::distance(begin, end) == 3);
  }
  {
    IPRange r{"192.168.57.128-192.168.67.35"}; // only last range.
    auto &&[begin, end] = space.intersection(r);
    REQUIRE(std::distance(begin, end) == 1);
  }
  {
    IPRange r{"192.168.57.128-192.168.58.10"}; // only last range.
    auto &&[begin, end] = space.intersection(r);
    REQUIRE(std::distance(begin, end) == 1);
  }
  {
    IPRange r{"192.168.50.0-192.168.57.35"}; // include last range.
    auto &&[begin, end] = space.intersection(r);
    REQUIRE(std::distance(begin, end) == 2);
  }
}

TEST_CASE("IPSrv", "[libswoc][IPSrv]") {
  using swoc::IP4Srv;
  using swoc::IP6Srv;
  using swoc::IPSrv;

  IP4Srv s4;
  IP6Srv s6;
  IPSrv s;

  IP4Addr a1{"192.168.34.56"};
  IP4Addr a2{"10.9.8.7"};
  IP6Addr aa1{"ffee:1f2d:c587:24c3:9128:3349:3cee:143"};

  s6.assign(aa1, 99);
  REQUIRE(s6.addr() == aa1);
  REQUIRE(s6.host_order_port() == 99);
  REQUIRE(s6 == IP6Srv(aa1, 99));

  // Test various constructors.
  s4.assign(a2, 88);
  IP4Addr tmp1{s4.addr()};
  REQUIRE(s4 == tmp1);
  IP4Addr tmp2 = s4;
  REQUIRE(s4 == tmp2);
  IP4Addr tmp3{s4};
  REQUIRE(s4 == tmp3);
  REQUIRE(s4.addr() == tmp3); // double check equality.

  IP4Srv s4_1{"10.9.8.7:56"};
  REQUIRE(s4_1.host_order_port() == 56);
  REQUIRE(s4_1 == a2);
  CHECK(s4_1.load("10.2:56"));
  CHECK_FALSE(s4_1.load("10.1.2.3.567899"));
  CHECK_FALSE(s4_1.load("10.1.2.3.56f"));
  CHECK_FALSE(s4_1.load("10.1.2.56f"));
  CHECK(s4_1.load("10.1.2.3"));
  REQUIRE(s4_1.host_order_port() == 0);

  CHECK(s6.load("[ffee:1f2d:c587:24c3:9128:3349:3cee:143]:956"));
  REQUIRE(s6 == aa1);
  REQUIRE(s6.host_order_port() == 956);
  CHECK(s6.load("ffee:1f2d:c587:24c3:9128:3349:3cee:143"));
  REQUIRE(s6 == aa1);
  REQUIRE(s6.host_order_port() == 0);

  CHECK(s.load("[ffee:1f2d:c587:24c3:9128:3349:3cee:143]:956"));
  REQUIRE(s == aa1);
  REQUIRE(s.host_order_port() == 956);
  CHECK(s.load("ffee:1f2d:c587:24c3:9128:3349:3cee:143"));
  REQUIRE(s == aa1);
  REQUIRE(s.host_order_port() == 0);
}

TEST_CASE("IPRangeSet", "[libswoc][iprangeset]") {
  std::array<TextView, 6> ranges = {"172.28.56.12-172.28.56.99"_tv,
                                    "10.10.35.0/24"_tv,
                                    "192.168.56.0/25"_tv,
                                    "1337::ded:beef-1337::ded:ceef"_tv,
                                    "ffee:1f2d:c587:24c3:9128:3349:3cee:143-ffee:1f2d:c587:24c3:9128:3349:3cFF:FFFF"_tv,
                                    "10.12.148.0/23"_tv};

  IPRangeSet addrs;

  for (auto rtxt : ranges) {
    IPRange r{rtxt};
    addrs.mark(r);
  }

  unsigned n   = 0;
  bool valid_p = true;
  for (auto r : addrs) {
    valid_p = valid_p && !r.empty();
    ++n;
  }
  REQUIRE(n == addrs.count());
  REQUIRE(valid_p);
}

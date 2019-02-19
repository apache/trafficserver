/** @file

    IpMap unit tests.

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

#include "tscore/IpMap.h"
#include <sstream>
#include <catch.hpp>

std::ostream &
operator<<(std::ostream &s, IpEndpoint const &addr)
{
  ip_text_buffer b;
  ats_ip_ntop(addr, b, sizeof(b));
  s << b;
  return s;
}

void
IpMapTestPrint(IpMap &map)
{
  printf("IpMap Dump\n");
  for (auto &spot : map) {
    ip_text_buffer ipb1, ipb2;

    printf("%s - %s : %p\n", ats_ip_ntop(spot.min(), ipb1, sizeof ipb1), ats_ip_ntop(spot.max(), ipb2, sizeof(ipb2)), spot.data());
  }
  printf("\n");
}

// --- Test helper classes ---
class MapMarkedAt : public Catch::MatcherBase<IpMap>
{
  IpEndpoint const &_addr;

public:
  MapMarkedAt(IpEndpoint const &addr) : _addr(addr) {}

  bool
  match(IpMap const &map) const override
  {
    return map.contains(&_addr);
  }

  std::string
  describe() const override
  {
    std::ostringstream ss;
    ss << _addr << " is marked";
    return ss.str();
  }
};

// The builder function
inline MapMarkedAt
IsMarkedAt(IpEndpoint const &_addr)
{
  return {_addr};
}

class MapMarkedWith : public Catch::MatcherBase<IpMap>
{
  IpEndpoint const &_addr;
  void *_mark;
  mutable bool _found_p = false;

public:
  MapMarkedWith(IpEndpoint const &addr, void *mark) : _addr(addr), _mark(mark) {}

  bool
  match(IpMap const &map) const override
  {
    void *mark = nullptr;
    return (_found_p = map.contains(&_addr, &mark)) && mark == _mark;
  }

  std::string
  describe() const override
  {
    std::ostringstream ss;
    if (_found_p) {
      ss << "is marked at " << _addr << " with " << std::hex << reinterpret_cast<intptr_t>(_mark);
    } else {
      ss << "is not marked at " << _addr;
    }
    return ss.str();
  }
};

inline MapMarkedWith
IsMarkedWith(IpEndpoint const &addr, void *mark)
{
  return {addr, mark};
}

// -------------
// --- TESTS ---
// -------------
TEST_CASE("IpMap Basic", "[libts][ipmap]")
{
  IpMap map;
  void *const markA = reinterpret_cast<void *>(1);
  void *const markB = reinterpret_cast<void *>(2);
  void *const markC = reinterpret_cast<void *>(3);
  void *mark; // for retrieval

  in_addr_t ip5 = htonl(5), ip9 = htonl(9);
  in_addr_t ip10 = htonl(10), ip15 = htonl(15), ip20 = htonl(20);
  in_addr_t ip50 = htonl(50), ip60 = htonl(60);
  in_addr_t ip100 = htonl(100), ip120 = htonl(120), ip140 = htonl(140);
  in_addr_t ip150 = htonl(150), ip160 = htonl(160);
  in_addr_t ip200 = htonl(200);
  in_addr_t ip0   = 0;
  in_addr_t ipmax = ~static_cast<in_addr_t>(0);

  map.mark(ip10, ip20, markA);
  map.mark(ip5, ip9, markA);
  {
    INFO("Coalesce failed");
    CHECK(map.count() == 1);
  }
  {
    INFO("Range max not found.");
    CHECK(map.contains(ip9));
  }
  {
    INFO("Span min not found");
    CHECK(map.contains(ip10, &mark));
  }
  {
    INFO("Mark not preserved.");
    CHECK(mark == markA);
  }

  map.fill(ip15, ip100, markB);
  {
    INFO("Fill failed.");
    CHECK(map.count() == 2);
  }
  {
    INFO("fill interior missing");
    CHECK(map.contains(ip50, &mark));
  }
  {
    INFO("Fill mark not preserved.");
    CHECK(mark == markB);
  }
  {
    INFO("Span min not found.");
    CHECK(!map.contains(ip200));
  }
  {
    INFO("Old span interior not found");
    CHECK(map.contains(ip15, &mark));
  }
  {
    INFO("Fill overwrote mark.");
    CHECK(mark == markA);
  }

  map.clear();
  {
    INFO("Clear failed.");
    CHECK(map.count() == 0);
  }

  map.mark(ip20, ip50, markA);
  map.mark(ip100, ip150, markB);
  map.fill(ip10, ip200, markC);
  CHECK(map.count() == 5);
  {
    INFO("Left span missing");
    CHECK(map.contains(ip15, &mark));
  }
  {
    INFO("Middle span missing");
    CHECK(map.contains(ip60, &mark));
  }
  {
    INFO("fill mark wrong.");
    CHECK(mark == markC);
  }
  {
    INFO("right span missing.");
    CHECK(map.contains(ip160));
  }
  {
    INFO("right span missing");
    CHECK(map.contains(ip120, &mark));
  }
  {
    INFO("wrong data on right mark span.");
    CHECK(mark == markB);
  }

  map.unmark(ip140, ip160);
  {
    INFO("unmark failed");
    CHECK(map.count() == 5);
  }
  {
    INFO("unmark left edge still there.");
    CHECK(!map.contains(ip140));
  }
  {
    INFO("unmark middle still there.");
    CHECK(!map.contains(ip150));
  }
  {
    INFO("unmark right edge still there.");
    CHECK(!map.contains(ip160));
  }

  map.clear();
  map.mark(ip20, ip20, markA);
  {
    INFO("Map failed on singleton insert");
    CHECK(map.contains(ip20));
  }
  map.mark(ip10, ip200, markB);
  mark = 0;
  map.contains(ip20, &mark);
  {
    INFO("Map held singleton against range.");
    CHECK(mark == markB);
  }
  map.mark(ip100, ip120, markA);
  map.mark(ip150, ip160, markB);
  map.mark(ip0, ipmax, markC);
  {
    INFO("IpMap: Full range fill left extra ranges.");
    CHECK(map.count() == 1);
  }
}

TEST_CASE("IpMap Unmark", "[libts][ipmap]")
{
  IpMap map;
  //  ip_text_buffer ipb1, ipb2;
  void *const markA = reinterpret_cast<void *>(1);

  IpEndpoint a_0, a_0_0_0_16, a_0_0_0_17, a_max;
  IpEndpoint a_10_28_56_0, a_10_28_56_4, a_10_28_56_255;
  IpEndpoint a_10_28_55_255, a_10_28_57_0;
  IpEndpoint a_63_128_1_12;
  IpEndpoint a_loopback, a_loopback2;
  IpEndpoint a6_0, a6_max, a6_fe80_9d90, a6_fe80_9d9d, a6_fe80_9d95;

  ats_ip_pton("0.0.0.0", &a_0);
  ats_ip_pton("0.0.0.16", &a_0_0_0_16);
  ats_ip_pton("0.0.0.17", &a_0_0_0_17);
  ats_ip_pton("255.255.255.255", &a_max);
  ats_ip_pton("10.28.55.255", &a_10_28_55_255);
  ats_ip_pton("10.28.56.0", &a_10_28_56_0);
  ats_ip_pton("10.28.56.4", &a_10_28_56_4);
  ats_ip_pton("10.28.56.255", &a_10_28_56_255);
  ats_ip_pton("10.28.57.0", &a_10_28_57_0);
  ats_ip_pton("63.128.1.12", &a_63_128_1_12);
  ats_ip_pton("::", &a6_0);
  ats_ip_pton("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff", &a6_max);
  ats_ip_pton("fe80::221:9bff:fe10:9d90", &a6_fe80_9d90);
  ats_ip_pton("fe80::221:9bff:fe10:9d9d", &a6_fe80_9d9d);
  ats_ip_pton("fe80::221:9bff:fe10:9d95", &a6_fe80_9d95);
  ats_ip_pton("127.0.0.1", &a_loopback);
  ats_ip_pton("127.0.0.255", &a_loopback2);

  map.mark(&a_0, &a_max, markA);
  {
    INFO("IpMap Unmark: Full range not single.");
    CHECK(map.count() == 1);
  }
  map.unmark(&a_10_28_56_0, &a_10_28_56_255);
  {
    INFO("IpMap Unmark: Range unmark failed.");
    CHECK(map.count() == 2);
  }
  // Generic range check.
  {
    INFO("IpMap Unmark: Range unmark min address not removed.");
    CHECK(!map.contains(&a_10_28_56_0));
  }
  {
    INFO("IpMap Unmark: Range unmark max address not removed.");
    CHECK(!map.contains(&a_10_28_56_255));
  }
  {
    INFO("IpMap Unmark: Range unmark min-1 address removed.");
    CHECK(map.contains(&a_10_28_55_255));
  }
  {
    INFO("IpMap Unmark: Range unmark max+1 address removed.");
    CHECK(map.contains(&a_10_28_57_0));
  }
  // Test min bounded range.
  map.unmark(&a_0, &a_0_0_0_16);
  {
    INFO("IpMap Unmark: Range unmark zero address not removed.");
    CHECK(!map.contains(&a_0));
  }
  {
    INFO("IpMap Unmark: Range unmark zero bounded range max not removed.");
    CHECK(!map.contains(&a_0_0_0_16));
  }
  {
    INFO("IpMap Unmark: Range unmark zero bounded range max+1 removed.");
    CHECK(map.contains(&a_0_0_0_17));
  }
}

TEST_CASE("IpMap Fill", "[libts][ipmap]")
{
  IpMap map;
  void *const allow = reinterpret_cast<void *>(0);
  void *const deny  = reinterpret_cast<void *>(~0);
  void *const markA = reinterpret_cast<void *>(1);
  void *const markB = reinterpret_cast<void *>(2);
  void *const markC = reinterpret_cast<void *>(3);

  IpEndpoint a0, a_10_28_56_0, a_10_28_56_4, a_10_28_56_255, a3, a4;
  IpEndpoint a_9_255_255_255, a_10_0_0_0, a_10_0_0_19, a_10_0_0_255, a_10_0_1_0;
  IpEndpoint a_max, a_loopback, a_loopback2;
  IpEndpoint a_10_28_55_255, a_10_28_57_0;
  IpEndpoint a_63_128_1_12;
  IpEndpoint a_0000_0000, a_0000_0001, a_ffff_ffff;
  IpEndpoint a_fe80_9d8f, a_fe80_9d90, a_fe80_9d95, a_fe80_9d9d, a_fe80_9d9e;

  ats_ip_pton("0.0.0.0", &a0);
  ats_ip_pton("255.255.255.255", &a_max);

  ats_ip_pton("9.255.255.255", &a_9_255_255_255);
  ats_ip_pton("10.0.0.0", &a_10_0_0_0);
  ats_ip_pton("10.0.0.19", &a_10_0_0_19);
  ats_ip_pton("10.0.0.255", &a_10_0_0_255);
  ats_ip_pton("10.0.1.0", &a_10_0_1_0);

  ats_ip_pton("10.28.55.255", &a_10_28_55_255);
  ats_ip_pton("10.28.56.0", &a_10_28_56_0);
  ats_ip_pton("10.28.56.4", &a_10_28_56_4);
  ats_ip_pton("10.28.56.255", &a_10_28_56_255);
  ats_ip_pton("10.28.57.0", &a_10_28_57_0);

  ats_ip_pton("192.168.1.0", &a3);
  ats_ip_pton("192.168.1.255", &a4);

  ats_ip_pton("::", &a_0000_0000);
  ats_ip_pton("::1", &a_0000_0001);
  ats_ip_pton("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff", &a_ffff_ffff);
  ats_ip_pton("fe80::221:9bff:fe10:9d8f", &a_fe80_9d8f);
  ats_ip_pton("fe80::221:9bff:fe10:9d90", &a_fe80_9d90);
  ats_ip_pton("fe80::221:9bff:fe10:9d95", &a_fe80_9d95);
  ats_ip_pton("fe80::221:9bff:fe10:9d9d", &a_fe80_9d9d);
  ats_ip_pton("fe80::221:9bff:fe10:9d9e", &a_fe80_9d9e);

  ats_ip_pton("127.0.0.0", &a_loopback);
  ats_ip_pton("127.0.0.255", &a_loopback2);
  ats_ip_pton("63.128.1.12", &a_63_128_1_12);

  SECTION("subnet overfill")
  {
    map.fill(&a_10_28_56_0, &a_10_28_56_255, deny);
    map.fill(&a0, &a_max, allow);
    CHECK_THAT(map, IsMarkedWith(a_10_28_56_4, deny));
  }

  SECTION("singleton overfill")
  {
    map.fill(&a_loopback, &a_loopback, allow);
    {
      INFO("singleton not marked.");
      CHECK_THAT(map, IsMarkedAt(a_loopback));
    }
    map.fill(&a0, &a_max, deny);
    THEN("singleton mark")
    {
      CHECK_THAT(map, IsMarkedWith(a_loopback, allow));
      THEN("not empty")
      {
        REQUIRE(map.begin() != map.end());
        IpMap::iterator spot = map.begin();
        ++spot;
        THEN("more than one range")
        {
          REQUIRE(spot != map.end());
          THEN("ranges disjoint")
          {
            INFO(" " << map.begin()->max() << " < " << spot->min());
            REQUIRE(-1 == ats_ip_addr_cmp(map.begin()->max(), spot->min()));
          }
        }
      }
    }
  }

  SECTION("3")
  {
    map.fill(&a_loopback, &a_loopback2, markA);
    map.fill(&a_10_28_56_0, &a_10_28_56_255, markB);
    {
      INFO("over extended range");
      CHECK_THAT(map, !IsMarkedWith(a_63_128_1_12, markC));
    }
    map.fill(&a0, &a_max, markC);
    {
      INFO("IpMap[2]: Fill failed.");
      CHECK(map.count() == 5);
    }
    {
      INFO("invalid mark in range gap");
      CHECK_THAT(map, IsMarkedWith(a_63_128_1_12, markC));
    }
  }

  SECTION("4")
  {
    map.fill(&a_10_0_0_0, &a_10_0_0_255, allow);
    map.fill(&a_loopback, &a_loopback2, allow);
    {
      INFO("invalid mark between ranges");
      CHECK_THAT(map, !IsMarkedAt(a_63_128_1_12));
    }
    {
      INFO("invalid mark in lower range");
      CHECK_THAT(map, IsMarkedWith(a_10_0_0_19, allow));
    }
    map.fill(&a0, &a_max, deny);
    {
      INFO("range count incorrect");
      CHECK(map.count() == 5);
    }
    {
      INFO("mark between ranges");
      CHECK_THAT(map, IsMarkedWith(a_63_128_1_12, deny));
    }

    map.fill(&a_fe80_9d90, &a_fe80_9d9d, markA);
    map.fill(&a_0000_0001, &a_0000_0001, markA);
    map.fill(&a_0000_0000, &a_ffff_ffff, markB);

    {
      INFO("IpMap Fill[v6]: Zero address has bad mark.");
      CHECK_THAT(map, IsMarkedWith(a_0000_0000, markB));
    }
    {
      INFO("IpMap Fill[v6]: Max address has bad mark.");
      CHECK_THAT(map, IsMarkedWith(a_ffff_ffff, markB));
    }
    {
      INFO("IpMap Fill[v6]: 9d90 address has bad mark.");
      CHECK_THAT(map, IsMarkedWith(a_fe80_9d90, markA));
    }
    {
      INFO("IpMap Fill[v6]: 9d8f address has bad mark.");
      CHECK_THAT(map, IsMarkedWith(a_fe80_9d8f, markB));
    }
    {
      INFO("IpMap Fill[v6]: 9d9d address has bad mark.");
      CHECK_THAT(map, IsMarkedWith(a_fe80_9d9d, markA));
    }
    {
      INFO("IpMap Fill[v6]: 9d9b address has bad mark.");
      CHECK_THAT(map, IsMarkedWith(a_fe80_9d9e, markB));
    }
    {
      INFO("IpMap Fill[v6]: ::1 has bad mark.");
      CHECK_THAT(map, IsMarkedWith(a_0000_0001, markA));
    }

    {
      INFO("IpMap Fill[pre-refill]: Bad range count.");
      CHECK(map.count() == 10);
    }
    // These should be ignored by the map as it is completely covered for IPv6.
    map.fill(&a_fe80_9d90, &a_fe80_9d9d, markA);
    map.fill(&a_0000_0001, &a_0000_0001, markC);
    map.fill(&a_0000_0000, &a_ffff_ffff, markB);
    {
      INFO("IpMap Fill[post-refill]: Bad range count.");
      CHECK(map.count() == 10);
    }
  }

  SECTION("5")
  {
    map.fill(&a_fe80_9d90, &a_fe80_9d9d, markA);
    map.fill(&a_0000_0001, &a_0000_0001, markC);
    map.fill(&a_0000_0000, &a_ffff_ffff, markB);
    {
      INFO("IpMap Fill[v6-2]: Zero address has bad mark.");
      CHECK_THAT(map, IsMarkedWith(a_0000_0000, markB));
    }
    {
      INFO("IpMap Fill[v6-2]: Max address has bad mark.");
      CHECK_THAT(map, IsMarkedWith(a_ffff_ffff, markB));
    }
    {
      INFO("IpMap Fill[v6-2]: 9d90 address has bad mark.");
      CHECK_THAT(map, IsMarkedWith(a_fe80_9d90, markA));
    }
    {
      INFO("IpMap Fill[v6-2]: 9d8f address has bad mark.");
      CHECK_THAT(map, IsMarkedWith(a_fe80_9d8f, markB));
    }
    {
      INFO("IpMap Fill[v6-2]: 9d9d address has bad mark.");
      CHECK_THAT(map, IsMarkedWith(a_fe80_9d9d, markA));
    }
    {
      INFO("IpMap Fill[v6-2]: 9d9b address has bad mark.");
      CHECK_THAT(map, IsMarkedWith(a_fe80_9d9e, markB));
    }
    {
      INFO("IpMap Fill[v6-2]: ::1 has bad mark.");
      CHECK_THAT(map, IsMarkedWith(a_0000_0001, markC));
    }
  }
}

TEST_CASE("IpMap CloseIntersection", "[libts][ipmap]")
{
  IpMap map;
  void *const markA = reinterpret_cast<void *>(1);
  void *const markB = reinterpret_cast<void *>(2);
  void *const markC = reinterpret_cast<void *>(3);
  void *const markD = reinterpret_cast<void *>(4);

  IpEndpoint a_1_l, a_1_u, a_2_l, a_2_u, a_3_l, a_3_u, a_4_l, a_4_u, a_5_l, a_5_u, a_6_l, a_6_u, a_7_l, a_7_u;
  IpEndpoint b_1_l, b_1_u;
  IpEndpoint c_1_l, c_1_u, c_2_l, c_2_u, c_3_l, c_3_u;
  IpEndpoint c_3_m;
  IpEndpoint d_1_l, d_1_u, d_2_l, d_2_u;

  IpEndpoint a_1_m;

  ats_ip_pton("123.88.172.0", &a_1_l);
  ats_ip_pton("123.88.180.93", &a_1_m);
  ats_ip_pton("123.88.191.255", &a_1_u);
  ats_ip_pton("123.89.132.0", &a_2_l);
  ats_ip_pton("123.89.135.255", &a_2_u);
  ats_ip_pton("123.89.160.0", &a_3_l);
  ats_ip_pton("123.89.167.255", &a_3_u);
  ats_ip_pton("123.90.108.0", &a_4_l);
  ats_ip_pton("123.90.111.255", &a_4_u);
  ats_ip_pton("123.90.152.0", &a_5_l);
  ats_ip_pton("123.90.159.255", &a_5_u);
  ats_ip_pton("123.91.0.0", &a_6_l);
  ats_ip_pton("123.91.35.255", &a_6_u);
  ats_ip_pton("123.91.40.0", &a_7_l);
  ats_ip_pton("123.91.47.255", &a_7_u);

  ats_ip_pton("123.78.100.0", &b_1_l);
  ats_ip_pton("123.78.115.255", &b_1_u);

  ats_ip_pton("123.88.204.0", &c_1_l);
  ats_ip_pton("123.88.219.255", &c_1_u);
  ats_ip_pton("123.90.112.0", &c_2_l);
  ats_ip_pton("123.90.119.255", &c_2_u);
  ats_ip_pton("123.90.132.0", &c_3_l);
  ats_ip_pton("123.90.134.157", &c_3_m);
  ats_ip_pton("123.90.135.255", &c_3_u);

  ats_ip_pton("123.82.196.0", &d_1_l);
  ats_ip_pton("123.82.199.255", &d_1_u);
  ats_ip_pton("123.82.204.0", &d_2_l);
  ats_ip_pton("123.82.219.255", &d_2_u);

  map.mark(a_1_l, a_1_u, markA);
  map.mark(a_2_l, a_2_u, markA);
  map.mark(a_3_l, a_3_u, markA);
  map.mark(a_4_l, a_4_u, markA);
  map.mark(a_5_l, a_5_u, markA);
  map.mark(a_6_l, a_6_u, markA);
  map.mark(a_7_l, a_7_u, markA);
  CHECK_THAT(map, IsMarkedAt(a_1_m));

  map.mark(b_1_l, b_1_u, markB);
  CHECK_THAT(map, IsMarkedWith(a_1_m, markA));

  map.mark(c_1_l, c_1_u, markC);
  map.mark(c_2_l, c_2_u, markC);
  map.mark(c_3_l, c_3_u, markC);
  CHECK_THAT(map, IsMarkedWith(a_1_m, markA));

  map.mark(d_1_l, d_1_u, markD);
  map.mark(d_2_l, d_2_u, markD);
  CHECK_THAT(map, IsMarkedAt(a_1_m));
  CHECK_THAT(map, IsMarkedWith(b_1_u, markB));
  CHECK_THAT(map, IsMarkedWith(c_3_m, markC));
  CHECK_THAT(map, IsMarkedWith(d_2_l, markD));

  CHECK(map.count() == 13);

  // Check move constructor.
  IpMap m2{std::move(map)};
  // Original map should be empty.
  REQUIRE(map.count() == 0);
  // Do all these again on the destination map.
  CHECK_THAT(m2, IsMarkedWith(a_1_m, markA));
  CHECK_THAT(m2, IsMarkedWith(a_1_m, markA));
  CHECK_THAT(m2, IsMarkedWith(a_1_m, markA));
  CHECK_THAT(m2, IsMarkedWith(a_1_m, markA));
  CHECK_THAT(m2, IsMarkedWith(b_1_u, markB));
  CHECK_THAT(m2, IsMarkedWith(c_3_m, markC));
  CHECK_THAT(m2, IsMarkedWith(d_2_l, markD));
  CHECK(m2.count() == 13);
};

/** @file

    A brief file description

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

#include <ts/IpMap.h>
#include <ts/TestBox.h>

void
IpMapTestPrint(IpMap &map)
{
  printf("IpMap Dump\n");
  for (IpMap::iterator spot(map.begin()), limit(map.end()); spot != limit; ++spot) {
    ip_text_buffer ipb1, ipb2;

    printf("%s - %s : %p\n", ats_ip_ntop(spot->min(), ipb1, sizeof ipb1), ats_ip_ntop(spot->max(), ipb2, sizeof(ipb2)),
           spot->data());
  }
  printf("\n");
}

REGRESSION_TEST(IpMap_Basic)(RegressionTest *t, int /* atype ATS_UNUSED */, int *pstatus)
{
  TestBox tb(t, pstatus);

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

  *pstatus = REGRESSION_TEST_PASSED;

  map.mark(ip10, ip20, markA);
  map.mark(ip5, ip9, markA);
  tb.check(map.getCount() == 1, "Coalesce failed");
  tb.check(map.contains(ip9), "Range max not found.");
  tb.check(map.contains(ip10, &mark), "Span min not found.");
  tb.check(mark == markA, "Mark not preserved.");

  map.fill(ip15, ip100, markB);
  tb.check(map.getCount() == 2, "Fill failed.");
  tb.check(map.contains(ip50, &mark), "Fill interior missing.");
  tb.check(mark == markB, "Fill mark not preserved.");
  tb.check(!map.contains(ip200), "Span min not found.");
  tb.check(map.contains(ip15, &mark), "Old span interior not found.");
  tb.check(mark == markA, "Fill overwrote mark.");

  map.clear();
  tb.check(map.getCount() == 0, "Clear failed.");

  map.mark(ip20, ip50, markA);
  map.mark(ip100, ip150, markB);
  map.fill(ip10, ip200, markC);
  tb.check(map.getCount() == 5, "Test 3 failed [expected 5, got %d].", map.getCount());
  tb.check(map.contains(ip15, &mark), "Test 3 - left span missing.");
  tb.check(map.contains(ip60, &mark), "Test 3 - middle span missing.");
  tb.check(mark == markC, "Test 3 - fill mark wrong.");
  tb.check(map.contains(ip160), "Test 3 - right span missing.");
  tb.check(map.contains(ip120, &mark), "Test 3 - right mark span missing.");
  tb.check(mark == markB, "Test 3 - wrong data on right mark span.");
  map.unmark(ip140, ip160);
  tb.check(map.getCount() == 5, "Test 3 unmark failed [expected 5, got %d].", map.getCount());
  tb.check(!map.contains(ip140), "Test 3 - unmark left edge still there.");
  tb.check(!map.contains(ip150), "Test 3 - unmark middle still there.");
  tb.check(!map.contains(ip160), "Test 3 - unmark right edge still there.");

  map.clear();
  map.mark(ip20, ip20, markA);
  tb.check(map.contains(ip20), "Map failed on singleton insert");
  map.mark(ip10, ip200, markB);
  mark = 0;
  map.contains(ip20, &mark);
  tb.check(mark == markB, "Map held singleton against range.");
  map.mark(ip100, ip120, markA);
  map.mark(ip150, ip160, markB);
  map.mark(ip0, ipmax, markC);
  tb.check(map.getCount() == 1, "IpMap: Full range fill left extra ranges.");
}

REGRESSION_TEST(IpMap_Unmark)(RegressionTest *t, int /* atype ATS_UNUSED */, int *pstatus)
{
  TestBox tb(t, pstatus);
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
  *pstatus = REGRESSION_TEST_PASSED;

  map.mark(&a_0, &a_max, markA);
  tb.check(map.getCount() == 1, "IpMap Unmark: Full range not single.");
  map.unmark(&a_10_28_56_0, &a_10_28_56_255);
  tb.check(map.getCount() == 2, "IpMap Unmark: Range unmark failed.");
  // Generic range check.
  tb.check(!map.contains(&a_10_28_56_0), "IpMap Unmark: Range unmark min address not removed.");
  tb.check(!map.contains(&a_10_28_56_255), "IpMap Unmark: Range unmark max address not removed.");
  tb.check(map.contains(&a_10_28_55_255), "IpMap Unmark: Range unmark min-1 address removed.");
  tb.check(map.contains(&a_10_28_57_0), "IpMap Unmark: Range unmark max+1 address removed.");
  // Test min bounded range.
  map.unmark(&a_0, &a_0_0_0_16);
  tb.check(!map.contains(&a_0), "IpMap Unmark: Range unmark zero address not removed.");
  tb.check(!map.contains(&a_0_0_0_16), "IpMap Unmark: Range unmark zero bounded range max not removed.");
  tb.check(map.contains(&a_0_0_0_17), "IpMap Unmark: Range unmark zero bounded range max+1 removed.");
}

REGRESSION_TEST(IpMap_Fill)(RegressionTest *t, int /* atype ATS_UNUSED */, int *pstatus)
{
  TestBox tb(t, pstatus);
  IpMap map;
  ip_text_buffer ipb1, ipb2;
  void *const allow = reinterpret_cast<void *>(0);
  void *const deny  = reinterpret_cast<void *>(~0);
  void *const markA = reinterpret_cast<void *>(1);
  void *const markB = reinterpret_cast<void *>(2);
  void *const markC = reinterpret_cast<void *>(3);
  void *mark; // for retrieval

  IpEndpoint a0, a_10_28_56_0, a_10_28_56_255, a3, a4;
  IpEndpoint a_9_255_255_255, a_10_0_0_0, a_10_0_0_19, a_10_0_0_255, a_10_0_1_0;
  IpEndpoint a_10_28_56_4, a_max, a_loopback, a_loopback2;
  IpEndpoint a_10_28_55_255, a_10_28_57_0;
  IpEndpoint a_63_128_1_12;
  IpEndpoint a_0000_0000, a_0000_0001, a_ffff_ffff;
  IpEndpoint a_fe80_9d8f, a_fe80_9d90, a_fe80_9d95, a_fe80_9d9d, a_fe80_9d9e;

  *pstatus = REGRESSION_TEST_PASSED;

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

  map.fill(&a_10_28_56_0, &a_10_28_56_255, deny);
  map.fill(&a0, &a_max, allow);

  tb.check(map.contains(&a_10_28_56_4, &mark), "IpMap Fill: Target not found.");
  tb.check(mark == deny, "IpMap Fill: Expected deny, got allow at %s.", ats_ip_ntop(&a_10_28_56_4, ipb1, sizeof(ipb1)));

  map.clear();
  map.fill(&a_loopback, &a_loopback, allow);
  tb.check(map.contains(&a_loopback), "IpMap fill: singleton not marked.");
  map.fill(&a0, &a_max, deny);

  mark = 0;
  tb.check(map.contains(&a_loopback, &mark), "IpMap fill: singleton marking lost.");
  tb.check(mark == allow, "IpMap fill: overwrote existing singleton mark.");
  if (tb.check(map.begin() != map.end(), "IpMap fill: map is empty.")) {
    if (tb.check(++(map.begin()) != map.end(), "IpMap fill: only one range.")) {
      tb.check(-1 == ats_ip_addr_cmp(map.begin()->max(), (++map.begin())->min()), "IpMap fill: ranges not disjoint [%s < %s].",
               ats_ip_ntop(map.begin()->max(), ipb1, sizeof(ipb1)), ats_ip_ntop((++map.begin())->min(), ipb2, sizeof(ipb2)));
    }
  }

  map.clear();
  map.fill(&a_loopback, &a_loopback2, markA);
  map.fill(&a_10_28_56_0, &a_10_28_56_255, markB);
  tb.check(!map.contains(&a_63_128_1_12, &mark), "IpMap fill[2]: over extended range.");
  map.fill(&a0, &a_max, markC);
  tb.check(map.getCount() == 5, "IpMap[2]: Fill failed.");
  if (tb.check(map.contains(&a_63_128_1_12, &mark), "IpMap fill[2]: Collapsed range.")) {
    tb.check(mark == markC, "IpMap fill[2]: invalid mark for range gap.");
  }

  map.clear();
  map.fill(&a_10_0_0_0, &a_10_0_0_255, allow);
  map.fill(&a_loopback, &a_loopback2, allow);
  tb.check(!map.contains(&a_63_128_1_12, &mark), "IpMap fill[3]: invalid mark between ranges.");
  tb.check(map.contains(&a_10_0_0_19, &mark) && mark == allow, "IpMap fill[3]: invalid mark in lower range.");
  map.fill(&a0, &a_max, deny);
  if (!tb.check(map.getCount() == 5, "IpMap[3]: Wrong number of ranges."))
    IpMapTestPrint(map);
  if (tb.check(map.contains(&a_63_128_1_12, &mark), "IpMap fill[3]: Missing mark between ranges")) {
    tb.check(mark == deny, "IpMap fill[3]: gap range invalidly marked");
  }

  map.fill(&a_fe80_9d90, &a_fe80_9d9d, markA);
  map.fill(&a_0000_0001, &a_0000_0001, markA);
  map.fill(&a_0000_0000, &a_ffff_ffff, markB);

  tb.check(map.contains(&a_0000_0000, &mark) && mark == markB, "IpMap Fill[v6]: Zero address has bad mark.");
  tb.check(map.contains(&a_ffff_ffff, &mark) && mark == markB, "IpMap Fill[v6]: Max address has bad mark.");
  tb.check(map.contains(&a_fe80_9d90, &mark) && mark == markA, "IpMap Fill[v6]: 9d90 address has bad mark.");
  tb.check(map.contains(&a_fe80_9d8f, &mark) && mark == markB, "IpMap Fill[v6]: 9d8f address has bad mark.");
  tb.check(map.contains(&a_fe80_9d9d, &mark) && mark == markA, "IpMap Fill[v6]: 9d9d address has bad mark.");
  tb.check(map.contains(&a_fe80_9d9e, &mark) && mark == markB, "IpMap Fill[v6]: 9d9b address has bad mark.");
  tb.check(map.contains(&a_0000_0001, &mark) && mark == markA, "IpMap Fill[v6]: ::1 has bad mark.");

  tb.check(map.getCount() == 10, "IpMap Fill[pre-refill]: Bad range count.");
  // These should be ignored by the map as it is completely covered for IPv6.
  map.fill(&a_fe80_9d90, &a_fe80_9d9d, markA);
  map.fill(&a_0000_0001, &a_0000_0001, markC);
  map.fill(&a_0000_0000, &a_ffff_ffff, markB);
  tb.check(map.getCount() == 10, "IpMap Fill[post-refill]: Bad range count.");

  map.clear();
  map.fill(&a_fe80_9d90, &a_fe80_9d9d, markA);
  map.fill(&a_0000_0001, &a_0000_0001, markC);
  map.fill(&a_0000_0000, &a_ffff_ffff, markB);
  tb.check(map.contains(&a_0000_0000, &mark) && mark == markB, "IpMap Fill[v6-2]: Zero address has bad mark.");
  tb.check(map.contains(&a_ffff_ffff, &mark) && mark == markB, "IpMap Fill[v6-2]: Max address has bad mark.");
  tb.check(map.contains(&a_fe80_9d90, &mark) && mark == markA, "IpMap Fill[v6-2]: 9d90 address has bad mark.");
  tb.check(map.contains(&a_fe80_9d8f, &mark) && mark == markB, "IpMap Fill[v6-2]: 9d8f address has bad mark.");
  tb.check(map.contains(&a_fe80_9d9d, &mark) && mark == markA, "IpMap Fill[v6-2]: 9d9d address has bad mark.");
  tb.check(map.contains(&a_fe80_9d9e, &mark) && mark == markB, "IpMap Fill[v6-2]: 9d9b address has bad mark.");
  tb.check(map.contains(&a_0000_0001, &mark) && mark == markC, "IpMap Fill[v6-2]: ::1 has bad mark.");
}

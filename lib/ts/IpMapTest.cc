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

# include "IpMap.h"
# include "Regression.h"

inline bool check(RegressionTest* t, int* pstatus, bool test, char const* fmt, ...) {
  if (!test) {
    static size_t const N = 1<<16;
    char buffer[N]; // just stack, go big.
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buffer, N, fmt, ap);
    va_end(ap);
    rprintf(t, "%s\n", buffer);
    *pstatus = REGRESSION_TEST_FAILED;
  }
  return test;
}

REGRESSION_TEST(IpMap_Test_Basic)(RegressionTest* t, int atype, int* pstatus) {
  IpMap map;
  void* const markA = reinterpret_cast<void*>(1);
  void* const markB = reinterpret_cast<void*>(2);
  void* const markC = reinterpret_cast<void*>(3);
  void* mark; // for retrieval

  in_addr_t ip5 = htonl(5), ip9 = htonl(9);
  in_addr_t ip10 = htonl(10), ip15 = htonl(15), ip20 = htonl(20);
  in_addr_t ip50 = htonl(50), ip60 = htonl(60);
  in_addr_t ip100 = htonl(100), ip120 = htonl(120), ip140 = htonl(140);
  in_addr_t ip150 = htonl(150), ip160 = htonl(160);
  in_addr_t ip200 = htonl(200);

  *pstatus = REGRESSION_TEST_PASSED;

  map.mark(ip10,ip20,markA);
  map.mark(ip5, ip9, markA);
  check(t, pstatus, map.getCount() == 1, "Coalesce failed");
  check(t, pstatus, map.contains(ip9), "Range max not found.");
  check(t, pstatus, map.contains(ip10, &mark), "Span min not found.");
  check(t, pstatus, mark == markA, "Mark not preserved.");

  map.fill(ip15, ip100, markB);
  check(t, pstatus, map.getCount() == 2, "Fill failed.");
  check(t, pstatus, map.contains(ip50, &mark), "Fill interior missing.");
  check(t, pstatus, mark == markB, "Fill mark not preserved.");
  check(t, pstatus, !map.contains(ip200), "Span min not found.");
  check(t, pstatus, map.contains(ip15, &mark), "Old span interior not found.");
  check(t, pstatus, mark == markA, "Fill overwrote mark.");

  map.clear();
  check(t, pstatus, map.getCount() == 0, "Clear failed.");

  map.mark(ip20, ip50, markA);
  map.mark(ip100, ip150, markB);
  map.fill(ip10, ip200, markC);
  check(t, pstatus, map.getCount() == 5, "Test 3 failed [expected 5, got %d].", map.getCount());
  check(t, pstatus, map.contains(ip15, &mark), "Test 3 - left span missing.");
  check(t, pstatus, map.contains(ip60, &mark), "Test 3 - middle span missing.");
  check(t, pstatus, mark == markC, "Test 3 - fill mark wrong.");
  check(t, pstatus, map.contains(ip160), "Test 3 - right span missing.");
  check(t, pstatus, map.contains(ip120, &mark), "Test 3 - right mark span missing.");
  check(t, pstatus, mark == markB, "Test 3 - wrong data on right mark span.");
  map.unmark(ip140, ip160);
  check(t, pstatus, map.getCount() == 5, "Test 3 unmark failed [expected 5, got %d].", map.getCount());
  check(t, pstatus, !map.contains(ip140), "Test 3 - unmark left edge still there.");
  check(t, pstatus, !map.contains(ip150), "Test 3 - unmark middle still there.");
  check(t, pstatus, !map.contains(ip160), "Test 3 - unmark right edge still there.");
}

REGRESSION_TEST(IpMap_Test_Sample)(RegressionTest* t, int atype, int* pstatus) {
  IpMap map;
  void* const allow = reinterpret_cast<void*>(1);
  void* const deny = reinterpret_cast<void*>(2); 
  void* mark; // for retrieval

  IpEndpoint a1,a2,a3,a4,a5,a6, a7, a8;
  IpEndpoint target;
  IpEndpoint t6;

  *pstatus = REGRESSION_TEST_PASSED;

  ats_ip_pton("10.28.56.0", &a1);
  ats_ip_pton("10.28.56.255", &a2);
  ats_ip_pton("0.0.0.0", &a3);
  ats_ip_pton("255.255.255.255", &a4);
  ats_ip_pton("::", &a5);
  ats_ip_pton("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff", &a6);
  ats_ip_pton("fe80::221:9bff:fe10:9d90", &a7);
  ats_ip_pton("fe80::221:9bff:fe10:9d9d", &a8);
  ats_ip_pton("10.28.56.4", &target);
  ats_ip_pton("fe80::221:9bff:fe10:9d95", &t6);

  map.fill(&a1,&a2,deny);
  map.fill(&a3,&a4,allow);
  map.fill(&a5,&a6,allow);
  map.fill(&a7,&a8,deny);

  check(t, pstatus, map.contains(&target,&mark), "IpMap Sample: Target not found.");
  check(t, pstatus, mark == deny, "IpMap Sample: Bad target value. Expected deny, got allow.");
  check(t, pstatus, map.contains(&t6,&mark), "IpMap Sample: T6 not found.");
  check(t, pstatus, mark == allow, "IpMap Sample: Bad T6 value. Expected allow, got deny.");

  map.clear();
  map.fill(&a1,&a2,deny);
  map.fill(&a7,&a8,deny);
  map.fill(&a3,&a4,allow);
  map.fill(&a5,&a6,allow);
  check(t, pstatus, map.contains(&t6,&mark), "IpMap Sample: T6 not found.");
  check(t, pstatus, mark == deny, "IpMap Sample: Bad T6 value. Expected deny, got allow.");

}

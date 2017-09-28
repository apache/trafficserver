/**
  @file
  @brief Tests for IpHostMap implementations

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
#define CATCH_CONFIG_RUNNER
#include "catch.hpp"

#include "ip_host_map.h"
#include <string>
#include <list>
#include <iostream>

using namespace std;

// from test_sslheaders.cc
void
TSError(const char *fmt, ...)
{
  va_list args;

  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);
  fprintf(stderr, "\n");
}

void
TSDebug(const char *tag, const char *fmt, ...)
{
  va_list args;

  fprintf(stderr, "%s", tag);
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);
  fprintf(stderr, "\n");
}

bool
test_single_service_file_map(string file_location, list<tuple<sockaddr *, string>> &in, list<sockaddr *> &out)
{
  SingleServiceFileMap hostMap(file_location);
  bool fail = 0;
  for (const auto pair : in) {
    string expected = get<1>(pair);
    string actual   = hostMap.findHostForIP(get<0>(pair));
    if (actual.empty() || expected.compare(actual) != 0) {
      cout << "Expected host " << expected << ", but got " << (actual.empty() ? "an empty string" : actual) << endl;
      fail = 1;
    }
  }

  for (const auto ip : out) {
    if (hostMap.findHostForIP(ip) != nullptr) {
      cout << "Found an IP address that wasn't expected in the file." << endl;
      fail = 1;
    }
  }
  return fail;
}

string __ip_host_map_test__testfile_location;

int
main(int argc, char *argv[])
{
  // Get location of test file configurations
  string executable_location(*argv);
  __ip_host_map_test__testfile_location =
    executable_location.substr(0, executable_location.find_last_of("/")) + "/../example_configs/";

  // The rest is Catch business as usual.
  int result = Catch::Session().run(argc, argv);

  return (result < 0xff ? result : 0xff);
}

SCENARIO("Testing the ip host map with the SingleServiceFileMap", "[ip_host_map][single_service_file_map]")
{
  string testfile_location = __ip_host_map_test__testfile_location;

  sockaddr_storage a_10_28_56_4, a4;
  sockaddr_storage a_63_128_1_12;

  ats_ip_pton("10.28.56.4", (sockaddr *)&a_10_28_56_4);
  ats_ip_pton("192.168.1.255", (sockaddr *)&a4);
  ats_ip_pton("63.128.1.12", (sockaddr *)&a_63_128_1_12);

  sockaddr_storage address4_1, address4_2, address4_3, address4_4;
  sockaddr_storage address4_a, address4_b, address4_c, address4_d;
  sockaddr_storage address6_1, address6_2, address6_3, address6_4, address6_5, address6_6;
  sockaddr_storage address6_address4_1;

  ats_ip_pton("18.99.78.18", (sockaddr *)&address4_1);
  ats_ip_pton("18.74.249.181", (sockaddr *)&address4_2);
  ats_ip_pton("64.77.45.235", (sockaddr *)&address4_3);
  ats_ip_pton("64.77.148.24", (sockaddr *)&address4_4);
  ats_ip_pton("123.88.173.91", (sockaddr *)&address4_a);
  ats_ip_pton("123.78.102.62", (sockaddr *)&address4_b);
  ats_ip_pton("123.88.208.42", (sockaddr *)&address4_c);
  ats_ip_pton("123.82.209.166", (sockaddr *)&address4_d);

  ats_ip_pton("7ee9:6191:6f13:e7e6:444:4f5:75b9:54f9", (sockaddr *)&address6_1);
  ats_ip_pton("7ee9:a8f7:5ee:448e:ccea:64aa:28b7:c141", (sockaddr *)&address6_2);
  ats_ip_pton("7e3a:f3f3:3e2f:1d24:f980:75d0:653f:fcf7", (sockaddr *)&address6_3);
  ats_ip_pton("7e3a:f3f3:8c0b:7452:e615:ef7e:cec7:5266", (sockaddr *)&address6_4);
  ats_ip_pton("28b7::a8f7", (sockaddr *)&address6_5);
  ats_ip_pton("7e3a:dead::54f9", (sockaddr *)&address6_6);
  ats_ip_pton("2002:1263:4e12::", (sockaddr *)&address6_address4_1); // 6to4 address for address4_1

  GIVEN(" a configuration with one mapping")
  {
    string test_location = testfile_location + "single_service_file/test1.txt";

    WHEN(" we query for these two ip addresses")
    {
      list<tuple<sockaddr *, string>> in(1, make_tuple((sockaddr *)&a_63_128_1_12, "nebraska.example.com"));
      list<sockaddr *> out(1, (sockaddr *)&a_10_28_56_4);
      THEN(" one should be in and one should be out") { REQUIRE(test_single_service_file_map(test_location, in, out) == 0); }
    }
  }

  GIVEN(" a configuration with two mappings")
  {
    string test_location = testfile_location + "single_service_file/test2.txt";

    WHEN(" we query for these three ip addresses")
    {
      list<tuple<sockaddr *, string>> in;
      in.push_back(make_tuple((sockaddr *)&a_63_128_1_12, "buffalo.example.com"));
      in.push_back(make_tuple((sockaddr *)&a4, "washington.example.com"));
      list<sockaddr *> out(1, (sockaddr *)&a_10_28_56_4);
      THEN(" two should be in and one should be out") { REQUIRE(test_single_service_file_map(test_location, in, out) == 0); }
    }
  }

  GIVEN(" a configuration with ipv6 mappings")
  {
    string test_location = testfile_location + "single_service_file/test3.txt";
    WHEN(" we query for these five ip addresses")
    {
      list<tuple<sockaddr *, string>> in;
      in.push_back(make_tuple((sockaddr *)&address6_1, "singapore.example.com"));
      in.push_back(make_tuple((sockaddr *)&address6_2, "singapore.example.com"));
      in.push_back(make_tuple((sockaddr *)&address6_3, "taiwan.example.com"));
      in.push_back(make_tuple((sockaddr *)&address6_5, "newyork.example.com"));
      list<sockaddr *> out;
      out.push_back((sockaddr *)&address6_6);
      THEN(" four should be in and one should be out") { REQUIRE(test_single_service_file_map(test_location, in, out) == 0); }
    }
  }

  GIVEN(" a configuration with ipv6 and ipv4 mappings")
  {
    string test_location = testfile_location + "single_service_file/test4.txt";
    WHEN(" we query for these eight ip addresses")
    {
      list<tuple<sockaddr *, string>> in;
      in.push_back(make_tuple((sockaddr *)&address6_1, "egypt.example.com"));
      in.push_back(make_tuple((sockaddr *)&address6_2, "egypt.example.com"));
      in.push_back(make_tuple((sockaddr *)&address6_3, "morocco.example.com"));
      in.push_back(make_tuple((sockaddr *)&address6_4, "morocco.example.com"));
      in.push_back(make_tuple((sockaddr *)&address4_1, "egypt.example.com"));
      in.push_back(make_tuple((sockaddr *)&address4_2, "egypt.example.com"));
      in.push_back(make_tuple((sockaddr *)&address4_3, "morocco.example.com"));
      in.push_back(make_tuple((sockaddr *)&address4_4, "morocco.example.com"));
      list<sockaddr *> out;
      THEN(" all eight should be in the map") { REQUIRE(test_single_service_file_map(test_location, in, out) == 0); }
    }
  }

  // TODO uncomment and fix this most-specific-prefix sorting-based test
  /*
  GIVEN(" a configuration cross-nested ipv4/ipv6 mappings")
  {
    string test_location = testfile_location + "single_service_file/test5.txt";
    WHEN(" we query for these four ip addresses")
    {
      list< tuple<sockaddr  *, string> > in;
      in.push_back(make_tuple((sockaddr *)&address4_3, "ireland.example.com"));
      in.push_back(make_tuple((sockaddr *)&address4_4, "france.example.com"));
      in.push_back(make_tuple((sockaddr *)&address6_3, "france.example.com"));
      in.push_back(make_tuple((sockaddr *)&address6_4, "ireland.example.com"));
      list<sockaddr *> out;
      THEN(" all four should be in the map")
      {
        REQUIRE(test_single_service_file_map(test_location, in, out) == 0);
      }
    }
  }
  */

  GIVEN(" a configuration with ip 6to4 mappings")
  {
    string test_location = testfile_location + "single_service_file/test6.txt";
    WHEN(" we query for these three ip addresses")
    {
      list<tuple<sockaddr *, string>> in;
      in.push_back(make_tuple((sockaddr *)&address4_1, "sao.example.com"));
      in.push_back(make_tuple((sockaddr *)&address4_2, "sao.example.com"));
      // Even though this address is "semantically" the same, it won't map to the expected ipv4 prefix we defined.
      // in.push_back(make_tuple((sockaddr *)&address6_address4_1, "sao.example.com"));
      in.push_back(make_tuple((sockaddr *)&address6_address4_1, "rio.example.com"));
      list<sockaddr *> out;
      THEN(" all three should be in the map") { REQUIRE(test_single_service_file_map(test_location, in, out) == 0); }
    }
  }

  GIVEN(" a configuration that was regressing")
  {
    string test_location = testfile_location + "single_service_file/test7.txt";
    WHEN(" we query for these four ip addresses")
    {
      list<tuple<sockaddr *, string>> in;
      in.push_back(make_tuple((sockaddr *)&address4_a, "colorado.example.com"));
      in.push_back(make_tuple((sockaddr *)&address4_b, "utah.example.com"));
      in.push_back(make_tuple((sockaddr *)&address4_c, "arizona.example.com"));
      in.push_back(make_tuple((sockaddr *)&address4_d, "newmexico.example.com"));
      list<sockaddr *> out;
      THEN(" all four should be in the map") { REQUIRE(test_single_service_file_map(test_location, in, out) == 0); }
    }
  }
}

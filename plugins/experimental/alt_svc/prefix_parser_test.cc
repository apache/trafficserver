/**
  @file
  @brief Tests parsing and interpreting CIDR IP addresses and prefixes.

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

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include <string.h>
#include <iostream>
#include "prefix_parser.h"

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

/**
    Compare the output of a range with the resulting IP addresses.
    Provide, in this order, the prefix, the number on the "slash", the expected lower-bound, and the expected upper-bound.
*/
bool
ip_compare(const char *prefix, int prefix_num, const char *expected_lower, const char *expected_upper)
{
  sockaddr_storage ip_lower, ip_upper;
  if (parse_addresses(prefix, prefix_num, &ip_lower, &ip_upper) != PrefixParseError::ok) {
    std::cout << "The parse should be okay in these cases." << std::endl;
    return 1;
  }
  size_t max = INET6_ADDRSTRLEN;
  char actual_lower[INET6_ADDRSTRLEN], actual_upper[INET6_ADDRSTRLEN];
  bool fail = 0;
  if (strcmp(ats_ip_ntop((sockaddr *)&ip_lower, actual_lower, max), expected_lower) != 0) {
    std::cout << "Expected " << expected_lower << " for lower, but got: " << actual_lower << std::endl;
    fail = 1;
  }
  if (strcmp(ats_ip_ntop((sockaddr *)&ip_upper, actual_upper, max), expected_upper) != 0) {
    std::cout << "Expected " << expected_upper << " for upper, but got: " << actual_upper << std::endl;
    fail = 1;
  }
  return fail;
}

TEST_CASE("Test IPv4 works correctly", "[prefix_parser][ipv4]")
{
  REQUIRE(ip_compare("192.168.100.0", 22, "192.168.100.0", "192.168.103.255") == 0); // Test basic IPv4 prefix
  REQUIRE(ip_compare("127.0.0.1", 32, "127.0.0.1", "127.0.0.1") == 0);               // Test IPv4 full prefix
  REQUIRE(ip_compare("127.0.0.1", 31, "127.0.0.0", "127.0.0.1") == 0);               // Test IPv4 almost-full prefix
  REQUIRE(ip_compare("123.231.98.76", 0, "0.0.0.0", "255.255.255.255") == 0);        // Test IPv4 none pizza w/ left beef
}

TEST_CASE("Test IPv6 works correctly", "[prefix_parser][ipv6]")
{
  REQUIRE(ip_compare("2001:db8::", 48, "2001:db8::", "2001:db8:0:ffff:ffff:ffff:ffff:ffff") == 0); // Test basic IPv6 prefix
  REQUIRE(ip_compare("1000::", 120, "1000::", "1000::ff") == 0);                                   // Test IPv6 prefix on byte
  REQUIRE(ip_compare("1000::", 121, "1000::", "1000::7f") == 0);     // Test IPv6 prefix cross-byte (under)
  REQUIRE(ip_compare("1000::", 119, "1000::", "1000::1ff") == 0);    // Test IPv6 prefix cross-byte (over)
  REQUIRE(ip_compare("1000::", 111, "1000::", "1000::1:ffff") == 0); // Test another IPv6 prefix cross-byte (over)
  REQUIRE(ip_compare("7ee9::", 16, "7ee9::", "7ee9:ffff:ffff:ffff:ffff:ffff:ffff:ffff") == 0);           // Test IPv6 16 bit prefix
  REQUIRE(ip_compare("7e3a:f3f3::", 32, "7e3a:f3f3::", "7e3a:f3f3:ffff:ffff:ffff:ffff:ffff:ffff") == 0); // Test IPv6 32 bit prefix
  REQUIRE(ip_compare("::1", 128, "::1", "::1") == 0); // Test IPv6 Loopback prefix (full)
  REQUIRE(ip_compare("1234:5678::9abc:def0", 0, "::", "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff") ==
          0); // Test IPv6 none pizza w/ left beef
  REQUIRE(ip_compare("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff", 128, "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff",
                     "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff") == 0); // Test a buffer stress test
}

TEST_CASE("Test prefix works correctly", "[prefix_parser]")
{
  sockaddr_storage ip_lower, ip_upper;
  REQUIRE(parse_addresses("192.168.100.0", -1, &ip_lower, &ip_upper) == PrefixParseError::bad_prefix); // Test negative prefix
  REQUIRE(parse_addresses("2001:db8::", 129, &ip_lower, &ip_upper) == PrefixParseError::bad_prefix);   // Test too-big prefix
  REQUIRE(parse_addresses("192.168.100.0", 33, &ip_lower, &ip_upper) == PrefixParseError::bad_prefix); // Test too-big prefix IPv4
}

TEST_CASE("Test ip parser works correctly", "[prefix_parser]")
{
  sockaddr_storage ip_lower, ip_upper;
  REQUIRE(parse_addresses("lolwut", 4, &ip_lower, &ip_upper) == PrefixParseError::bad_ip); // Test absolute nonsense IP address
  REQUIRE(parse_addresses("192.168.256.0", 4, &ip_lower, &ip_upper) == PrefixParseError::bad_ip); // Test byte-madness IPv4 address
  REQUIRE(parse_addresses("123.68..0", 4, &ip_lower, &ip_upper) == PrefixParseError::bad_ip);     // Test missing byte IPv4 address
  REQUIRE(parse_addresses("1234::7a::ff", 48, &ip_lower, &ip_upper) == PrefixParseError::bad_ip); // Test weird IPv6 address
  REQUIRE(parse_addresses("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff:1234", 128, &ip_lower, &ip_upper) ==
          PrefixParseError::bad_ip); // Test buffer busting
}

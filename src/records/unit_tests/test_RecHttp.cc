/** @file

   Catch-based tests for HdrsUtils.cc

   @section license License

   Licensed to the Apache Software Foundation (ASF) under one or more contributor license agreements.
   See the NOTICE file distributed with this work for additional information regarding copyright
   ownership.  The ASF licenses this file to you under the Apache License, Version 2.0 (the
   "License"); you may not use this file except in compliance with the License.  You may obtain a
   copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software distributed under the License
   is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
   or implied. See the License for the specific language governing permissions and limitations under
   the License.
 */

#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "catch.hpp"

#include "records/RecHttp.h"
#include "test_Diags.h"
#include "tscore/ink_defs.h"

using swoc::TextView;

TEST_CASE("RecHttp", "[librecords][RecHttp]")
{
  std::vector<HttpProxyPort> ports;
  CatchDiags                *cdiag = static_cast<CatchDiags *>(diags());
  cdiag->messages.clear();

  SECTION("base")
  {
    HttpProxyPort::loadValue(ports, "8080");
    REQUIRE(ports.size() == 1);
    REQUIRE(ports[0].m_port == 8080);
  }

  SECTION("two")
  {
    HttpProxyPort::loadValue(ports, "8080 8090");
    REQUIRE(ports.size() == 2);
    REQUIRE(ports[0].m_port == 8080);
    REQUIRE(ports[1].m_port == 8090);
  }

  SECTION("family")
  {
    HttpProxyPort::loadValue(ports, "7070:ipv4:ip-in=192.168.56.1");
    REQUIRE(ports.size() == 1);
    REQUIRE(ports[0].m_port == 7070);
    REQUIRE(ports[0].m_family == AF_INET);
    REQUIRE(ports[0].isSSL() == false);
  }

  SECTION("crossed-family")
  {
    HttpProxyPort::loadValue(ports, "7070:ipv6:ip-in=192.168.56.1");
    REQUIRE(ports.size() == 0);
    REQUIRE(cdiag->messages.size() == 2);
    REQUIRE(cdiag->messages[0].find("[ipv6]") != std::string::npos);
    REQUIRE(cdiag->messages[0].find("[ipv4]") != std::string::npos);
  }

  SECTION("ipv6-a")
  {
    TextView descriptor{"4443:ssl:ip-in=[ffee::24c3:3349:3cee:0143]"};
    HttpProxyPort::loadValue(ports, descriptor.data());
    REQUIRE(ports.size() == 1);
    REQUIRE(ports[0].m_port == 4443);
    REQUIRE(ports[0].m_family == AF_INET6);
    REQUIRE(ports[0].isSSL() == true);
  }

  SECTION("dual-addr")
  {
    TextView descriptor{"4443:ssl:ipv6:ip-out=[ffee::24c3:3349:3cee:0143]:ip-out=10.1.2.3"};
    HttpProxyPort::loadValue(ports, descriptor.data());
    char buff[256];
    ports[0].print(buff, sizeof(buff));
    std::string_view view{buff};
    REQUIRE(ports.size() == 1);
    REQUIRE(ports[0].m_port == 4443);
    REQUIRE(ports[0].m_family == AF_INET6);
    REQUIRE(ports[0].isSSL() == true);
    REQUIRE(ports[0].m_outbound.has_ip6() == true);
    REQUIRE(ports[0].m_outbound.has_ip4() == true);
    REQUIRE(ports[0].m_inbound_ip.isValid() == false);
    REQUIRE(view.find(":ssl") != TextView::npos);
    REQUIRE(view.find(":proto") == TextView::npos); // it's default, should not have this.
  }
}

struct ConvertAlpnToWireFormatTestCase {
  std::string   description;
  std::string   alpn_input;
  unsigned char expected_alpn_wire_format[MAX_ALPN_STRING] = {0};
  int           expected_alpn_wire_format_len              = MAX_ALPN_STRING;
  bool          expected_return                            = true;
};

// clang-format off
std::vector<ConvertAlpnToWireFormatTestCase> convertAlpnToWireFormatTestCases = {
  // --------------------------------------------------------------------------
  // Malformed input.
  // --------------------------------------------------------------------------
  {
    "Empty input protocol list",
    "",
    { 0 },
    0,
    false
  },
  {
    "Include an empty protocol in the list",
    "http/1.1,,http/1.0",
    { 0 },
    0,
    false
  },
  {
    "A protocol that exceeds the output buffer length (MAX_ALPN_STRING)",
    "some_really_long_protocol_name_that_exceeds_the_output_buffer_length_that_is_MAX_ALPN_STRING",
    { 0 },
    0,
    false
  },
  {
    "The sum of protocols exceeds the output buffer length (MAX_ALPN_STRING)",
    "protocol_one,protocol_two,protocol_three",
    { 0 },
    0,
    false
  },
  {
    "A protocol that exceeds the length described by a single byte (255)",
    "some_really_long_protocol_name_that_exceeds_255_bytes_some_really_long_protocol_name_that_exceeds_255_bytes_some_really_long_protocol_name_that_exceeds_255_bytes_some_really_long_protocol_name_that_exceeds_255_bytes_some_really_long_protocol_name_that_exceeds_255_bytes",
    { 0 },
    0,
    false
  },
  // --------------------------------------------------------------------------
  // Unsupported protocols.
  // --------------------------------------------------------------------------
  {
    "Unrecognized protocol: HTTP/6",
    "h6",
    { 0 },
    0,
    false
  },
  {
    "Single protocol: HTTP/0.9",
    "http/0.9",
    { 0 },
    0,
    false
  },
  {
    "Single protocol: HTTP/3 (currently unsupported)",
    "h3",
    { 0 },
    0,
    false
  },
  // --------------------------------------------------------------------------
  // Happy cases.
  // --------------------------------------------------------------------------
  {
    "Single protocol: HTTP/1.1",
    "http/1.1",
    {0x08, 'h', 't', 't', 'p', '/', '1', '.', '1'},
    9,
    true
  },
  {
    "Single protocol: HTTP/2",
    "h2",
    {0x02, 'h', '2'},
    3,
    true
  },
  {
    "Multiple protocols: HTTP/1.1, HTTP/1.0",
    "http/1.1,http/1.0",
    {0x08, 'h', 't', 't', 'p', '/', '1', '.', '1', 0x08, 'h', 't', 't', 'p', '/', '1', '.', '0'},
    18,
    true
  },
  {
    "Whitespace: verify that we gracefully handle padded whitespace",
    "http/1.1, http/1.0",
    {0x08, 'h', 't', 't', 'p', '/', '1', '.', '1', 0x08, 'h', 't', 't', 'p', '/', '1', '.', '0'},
    18,
    true
  },
  {
    "Both HTTP/2 and HTTP/1.1",
    "h2,http/1.1",
    {0x02, 'h', '2', 0x08, 'h', 't', 't', 'p', '/', '1', '.', '1'},
    12,
    true
  },
};
// clang-format on

TEST_CASE("convert_alpn_to_wire_format", "[librecords][RecHttp]")
{
  for (auto const &test_case : convertAlpnToWireFormatTestCases) {
    SECTION(test_case.description)
    {
      unsigned char alpn_wire_format[MAX_ALPN_STRING] = {0xab};
      int           alpn_wire_format_len              = MAX_ALPN_STRING;
      auto const    result = convert_alpn_to_wire_format(test_case.alpn_input, alpn_wire_format, alpn_wire_format_len);
      REQUIRE(result == test_case.expected_return);
      REQUIRE(alpn_wire_format_len == test_case.expected_alpn_wire_format_len);
      REQUIRE(memcmp(alpn_wire_format, test_case.expected_alpn_wire_format, test_case.expected_alpn_wire_format_len) == 0);
    }
  }
}

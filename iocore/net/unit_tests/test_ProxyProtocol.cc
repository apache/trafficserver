/** @file

  Catch based unit tests for PROXY Protocol

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

#include "ProxyProtocol.h"

using namespace std::literals;

TEST_CASE("PROXY Protocol v1 Parser", "[ProxyProtocol][ProxyProtocolv1]")
{
  IpEndpoint src_addr;
  IpEndpoint dst_addr;

  SECTION("TCP over IPv4")
  {
    ts::TextView raw_data = "PROXY TCP4 192.0.2.1 198.51.100.1 50000 443\r\n"sv;

    ProxyProtocol pp_info;
    REQUIRE(proxy_protocol_parse(&pp_info, raw_data) == raw_data.size());

    REQUIRE(ats_ip_pton("192.0.2.1:50000", src_addr) == 0);
    REQUIRE(ats_ip_pton("198.51.100.1:443", dst_addr) == 0);

    CHECK(pp_info.version == ProxyProtocolVersion::V1);
    CHECK(pp_info.ip_family == AF_INET);
    CHECK(pp_info.src_addr == src_addr);
    CHECK(pp_info.dst_addr == dst_addr);
  }

  SECTION("TCP over IPv6")
  {
    ts::TextView raw_data = "PROXY TCP6 2001:0DB8:0:0:0:0:0:1 2001:0DB8:0:0:0:0:0:2 50000 443\r\n"sv;

    ProxyProtocol pp_info;
    REQUIRE(proxy_protocol_parse(&pp_info, raw_data) == raw_data.size());

    REQUIRE(ats_ip_pton("[2001:0DB8:0:0:0:0:0:1]:50000", src_addr) == 0);
    REQUIRE(ats_ip_pton("[2001:0DB8:0:0:0:0:0:2]:443", dst_addr) == 0);

    CHECK(pp_info.version == ProxyProtocolVersion::V1);
    CHECK(pp_info.ip_family == AF_INET6);
    CHECK(pp_info.src_addr == src_addr);
    CHECK(pp_info.dst_addr == dst_addr);
  }

  SECTION("UNKNOWN connection (short form)")
  {
    ts::TextView raw_data = "PROXY UNKNOWN\r\n"sv;

    ProxyProtocol pp_info;
    REQUIRE(proxy_protocol_parse(&pp_info, raw_data) == raw_data.size());

    CHECK(pp_info.version == ProxyProtocolVersion::V1);
    CHECK(pp_info.ip_family == AF_UNSPEC);
  }

  SECTION("UNKNOWN connection (worst case)")
  {
    ts::TextView raw_data =
      "PROXY UNKNOWN ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff 65535 65535\r\n"sv;

    ProxyProtocol pp_info;
    REQUIRE(proxy_protocol_parse(&pp_info, raw_data) == raw_data.size());

    CHECK(pp_info.version == ProxyProtocolVersion::V1);
    CHECK(pp_info.ip_family == AF_UNSPEC);
  }

  SECTION("Malformed Headers")
  {
    ProxyProtocol pp_info;

    // lack of some fields
    CHECK(proxy_protocol_parse(&pp_info, "PROXY TCP4"sv) == 0);
    CHECK(proxy_protocol_parse(&pp_info, "PROXY TCP4 192.0.2.1"sv) == 0);
    CHECK(proxy_protocol_parse(&pp_info, "PROXY TCP4 192.0.2.1 198.51.100.1\r\n"sv) == 0);
    CHECK(proxy_protocol_parse(&pp_info, "PROXY TCP4 192.0.2.1 198.51.100.1\r\n"sv) == 0);
    CHECK(proxy_protocol_parse(&pp_info, "PROXY TCP4 192.0.2.1 198.51.100.1 50000 \r\n"sv) == 0);

    // invalid preface
    CHECK(proxy_protocol_parse(&pp_info, "PROX TCP4 192.0.2.1 198.51.100.1 50000 443\r\n"sv) == 0);
    CHECK(proxy_protocol_parse(&pp_info, "PROXZ TCP4 192.0.2.1 198.51.100.1 50000 443\r\n"sv) == 0);

    // invalid transport protocol & address family
    CHECK(proxy_protocol_parse(&pp_info, "PROXY TCP1 192.0.2.1 198.51.100.1 50000 443\r\n"sv) == 0);
    CHECK(proxy_protocol_parse(&pp_info, "PROXY UDP4 192.0.2.1 198.51.100.1 50000 443\r\n"sv) == 0);

    // extra space
    CHECK(proxy_protocol_parse(&pp_info, "PROXY  TCP4 192.0.2.1 198.51.100.1 50000 443\r\n"sv) == 0);
    CHECK(proxy_protocol_parse(&pp_info, "PROXY TCP4  192.0.2.1 198.51.100.1 50000 443\r\n"sv) == 0);
    CHECK(proxy_protocol_parse(&pp_info, "PROXY TCP4 192.0.2.1  198.51.100.1 50000 443\r\n"sv) == 0);
    CHECK(proxy_protocol_parse(&pp_info, "PROXY TCP4 192.0.2.1 198.51.100.1  50000 443\r\n"sv) == 0);
    CHECK(proxy_protocol_parse(&pp_info, "PROXY TCP4 192.0.2.1 198.51.100.1 50000  443\r\n"sv) == 0);
    CHECK(proxy_protocol_parse(&pp_info, "PROXY TCP4 192.0.2.1 198.51.100.1 50000 443 \r\n"sv) == 0);

    // invalid CRLF
    CHECK(proxy_protocol_parse(&pp_info, "PROXY TCP4 192.0.2.1 198.51.100.1 50000 443"sv) == 0);
    CHECK(proxy_protocol_parse(&pp_info, "PROXY TCP4 192.0.2.1 198.51.100.1 50000 443\n"sv) == 0);
    CHECK(proxy_protocol_parse(&pp_info, "PROXY TCP4 192.0.2.1 198.51.100.1 50000 443\r"sv) == 0);
  }
}

TEST_CASE("PROXY Protocol v2 Parser", "[ProxyProtocol][ProxyProtocolv2]")
{
  IpEndpoint src_addr;
  IpEndpoint dst_addr;

  SECTION("TCP over IPv4 without TLVs")
  {
    uint8_t raw_data[] = {
      0x0D, 0x0A, 0x0D, 0x0A, 0x00, 0x0D, 0x0A, 0x51, ///< preface
      0x55, 0x49, 0x54, 0x0A,                         ///<
      0x21,                                           ///< version & command
      0x11,                                           ///< protocol & family
      0x00, 0x0C,                                     ///< len
      0xC0, 0x00, 0x02, 0x01,                         ///< src_addr
      0xC6, 0x33, 0x64, 0x01,                         ///< dst_addr
      0xC3, 0x50,                                     ///< src_port
      0x01, 0xBB,                                     ///< dst_port
    };

    ts::TextView tv(reinterpret_cast<char *>(raw_data), sizeof(raw_data));

    ProxyProtocol pp_info;
    REQUIRE(proxy_protocol_parse(&pp_info, tv) == tv.size());

    REQUIRE(ats_ip_pton("192.0.2.1:50000", src_addr) == 0);
    REQUIRE(ats_ip_pton("198.51.100.1:443", dst_addr) == 0);

    CHECK(pp_info.version == ProxyProtocolVersion::V2);
    CHECK(pp_info.ip_family == AF_INET);
    CHECK(pp_info.src_addr == src_addr);
    CHECK(pp_info.dst_addr == dst_addr);
  }

  SECTION("TCP over IPv6 without TLVs")
  {
    uint8_t raw_data[] = {
      0x0D, 0x0A, 0x0D, 0x0A, 0x00, 0x0D, 0x0A, 0x51, ///< preface
      0x55, 0x49, 0x54, 0x0A,                         ///<
      0x21,                                           ///< version & command
      0x21,                                           ///< protocol & family
      0x00, 0x24,                                     ///< len
      0x20, 0x01, 0x0D, 0xB8, 0x00, 0x00, 0x00, 0x01, ///< src_addr
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, ///<
      0x20, 0x01, 0x0D, 0xB8, 0x00, 0x00, 0x00, 0x02, ///< dst_addr
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, ///<
      0xC3, 0x50,                                     ///< src_port
      0x01, 0xBB,                                     ///< dst_port
    };

    ts::TextView tv(reinterpret_cast<char *>(raw_data), sizeof(raw_data));

    ProxyProtocol pp_info;
    REQUIRE(proxy_protocol_parse(&pp_info, tv) == tv.size());

    REQUIRE(ats_ip_pton("[2001:db8:0:1::]:50000", src_addr) == 0);
    REQUIRE(ats_ip_pton("[2001:db8:0:2::]:443", dst_addr) == 0);

    CHECK(pp_info.version == ProxyProtocolVersion::V2);
    CHECK(pp_info.ip_family == AF_INET6);
    CHECK(pp_info.src_addr == src_addr);
    CHECK(pp_info.dst_addr == dst_addr);
  }

  SECTION("LOCAL command - health check")
  {
    uint8_t raw_data[] = {
      0x0D, 0x0A, 0x0D, 0x0A, 0x00, 0x0D, 0x0A, 0x51, ///< preface
      0x55, 0x49, 0x54, 0x0A,                         ///<
      0x20,                                           ///< version & command
      0x00,                                           ///< protocol & family
      0x00, 0x24,                                     ///< len
      0x20, 0x01, 0x0D, 0xB8, 0x00, 0x00, 0x00, 0x01, ///< src_addr
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, ///<
      0x20, 0x01, 0x0D, 0xB8, 0x00, 0x00, 0x00, 0x02, ///< dst_addr
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, ///<
      0xC3, 0x50,                                     ///< src_port
      0x01, 0xBB,                                     ///< dst_port
    };

    ts::TextView tv(reinterpret_cast<char *>(raw_data), sizeof(raw_data));

    ProxyProtocol pp_info;
    REQUIRE(proxy_protocol_parse(&pp_info, tv) == tv.size());

    CHECK(pp_info.version == ProxyProtocolVersion::V2);
    CHECK(pp_info.ip_family == AF_UNSPEC);
  }

  SECTION("UNSPEC - unknownun/specified/unsupported transport protocol & address family")
  {
    uint8_t raw_data[] = {
      0x0D, 0x0A, 0x0D, 0x0A, 0x00, 0x0D, 0x0A, 0x51, ///< preface
      0x55, 0x49, 0x54, 0x0A,                         ///<
      0x21,                                           ///< version & command
      0x00,                                           ///< protocol & family
      0x00, 0x24,                                     ///< len
      0x20, 0x01, 0x0D, 0xB8, 0x00, 0x00, 0x00, 0x01, ///< src_addr
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, ///<
      0x20, 0x01, 0x0D, 0xB8, 0x00, 0x00, 0x00, 0x02, ///< dst_addr
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, ///<
      0xC3, 0x50,                                     ///< src_port
      0x01, 0xBB,                                     ///< dst_port
    };

    ts::TextView tv(reinterpret_cast<char *>(raw_data), sizeof(raw_data));

    ProxyProtocol pp_info;
    REQUIRE(proxy_protocol_parse(&pp_info, tv) == 0);

    CHECK(pp_info.version == ProxyProtocolVersion::UNDEFINED);
    CHECK(pp_info.ip_family == AF_UNSPEC);
  }

  // TLVs are not supported yet. Checking TLVs are skipped as expected for now.
  SECTION("TLVs")
  {
    uint8_t raw_data[] = {
      0x0D, 0x0A, 0x0D, 0x0A, 0x00, 0x0D, 0x0A, 0x51, ///< preface
      0x55, 0x49, 0x54, 0x0A,                         ///<
      0x21,                                           ///< version & command
      0x11,                                           ///< protocol & family
      0x00, 0x11,                                     ///< len
      0xC0, 0x00, 0x02, 0x01,                         ///< src_addr
      0xC6, 0x33, 0x64, 0x01,                         ///< dst_addr
      0xC3, 0x50,                                     ///< src_port
      0x01, 0xBB,                                     ///< dst_port
      0x01, 0x00, 0x02, 0x68, 0x32,                   /// PP2_TYPE_ALPN (h2)
    };

    ts::TextView tv(reinterpret_cast<char *>(raw_data), sizeof(raw_data));

    ProxyProtocol pp_info;
    REQUIRE(proxy_protocol_parse(&pp_info, tv) == tv.size());

    REQUIRE(ats_ip_pton("192.0.2.1:50000", src_addr) == 0);
    REQUIRE(ats_ip_pton("198.51.100.1:443", dst_addr) == 0);

    CHECK(pp_info.version == ProxyProtocolVersion::V2);
    CHECK(pp_info.ip_family == AF_INET);
    CHECK(pp_info.src_addr == src_addr);
    CHECK(pp_info.dst_addr == dst_addr);
  }

  SECTION("Malformed Headers")
  {
    ProxyProtocol pp_info;

    SECTION("invalid preface")
    {
      uint8_t raw_data[] = {
        0xDE, 0xAD, 0xBE, 0xEF, 0xDE, 0xAD, 0xBE, 0xEF, ///< preface
        0xDE, 0xAD, 0xBE, 0xEF,                         ///<
        0x21,                                           ///< version & command
        0x21,                                           ///< protocol & family
        0x00, 0x24,                                     ///< len
        0x20, 0x01, 0x0D, 0xB8, 0x00, 0x00, 0x00, 0x01, ///< src_addr
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, ///<
        0x20, 0x01, 0x0D, 0xB8, 0x00, 0x00, 0x00, 0x02, ///< dst_addr
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, ///<
        0xC3, 0x50,                                     ///< src_port
        0x01, 0xBB,                                     ///< dst_port
      };

      ts::TextView tv(reinterpret_cast<char *>(raw_data), sizeof(raw_data));

      CHECK(proxy_protocol_parse(&pp_info, tv) == 0);
      CHECK(pp_info.version == ProxyProtocolVersion::UNDEFINED);
      CHECK(pp_info.ip_family == AF_UNSPEC);
    }

    SECTION("unsupported version & command")
    {
      uint8_t raw_data[] = {
        0x0D, 0x0A, 0x0D, 0x0A, 0x00, 0x0D, 0x0A, 0x51, ///< preface
        0x55, 0x49, 0x54, 0x0A,                         ///<
        0xFF,                                           ///< version & command
        0x21,                                           ///< protocol & family
        0x00, 0x24,                                     ///< len
        0x20, 0x01, 0x0D, 0xB8, 0x00, 0x00, 0x00, 0x01, ///< src_addr
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, ///<
        0x20, 0x01, 0x0D, 0xB8, 0x00, 0x00, 0x00, 0x02, ///< dst_addr
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, ///<
        0xC3, 0x50,                                     ///< src_port
        0x01, 0xBB,                                     ///< dst_port
      };

      ts::TextView tv(reinterpret_cast<char *>(raw_data), sizeof(raw_data));

      CHECK(proxy_protocol_parse(&pp_info, tv) == 0);
      CHECK(pp_info.version == ProxyProtocolVersion::UNDEFINED);
      CHECK(pp_info.ip_family == AF_UNSPEC);
    }

    SECTION("unsupported protocol & family")
    {
      uint8_t raw_data[] = {
        0x0D, 0x0A, 0x0D, 0x0A, 0x00, 0x0D, 0x0A, 0x51, ///< preface
        0x55, 0x49, 0x54, 0x0A,                         ///<
        0x21,                                           ///< version & command
        0xFF,                                           ///< protocol & family
        0x00, 0x24,                                     ///< len
        0x20, 0x01, 0x0D, 0xB8, 0x00, 0x00, 0x00, 0x01, ///< src_addr
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, ///<
        0x20, 0x01, 0x0D, 0xB8, 0x00, 0x00, 0x00, 0x02, ///< dst_addr
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, ///<
        0xC3, 0x50,                                     ///< src_port
        0x01, 0xBB,                                     ///< dst_port
      };

      ts::TextView tv(reinterpret_cast<char *>(raw_data), sizeof(raw_data));

      CHECK(proxy_protocol_parse(&pp_info, tv) == 0);
      CHECK(pp_info.version == ProxyProtocolVersion::UNDEFINED);
      CHECK(pp_info.ip_family == AF_UNSPEC);
    }

    SECTION("invalid len value - too long")
    {
      uint8_t raw_data[] = {
        0x0D, 0x0A, 0x0D, 0x0A, 0x00, 0x0D, 0x0A, 0x51, ///< preface
        0x55, 0x49, 0x54, 0x0A,                         ///<
        0x21,                                           ///< version & command
        0x21,                                           ///< protocol & family
        0x00, 0x25,                                     ///< len
        0x20, 0x01, 0x0D, 0xB8, 0x00, 0x00, 0x00, 0x01, ///< src_addr
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, ///<
        0x20, 0x01, 0x0D, 0xB8, 0x00, 0x00, 0x00, 0x02, ///< dst_addr
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, ///<
        0xC3, 0x50,                                     ///< src_port
        0x01, 0xBB,                                     ///< dst_port
      };

      ts::TextView tv(reinterpret_cast<char *>(raw_data), sizeof(raw_data));

      CHECK(proxy_protocol_parse(&pp_info, tv) == 0);
      CHECK(pp_info.version == ProxyProtocolVersion::UNDEFINED);
      CHECK(pp_info.ip_family == AF_UNSPEC);
    }

    SECTION("invalid len - actual buffer is shorter than the value")
    {
      uint8_t raw_data[] = {
        0x0D, 0x0A, 0x0D, 0x0A, 0x00, 0x0D, 0x0A, 0x51, ///< preface
        0x55, 0x49, 0x54, 0x0A,                         ///<
        0x21,                                           ///< version & command
        0x21,                                           ///< protocol & family
        0x00, 0x24,                                     ///< len
        0x20, 0x01, 0x0D, 0xB8, 0x00, 0x00, 0x00, 0x01, ///< src_addr
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, ///<
        0x20, 0x01, 0x0D, 0xB8, 0x00, 0x00, 0x00, 0x02, ///< dst_addr
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, ///<
        0xC3, 0x50,                                     ///< src_port
        0x01,                                           ///< dst_port
      };

      ts::TextView tv(reinterpret_cast<char *>(raw_data), sizeof(raw_data));

      CHECK(proxy_protocol_parse(&pp_info, tv) == 0);
      CHECK(pp_info.version == ProxyProtocolVersion::UNDEFINED);
      CHECK(pp_info.ip_family == AF_UNSPEC);
    }

    SECTION("invalid len - too short for INET")
    {
      uint8_t raw_data[] = {
        0x0D, 0x0A, 0x0D, 0x0A, 0x00, 0x0D, 0x0A, 0x51, ///< preface
        0x55, 0x49, 0x54, 0x0A,                         ///<
        0x21,                                           ///< version & command
        0x11,                                           ///< protocol & family
        0x00, 0x0C,                                     ///< len
        0xC0, 0x00,                                     ///< src_addr
        0xC6, 0x33,                                     ///< dst_addr
        0xC3, 0x50,                                     ///< src_port
        0x01, 0xBB,                                     ///< dst_port
      };

      ts::TextView tv(reinterpret_cast<char *>(raw_data), sizeof(raw_data));

      CHECK(proxy_protocol_parse(&pp_info, tv) == 0);
      CHECK(pp_info.version == ProxyProtocolVersion::UNDEFINED);
      CHECK(pp_info.ip_family == AF_UNSPEC);
    }

    SECTION("invalid len - too short for INET6")
    {
      uint8_t raw_data[] = {
        0x0D, 0x0A, 0x0D, 0x0A, 0x00, 0x0D, 0x0A, 0x51, ///< preface
        0x55, 0x49, 0x54, 0x0A,                         ///<
        0x21,                                           ///< version & command
        0x21,                                           ///< protocol & family
        0x00, 0x24,                                     ///< len
        0x20, 0x01, 0x0D, 0xB8, 0x00, 0x00, 0x00, 0x01, ///< src_addr
        0x20, 0x01, 0x0D, 0xB8, 0x00, 0x00, 0x00, 0x02, ///< dst_addr
        0xC3, 0x50,                                     ///< src_port
        0x01, 0xBB,                                     ///< dst_port
      };

      ts::TextView tv(reinterpret_cast<char *>(raw_data), sizeof(raw_data));

      CHECK(proxy_protocol_parse(&pp_info, tv) == 0);
      CHECK(pp_info.version == ProxyProtocolVersion::UNDEFINED);
      CHECK(pp_info.ip_family == AF_UNSPEC);
    }
  }
}

TEST_CASE("ProxyProtocol v1 Builder", "[ProxyProtocol][ProxyProtocolv1]")
{
  SECTION("TCP over IPv4")
  {
    uint8_t buf[PPv1_CONNECTION_HEADER_LEN_MAX] = {0};

    ProxyProtocol pp_info;
    pp_info.version   = ProxyProtocolVersion::V1;
    pp_info.ip_family = AF_INET;
    ats_ip_pton("192.0.2.1:50000", pp_info.src_addr);
    ats_ip_pton("198.51.100.1:443", pp_info.dst_addr);

    size_t len = proxy_protocol_build(buf, sizeof(buf), pp_info);

    std::string_view expected = "PROXY TCP4 192.0.2.1 198.51.100.1 50000 443\r\n"sv;

    CHECK(len == expected.size());
    CHECK(memcmp(buf, expected.data(), expected.size()) == 0);
  }

  SECTION("TCP over IPv6")
  {
    uint8_t buf[PPv1_CONNECTION_HEADER_LEN_MAX] = {0};

    ProxyProtocol pp_info;
    pp_info.version   = ProxyProtocolVersion::V1;
    pp_info.ip_family = AF_INET6;
    ats_ip_pton("[2001:db8:0:1::]:50000", pp_info.src_addr);
    ats_ip_pton("[2001:db8:0:2::]:443", pp_info.dst_addr);

    size_t len = proxy_protocol_build(buf, sizeof(buf), pp_info);

    std::string_view expected = "PROXY TCP6 2001:db8:0:1:: 2001:db8:0:2:: 50000 443\r\n"sv;

    CHECK(len == expected.size());
    CHECK(memcmp(buf, expected.data(), expected.size()) == 0);
  }
}

TEST_CASE("ProxyProtocol v2 Builder", "[ProxyProtocol][ProxyProtocolv2]")
{
  SECTION("TCP over IPv4 / no TLV")
  {
    uint8_t buf[1024] = {0};

    ProxyProtocol pp_info;
    pp_info.version   = ProxyProtocolVersion::V2;
    pp_info.ip_family = AF_INET;
    ats_ip_pton("192.0.2.1:50000", pp_info.src_addr);
    ats_ip_pton("198.51.100.1:443", pp_info.dst_addr);

    size_t len = proxy_protocol_build(buf, sizeof(buf), pp_info);

    uint8_t expected[] = {
      0x0D, 0x0A, 0x0D, 0x0A, 0x00, 0x0D, 0x0A, 0x51, ///< sig
      0x55, 0x49, 0x54, 0x0A,                         ///<
      0x21,                                           ///< ver_vmd
      0x11,                                           ///< fam
      0x00, 0x0C,                                     ///< len
      0xC0, 0x00, 0x02, 0x01,                         ///< src_addr
      0xC6, 0x33, 0x64, 0x01,                         ///< dst_addr
      0xC3, 0x50,                                     ///< src_port
      0x01, 0xBB,                                     ///< dst_port
    };

    CHECK(len == sizeof(expected));
    CHECK(memcmp(expected, buf, len) == 0);
  }

  SECTION("TCP over IPv6 / no TLV")
  {
    uint8_t buf[1024] = {0};

    ProxyProtocol pp_info;
    pp_info.version   = ProxyProtocolVersion::V2;
    pp_info.ip_family = AF_INET6;
    ats_ip_pton("[2001:db8:0:1::]:50000", pp_info.src_addr);
    ats_ip_pton("[2001:db8:0:2::]:443", pp_info.dst_addr);

    size_t len = proxy_protocol_build(buf, sizeof(buf), pp_info);

    uint8_t expected[] = {
      0x0D, 0x0A, 0x0D, 0x0A, 0x00, 0x0D, 0x0A, 0x51, ///< sig
      0x55, 0x49, 0x54, 0x0A,                         ///<
      0x21,                                           ///< ver_vmd
      0x21,                                           ///< fam
      0x00, 0x24,                                     ///< len
      0x20, 0x01, 0x0D, 0xB8, 0x00, 0x00, 0x00, 0x01, ///< src_addr
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, ///<
      0x20, 0x01, 0x0D, 0xB8, 0x00, 0x00, 0x00, 0x02, ///< dst_addr
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, ///<
      0xC3, 0x50,                                     ///< src_port
      0x01, 0xBB,                                     ///< dst_port
    };

    CHECK(len == sizeof(expected));
    CHECK(memcmp(expected, buf, len) == 0);
  }
}

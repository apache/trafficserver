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

#include <catch2/catch_test_macros.hpp>

#include "iocore/net/ProxyProtocol.h"

using namespace std::literals;

TEST_CASE("PROXY Protocol v1 Parser", "[ProxyProtocol][ProxyProtocolv1]")
{
  IpEndpoint src_addr;
  IpEndpoint dst_addr;

  SECTION("TCP over IPv4")
  {
    swoc::TextView raw_data = "PROXY TCP4 192.0.2.1 198.51.100.1 50000 443\r\n"sv;

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
    swoc::TextView raw_data = "PROXY TCP6 2001:0DB8:0:0:0:0:0:1 2001:0DB8:0:0:0:0:0:2 50000 443\r\n"sv;

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
    swoc::TextView raw_data = "PROXY UNKNOWN\r\n"sv;

    ProxyProtocol pp_info;
    REQUIRE(proxy_protocol_parse(&pp_info, raw_data) == raw_data.size());

    CHECK(pp_info.version == ProxyProtocolVersion::V1);
    CHECK(pp_info.ip_family == AF_UNSPEC);
  }

  SECTION("UNKNOWN connection (worst case)")
  {
    swoc::TextView raw_data =
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

    swoc::TextView tv(reinterpret_cast<char *>(raw_data), sizeof(raw_data));

    ProxyProtocol pp_info;
    REQUIRE(proxy_protocol_parse(&pp_info, tv) == tv.size());

    REQUIRE(ats_ip_pton("192.0.2.1:50000", src_addr) == 0);
    REQUIRE(ats_ip_pton("198.51.100.1:443", dst_addr) == 0);

    CHECK(pp_info.version == ProxyProtocolVersion::V2);
    CHECK(pp_info.ip_family == AF_INET);
    CHECK(pp_info.type == SOCK_STREAM);
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

    swoc::TextView tv(reinterpret_cast<char *>(raw_data), sizeof(raw_data));

    ProxyProtocol pp_info;
    REQUIRE(proxy_protocol_parse(&pp_info, tv) == tv.size());

    REQUIRE(ats_ip_pton("[2001:db8:0:1::]:50000", src_addr) == 0);
    REQUIRE(ats_ip_pton("[2001:db8:0:2::]:443", dst_addr) == 0);

    CHECK(pp_info.version == ProxyProtocolVersion::V2);
    CHECK(pp_info.ip_family == AF_INET6);
    CHECK(pp_info.type == SOCK_STREAM);
    CHECK(pp_info.src_addr == src_addr);
    CHECK(pp_info.dst_addr == dst_addr);
  }

  SECTION("UDP over IPv4 without TLVs")
  {
    uint8_t raw_data[] = {
      0x0D, 0x0A, 0x0D, 0x0A, 0x00, 0x0D, 0x0A, 0x51, ///< preface
      0x55, 0x49, 0x54, 0x0A,                         ///<
      0x21,                                           ///< version & command
      0x12,                                           ///< protocol & family
      0x00, 0x0C,                                     ///< len
      0xC0, 0x00, 0x02, 0x01,                         ///< src_addr
      0xC6, 0x33, 0x64, 0x01,                         ///< dst_addr
      0xC3, 0x50,                                     ///< src_port
      0x01, 0xBB,                                     ///< dst_port
    };

    swoc::TextView tv(reinterpret_cast<char *>(raw_data), sizeof(raw_data));

    ProxyProtocol pp_info;
    REQUIRE(proxy_protocol_parse(&pp_info, tv) == tv.size());

    REQUIRE(ats_ip_pton("192.0.2.1:50000", src_addr) == 0);
    REQUIRE(ats_ip_pton("198.51.100.1:443", dst_addr) == 0);

    CHECK(pp_info.version == ProxyProtocolVersion::V2);
    CHECK(pp_info.ip_family == AF_INET);
    CHECK(pp_info.type == SOCK_DGRAM);
    CHECK(pp_info.src_addr == src_addr);
    CHECK(pp_info.dst_addr == dst_addr);
  }

  SECTION("UDP over IPv6 without TLVs")
  {
    uint8_t raw_data[] = {
      0x0D, 0x0A, 0x0D, 0x0A, 0x00, 0x0D, 0x0A, 0x51, ///< preface
      0x55, 0x49, 0x54, 0x0A,                         ///<
      0x21,                                           ///< version & command
      0x22,                                           ///< protocol & family
      0x00, 0x24,                                     ///< len
      0x20, 0x01, 0x0D, 0xB8, 0x00, 0x00, 0x00, 0x01, ///< src_addr
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, ///<
      0x20, 0x01, 0x0D, 0xB8, 0x00, 0x00, 0x00, 0x02, ///< dst_addr
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, ///<
      0xC3, 0x50,                                     ///< src_port
      0x01, 0xBB,                                     ///< dst_port
    };

    swoc::TextView tv(reinterpret_cast<char *>(raw_data), sizeof(raw_data));

    ProxyProtocol pp_info;
    REQUIRE(proxy_protocol_parse(&pp_info, tv) == tv.size());

    REQUIRE(ats_ip_pton("[2001:db8:0:1::]:50000", src_addr) == 0);
    REQUIRE(ats_ip_pton("[2001:db8:0:2::]:443", dst_addr) == 0);

    CHECK(pp_info.version == ProxyProtocolVersion::V2);
    CHECK(pp_info.ip_family == AF_INET6);
    CHECK(pp_info.type == SOCK_DGRAM);
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

    swoc::TextView tv(reinterpret_cast<char *>(raw_data), sizeof(raw_data));

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

    swoc::TextView tv(reinterpret_cast<char *>(raw_data), sizeof(raw_data));

    ProxyProtocol pp_info;
    REQUIRE(proxy_protocol_parse(&pp_info, tv) == 0);

    CHECK(pp_info.version == ProxyProtocolVersion::UNDEFINED);
    CHECK(pp_info.ip_family == AF_UNSPEC);
  }

  SECTION("TLVs")
  {
    uint8_t raw_data[] = {
      0x0D, 0x0A, 0x0D, 0x0A, 0x00, 0x0D, 0x0A, 0x51, ///< preface
      0x55, 0x49, 0x54, 0x0A,                         ///<
      0x21,                                           ///< version & command
      0x11,                                           ///< protocol & family
      0x00, 0x17,                                     ///< len
      0xC0, 0x00, 0x02, 0x01,                         ///< src_addr
      0xC6, 0x33, 0x64, 0x01,                         ///< dst_addr
      0xC3, 0x50,                                     ///< src_port
      0x01, 0xBB,                                     ///< dst_port
      0x01, 0x00, 0x02, 0x68, 0x32,                   /// PP2_TYPE_ALPN (h2)
      0x02, 0x00, 0x03, 0x61, 0x62, 0x63              /// PP2_TYPE_AUTHORITY (abc)
    };

    swoc::TextView tv(reinterpret_cast<char *>(raw_data), sizeof(raw_data));

    ProxyProtocol pp_info;
    REQUIRE(proxy_protocol_parse(&pp_info, tv) == tv.size());

    REQUIRE(ats_ip_pton("192.0.2.1:50000", src_addr) == 0);
    REQUIRE(ats_ip_pton("198.51.100.1:443", dst_addr) == 0);

    CHECK(pp_info.version == ProxyProtocolVersion::V2);
    CHECK(pp_info.ip_family == AF_INET);
    CHECK(pp_info.src_addr == src_addr);
    CHECK(pp_info.dst_addr == dst_addr);

    CHECK(pp_info.tlv[PP2_TYPE_ALPN] == "h2");
    CHECK(pp_info.tlv[PP2_TYPE_AUTHORITY] == "abc");
  }

  SECTION("TLVs with extra data")
  {
    uint8_t raw_data[] = {
      0x0D, 0x0A, 0x0D, 0x0A, 0x00, 0x0D, 0x0A, 0x51, ///< preface
      0x55, 0x49, 0x54, 0x0A,                         ///<
      0x21,                                           ///< version & command
      0x11,                                           ///< protocol & family
      0x00, 0x17,                                     ///< len
      0xC0, 0x00, 0x02, 0x01,                         ///< src_addr
      0xC6, 0x33, 0x64, 0x01,                         ///< dst_addr
      0xC3, 0x50,                                     ///< src_port
      0x01, 0xBB,                                     ///< dst_port
      0x01, 0x00, 0x02, 0x68, 0x32,                   /// PP2_TYPE_ALPN (h2)
      0x02, 0x00, 0x03, 0x61, 0x62, 0x63,             /// PP2_TYPE_AUTHORITY (abc)
      0x47, 0x45, 0x54, 0x20, 0x2F                    /// Extra data (GET /)
    };

    swoc::TextView tv(reinterpret_cast<char *>(raw_data), sizeof(raw_data));

    ProxyProtocol pp_info;
    REQUIRE(proxy_protocol_parse(&pp_info, tv) == tv.size() - 5); // The extra 5 bytes at the end should not be parsed

    REQUIRE(ats_ip_pton("192.0.2.1:50000", src_addr) == 0);
    REQUIRE(ats_ip_pton("198.51.100.1:443", dst_addr) == 0);

    CHECK(pp_info.version == ProxyProtocolVersion::V2);
    CHECK(pp_info.ip_family == AF_INET);
    CHECK(pp_info.src_addr == src_addr);
    CHECK(pp_info.dst_addr == dst_addr);

    CHECK(pp_info.tlv[PP2_TYPE_ALPN] == "h2");
    CHECK(pp_info.tlv[PP2_TYPE_AUTHORITY] == "abc");
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

      swoc::TextView tv(reinterpret_cast<char *>(raw_data), sizeof(raw_data));

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

      swoc::TextView tv(reinterpret_cast<char *>(raw_data), sizeof(raw_data));

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

      swoc::TextView tv(reinterpret_cast<char *>(raw_data), sizeof(raw_data));

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

      swoc::TextView tv(reinterpret_cast<char *>(raw_data), sizeof(raw_data));

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

      swoc::TextView tv(reinterpret_cast<char *>(raw_data), sizeof(raw_data));

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

      swoc::TextView tv(reinterpret_cast<char *>(raw_data), sizeof(raw_data));

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

      swoc::TextView tv(reinterpret_cast<char *>(raw_data), sizeof(raw_data));

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

TEST_CASE("ProxyProtocol Rule of 5", "[ProxyProtocol]")
{
  SECTION("Copy constructor with no additional_data")
  {
    ProxyProtocol original;
    original.version   = ProxyProtocolVersion::V2;
    original.ip_family = AF_INET;
    ats_ip_pton("192.0.2.1:50000", original.src_addr);
    ats_ip_pton("198.51.100.1:443", original.dst_addr);

    ProxyProtocol copy(original);

    CHECK(copy.version == original.version);
    CHECK(copy.ip_family == original.ip_family);
    CHECK(copy.src_addr == original.src_addr);
    CHECK(copy.dst_addr == original.dst_addr);
  }

  SECTION("Copy constructor with additional_data")
  {
    ProxyProtocol original;
    original.version   = ProxyProtocolVersion::V2;
    original.ip_family = AF_INET;
    ats_ip_pton("192.0.2.1:50000", original.src_addr);

    // Set properly formatted TLV data: type=0x01, length=0x0002, value="h2"
    std::string_view tlv_data("\x01\x00\x02h2", 5);
    original.set_additional_data(tlv_data);

    ProxyProtocol copy(original);

    CHECK(copy.version == original.version);
    CHECK(copy.ip_family == original.ip_family);
    CHECK(copy.src_addr == original.src_addr);

    // Verify the data was copied
    auto original_tlv = original.get_tlv(0x01);
    auto copy_tlv     = copy.get_tlv(0x01);
    REQUIRE(original_tlv.has_value());
    REQUIRE(copy_tlv.has_value());
    CHECK(original_tlv.value() == "h2");
    CHECK(copy_tlv.value() == "h2");
  }

  SECTION("Move constructor with no additional_data")
  {
    ProxyProtocol original;
    original.version   = ProxyProtocolVersion::V2;
    original.ip_family = AF_INET;
    ats_ip_pton("192.0.2.1:50000", original.src_addr);
    ats_ip_pton("198.51.100.1:443", original.dst_addr);

    ProxyProtocol moved(std::move(original));

    CHECK(moved.version == ProxyProtocolVersion::V2);
    CHECK(moved.ip_family == AF_INET);
  }

  SECTION("Move constructor with additional_data")
  {
    ProxyProtocol original;
    original.version   = ProxyProtocolVersion::V2;
    original.ip_family = AF_INET;
    ats_ip_pton("192.0.2.1:50000", original.src_addr);

    // Set properly formatted TLV data: type=0x01, length=0x0002, value="h2"
    std::string_view tlv_data("\x01\x00\x02h2", 5);
    original.set_additional_data(tlv_data);

    auto original_tlv = original.get_tlv(0x01);
    REQUIRE(original_tlv.has_value());
    CHECK(original_tlv.value() == "h2");

    ProxyProtocol moved(std::move(original));

    // Verify the moved object has the data
    CHECK(moved.version == ProxyProtocolVersion::V2);
    auto moved_tlv = moved.get_tlv(0x01);
    REQUIRE(moved_tlv.has_value());
    CHECK(moved_tlv.value() == "h2");

    // Original should have been moved from (additional_data set to nullptr)
    auto original_tlv_after = original.get_tlv(0x01);
    CHECK_FALSE(original_tlv_after.has_value());
  }

  SECTION("Copy assignment with no additional_data")
  {
    ProxyProtocol original;
    original.version   = ProxyProtocolVersion::V2;
    original.ip_family = AF_INET;
    ats_ip_pton("192.0.2.1:50000", original.src_addr);

    ProxyProtocol copy;
    copy = original;

    CHECK(copy.version == original.version);
    CHECK(copy.ip_family == original.ip_family);
    CHECK(copy.src_addr == original.src_addr);
  }

  SECTION("Copy assignment with additional_data")
  {
    ProxyProtocol original;
    original.version   = ProxyProtocolVersion::V2;
    original.ip_family = AF_INET;
    ats_ip_pton("192.0.2.1:50000", original.src_addr);

    // Set properly formatted TLV data: type=0x02, length=0x0003, value="abc"
    std::string_view tlv_data("\x02\x00\x03"
                              "abc",
                              6);
    original.set_additional_data(tlv_data);

    ProxyProtocol copy;
    copy = original;

    CHECK(copy.version == original.version);
    CHECK(copy.ip_family == original.ip_family);

    // Verify deep copy - both should have independent data
    auto original_tlv = original.get_tlv(0x02);
    auto copy_tlv     = copy.get_tlv(0x02);
    REQUIRE(original_tlv.has_value());
    REQUIRE(copy_tlv.has_value());
    CHECK(original_tlv.value() == "abc");
    CHECK(copy_tlv.value() == "abc");
  }

  SECTION("Copy assignment overwrites existing additional_data")
  {
    ProxyProtocol original;
    original.version   = ProxyProtocolVersion::V2; // Must be V2 for get_tlv to work
    original.ip_family = AF_INET;

    // Set properly formatted TLV data for original: type=0x01, length=0x0008, value="original"
    std::string_view orig_tlv_data("\x01\x00\x08original", 11);
    original.set_additional_data(orig_tlv_data);

    ProxyProtocol copy;
    copy.version   = ProxyProtocolVersion::V2;
    copy.ip_family = AF_INET6;

    // Set properly formatted TLV data for copy: type=0x02, length=0x0004, value="copy"
    std::string_view copy_tlv_data("\x02\x00\x04copy", 7);
    copy.set_additional_data(copy_tlv_data);

    copy = original;

    // After assignment, copy should have original's data
    CHECK(copy.version == ProxyProtocolVersion::V2);
    CHECK(copy.ip_family == AF_INET);

    // Verify copy now has original's TLV data
    auto copy_tlv = copy.get_tlv(0x01);
    REQUIRE(copy_tlv.has_value());
    CHECK(copy_tlv.value() == "original");

    // Old TLV should be gone
    auto old_tlv = copy.get_tlv(0x02);
    CHECK_FALSE(old_tlv.has_value());
  }

  SECTION("Move assignment with no additional_data")
  {
    ProxyProtocol original;
    original.version   = ProxyProtocolVersion::V2;
    original.ip_family = AF_INET;
    ats_ip_pton("192.0.2.1:50000", original.src_addr);

    ProxyProtocol target;
    target = std::move(original);

    CHECK(target.version == ProxyProtocolVersion::V2);
    CHECK(target.ip_family == AF_INET);
  }

  SECTION("Move assignment with additional_data")
  {
    ProxyProtocol original;
    original.version   = ProxyProtocolVersion::V2;
    original.ip_family = AF_INET;
    ats_ip_pton("192.0.2.1:50000", original.src_addr);

    // Set properly formatted TLV data: type=0x01, length=0x0004, value="test"
    std::string_view tlv_data("\x01\x00\x04test", 7);
    original.set_additional_data(tlv_data);

    auto original_tlv = original.get_tlv(0x01);
    REQUIRE(original_tlv.has_value());
    CHECK(original_tlv.value() == "test");

    ProxyProtocol target;
    target = std::move(original);

    // Verify target has the data
    CHECK(target.version == ProxyProtocolVersion::V2);
    auto target_tlv = target.get_tlv(0x01);
    REQUIRE(target_tlv.has_value());
    CHECK(target_tlv.value() == "test");

    // Original should have been moved from
    auto original_tlv_after = original.get_tlv(0x01);
    CHECK_FALSE(original_tlv_after.has_value());
  }

  SECTION("Move assignment overwrites existing additional_data")
  {
    ProxyProtocol original;
    original.version   = ProxyProtocolVersion::V2; // Must be V2 for get_tlv to work
    original.ip_family = AF_INET;

    // Set properly formatted TLV data for original: type=0x01, length=0x0008, value="original"
    std::string_view orig_tlv_data("\x01\x00\x08original", 11);
    original.set_additional_data(orig_tlv_data);

    ProxyProtocol target;
    target.version   = ProxyProtocolVersion::V2;
    target.ip_family = AF_INET6;

    // Set properly formatted TLV data for target: type=0x02, length=0x0006, value="target"
    std::string_view target_tlv_data("\x02\x00\x06target", 9);
    target.set_additional_data(target_tlv_data);

    target = std::move(original);

    // After move assignment, target should have original's data
    CHECK(target.version == ProxyProtocolVersion::V2);
    CHECK(target.ip_family == AF_INET);

    // Verify target has original's TLV
    auto target_tlv = target.get_tlv(0x01);
    REQUIRE(target_tlv.has_value());
    CHECK(target_tlv.value() == "original");

    // Original should have been moved from
    auto original_tlv = original.get_tlv(0x01);
    CHECK_FALSE(original_tlv.has_value());
  }

  SECTION("Destructor cleans up additional_data")
  {
    // Test that destructor properly frees memory
    // This is mainly tested through sanitizers (ASAN)
    {
      ProxyProtocol pp;
      pp.version = ProxyProtocolVersion::V2; // get_tlv requires V2

      // Set properly formatted TLV data: type=0x01, length=0x0004, value="test"
      std::string_view tlv_data("\x01\x00\x04test", 7);
      pp.set_additional_data(tlv_data);

      auto tlv = pp.get_tlv(0x01);
      REQUIRE(tlv.has_value());
      CHECK(tlv.value() == "test");
    }
    // Object destroyed here - ASAN will catch any memory leaks
  }

  SECTION("Multiple copy and move operations")
  {
    ProxyProtocol original;
    original.version   = ProxyProtocolVersion::V2;
    original.ip_family = AF_INET;

    // Set properly formatted TLV data: type=0x01, length=0x0008, value="original"
    std::string_view tlv_data("\x01\x00\x08original", 11);
    original.set_additional_data(tlv_data);

    ProxyProtocol copy1(original);
    ProxyProtocol copy2;
    copy2 = copy1;

    ProxyProtocol moved1(std::move(copy2));
    ProxyProtocol moved2;
    moved2 = std::move(moved1);

    // Final object should have the data
    CHECK(moved2.version == ProxyProtocolVersion::V2);
    auto final_tlv = moved2.get_tlv(0x01);
    REQUIRE(final_tlv.has_value());
    CHECK(final_tlv.value() == "original");

    // Original should still have its data (wasn't moved from)
    auto orig_tlv = original.get_tlv(0x01);
    REQUIRE(orig_tlv.has_value());
    CHECK(orig_tlv.value() == "original");
  }
}

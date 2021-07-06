/** @file
 *
 *  A brief file description
 *
 *  @section license License
 *
 *  Licensed to the Apache Software Foundation (ASF) under one
 *  or more contributor license agreements.  See the NOTICE file
 *  distributed with this work for additional information
 *  regarding copyright ownership.  The ASF licenses this file
 *  to you under the Apache License, Version 2.0 (the
 *  "License"); you may not use this file except in compliance
 *  with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include "catch.hpp"

#include "quic/QUICAltConnectionManager.h"
#include "quic/QUICIntUtil.h"
#include <memory>

TEST_CASE("QUICPreferredAddress", "[quic]")
{
  uint8_t buf[] = {
    0x12, 0x34, 0x56, 0x78,                                     // IPv4 address
    0x23, 0x45,                                                 // IPv4 port
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,             // IPv6 address
    0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x34, 0x56, // IPv6 port
    0x01,                                                       // ConnectionId length
    0x55,                                                       // ConnectionId
    0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,             // Stateless Reset Token
    0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff,
  };
  uint8_t cid_buf[] = {0x55};
  QUICConnectionId cid55(cid_buf, sizeof(cid_buf));
  in6_addr ipv6_addr;
  memcpy(ipv6_addr.s6_addr, "\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19\x1a\x1b\x1c\x1d\x1e\x1f", 16);

  SECTION("load")
  {
    auto pref_addr = new QUICPreferredAddress(buf, sizeof(buf));
    CHECK(pref_addr->is_available());
    CHECK(pref_addr->has_ipv4());
    CHECK(pref_addr->endpoint_ipv4().isIp4());
    CHECK(pref_addr->endpoint_ipv4().host_order_port() == 0x2345);
    CHECK(pref_addr->endpoint_ipv4().sin.sin_addr.s_addr == 0x78563412);
    CHECK(pref_addr->has_ipv6());
    CHECK(pref_addr->endpoint_ipv6().isIp6());
    CHECK(pref_addr->endpoint_ipv6().host_order_port() == 0x3456);
    CHECK(memcmp(pref_addr->endpoint_ipv6().sin6.sin6_addr.s6_addr, ipv6_addr.s6_addr, 16) == 0);
    CHECK(pref_addr->cid() == cid55);
    CHECK(memcmp(pref_addr->token().buf(), buf + 26, 16) == 0);
    delete pref_addr;
  }

  SECTION("store")
  {
    IpEndpoint ep_ipv4;
    ats_ip4_set(&ep_ipv4, 0x78563412, 0x4523);

    IpEndpoint ep_ipv6;
    ats_ip6_set(&ep_ipv6, ipv6_addr, 0x5634);

    auto pref_addr = new QUICPreferredAddress(ep_ipv4, ep_ipv6, cid55, {buf + 26});
    CHECK(pref_addr->is_available());
    CHECK(pref_addr->has_ipv4());
    CHECK(pref_addr->endpoint_ipv4().isIp4());
    CHECK(pref_addr->endpoint_ipv4().host_order_port() == 0x2345);
    CHECK(pref_addr->endpoint_ipv4().sin.sin_addr.s_addr == 0x78563412);
    CHECK(pref_addr->has_ipv6());
    CHECK(pref_addr->endpoint_ipv6().isIp6());
    CHECK(pref_addr->endpoint_ipv6().host_order_port() == 0x3456);
    CHECK(memcmp(pref_addr->endpoint_ipv6().sin6.sin6_addr.s6_addr, ipv6_addr.s6_addr, 16) == 0);
    CHECK(pref_addr->cid() == cid55);

    uint8_t actual[QUICPreferredAddress::MAX_LEN];
    uint16_t len;
    pref_addr->store(actual, len);
    CHECK(sizeof(buf) == len);
    CHECK(memcmp(buf, actual, sizeof(buf)) == 0);
    delete pref_addr;
  }

  SECTION("unavailable")
  {
    auto pref_addr = new QUICPreferredAddress(nullptr, 0);
    CHECK(!pref_addr->is_available());
    CHECK(!pref_addr->has_ipv4());
    CHECK(!pref_addr->has_ipv6());
    delete pref_addr;
  }
}

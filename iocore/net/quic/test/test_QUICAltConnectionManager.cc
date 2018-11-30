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
  const uint8_t buf[] = {
    0x04,                   // ipVersion
    0x04,                   // ipAddress length
    0x01, 0x02, 0x03, 0x04, // ipAddress
    0x11, 0x22,             // port
    0x01,                   // connectionId length
    0x55,                   // connectionId
    0x10, 0x11, 0x12, 0x13, // statelessResetToken
    0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
  };
  uint8_t cid_buf[] = {0x55};
  QUICConnectionId cid55(cid_buf, sizeof(cid_buf));

  SECTION("load")
  {
    auto pref_addr = new QUICPreferredAddress(buf, sizeof(buf));
    CHECK(pref_addr->is_available());
    CHECK(pref_addr->endpoint().isIp4());
    CHECK(pref_addr->endpoint().host_order_port() == 0x1122);
    CHECK(pref_addr->endpoint().sin.sin_addr.s_addr == 0x04030201);
    CHECK(pref_addr->cid() == cid55);
    CHECK(memcmp(pref_addr->token().buf(), buf + 10, 16) == 0);
  }

  SECTION("store")
  {
    IpEndpoint ep;
    ats_ip4_set(&ep, 0x04030201, 0x2211);

    auto pref_addr = new QUICPreferredAddress(ep, cid55, {buf + 10});
    CHECK(pref_addr->is_available());
    CHECK(pref_addr->endpoint().isIp4());
    CHECK(pref_addr->endpoint().host_order_port() == 0x1122);
    CHECK(pref_addr->endpoint().sin.sin_addr.s_addr == 0x04030201);
    CHECK(pref_addr->cid() == cid55);

    uint8_t actual[QUICPreferredAddress::MAX_LEN];
    uint16_t len;
    pref_addr->store(actual, len);
    CHECK(sizeof(buf) == len);
    CHECK(memcmp(buf, actual, sizeof(buf)) == 0);
  }

  SECTION("unavailable")
  {
    auto pref_addr = new QUICPreferredAddress(nullptr, 0);
    CHECK(!pref_addr->is_available());
  }
}

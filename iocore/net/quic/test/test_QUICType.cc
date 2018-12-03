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

#include "quic/QUICTypes.h"
#include "I_EventSystem.h"
#include "tscore/ink_hrtime.h"
#include <memory>

TEST_CASE("QUICType", "[quic]")
{
  SECTION("QUICRetryToken")
  {
    IpEndpoint ep;
    ats_ip4_set(&ep, 0x04030201, 0x2211);

    uint8_t cid_buf[] = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
                         0x19, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27};
    QUICConnectionId cid(cid_buf, sizeof(cid_buf));

    QUICRetryToken token1(ep, cid);
    QUICRetryToken token2(token1.buf(), token1.length());

    CHECK(token1.is_valid(ep));
    CHECK(token2.is_valid(ep));
    CHECK(QUICAddressValidationToken::type(token1.buf()) == QUICAddressValidationToken::Type::RETRY);
    CHECK(QUICAddressValidationToken::type(token2.buf()) == QUICAddressValidationToken::Type::RETRY);
    CHECK(token1 == token2);
    CHECK(token1.length() == token2.length());
    CHECK(memcmp(token1.buf(), token2.buf(), token1.length()) == 0);
    CHECK(token1.original_dcid() == token2.original_dcid());
  }

  SECTION("QUICResumptionToken")
  {
    IpEndpoint ep;
    ats_ip4_set(&ep, 0x04030201, 0x2211);

    uint8_t cid_buf[] = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
                         0x19, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27};
    QUICConnectionId cid(cid_buf, sizeof(cid_buf));

    ink_hrtime expire_date = Thread::get_hrtime() + (3 * HRTIME_DAY);

    QUICResumptionToken token1(ep, cid, expire_date);
    QUICResumptionToken token2(token1.buf(), token1.length());

    CHECK(token1.is_valid(ep));
    CHECK(token2.is_valid(ep));
    CHECK(QUICAddressValidationToken::type(token1.buf()) == QUICAddressValidationToken::Type::RESUMPTION);
    CHECK(QUICAddressValidationToken::type(token2.buf()) == QUICAddressValidationToken::Type::RESUMPTION);
    CHECK(token1 == token2);
    CHECK(token1.length() == token2.length());
    CHECK(memcmp(token1.buf(), token2.buf(), token1.length()) == 0);
    CHECK(token1.cid() == token2.cid());
  }
}

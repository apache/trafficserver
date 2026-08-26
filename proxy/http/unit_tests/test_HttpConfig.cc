/** @file

  Catch-based tests for HttpConfig.cc.

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

#include "catch.hpp"

#include "HttpConfig.h"
#include "HttpProxyAPIEnums.h"

// The pooled outbound-session lookup only evaluates a validator when its bit is
// set in the match mask. A mask without MASK_CERT therefore never compares the
// client certificate of a pooled TLS session against the certificate the new
// transaction would present, so a session authenticated with one client
// certificate can be handed to a transaction configured for another. Because
// the outbound certificate is per-transaction overridable, that is an identity
// confusion at the origin, so the default value has to carry the bit.
TEST_CASE("server_session_sharing.match parsing", "[HttpConfig]")
{
  MgmtByte mask = 0;

  // Each section pins the whole mask rather than individual bits, so a validator
  // that is silently added to or dropped from a value is a test failure.

  SECTION("the default value matches address, host, host/SNI sync, and the client certificate")
  {
    REQUIRE(HttpConfig::load_server_session_sharing_match("both", mask) == true);
    CHECK(mask == (TS_SERVER_SESSION_SHARING_MATCH_MASK_IP | TS_SERVER_SESSION_SHARING_MATCH_MASK_HOSTONLY |
                   TS_SERVER_SESSION_SHARING_MATCH_MASK_HOSTSNISYNC | TS_SERVER_SESSION_SHARING_MATCH_MASK_CERT));
  }

  SECTION("an explicit list selects exactly the named validators")
  {
    REQUIRE(HttpConfig::load_server_session_sharing_match("ip,sni,cert", mask) == true);
    CHECK(mask == (TS_SERVER_SESSION_SHARING_MATCH_MASK_IP | TS_SERVER_SESSION_SHARING_MATCH_MASK_SNI |
                   TS_SERVER_SESSION_SHARING_MATCH_MASK_CERT));
  }

  SECTION("none disables every validator")
  {
    REQUIRE(HttpConfig::load_server_session_sharing_match("none", mask) == true);
    CHECK(mask == 0);
  }

  SECTION("ip,host selects what the default selected before cert was added")
  {
    MgmtByte explicit_mask = 0;
    REQUIRE(HttpConfig::load_server_session_sharing_match("ip,host", explicit_mask) == true);
    CHECK(explicit_mask == (TS_SERVER_SESSION_SHARING_MATCH_MASK_IP | TS_SERVER_SESSION_SHARING_MATCH_MASK_HOSTONLY |
                            TS_SERVER_SESSION_SHARING_MATCH_MASK_HOSTSNISYNC));
  }

  SECTION("an unknown value is rejected") { CHECK(HttpConfig::load_server_session_sharing_match("nonsense", mask) == false); }
}

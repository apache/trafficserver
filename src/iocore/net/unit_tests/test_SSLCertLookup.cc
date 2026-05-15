/** @file

  Catch based unit tests for SSLCertLookup

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

#include "../P_SSLCertLookup.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

TEST_CASE("SSLCertLookup handles high-bit bytes while normalizing hostnames")
{
  constexpr auto HIGH_ORDER_BIT = static_cast<char>(0x80);

  SSLCertLookup lookup;
  auto         *ctx = SSL_CTX_new(SSLv23_server_method());
  REQUIRE(ctx != nullptr);

  SSLCertContext context(ctx);

  std::string cert_name{"High"};
  cert_name.push_back(HIGH_ORDER_BIT);
  cert_name.append(".Example.Com");

  std::string lookup_name{"hIGH"};
  lookup_name.push_back(HIGH_ORDER_BIT);
  lookup_name.append(".eXAMPLE.cOM");

  REQUIRE(lookup.insert(cert_name.c_str(), context) >= 0);

  SSLCertContext *matched = lookup.find(lookup_name);
  REQUIRE(matched != nullptr);
  CHECK(matched->getCtx().get() == ctx);
}

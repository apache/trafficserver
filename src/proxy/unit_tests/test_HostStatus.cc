/** @file

  Unit tests for HostStatus reason formatting

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

#include "proxy/HostStatus.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("HostStatus reason masks have readable names", "[HostStatus]")
{
  REQUIRE(Reason::getReasonName(0) == "none");
  REQUIRE(Reason::getReasonName(Reason::ACTIVE) == "active");
  REQUIRE(Reason::getReasonName(Reason::LOCAL) == "local");
  REQUIRE(Reason::getReasonName(Reason::MANUAL) == "manual");
  REQUIRE(Reason::getReasonName(Reason::SELF_DETECT) == "self_detect");
  REQUIRE(Reason::getReasonName(Reason::ALL) == "all");
  REQUIRE(Reason::getReasonName(Reason::ACTIVE | Reason::LOCAL) == "active,local");
  REQUIRE(Reason::getReasonName(Reason::getReason(Reason::MANUAL_REASON)) == "manual");
}

TEST_CASE("HostStatus unknown reason bits stay visible", "[HostStatus]")
{
  REQUIRE(Reason::getReasonName(0x20) == "unknown(0x20)");
  REQUIRE(Reason::getReasonName(0xf0) == "unknown(0xf0)");
  REQUIRE(Reason::getReasonName(Reason::ACTIVE | 0x30) == "active,unknown(0x30)");
  REQUIRE(Reason::getReasonName(Reason::ALL | 0x20) == "all,unknown(0x20)");
}

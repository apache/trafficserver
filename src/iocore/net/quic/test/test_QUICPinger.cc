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

#include "QUICPinger.h"

static constexpr QUICEncryptionLevel level = QUICEncryptionLevel::ONE_RTT;
static uint8_t frame[1024]                 = {0};

TEST_CASE("QUICPinger", "[quic]")
{
  SECTION("request and cancel")
  {
    QUICPinger pinger;
    pinger.request(level);
    REQUIRE(pinger.count(level) == 1);
    pinger.request(level);
    REQUIRE(pinger.count(level) == 2);
    pinger.cancel(level);
    REQUIRE(pinger.count(level) == 1);
    REQUIRE(pinger.generate_frame(frame, level, UINT64_MAX, UINT16_MAX, 0, 0) != nullptr);
    REQUIRE(pinger.count(level) == 0);
  }

  SECTION("generate PING Frame twice")
  {
    QUICPinger pinger;
    pinger.request(level);
    REQUIRE(pinger.count(level) == 1);
    pinger.request(level);
    REQUIRE(pinger.count(level) == 2);
    REQUIRE(pinger.will_generate_frame(level, UINT64_MAX, false, 0) == true);
    REQUIRE(pinger.count(level) == 2);
    REQUIRE(pinger.will_generate_frame(level, UINT64_MAX, false, 0) == false);
    REQUIRE(pinger.count(level) == 2);
  }

  SECTION("don't generate frame when packet is ack_elicting")
  {
    QUICPinger pinger;
    pinger.request(level);
    REQUIRE(pinger.count(level) == 1);
    pinger.request(level);
    REQUIRE(pinger.count(level) == 2);
    REQUIRE(pinger.will_generate_frame(level, UINT64_MAX, true, 0) == false);
    REQUIRE(pinger.count(level) == 1);
    REQUIRE(pinger.will_generate_frame(level, UINT64_MAX, true, 1) == false);
    REQUIRE(pinger.count(level) == 0);
  }

  SECTION("generating PING Frame for next continuous un-ack-eliciting packets")
  {
    QUICPinger pinger;
    REQUIRE(pinger.will_generate_frame(level, UINT64_MAX, false, 0) == true);
    REQUIRE(pinger.count(level) == 1);
    REQUIRE(pinger.will_generate_frame(level, UINT64_MAX, true, 1) == false);
    REQUIRE(pinger.count(level) == 0);
    REQUIRE(pinger.will_generate_frame(level, UINT64_MAX, false, 2) == false);
    REQUIRE(pinger.count(level) == 0);
    REQUIRE(pinger.will_generate_frame(level, UINT64_MAX, false, 3) == true);
    REQUIRE(pinger.count(level) == 1);
  }

  SECTION("don't send PING Frame for empty packet")
  {
    QUICPinger pinger;
    REQUIRE(pinger.will_generate_frame(level, 0, false, 0) == false);
    REQUIRE(pinger.count(level) == 0);
    REQUIRE(pinger.will_generate_frame(level, UINT64_MAX, false, 1) == true);
    REQUIRE(pinger.count(level) == 1);
    REQUIRE(pinger.will_generate_frame(level, UINT64_MAX, true, 2) == false);
    REQUIRE(pinger.count(level) == 0);
    REQUIRE(pinger.will_generate_frame(level, UINT64_MAX, false, 3) == false);
    REQUIRE(pinger.count(level) == 0);
    REQUIRE(pinger.will_generate_frame(level, 0, false, 4) == false);
    REQUIRE(pinger.count(level) == 0);
    REQUIRE(pinger.will_generate_frame(level, 1, false, 5) == true);
    REQUIRE(pinger.count(level) == 1);
  }
}

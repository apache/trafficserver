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

#include "quic/QUICVersionNegotiator.h"
#include "quic/Mock.h"

TEST_CASE("QUICVersionNegotiator", "[quic]")
{
  QUICPacketFactory packet_factory;
  MockQUICCrypto crypto;
  packet_factory.set_crypto_module(&crypto);
  QUICVersionNegotiator vn;

  SECTION("Normal case")
  {
    // Check initial state
    CHECK(vn.status() == QUICVersionNegotiationStatus::NOT_NEGOTIATED);

    // Negotiate version
    QUICPacketUPtr initial_packet =
      packet_factory.create_initial_packet({}, 0, QUIC_SUPPORTED_VERSIONS[0], ats_unique_malloc(0), 0);
    vn.negotiate(initial_packet.get());
    CHECK(vn.status() == QUICVersionNegotiationStatus::NEGOTIATED);

    // Validate version
    QUICTransportParametersInClientHello tp(QUIC_SUPPORTED_VERSIONS[0]);
    vn.validate(&tp);
    CHECK(vn.status() == QUICVersionNegotiationStatus::VALIDATED);
    CHECK(vn.negotiated_version() == QUIC_SUPPORTED_VERSIONS[0]);
  }

  SECTION("Negotiation case")
  {
    // Check initial state
    CHECK(vn.status() == QUICVersionNegotiationStatus::NOT_NEGOTIATED);

    // Negotiate version
    QUICPacketUPtr initial_packet =
      packet_factory.create_initial_packet({}, 0, QUIC_SUPPORTED_VERSIONS[0], ats_unique_malloc(0), 0);
    vn.negotiate(initial_packet.get());
    CHECK(vn.status() == QUICVersionNegotiationStatus::NEGOTIATED);

    // Validate version
    QUICTransportParametersInClientHello tp(0xbabababa);
    vn.validate(&tp);
    CHECK(vn.status() == QUICVersionNegotiationStatus::VALIDATED);
    CHECK(vn.negotiated_version() == QUIC_SUPPORTED_VERSIONS[0]);
  }

  SECTION("Downgrade case")
  {
    // Check initial state
    CHECK(vn.status() == QUICVersionNegotiationStatus::NOT_NEGOTIATED);

    // Negotiate version
    QUICPacketUPtr initial_packet = packet_factory.create_initial_packet({}, 0, 0xbabababa, ats_unique_malloc(0), 0);
    vn.negotiate(initial_packet.get());
    CHECK(vn.status() == QUICVersionNegotiationStatus::NOT_NEGOTIATED);

    // Validate version
    QUICTransportParametersInClientHello tp(QUIC_SUPPORTED_VERSIONS[0]);
    vn.validate(&tp);
    CHECK(vn.status() == QUICVersionNegotiationStatus::FAILED);
    CHECK(vn.negotiated_version() != QUIC_SUPPORTED_VERSIONS[0]);
  }
}

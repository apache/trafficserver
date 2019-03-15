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
#include "quic/QUICPacketProtectionKeyInfo.h"
#include "quic/Mock.h"

TEST_CASE("QUICVersionNegotiator - Server Side", "[quic]")
{
  MockQUICPacketProtectionKeyInfo pp_key_info;
  QUICPacketFactory packet_factory(pp_key_info);
  QUICVersionNegotiator vn;
  ats_unique_buf dummy_payload = ats_unique_malloc(128);
  size_t dummy_payload_len     = 128;

  SECTION("Normal case")
  {
    // Check initial state
    CHECK(vn.status() == QUICVersionNegotiationStatus::NOT_NEGOTIATED);

    // Negotiate version
    packet_factory.set_version(QUIC_SUPPORTED_VERSIONS[0]);
    QUICPacketUPtr initial_packet =
      packet_factory.create_initial_packet({}, {}, 0, std::move(dummy_payload), dummy_payload_len, true, false, true);
    vn.negotiate(*initial_packet);
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
    packet_factory.set_version(QUIC_SUPPORTED_VERSIONS[0]);
    QUICPacketUPtr initial_packet =
      packet_factory.create_initial_packet({}, {}, 0, std::move(dummy_payload), dummy_payload_len, true, false, true);
    vn.negotiate(*initial_packet);
    CHECK(vn.status() == QUICVersionNegotiationStatus::NEGOTIATED);

    // Validate version
    QUICTransportParametersInClientHello tp(QUIC_EXERCISE_VERSION);
    vn.validate(&tp);
    CHECK(vn.status() == QUICVersionNegotiationStatus::VALIDATED);
    CHECK(vn.negotiated_version() == QUIC_SUPPORTED_VERSIONS[0]);
  }

  SECTION("Downgrade case")
  {
    // Check initial state
    CHECK(vn.status() == QUICVersionNegotiationStatus::NOT_NEGOTIATED);

    // Negotiate version
    packet_factory.set_version(QUIC_EXERCISE_VERSION);
    QUICPacketUPtr initial_packet =
      packet_factory.create_initial_packet({}, {}, 0, std::move(dummy_payload), dummy_payload_len, true, false, true);
    vn.negotiate(*initial_packet);
    CHECK(vn.status() == QUICVersionNegotiationStatus::NOT_NEGOTIATED);

    // Validate version
    QUICTransportParametersInClientHello tp(QUIC_SUPPORTED_VERSIONS[0]);
    vn.validate(&tp);
    CHECK(vn.status() == QUICVersionNegotiationStatus::FAILED);
    CHECK(vn.negotiated_version() != QUIC_SUPPORTED_VERSIONS[0]);
  }
}

TEST_CASE("QUICVersionNegotiator - Client Side", "[quic]")
{
  MockQUICPacketProtectionKeyInfo pp_key_info;
  QUICPacketFactory packet_factory(pp_key_info);
  QUICVersionNegotiator vn;
  ats_unique_buf dummy_payload = ats_unique_malloc(128);
  size_t dummy_payload_len     = 128;

  SECTION("Normal case")
  {
    // Check initial state
    CHECK(vn.status() == QUICVersionNegotiationStatus::NOT_NEGOTIATED);

    // No Version Negotiation packet from server

    // Validate version
    QUICTransportParametersInEncryptedExtensions tp(QUIC_SUPPORTED_VERSIONS[0]);
    tp.add_version(QUIC_SUPPORTED_VERSIONS[0]);

    vn.validate(&tp);
    CHECK(vn.status() == QUICVersionNegotiationStatus::VALIDATED);
    CHECK(vn.negotiated_version() == QUIC_SUPPORTED_VERSIONS[0]);
  }

  SECTION("Negotiation case")
  {
    // Check initial state
    CHECK(vn.status() == QUICVersionNegotiationStatus::NOT_NEGOTIATED);

    // Negotiate version
    packet_factory.set_version(QUIC_EXERCISE_VERSION);
    QUICPacketUPtr initial_packet =
      packet_factory.create_initial_packet({}, {}, 0, std::move(dummy_payload), dummy_payload_len, true, false, true);

    // Server send VN packet based on Initial packet
    QUICPacketUPtr vn_packet =
      packet_factory.create_version_negotiation_packet(initial_packet->source_cid(), initial_packet->destination_cid());

    // Negotiate version
    vn.negotiate(*vn_packet);
    CHECK(vn.status() == QUICVersionNegotiationStatus::NEGOTIATED);
    CHECK(vn.negotiated_version() == QUIC_SUPPORTED_VERSIONS[0]);

    // Validate version
    QUICTransportParametersInEncryptedExtensions tp(QUIC_SUPPORTED_VERSIONS[0]);
    tp.add_version(QUIC_SUPPORTED_VERSIONS[0]);

    vn.validate(&tp);
    CHECK(vn.status() == QUICVersionNegotiationStatus::VALIDATED);
  }
}

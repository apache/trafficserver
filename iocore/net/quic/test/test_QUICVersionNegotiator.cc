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

TEST_CASE("QUICVersionNegotiator_Normal", "[quic]")
{
  QUICPacketFactory packet_factory;
  QUICVersionNegotiator vn;

  // Check initial state
  CHECK(vn.status() == QUICVersionNegotiationStatus::NOT_NEGOTIATED);

  // Negotiate version
  std::unique_ptr<QUICPacket> initial_packet =
    packet_factory.create_client_initial_packet({}, QUIC_SUPPORTED_VERSIONS[0], ats_unique_malloc(0), 0);
  vn.negotiate(initial_packet.get());
  CHECK(vn.status() == QUICVersionNegotiationStatus::NEGOTIATED);

  // Revalidate version
  QUICTransportParametersInClientHello tp(QUIC_SUPPORTED_VERSIONS[0], QUIC_SUPPORTED_VERSIONS[0]);
  vn.revalidate(&tp);
  CHECK(vn.status() == QUICVersionNegotiationStatus::REVALIDATED);
  CHECK(vn.negotiated_version() == QUIC_SUPPORTED_VERSIONS[0]);
}

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

#include "quic/QUICPacket.h"
#include "quic/Mock.h"

TEST_CASE("QUICPacketFactory_Create_VersionNegotiationPacket", "[quic]")
{
  QUICPacketFactory factory;
  MockQUICCrypto crypto;
  factory.set_crypto_module(&crypto);

  uint8_t client_initial_packet_header[] = {
    0x82,                                           // Type
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // Connection id
    0x00, 0x00, 0x00, 0x00,                         // Packet number
    0xaa, 0xbb, 0xcc, 0xdd,                         // Version
  };
  uint8_t client_initial_packet_payload[] = {
    0x00 // Payload
  };

  QUICPacketHeader *header = QUICPacketHeader::load(client_initial_packet_header, sizeof(client_initial_packet_header), 0);
  QUICPacket client_initial_packet(header, ats_unique_buf(client_initial_packet_payload, [](void *) {}),
                                   sizeof(client_initial_packet_payload), 0);

  QUICPacketUPtr packet = factory.create_version_negotiation_packet(&client_initial_packet, 0);
  CHECK(packet->type() == QUICPacketType::VERSION_NEGOTIATION);
  CHECK(packet->connection_id() == client_initial_packet.connection_id());
  CHECK(packet->packet_number() == client_initial_packet.packet_number());
  CHECK(memcmp(packet->payload(), "\xff\x00\x00\x07", 4) == 0);
}

TEST_CASE("QUICPacketFactory_Create_Handshake", "[quic]")
{
  QUICPacketFactory factory;
  MockQUICCrypto crypto;
  factory.set_crypto_module(&crypto);
  factory.set_version(0x11223344);

  uint8_t raw[]          = {0xaa, 0xbb, 0xcc, 0xdd};
  ats_unique_buf payload = ats_unique_malloc(sizeof(raw));
  memcpy(payload.get(), raw, sizeof(raw));

  QUICPacketUPtr packet = factory.create_handshake_packet(0x01020304, 0, std::move(payload), sizeof(raw), true);
  CHECK(packet->type() == QUICPacketType::HANDSHAKE);
  CHECK(packet->connection_id() == 0x01020304);
  CHECK(memcmp(packet->payload(), raw, sizeof(raw)) == 0);
  CHECK((packet->packet_number() & 0xFFFFFFFF80000000) == 0);
  CHECK(packet->version() == 0x11223344);
}

TEST_CASE("QUICPacketFactory_Create_StatelessResetPacket", "[quic]")
{
  QUICPacketFactory factory;
  MockQUICCrypto crypto;
  factory.set_crypto_module(&crypto);
  QUICStatelessResetToken token;
  token.generate(12345, 67890);
  uint8_t expected_output[] = {
    0x41,                                           // 0CK0001
    0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04, // Connection ID
    0xa1, 0x45, 0x7b, 0x7e, 0x8f, 0x85, 0x0b, 0x14, // Token
    0xd2, 0x43, 0xa1, 0xaf, 0x7c, 0xe2, 0x91, 0x50,
    // Random data
  };
  uint8_t output[1024];
  size_t out_len = 0;

  QUICPacketUPtr packet = factory.create_stateless_reset_packet(0x01020304, token);
  CHECK(packet->type() == QUICPacketType::STATELESS_RESET);
  CHECK(packet->connection_id() == 0x01020304);
}

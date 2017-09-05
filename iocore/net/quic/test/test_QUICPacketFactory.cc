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

TEST_CASE("QUICPacketFactory_Create_VersionNegotiationPacket", "[quic]")
{
  QUICPacketFactory factory;

  const uint8_t client_initial_packet_data[] = {
    0x82,                                           // Type
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // Connection id
    0x00, 0x00, 0x00, 0x00,                         // Packet number
    0xaa, 0xbb, 0xcc, 0xdd,                         // Version
    0x00                                            // Payload
  };

  IOBufferBlock *block = new_IOBufferBlock();
  block->alloc(iobuffer_size_to_index(sizeof(client_initial_packet_data)));
  memcpy(block->end(), client_initial_packet_data, sizeof(client_initial_packet_data));
  block->fill(sizeof(client_initial_packet_data));

  QUICPacket client_initial_packet(block, 0);

  std::unique_ptr<QUICPacket, QUICPacketDeleterFunc> packet = factory.create_version_negotiation_packet(&client_initial_packet, 0);
  CHECK(packet->type() == QUICPacketType::VERSION_NEGOTIATION);
  CHECK(packet->connection_id() == client_initial_packet.connection_id());
  CHECK(packet->packet_number() == client_initial_packet.packet_number());
  CHECK(memcmp(packet->payload(), "\xff\x00\x00\x05", 4) == 0);
}

TEST_CASE("QUICPacketFactory_Create_ServerCleartextPacket", "[quic]")
{
  QUICPacketFactory factory;
  factory.set_version(0x11223344);

  uint8_t raw[]          = {0xaa, 0xbb, 0xcc, 0xdd};
  ats_unique_buf payload = ats_unique_malloc(sizeof(raw));
  memcpy(payload.get(), raw, sizeof(raw));

  std::unique_ptr<QUICPacket, QUICPacketDeleterFunc> packet =
    factory.create_server_cleartext_packet(0x01020304, 0, std::move(payload), sizeof(raw), true);
  CHECK(packet->type() == QUICPacketType::SERVER_CLEARTEXT);
  CHECK(packet->connection_id() == 0x01020304);
  CHECK(memcmp(packet->payload(), raw, sizeof(raw)) == 0);
  CHECK(packet->packet_number() == 0);
  CHECK(packet->version() == 0x11223344);
}

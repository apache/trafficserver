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
  MockQUICHandshakeProtocol hs_protocol;
  factory.set_hs_protocol(&hs_protocol);

  uint8_t initial_packet_header[] = {
    0xFF,                                           // Type
    0xaa, 0xbb, 0xcc, 0xdd,                         // Version
    0x55,                                           // DCIL/SCIL
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // Destination Connection id
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, // Source Connection id
    0x01,                                           // Payload Length
    0x00, 0x00, 0x00, 0x00,                         // Packet number
  };
  uint8_t initial_packet_payload[] = {
    0x00 // Payload
  };

  QUICPacketHeaderUPtr header =
    QUICPacketHeader::load({}, {initial_packet_header, [](void *) {}}, sizeof(initial_packet_header), 0, 8);

  QUICPacket initial_packet(std::move(header), ats_unique_buf(initial_packet_payload, [](void *) {}),
                            sizeof(initial_packet_payload), 0);

  QUICPacketUPtr vn_packet = factory.create_version_negotiation_packet(&initial_packet);
  CHECK(vn_packet->type() == QUICPacketType::VERSION_NEGOTIATION);

  uint8_t dst_cid[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
  uint8_t src_cid[] = {0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18};
  CHECK((vn_packet->source_cid() == QUICConnectionId(dst_cid, sizeof(dst_cid))));
  CHECK((vn_packet->destination_cid() == QUICConnectionId(src_cid, sizeof(src_cid))));
  CHECK(vn_packet->version() == 0x00);

  QUICVersion supported_version = QUICTypeUtil::read_QUICVersion(vn_packet->payload());
  CHECK(supported_version == QUIC_SUPPORTED_VERSIONS[0]);

  uint8_t expected[] = {
    0xa7,                                           // Long header, Type: NONE
    0x00, 0x00, 0x00, 0x00,                         // Version
    0x55,                                           // DCIL/SCIL
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, // Destination Connection ID
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // Source Connection ID
    0xff, 0x00, 0x00, 0x0c,                         // Supported Version
  };
  uint8_t buf[1024] = {0};
  size_t buf_len;
  vn_packet->store(buf, &buf_len);
  CHECK(memcmp(buf, expected, buf_len) == 0);
}

TEST_CASE("QUICPacketFactory_Create_Retry", "[quic]")
{
  QUICPacketFactory factory;
  MockQUICHandshakeProtocol hs_protocol;
  factory.set_hs_protocol(&hs_protocol);
  factory.set_version(0x11223344);

  uint8_t raw[]          = {0xaa, 0xbb, 0xcc, 0xdd};
  ats_unique_buf payload = ats_unique_malloc(sizeof(raw));
  memcpy(payload.get(), raw, sizeof(raw));

  QUICPacketUPtr packet = factory.create_retry_packet(QUICConnectionId(reinterpret_cast<const uint8_t *>("\x01\x02\x03\x04"), 4),
                                                      QUICConnectionId(reinterpret_cast<const uint8_t *>("\x11\x12\x13\x14"), 4),
                                                      1234, std::move(payload), sizeof(raw), false);
  CHECK(packet->type() == QUICPacketType::RETRY);
  CHECK((packet->destination_cid() == QUICConnectionId(reinterpret_cast<const uint8_t *>("\x01\x02\x03\x04"), 4)));
  CHECK(memcmp(packet->payload(), raw, sizeof(raw)) == 0);
  CHECK(packet->packet_number() == 1234);
  CHECK(packet->version() == 0x11223344);
}

TEST_CASE("QUICPacketFactory_Create_Handshake", "[quic]")
{
  QUICPacketFactory factory;
  MockQUICHandshakeProtocol hs_protocol;
  factory.set_hs_protocol(&hs_protocol);
  factory.set_version(0x11223344);

  uint8_t raw[]          = {0xaa, 0xbb, 0xcc, 0xdd};
  ats_unique_buf payload = ats_unique_malloc(sizeof(raw));
  memcpy(payload.get(), raw, sizeof(raw));

  QUICPacketUPtr packet = factory.create_handshake_packet(
    QUICConnectionId(reinterpret_cast<const uint8_t *>("\x01\x02\x03\x04"), 4),
    QUICConnectionId(reinterpret_cast<const uint8_t *>("\x11\x12\x13\x14"), 4), 0, std::move(payload), sizeof(raw), true);
  CHECK(packet->type() == QUICPacketType::HANDSHAKE);
  CHECK((packet->destination_cid() == QUICConnectionId(reinterpret_cast<const uint8_t *>("\x01\x02\x03\x04"), 4)));
  CHECK(memcmp(packet->payload(), raw, sizeof(raw)) == 0);
  CHECK(packet->packet_number() <= 0xFFFFFBFF);
  CHECK(packet->version() == 0x11223344);
}

TEST_CASE("QUICPacketFactory_Create_StatelessResetPacket", "[quic]")
{
  QUICPacketFactory factory;
  MockQUICHandshakeProtocol hs_protocol;
  factory.set_hs_protocol(&hs_protocol);
  QUICStatelessResetToken token;
  token.generate({reinterpret_cast<const uint8_t *>("\x30\x39"), 2}, 67890);
  uint8_t expected_output[] = {
    0x41,                                           // 0CK0001
    0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04, // Connection ID
    0xa1, 0x45, 0x7b, 0x7e, 0x8f, 0x85, 0x0b, 0x14, // Token
    0xd2, 0x43, 0xa1, 0xaf, 0x7c, 0xe2, 0x91, 0x50,
    // Random data
  };
  uint8_t output[1024];
  size_t out_len = 0;

  QUICPacketUPtr packet =
    factory.create_stateless_reset_packet(QUICConnectionId(reinterpret_cast<const uint8_t *>("\x01\x02\x03\x04"), 4), token);
  CHECK(packet->type() == QUICPacketType::STATELESS_RESET);
  CHECK((packet->destination_cid() == QUICConnectionId(reinterpret_cast<const uint8_t *>("\x01\x02\x03\x04"), 4)));
}

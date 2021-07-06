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
#include "quic/QUICPacketFactory.h"
#include "quic/Mock.h"

TEST_CASE("QUICPacketFactory_Create_VersionNegotiationPacket", "[quic]")
{
  MockQUICPacketProtectionKeyInfo pp_key_info;
  QUICPacketFactory factory(pp_key_info);

  const uint8_t raw_dcid[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
  const uint8_t raw_scid[] = {0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18};
  QUICConnectionId dcid(raw_dcid, 8);
  QUICConnectionId scid(raw_scid, 8);

  QUICPacketUPtr packet = factory.create_version_negotiation_packet(scid, dcid, QUIC_EXERCISE_VERSION1);
  REQUIRE(packet != nullptr);

  QUICVersionNegotiationPacket &vn_packet = static_cast<QUICVersionNegotiationPacket &>(*packet);
  CHECK(vn_packet.type() == QUICPacketType::VERSION_NEGOTIATION);
  CHECK(vn_packet.destination_cid() == scid);
  CHECK(vn_packet.source_cid() == dcid);
  CHECK(vn_packet.version() == 0x00);

  QUICVersion supported_version = QUICTypeUtil::read_QUICVersion(reinterpret_cast<uint8_t *>(vn_packet.payload_block()->start()));
  CHECK(supported_version == QUIC_SUPPORTED_VERSIONS[0]);

  uint8_t expected[] = {
    0xa7,                                           // Long header, Type: NONE
    0x00, 0x00, 0x00, 0x00,                         // Version
    0x08,                                           // DCID Len
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, // Destination Connection ID
    0x08,                                           // SCID Len
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // Source Connection ID
    0xff, 0x00, 0x00, 0x1d,                         // Supported Version
    0xff, 0x00, 0x00, 0x1b,                         // Supported Version
    0x5a, 0x6a, 0x7a, 0x8a,                         // Exercise Version
  };
  uint8_t buf[1024] = {0};
  size_t buf_len;
  vn_packet.store(buf, &buf_len);
  CHECK((buf[0] & 0x80) == 0x80); // Lower 7 bits of the first byte is random
  CHECK(memcmp(buf + 1, expected + 1, buf_len - 1) == 0);
}

TEST_CASE("QUICPacketFactory_Create_Retry", "[quic]")
{
  MockQUICPacketProtectionKeyInfo pp_key_info;
  QUICPacketFactory factory(pp_key_info);
  factory.set_version(0x11223344);

  uint8_t raw[] = {0xaa, 0xbb, 0xcc, 0xdd};
  QUICRetryToken token(raw, 4);

  QUICPacketUPtr packet = factory.create_retry_packet(
    QUIC_SUPPORTED_VERSIONS[0], QUICConnectionId(reinterpret_cast<const uint8_t *>("\x01\x02\x03\x04"), 4),
    QUICConnectionId(reinterpret_cast<const uint8_t *>("\x11\x12\x13\x14"), 4), token);

  REQUIRE(packet != nullptr);

  QUICRetryPacket &retry_packet = static_cast<QUICRetryPacket &>(*packet);
  CHECK(retry_packet.type() == QUICPacketType::RETRY);
  CHECK(retry_packet.destination_cid() == QUICConnectionId(reinterpret_cast<const uint8_t *>("\x01\x02\x03\x04"), 4));
  CHECK(retry_packet.version() == QUIC_SUPPORTED_VERSIONS[0]);
  CHECK(retry_packet.token() == token);
}

TEST_CASE("QUICPacketFactory_Create_Handshake", "[quic]")
{
  MockQUICPacketProtectionKeyInfo pp_key_info;
  pp_key_info.set_encryption_key_available(QUICKeyPhase::HANDSHAKE);
  QUICPacketFactory factory(pp_key_info);
  factory.set_version(0x11223344);

  uint8_t raw[]              = {0xaa, 0xbb, 0xcc, 0xdd};
  Ptr<IOBufferBlock> payload = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  payload->alloc(iobuffer_size_to_index(sizeof(raw), BUFFER_SIZE_INDEX_32K));
  payload->fill(sizeof(raw));
  memcpy(payload->start(), raw, sizeof(raw));

  uint8_t packet_buf[QUICPacket::MAX_INSTANCE_SIZE];
  QUICPacketUPtr packet = factory.create_handshake_packet(
    packet_buf, QUICConnectionId(reinterpret_cast<const uint8_t *>("\x01\x02\x03\x04"), 4),
    QUICConnectionId(reinterpret_cast<const uint8_t *>("\x11\x12\x13\x14"), 4), 0, payload, sizeof(raw), true, false, true);
  REQUIRE(packet != nullptr);

  QUICHandshakePacket &handshake_packet = reinterpret_cast<QUICHandshakePacket &>(*packet);
  CHECK(handshake_packet.type() == QUICPacketType::HANDSHAKE);
  CHECK(handshake_packet.destination_cid() == QUICConnectionId(reinterpret_cast<const uint8_t *>("\x01\x02\x03\x04"), 4));
  CHECK(memcmp(handshake_packet.payload_block()->start(), raw, sizeof(raw)) != 0);
  CHECK(handshake_packet.packet_number() <= 0xFFFFFBFF);
  CHECK(handshake_packet.version() == 0x11223344);
}

TEST_CASE("QUICPacketFactory_Create_StatelessResetPacket", "[quic]")
{
  MockQUICPacketProtectionKeyInfo pp_key_info;
  QUICPacketFactory factory(pp_key_info);
  QUICStatelessResetToken token({reinterpret_cast<const uint8_t *>("\x30\x39"), 2}, 67890);

  QUICPacketUPtr packet = factory.create_stateless_reset_packet(token, 1200);

  REQUIRE(packet != nullptr);
  CHECK(packet->type() == QUICPacketType::STATELESS_RESET);

  QUICStatelessResetPacket *sr_packet = dynamic_cast<QUICStatelessResetPacket *>(packet.get());
  CHECK(sr_packet->token() == token);
}

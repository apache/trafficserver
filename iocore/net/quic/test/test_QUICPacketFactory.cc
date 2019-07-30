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
  MockQUICPacketProtectionKeyInfo pp_key_info;
  QUICPacketFactory factory(pp_key_info);

  const uint8_t raw_dcid[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
  const uint8_t raw_scid[] = {0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18};
  QUICConnectionId dcid(raw_dcid, 8);
  QUICConnectionId scid(raw_scid, 8);

  QUICPacketUPtr vn_packet = factory.create_version_negotiation_packet(scid, dcid);

  REQUIRE(vn_packet != nullptr);
  CHECK(vn_packet->type() == QUICPacketType::VERSION_NEGOTIATION);
  CHECK(vn_packet->destination_cid() == scid);
  CHECK(vn_packet->source_cid() == dcid);
  CHECK(vn_packet->version() == 0x00);

  QUICVersion supported_version = QUICTypeUtil::read_QUICVersion(vn_packet->payload());
  CHECK(supported_version == QUIC_SUPPORTED_VERSIONS[0]);

  uint8_t expected[] = {
    0xa7,                                           // Long header, Type: NONE
    0x00, 0x00, 0x00, 0x00,                         // Version
    0x55,                                           // DCIL/SCIL
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, // Destination Connection ID
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // Source Connection ID
    0xff, 0x00, 0x00, 0x14,                         // Supported Version
    0x1a, 0x2a, 0x3a, 0x4a,                         // Excercise Version
  };
  uint8_t buf[1024] = {0};
  size_t buf_len;
  vn_packet->store(buf, &buf_len);
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

  QUICPacketUPtr packet =
    factory.create_retry_packet(QUICConnectionId(reinterpret_cast<const uint8_t *>("\x01\x02\x03\x04"), 4),
                                QUICConnectionId(reinterpret_cast<const uint8_t *>("\x11\x12\x13\x14"), 4),
                                QUICConnectionId(reinterpret_cast<const uint8_t *>("\x04\x03\x02\x01"), 4), token);

  REQUIRE(packet != nullptr);
  CHECK(packet->type() == QUICPacketType::RETRY);
  CHECK((packet->destination_cid() == QUICConnectionId(reinterpret_cast<const uint8_t *>("\x01\x02\x03\x04"), 4)));
  CHECK(memcmp(packet->payload(), raw, sizeof(raw)) == 0);
  CHECK(packet->packet_number() == 0);
  CHECK(packet->version() == QUIC_SUPPORTED_VERSIONS[0]);
}

TEST_CASE("QUICPacketFactory_Create_Handshake", "[quic]")
{
  MockQUICPacketProtectionKeyInfo pp_key_info;
  pp_key_info.set_encryption_key_available(QUICKeyPhase::HANDSHAKE);
  QUICPacketFactory factory(pp_key_info);
  factory.set_version(0x11223344);

  uint8_t raw[]          = {0xaa, 0xbb, 0xcc, 0xdd};
  ats_unique_buf payload = ats_unique_malloc(sizeof(raw));
  memcpy(payload.get(), raw, sizeof(raw));

  QUICPacketUPtr packet =
    factory.create_handshake_packet(QUICConnectionId(reinterpret_cast<const uint8_t *>("\x01\x02\x03\x04"), 4),
                                    QUICConnectionId(reinterpret_cast<const uint8_t *>("\x11\x12\x13\x14"), 4), 0,
                                    std::move(payload), sizeof(raw), true, false, true);
  REQUIRE(packet != nullptr);
  CHECK(packet->type() == QUICPacketType::HANDSHAKE);
  CHECK((packet->destination_cid() == QUICConnectionId(reinterpret_cast<const uint8_t *>("\x01\x02\x03\x04"), 4)));
  CHECK(memcmp(packet->payload(), raw, sizeof(raw)) != 0);
  CHECK(packet->packet_number() <= 0xFFFFFBFF);
  CHECK(packet->version() == 0x11223344);
}

TEST_CASE("QUICPacketFactory_Create_StatelessResetPacket", "[quic]")
{
  MockQUICPacketProtectionKeyInfo pp_key_info;
  QUICPacketFactory factory(pp_key_info);
  QUICStatelessResetToken token({reinterpret_cast<const uint8_t *>("\x30\x39"), 2}, 67890);

  QUICPacketUPtr packet =
    factory.create_stateless_reset_packet(QUICConnectionId(reinterpret_cast<const uint8_t *>("\x01\x02\x03\x04"), 4), token);

  REQUIRE(packet != nullptr);
  CHECK(packet->type() == QUICPacketType::STATELESS_RESET);
  CHECK((packet->destination_cid() == QUICConnectionId(reinterpret_cast<const uint8_t *>("\x01\x02\x03\x04"), 4)));
}

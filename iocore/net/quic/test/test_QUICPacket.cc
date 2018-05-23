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

TEST_CASE("QUICPacketHeader - Long", "[quic]")
{
  SECTION("Long Header (load) Version Negotiation Packet")
  {
    const uint8_t input[] = {
      0x80,                                           // Long header, Type: NONE
      0x00, 0x00, 0x00, 0x00,                         // Version
      0x55,                                           // DCIL/SCIL
      0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // Destination Connection ID
      0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, // Source Connection ID
      0x00, 0x00, 0x00, 0x08,                         // Supported Version 1
      0x00, 0x00, 0x00, 0x09,                         // Supported Version 1
    };

    QUICPacketHeaderUPtr header = QUICPacketHeader::load({}, {const_cast<uint8_t *>(input), [](void *p) {}}, sizeof(input), 0);
    CHECK(header->size() == 22);
    CHECK(header->packet_size() == 30);
    CHECK(header->type() == QUICPacketType::VERSION_NEGOTIATION);
    CHECK(
      (header->destination_cid() == QUICConnectionId(reinterpret_cast<const uint8_t *>("\x01\x02\x03\x04\x05\x06\x07\x08"), 8)));
    CHECK((header->source_cid() == QUICConnectionId(reinterpret_cast<const uint8_t *>("\x11\x12\x13\x14\x15\x16\x17\x18"), 8)));
    CHECK(header->has_version() == true);
    CHECK(header->version() == 0x00000000);
    CHECK(header->has_key_phase() == false);
  }

  SECTION("Long Header (load)")
  {
    const uint8_t input[] = {
      0xFF,                                           // Long header, Type: INITIAL
      0x11, 0x22, 0x33, 0x44,                         // Version
      0x55,                                           // DCIL/SCIL
      0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // Destination Connection ID
      0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, // Source Connection ID
      0x02,                                           // Payload length
      0xC1, 0x23, 0x45, 0x67,                         // Packet number
      0xff, 0xff,                                     // Payload (dummy)
    };

    QUICPacketHeaderUPtr header = QUICPacketHeader::load({}, {const_cast<uint8_t *>(input), [](void *p) {}}, sizeof(input), 0);
    CHECK(header->size() == 27);
    CHECK(header->packet_size() == 29);
    CHECK(header->type() == QUICPacketType::INITIAL);
    CHECK(
      (header->destination_cid() == QUICConnectionId(reinterpret_cast<const uint8_t *>("\x01\x02\x03\x04\x05\x06\x07\x08"), 8)));
    CHECK((header->source_cid() == QUICConnectionId(reinterpret_cast<const uint8_t *>("\x11\x12\x13\x14\x15\x16\x17\x18"), 8)));
    CHECK(header->packet_number() == 0x01234567);
    CHECK(header->has_version() == true);
    CHECK(header->version() == 0x11223344);
    CHECK(header->has_key_phase() == false);
  }

  SECTION("Long Header (store)")
  {
    uint8_t buf[32] = {0};
    size_t len      = 0;

    const uint8_t expected[] = {
      0xFF,                                           // Long header, Type: INITIAL
      0x11, 0x22, 0x33, 0x44,                         // Version
      0x55,                                           // DCIL/SCIL
      0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // Destination Connection ID
      0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, // Source Connection ID
      0x15,                                           // Payload length (Not 0x05 because it will have 16 bytes of AEAD tag)
      0xC1, 0x23, 0x45, 0x67,                         // Packet number
      0x11, 0x22, 0x33, 0x44, 0x55,                   // Payload (dummy)
    };
    ats_unique_buf payload = ats_unique_malloc(5);
    memcpy(payload.get(), expected + 17, 5);

    QUICPacketHeaderUPtr header = QUICPacketHeader::build(
      QUICPacketType::INITIAL, {reinterpret_cast<const uint8_t *>("\x01\x02\x03\x04\x05\x06\x07\x08"), 8},
      {reinterpret_cast<const uint8_t *>("\x11\x12\x13\x14\x15\x16\x17\x18"), 8}, 0x01234567, 0, 0x11223344, std::move(payload), 5);

    CHECK(header->size() == 27);
    CHECK(header->has_key_phase() == false);
    CHECK(header->packet_size() == 32);
    CHECK(header->type() == QUICPacketType::INITIAL);
    CHECK(
      (header->destination_cid() == QUICConnectionId(reinterpret_cast<const uint8_t *>("\x01\x02\x03\x04\x05\x06\x07\x08"), 8)));
    CHECK((header->source_cid() == QUICConnectionId(reinterpret_cast<const uint8_t *>("\x11\x12\x13\x14\x15\x16\x17\x18"), 8)));
    CHECK(header->packet_number() == 0x01234567);
    CHECK(header->has_version() == true);
    CHECK(header->version() == 0x11223344);

    header->store(buf, &len);
    CHECK(len == 27);
    CHECK(memcmp(buf, expected, len) == 0);
  }
}

TEST_CASE("QUICPacketHeader - Short", "[quic]")
{
  SECTION("Short Header (load)")
  {
    const uint8_t input[] = {
      0x30,                                           // Short header with (K=0)
      0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // Destination Connection ID
      0xC1, 0x23, 0x45, 0x67,                         // Packet number
      0xff, 0xff,                                     // Payload (dummy)
    };

    QUICPacketHeaderUPtr header = QUICPacketHeader::load({}, {const_cast<uint8_t *>(input), [](void *p) {}}, sizeof(input), 0, 8);
    CHECK(header->size() == 13);
    CHECK(header->packet_size() == 15);
    CHECK(header->has_key_phase() == true);
    CHECK(header->key_phase() == QUICKeyPhase::PHASE_0);
    CHECK(
      (header->destination_cid() == QUICConnectionId(reinterpret_cast<const uint8_t *>("\x01\x02\x03\x04\x05\x06\x07\x08"), 8)));
    CHECK(header->packet_number() == 0x01234567);
    CHECK(header->has_version() == false);
  }

  SECTION("Short Header (store)")
  {
    uint8_t buf[32] = {0};
    size_t len      = 0;

    const uint8_t expected[] = {
      0x30,                                           // Short header with (K=0)
      0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // Destination Connection ID
      0xC1, 0x23, 0x45, 0x67,                         // Packet number
      0x11, 0x22, 0x33, 0x44, 0x55,                   // Protected Payload
    };

    ats_unique_buf payload = ats_unique_malloc(5);
    memcpy(payload.get(), expected + 13, 5);
    QUICPacketHeaderUPtr header = QUICPacketHeader::build(
      QUICPacketType::PROTECTED, QUICKeyPhase::PHASE_0, {reinterpret_cast<const uint8_t *>("\x01\x02\x03\x04\x05\x06\x07\x08"), 8},
      0x01234567, 0, std::move(payload), 32);
    CHECK(header->size() == 13);
    CHECK(header->packet_size() == 0);
    CHECK(header->has_key_phase() == true);
    CHECK(header->key_phase() == QUICKeyPhase::PHASE_0);
    CHECK(header->type() == QUICPacketType::PROTECTED);
    CHECK(
      (header->destination_cid() == QUICConnectionId(reinterpret_cast<const uint8_t *>("\x01\x02\x03\x04\x05\x06\x07\x08"), 8)));
    CHECK(header->packet_number() == 0x01234567);
    CHECK(header->has_version() == false);

    header->store(buf, &len);
    CHECK(len == 13);
    CHECK(memcmp(buf, expected, len) == 0);
  }
}

TEST_CASE("Encoded Packet Number Length", "[quic]")
{
  QUICPacketNumber base = 0x6afa2f;

  CHECK(QUICPacket::calc_packet_number_len(0x6b4264, base) == 2);
  CHECK(QUICPacket::calc_packet_number_len(0x6bc107, base) == 4);
}

TEST_CASE("Encoding Packet Number", "[quic]")
{
  QUICPacketNumber dst = 0;
  QUICPacketNumber src = 0xaa831f94;

  QUICPacket::encode_packet_number(dst, src, 2);
  CHECK(dst == 0x1f94);
}

TEST_CASE("Decoding Packet Number 1", "[quic]")
{
  QUICPacketNumber dst  = 0;
  QUICPacketNumber src  = 0x1f94;
  size_t len            = 2;
  QUICPacketNumber base = 0xaa82f30e;

  QUICPacket::decode_packet_number(dst, src, len, base);
  CHECK(dst == 0xaa831f94);
}

TEST_CASE("Decoding Packet Number 2", "[quic]")
{
  QUICPacketNumber dst  = 0;
  QUICPacketNumber src  = 0xf1;
  size_t len            = 1;
  QUICPacketNumber base = 0x18bf54f0;

  QUICPacket::decode_packet_number(dst, src, len, base);
  CHECK(dst == 0x18bf54f1);
}

TEST_CASE("Decoding Packet Number 3", "[quic]")
{
  QUICPacketNumber dst  = 0;
  QUICPacketNumber src  = 0x5694;
  size_t len            = 2;
  QUICPacketNumber base = 0x44D35695;
  QUICPacket::decode_packet_number(dst, src, len, base);
  CHECK(dst == 0x44D35694);
}

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

TEST_CASE("QUICPacketHeader", "[quic]")
{
  SECTION("Long Header (load) Version Negotiation Packet")
  {
    const uint8_t input[] = {
      0x80,                                           // Long header, Type: NONE
      0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // Connection ID
      0x00, 0x00, 0x00, 0x00,                         // Version
      0x00, 0x00, 0x00, 0x08,                         // Supported Version 1
      0x00, 0x00, 0x00, 0x09,                         // Supported Version 1
    };

    QUICPacketHeader *header = QUICPacketHeader::load(input, sizeof(input), 0);
    CHECK(header->size() == 13);
    CHECK(header->packet_size() == 21);
    CHECK(header->type() == QUICPacketType::VERSION_NEGOTIATION);
    CHECK(header->has_connection_id() == true);
    CHECK(header->connection_id() == 0x0102030405060708);
    CHECK(header->has_version() == true);
    CHECK(header->version() == 0x00000000);
    CHECK(header->has_key_phase() == false);
  }

  SECTION("Long Header (load)")
  {
    const uint8_t input[] = {
      0xFF,                                           // Long header, Type: INITIAL
      0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // Connection ID
      0x11, 0x22, 0x33, 0x44,                         // Version
      0x12, 0x34, 0x56, 0x78,                         // Packet number
      0xff, 0xff,                                     // Payload (dummy)
    };

    QUICPacketHeader *header = QUICPacketHeader::load(input, sizeof(input), 0);
    CHECK(header->size() == 17);
    CHECK(header->packet_size() == 19);
    CHECK(header->type() == QUICPacketType::INITIAL);
    CHECK(header->has_connection_id() == true);
    CHECK(header->connection_id() == 0x0102030405060708);
    CHECK(header->packet_number() == 0x12345678);
    CHECK(header->has_version() == true);
    CHECK(header->version() == 0x11223344);
    CHECK(header->has_key_phase() == false);
  }

  SECTION("Long Header (build)")
  {
    const uint8_t expected[] = {0x11, 0x22, 0x33, 0x44, 0x55};
    ats_unique_buf payload   = ats_unique_malloc(sizeof(expected));
    memcpy(payload.get(), expected, sizeof(expected));

    QUICPacketHeader *header =
      QUICPacketHeader::build(QUICPacketType::INITIAL, 0x0102030405060708, 0x12345678, 0, 0xa0a0a0a0, std::move(payload), 32);

    CHECK(header->size() == 17);
    CHECK(header->has_key_phase() == false);
    CHECK(header->packet_size() == 0);
    CHECK(header->type() == QUICPacketType::INITIAL);
    CHECK(header->has_connection_id() == true);
    CHECK(header->connection_id() == 0x0102030405060708);
    CHECK(header->packet_number() == 0x12345678);
    CHECK(header->has_version() == true);
    CHECK(header->version() == 0xa0a0a0a0);
  }

  SECTION("Short Header (load)")
  {
    const uint8_t input[] = {
      0x43,                                           // Short header, with Connection ID, KeyPhse 0, Type
      0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // Connection ID
      0x12, 0x34, 0x56, 0x78,                         // Packet number
      0xff, 0xff,                                     // Payload (dummy)
    };

    QUICPacketHeader *header = QUICPacketHeader::load(input, sizeof(input), 0);
    CHECK(header->size() == 13);
    CHECK(header->packet_size() == 15);
    CHECK(header->has_key_phase() == true);
    CHECK(header->key_phase() == QUICKeyPhase::PHASE_0);
    CHECK(header->has_connection_id() == true);
    CHECK(header->connection_id() == 0x0102030405060708);
    CHECK(header->packet_number() == 0x12345678);
    CHECK(header->has_version() == false);
  }

  SECTION("Short Header (build)")
  {
    const uint8_t expected[] = {0x11, 0x22, 0x33, 0x44, 0x55};
    ats_unique_buf payload   = ats_unique_malloc(sizeof(expected));
    memcpy(payload.get(), expected, sizeof(expected));

    QUICPacketHeader *header = QUICPacketHeader::build(QUICPacketType::PROTECTED, QUICKeyPhase::PHASE_0, 0x0102030405060708,
                                                       0x12345678, 0, std::move(payload), 32);

    CHECK(header->size() == 13);
    CHECK(header->packet_size() == 0);
    CHECK(header->has_key_phase() == true);
    CHECK(header->key_phase() == QUICKeyPhase::PHASE_0);
    CHECK(header->type() == QUICPacketType::PROTECTED);
    CHECK(header->has_connection_id() == true);
    CHECK(header->connection_id() == 0x0102030405060708);
    CHECK(header->packet_number() == 0x12345678);
    CHECK(header->has_version() == false);
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

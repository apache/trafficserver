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
  SECTION("Long Header")
  {
    const uint8_t input[] = {
      0x81,                                           // Long header, Type
      0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // Connection ID
      0x12, 0x34, 0x56, 0x78,                         // Packet number
      0x11, 0x22, 0x33, 0x44,                         // Version
      0xff, 0xff,                                     // Payload (dummy)
    };

    QUICPacketHeader *header = QUICPacketHeader::load(input, sizeof(input), 0);
    CHECK(header->length() == 17);
    CHECK(header->packet_size() == 19);
    CHECK(header->type() == QUICPacketType::VERSION_NEGOTIATION);
    CHECK(header->connection_id() == 0x0102030405060708);
    CHECK(header->packet_number() == 0x12345678);
    CHECK(header->version() == 0x11223344);
  }

  SECTION("Short Header")
  {
    const uint8_t input[] = {
      0x43,                                           // Short header, with Connection ID, KeyPhse 0, Type
      0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // Connection ID
      0x12, 0x34, 0x56, 0x78,                         // Packet number
      0xff, 0xff,                                     // Payload (dummy)
    };

    QUICPacketHeader *header = QUICPacketHeader::load(input, sizeof(input), 0);
    CHECK(header->length() == 13);
    CHECK(header->packet_size() == 15);
    CHECK(header->connection_id() == 0x0102030405060708);
    CHECK(header->packet_number() == 0x12345678);
  }
}

TEST_CASE("Loading Unknown Packet", "[quic]")
{
  const uint8_t buf[]      = {0xff};
  QUICPacketHeader *header = QUICPacketHeader::load(buf, sizeof(buf), 0);

  CHECK(header->type() == QUICPacketType::UNINITIALIZED);
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

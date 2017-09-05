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

TEST_CASE("Loading Long Header Packet", "[quic]")
{
  uint8_t raw[]          = {0x01, 0x02, 0x03, 0x04};
  ats_unique_buf payload = ats_unique_malloc(sizeof(raw));
  memcpy(payload.get(), raw, sizeof(raw));

  // Cleartext packet with a long header
  QUICPacket packet1(QUICPacketType::CLIENT_CLEARTEXT, 0xffddbb9977553311ULL, 0xffcc9966, 0x00112233, std::move(payload),
                     sizeof(raw), true);

  uint8_t buf[65536];
  size_t len;
  packet1.store(buf, &len);

  IOBufferBlock *block = new_IOBufferBlock();
  block->alloc(iobuffer_size_to_index(len));
  memcpy(block->end(), buf, len);
  block->fill(len);

  const QUICPacket packet2(block);

  CHECK(packet2.type() == QUICPacketType::CLIENT_CLEARTEXT);
  CHECK(packet2.connection_id() == 0xffddbb9977553311ULL);
  CHECK(packet2.packet_number() == 0xffcc9966);
  CHECK(packet2.version() == 0x00112233);
  CHECK(packet2.size() == 29);
  CHECK(packet2.payload_size() == sizeof(raw));
  CHECK(memcmp(packet2.payload(), raw, sizeof(raw)) == 0);
}

TEST_CASE("Loading Short Header Packet", "[quic]")
{
  uint8_t raw[]          = {0x01, 0x02, 0x03, 0x04};
  ats_unique_buf payload = ats_unique_malloc(sizeof(raw));
  memcpy(payload.get(), raw, sizeof(raw));

  uint8_t protected_raw[]          = {0x04, 0x03, 0x02, 0x01, 0x00};
  ats_unique_buf protected_payload = ats_unique_malloc(sizeof(protected_raw));
  memcpy(protected_payload.get(), protected_raw, sizeof(protected_raw));

  // Cleartext packet with a long header
  QUICPacket packet1(QUICPacketType::ONE_RTT_PROTECTED_KEY_PHASE_0, 0xffcc9966, std::move(payload), sizeof(raw), true);
  packet1.set_protected_payload(std::move(protected_payload), sizeof(protected_raw));

  uint8_t buf[65536];
  size_t len;
  packet1.store(buf, &len);

  IOBufferBlock *block = new_IOBufferBlock();
  block->alloc(iobuffer_size_to_index(len));
  memcpy(block->end(), buf, len);
  block->fill(len);

  const QUICPacket packet2(block);

  CHECK(packet2.type() == QUICPacketType::ONE_RTT_PROTECTED_KEY_PHASE_0);
  CHECK(packet2.packet_number() == 0xffcc9966);
  CHECK(packet2.size() == 10);
  CHECK(packet2.payload_size() == sizeof(protected_raw));
  CHECK(memcmp(packet2.payload(), protected_raw, sizeof(protected_raw)) == 0);
}

TEST_CASE("Loading Unknown Packet", "[quic]")
{
  const uint8_t buf[]      = {0xff};
  QUICPacketHeader *header = QUICPacketHeader::load(buf, sizeof(buf));

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

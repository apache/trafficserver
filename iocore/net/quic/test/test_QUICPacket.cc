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
      0xc0,                                           // Long header, Type: NONE
      0x00, 0x00, 0x00, 0x00,                         // Version
      0x55,                                           // DCIL/SCIL
      0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // Destination Connection ID
      0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, // Source Connection ID
      0x00, 0x00, 0x00, 0x08,                         // Supported Version 1
      0x00, 0x00, 0x00, 0x09,                         // Supported Version 1
    };
    ats_unique_buf uinput = ats_unique_malloc(sizeof(input));
    memcpy(uinput.get(), input, sizeof(input));

    QUICPacketHeaderUPtr header = QUICPacketHeader::load({}, std::move(uinput), sizeof(input), 0);
    CHECK(header->size() == 22);
    CHECK(header->packet_size() == 30);
    CHECK(header->type() == QUICPacketType::VERSION_NEGOTIATION);
    CHECK(
      (header->destination_cid() == QUICConnectionId(reinterpret_cast<const uint8_t *>("\x01\x02\x03\x04\x05\x06\x07\x08"), 8)));
    CHECK((header->source_cid() == QUICConnectionId(reinterpret_cast<const uint8_t *>("\x11\x12\x13\x14\x15\x16\x17\x18"), 8)));
    CHECK(header->has_version() == true);
    CHECK(header->version() == 0x00000000);
  }

  SECTION("Long Header (load) INITIAL Packet")
  {
    const uint8_t input[] = {
      0xc3,                                           // Long header, Type: INITIAL
      0x11, 0x22, 0x33, 0x44,                         // Version
      0x55,                                           // DCIL/SCIL
      0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // Destination Connection ID
      0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, // Source Connection ID
      0x00,                                           // Token Length (i), Token (*)
      0x02,                                           // Payload length
      0x01, 0x23, 0x45, 0x67,                         // Packet number
      0xff, 0xff,                                     // Payload (dummy)
    };
    ats_unique_buf uinput = ats_unique_malloc(sizeof(input));
    memcpy(uinput.get(), input, sizeof(input));

    QUICPacketHeaderUPtr header = QUICPacketHeader::load({}, std::move(uinput), sizeof(input), 0);
    CHECK(header->size() == sizeof(input) - 2); // Packet Length - Payload Length
    CHECK(header->packet_size() == sizeof(input));
    CHECK(header->type() == QUICPacketType::INITIAL);
    CHECK(
      (header->destination_cid() == QUICConnectionId(reinterpret_cast<const uint8_t *>("\x01\x02\x03\x04\x05\x06\x07\x08"), 8)));
    CHECK((header->source_cid() == QUICConnectionId(reinterpret_cast<const uint8_t *>("\x11\x12\x13\x14\x15\x16\x17\x18"), 8)));
    CHECK(header->packet_number() == 0x01234567);
    CHECK(header->has_version() == true);
    CHECK(header->version() == 0x11223344);
  }

  SECTION("Long Header (load) RETRY Packet")
  {
    const uint8_t input[] = {
      0xf5,                                           // Long header, Type: RETRY
      0x11, 0x22, 0x33, 0x44,                         // Version
      0x55,                                           // DCIL/SCIL
      0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // Destination Connection ID
      0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, // Source Connection ID
      0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, // Original Destination Connection ID
      0x00, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, // Retry Token
      0x80, 0x90, 0xa0, 0xb0, 0xc0, 0xd0, 0xe0, 0xf0,
    };
    ats_unique_buf uinput = ats_unique_malloc(sizeof(input));
    memcpy(uinput.get(), input, sizeof(input));

    const uint8_t retry_token[] = {0x00, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80, 0x90, 0xa0, 0xb0, 0xc0, 0xd0, 0xe0, 0xf0};

    QUICPacketHeaderUPtr header = QUICPacketHeader::load({}, std::move(uinput), sizeof(input), 0);
    CHECK(header->size() == sizeof(input) - 16); // Packet Length - Payload Length (Retry Token)
    CHECK(header->packet_size() == sizeof(input));
    CHECK(header->type() == QUICPacketType::RETRY);
    CHECK(
      (header->destination_cid() == QUICConnectionId(reinterpret_cast<const uint8_t *>("\x01\x02\x03\x04\x05\x06\x07\x08"), 8)));
    CHECK((header->source_cid() == QUICConnectionId(reinterpret_cast<const uint8_t *>("\x11\x12\x13\x14\x15\x16\x17\x18"), 8)));

    QUICPacketLongHeader *retry_header = static_cast<QUICPacketLongHeader *>(header.get());
    CHECK((retry_header->original_dcid() ==
           QUICConnectionId(reinterpret_cast<const uint8_t *>("\x08\x07\x06\x05\x04\x03\x02\x01"), 8)));

    CHECK(memcmp(header->payload(), retry_token, 16) == 0);
    CHECK(header->has_version() == true);
    CHECK(header->version() == 0x11223344);
  }

  SECTION("Long Header (store) INITIAL Packet")
  {
    uint8_t buf[64] = {0};
    size_t len      = 0;

    const uint8_t expected[] = {
      0xc3,                                           // Long header, Type: INITIAL
      0x11, 0x22, 0x33, 0x44,                         // Version
      0x55,                                           // DCIL/SCIL
      0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // Destination Connection ID
      0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, // Source Connection ID
      0x00,                                           // Token Length (i), Token (*)
      0x19,                                           // Length (Not 0x09 because it will have 16 bytes of AEAD tag)
      0x01, 0x23, 0x45, 0x67,                         // Packet number
      0x11, 0x22, 0x33, 0x44, 0x55,                   // Payload (dummy)
    };
    ats_unique_buf payload = ats_unique_malloc(5);
    memcpy(payload.get(), expected + 17, 5);

    QUICPacketHeaderUPtr header = QUICPacketHeader::build(
      QUICPacketType::INITIAL, QUICKeyPhase::INITIAL, {reinterpret_cast<const uint8_t *>("\x01\x02\x03\x04\x05\x06\x07\x08"), 8},
      {reinterpret_cast<const uint8_t *>("\x11\x12\x13\x14\x15\x16\x17\x18"), 8}, 0x01234567, 0, 0x11223344, true,
      std::move(payload), 5);

    CHECK(header->size() == sizeof(expected) - 5);
    CHECK(header->packet_size() == sizeof(expected));
    CHECK(header->type() == QUICPacketType::INITIAL);
    CHECK(
      (header->destination_cid() == QUICConnectionId(reinterpret_cast<const uint8_t *>("\x01\x02\x03\x04\x05\x06\x07\x08"), 8)));
    CHECK((header->source_cid() == QUICConnectionId(reinterpret_cast<const uint8_t *>("\x11\x12\x13\x14\x15\x16\x17\x18"), 8)));
    CHECK(header->packet_number() == 0x01234567);
    CHECK(header->has_version() == true);
    CHECK(header->version() == 0x11223344);
    CHECK(header->is_crypto_packet());

    header->store(buf, &len);
    CHECK(len == header->size());
    CHECK(memcmp(buf, expected, len) == 0);
  }

  SECTION("Long Header (store) RETRY Packet")
  {
    uint8_t buf[64] = {0};
    size_t len      = 0;

    const uint8_t expected[] = {
      0xf5,                                           // Long header, Type: RETRY
      0x11, 0x22, 0x33, 0x44,                         // Version
      0x55,                                           // DCIL/SCIL
      0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // Destination Connection ID
      0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, // Source Connection ID
      0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, // Original Destination Connection ID
      0x00, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, // Retry Token
      0x80, 0x90, 0xa0, 0xb0, 0xc0, 0xd0, 0xe0, 0xf0,
    };
    ats_unique_buf payload = ats_unique_malloc(16);
    memcpy(payload.get(), expected + 30, 16);

    QUICPacketHeaderUPtr header =
      QUICPacketHeader::build(QUICPacketType::RETRY, QUICKeyPhase::INITIAL, 0x11223344,
                              {reinterpret_cast<const uint8_t *>("\x01\x02\x03\x04\x05\x06\x07\x08"), 8},
                              {reinterpret_cast<const uint8_t *>("\x11\x12\x13\x14\x15\x16\x17\x18"), 8},
                              {reinterpret_cast<const uint8_t *>("\x08\x07\x06\x05\x04\x03\x02\x01"), 8}, std::move(payload), 16);

    CHECK(header->size() == sizeof(expected) - 16);
    CHECK(header->packet_size() == sizeof(expected));
    CHECK(header->type() == QUICPacketType::RETRY);
    CHECK(
      (header->destination_cid() == QUICConnectionId(reinterpret_cast<const uint8_t *>("\x01\x02\x03\x04\x05\x06\x07\x08"), 8)));
    CHECK((header->source_cid() == QUICConnectionId(reinterpret_cast<const uint8_t *>("\x11\x12\x13\x14\x15\x16\x17\x18"), 8)));
    CHECK(header->has_version() == true);
    CHECK(header->version() == 0x11223344);

    QUICPacketLongHeader *retry_header = static_cast<QUICPacketLongHeader *>(header.get());
    CHECK((retry_header->original_dcid() ==
           QUICConnectionId(reinterpret_cast<const uint8_t *>("\x08\x07\x06\x05\x04\x03\x02\x01"), 8)));

    header->store(buf, &len);
    CHECK(len == header->size());
    CHECK(memcmp(buf, expected, 22) == 0);
    CHECK(memcmp(buf + 22, expected + 22, 8) == 0);
  }
}

TEST_CASE("QUICPacketHeader - Short", "[quic]")
{
  const uint8_t raw_dcid[] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, // Destination Connection ID (144)
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, //
    0x10, 0x11,                                     //
  };
  QUICConnectionId dcid(raw_dcid, sizeof(raw_dcid));

  SECTION("Short Header (load)")
  {
    const uint8_t input[] = {
      0x43,                                           // Short header with (K=0)
      0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, // Destination Connection ID (144)
      0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, //
      0x10, 0x11,                                     //
      0x01, 0x23, 0x45, 0x67,                         // Packet number
      0xff, 0xff,                                     // Payload (dummy)
    };
    ats_unique_buf uinput = ats_unique_malloc(sizeof(input));
    memcpy(uinput.get(), input, sizeof(input));

    QUICPacketHeaderUPtr header = QUICPacketHeader::load({}, std::move(uinput), sizeof(input), 0);
    CHECK(header->size() == 23);
    CHECK(header->packet_size() == 25);
    CHECK(header->key_phase() == QUICKeyPhase::PHASE_0);
    CHECK(header->destination_cid() == dcid);
    CHECK(header->packet_number() == 0x01234567);
    CHECK(header->has_version() == false);
  }

  SECTION("Short Header (store)")
  {
    uint8_t buf[32] = {0};
    size_t len      = 0;

    const uint8_t expected[] = {
      0x43,                                           // Short header with (K=0)
      0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, // Destination Connection ID (144)
      0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, //
      0x10, 0x11,                                     //
      0x01, 0x23, 0x45, 0x67,                         // Packet number
      0x11, 0x22, 0x33, 0x44, 0x55,                   // Protected Payload
    };
    size_t payload_len = 5;
    size_t header_len  = sizeof(expected) - 5;

    ats_unique_buf payload = ats_unique_malloc(payload_len);
    memcpy(payload.get(), expected + header_len, payload_len);

    QUICPacketHeaderUPtr header =
      QUICPacketHeader::build(QUICPacketType::PROTECTED, QUICKeyPhase::PHASE_0, dcid, 0x01234567, 0, std::move(payload), 32);

    CHECK(header->size() == 23);
    CHECK(header->packet_size() == 0);
    CHECK(header->key_phase() == QUICKeyPhase::PHASE_0);
    CHECK(header->type() == QUICPacketType::PROTECTED);
    CHECK(header->destination_cid() == dcid);
    CHECK(header->packet_number() == 0x01234567);
    CHECK(header->has_version() == false);

    header->store(buf, &len);
    CHECK(len == header_len);
    CHECK(memcmp(buf, expected, header_len) == 0);
  }
}

TEST_CASE("Encoded Packet Number Length", "[quic]")
{
  QUICPacketNumber base = 0xabe8bc;

  CHECK(QUICPacket::calc_packet_number_len(0xace8fe, base) == 3);
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
  QUICPacketNumber src  = 0x9b3;
  size_t len            = 2;
  QUICPacketNumber base = 0xaa82f30e;

  QUICPacket::decode_packet_number(dst, src, len, base);
  CHECK(dst == 0xaa8309b3);
}

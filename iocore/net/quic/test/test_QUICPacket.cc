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

TEST_CASE("Receiving Packet", "[quic]")
{
  const uint8_t raw_dcid[] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, // Destination Connection ID (144)
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, //
    0x10, 0x11,                                     //
  };
  QUICConnectionId dcid(raw_dcid, sizeof(raw_dcid));

  SECTION("Version Negotiation Packet")
  {
    uint8_t input[] = {
      0xc0,                                           // Long header, Type: NONE
      0x00, 0x00, 0x00, 0x00,                         // Version
      0x08,                                           // DCID Len
      0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // Destination Connection ID
      0x08,                                           // SCID Len
      0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, // Source Connection ID
      0x00, 0x00, 0x00, 0x08,                         // Supported Version 1
      0x00, 0x00, 0x00, 0x09,                         // Supported Version 2
    };
    Ptr<IOBufferBlock> input_ibb = make_ptr<IOBufferBlock>(new_IOBufferBlock());
    input_ibb->set_internal(static_cast<void *>(input), sizeof(input), BUFFER_SIZE_NOT_ALLOCATED);

    QUICVersionNegotiationPacketR packet(nullptr, {}, {}, input_ibb);
    CHECK(packet.type() == QUICPacketType::VERSION_NEGOTIATION);
    CHECK(packet.size() == sizeof(input));
    CHECK(packet.destination_cid() == QUICConnectionId(reinterpret_cast<const uint8_t *>("\x01\x02\x03\x04\x05\x06\x07\x08"), 8));
    CHECK(packet.source_cid() == QUICConnectionId(reinterpret_cast<const uint8_t *>("\x11\x12\x13\x14\x15\x16\x17\x18"), 8));
    CHECK(packet.version() == 0x00000000);
  }

  SECTION("INITIAL Packet")
  {
    uint8_t input[] = {
      0xc3,                                           // Long header, Type: INITIAL
      0x11, 0x22, 0x33, 0x44,                         // Version
      0x08,                                           // DCID Len
      0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // Destination Connection ID
      0x08,                                           // SCID Len
      0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, // Source Connection ID
      0x00,                                           // Token Length (i), Token (*)
      0x06,                                           // Length
      0x01, 0x23, 0x45, 0x67,                         // Packet number
      0xff, 0xff,                                     // Payload (dummy)
    };
    Ptr<IOBufferBlock> input_ibb = make_ptr<IOBufferBlock>(new_IOBufferBlock());
    input_ibb->set_internal(static_cast<void *>(input), sizeof(input), BUFFER_SIZE_NOT_ALLOCATED);

    QUICInitialPacketR packet(nullptr, {}, {}, input_ibb, 0);
    CHECK(packet.type() == QUICPacketType::INITIAL);
    CHECK(packet.size() == sizeof(input)); // Packet Length - Payload Length
    CHECK(packet.destination_cid() == QUICConnectionId(reinterpret_cast<const uint8_t *>("\x01\x02\x03\x04\x05\x06\x07\x08"), 8));
    CHECK(packet.source_cid() == QUICConnectionId(reinterpret_cast<const uint8_t *>("\x11\x12\x13\x14\x15\x16\x17\x18"), 8));
    CHECK(packet.packet_number() == 0x01234567);
    CHECK(packet.version() == 0x11223344);
  }

  SECTION("RETRY Packet")
  {
    uint8_t input[] = {
      0xf5,                                           // Long header, Type: RETRY
      0x11, 0x22, 0x33, 0x44,                         // Version
      0x08,                                           // DCID Len
      0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // Destination Connection ID
      0x08,                                           // SCID Len
      0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, // Source Connection ID
      0x01, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, // Retry Token
      0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, //
      0x10, 0x11, 0x12, 0x13, 0x14, 0xf0, 0xf1, 0xf2, //
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Retry Integrity Tag
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    Ptr<IOBufferBlock> input_ibb = make_ptr<IOBufferBlock>(new_IOBufferBlock());
    input_ibb->set_internal(input, sizeof(input), BUFFER_SIZE_NOT_ALLOCATED);

    const uint8_t retry_token[] = {
      0x01, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b,
      0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0xf0, 0xf1, 0xf2,
    };

    QUICRetryPacketR packet(nullptr, {}, {}, input_ibb);
    CHECK(packet.type() == QUICPacketType::RETRY);
    CHECK(packet.size() == sizeof(input));
    CHECK(packet.destination_cid() == QUICConnectionId(reinterpret_cast<const uint8_t *>("\x01\x02\x03\x04\x05\x06\x07\x08"), 8));
    CHECK(packet.source_cid() == QUICConnectionId(reinterpret_cast<const uint8_t *>("\x11\x12\x13\x14\x15\x16\x17\x18"), 8));

    CHECK(memcmp(packet.token().buf(), retry_token, 24) == 0);
    CHECK(packet.version() == 0x11223344);
  }

  SECTION("INITIAL Packet")
  {
    uint8_t input[] = {
      0xc3,                                           // Long header, Type: INITIAL
      0x11, 0x22, 0x33, 0x44,                         // Version
      0x08,                                           // DCID Len
      0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // Destination Connection ID
      0x08,                                           // SCID Len
      0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, // Source Connection ID
      0x00,                                           // Token Length (i), Token (*)
      0x06,                                           // Length
      0x01, 0x23, 0x45, 0x67,                         // Packet number
      0xff, 0xff,                                     // Payload (dummy)
    };
    Ptr<IOBufferBlock> input_ibb = make_ptr<IOBufferBlock>(new_IOBufferBlock());
    input_ibb->set_internal(input, sizeof(input), BUFFER_SIZE_NOT_ALLOCATED);

    QUICInitialPacketR packet(nullptr, {}, {}, input_ibb, 0);

    CHECK(packet.type() == QUICPacketType::INITIAL);
    CHECK(packet.size() == sizeof(input));
    CHECK(packet.version() == 0x11223344);
    CHECK(packet.destination_cid() == QUICConnectionId(reinterpret_cast<const uint8_t *>("\x01\x02\x03\x04\x05\x06\x07\x08"), 8));
    CHECK(packet.source_cid() == QUICConnectionId(reinterpret_cast<const uint8_t *>("\x11\x12\x13\x14\x15\x16\x17\x18"), 8));
    CHECK(packet.token().length() == 0);

    size_t token_length;
    uint8_t token_length_field_len;
    size_t token_length_field_offset;
    CHECK(QUICInitialPacketR::token_length(token_length, token_length_field_len, token_length_field_offset, input, sizeof(input)));
    CHECK(token_length == 0);
    CHECK(token_length_field_len == 1);
    CHECK(token_length_field_offset == 23);
  }

  SECTION("Short Header Packet")
  {
    uint8_t input[] = {
      0x43,                                           // Short header with (K=0)
      0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, // Destination Connection ID (144)
      0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, //
      0x10, 0x11,                                     //
      0x01, 0x23, 0x45, 0x67,                         // Packet number
      0xff, 0xff,                                     // Payload (dummy)
    };
    Ptr<IOBufferBlock> input_ibb = make_ptr<IOBufferBlock>(new_IOBufferBlock());
    input_ibb->set_internal(static_cast<void *>(input), sizeof(input), BUFFER_SIZE_NOT_ALLOCATED);

    QUICShortHeaderPacketR packet(nullptr, {}, {}, input_ibb, 0);
    CHECK(packet.size() == 25);
    CHECK(packet.key_phase() == QUICKeyPhase::PHASE_0);
    CHECK(packet.destination_cid() == dcid);
    CHECK(packet.packet_number() == 0x01234567);
  }
}

TEST_CASE("Sending Packet", "[quic]")
{
  const uint8_t raw_dcid[] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, // Destination Connection ID (144)
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, //
    0x10, 0x11,                                     //
  };
  QUICConnectionId dcid(raw_dcid, sizeof(raw_dcid));

  SECTION("Short Header Packet (store)")
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
    size_t payload_len         = 5;
    Ptr<IOBufferBlock> payload = make_ptr<IOBufferBlock>(new_IOBufferBlock());
    payload->alloc(iobuffer_size_to_index(5, BUFFER_SIZE_INDEX_32K));
    payload->fill(5);
    memcpy(payload->start(), expected + sizeof(expected) - payload_len, payload_len);

    QUICShortHeaderPacket packet(dcid, 0x1234567, 0, QUICKeyPhase::PHASE_0, true, true);
    packet.attach_payload(payload, true);

    CHECK(packet.size() - 16 == 28);
    CHECK(packet.key_phase() == QUICKeyPhase::PHASE_0);
    CHECK(packet.type() == QUICPacketType::PROTECTED);
    CHECK(packet.destination_cid() == dcid);
    CHECK(packet.packet_number() == 0x01234567);

    packet.store(buf, &len);
    CHECK(len == sizeof(expected));
    CHECK(memcmp(buf, expected, sizeof(expected)) == 0);
  }
  SECTION("INITIAL Packet (store)")
  {
    uint8_t buf[64] = {0};
    size_t len      = 0;

    const uint8_t expected[] = {
      0xc3,                                           // Long header, Type: INITIAL
      0x11, 0x22, 0x33, 0x44,                         // Version
      0x08,                                           // DCID Len
      0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // Destination Connection ID
      0x08,                                           // SCID Len
      0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, // Source Connection ID
      0x00,                                           // Token Length (i), Token (*)
      0x19,                                           // Length (Not 0x09 because it will have 16 bytes of AEAD tag)
      0x01, 0x23, 0x45, 0x67,                         // Packet number
      0x11, 0x22, 0x33, 0x44, 0x55,                   // Payload (dummy)
    };
    Ptr<IOBufferBlock> payload = make_ptr<IOBufferBlock>(new_IOBufferBlock());
    payload->alloc(iobuffer_size_to_index(5, BUFFER_SIZE_INDEX_32K));
    payload->fill(5);
    memcpy(payload->start(), expected + 17, 5);

    QUICInitialPacket packet(0x11223344, {reinterpret_cast<const uint8_t *>("\x01\x02\x03\x04\x05\x06\x07\x08"), 8},
                             {reinterpret_cast<const uint8_t *>("\x11\x12\x13\x14\x15\x16\x17\x18"), 8}, 0, nullptr, 5, 0x01234567,
                             true, true, true);
    packet.attach_payload(payload, true);

    CHECK(packet.size() == sizeof(expected) + 16);
    CHECK(packet.type() == QUICPacketType::INITIAL);
    CHECK((packet.destination_cid() == QUICConnectionId(reinterpret_cast<const uint8_t *>("\x01\x02\x03\x04\x05\x06\x07\x08"), 8)));
    CHECK((packet.source_cid() == QUICConnectionId(reinterpret_cast<const uint8_t *>("\x11\x12\x13\x14\x15\x16\x17\x18"), 8)));
    CHECK(packet.packet_number() == 0x01234567);
    CHECK(packet.version() == 0x11223344);
    CHECK(packet.is_crypto_packet());

    packet.store(buf, &len);
    CHECK(len == packet.size() - 16);
    CHECK(memcmp(buf, expected, len - 16) == 0);
  }

  SECTION("RETRY Packet (store)")
  {
    uint8_t buf[128] = {0};
    size_t len       = 0;

    const uint8_t expected[] = {
      0xf0,                                           // Long header, Type: RETRY
      0x11, 0x22, 0x33, 0x44,                         // Version
      0x08,                                           // DCID Len
      0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // Destination Connection ID
      0x08,                                           // SCID Len
      0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, // Source Connection ID
      0x01, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, // Retry Token
      0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, //
      0x10, 0x11, 0x12, 0x13, 0x14, 0x08, 0x01, 0x02, //
      0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x08, 0x11, //
      0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,       //
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Retry Integrity Tag
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    QUICRetryToken token(expected + 23, 39);

    QUICRetryPacket packet(0x11223344, {reinterpret_cast<const uint8_t *>("\x01\x02\x03\x04\x05\x06\x07\x08"), 8},
                           {reinterpret_cast<const uint8_t *>("\x11\x12\x13\x14\x15\x16\x17\x18"), 8}, token);
    CHECK(packet.size() == sizeof(expected));
    CHECK(packet.type() == QUICPacketType::RETRY);
    CHECK((packet.destination_cid() == QUICConnectionId(reinterpret_cast<const uint8_t *>("\x01\x02\x03\x04\x05\x06\x07\x08"), 8)));
    CHECK((packet.source_cid() == QUICConnectionId(reinterpret_cast<const uint8_t *>("\x11\x12\x13\x14\x15\x16\x17\x18"), 8)));
    CHECK(packet.version() == 0x11223344);

    packet.store(buf, &len);
    CHECK(len == packet.size());
    CHECK(memcmp(buf, expected, sizeof(expected) - 16) == 0);
  }

  SECTION("VersionNegotiation Packet")
  {
    QUICConnectionId dummy;
    QUICVersionNegotiationPacket vn1(dummy, dummy, QUIC_SUPPORTED_VERSIONS, countof(QUIC_SUPPORTED_VERSIONS),
                                     QUIC_EXERCISE_VERSION1);
    for (auto i = 0; i < vn1.nversions(); ++i) {
      REQUIRE(vn1.versions()[i] != QUIC_EXERCISE_VERSION1);
    }
    QUICVersionNegotiationPacket vn2(dummy, dummy, QUIC_SUPPORTED_VERSIONS, countof(QUIC_SUPPORTED_VERSIONS),
                                     QUIC_EXERCISE_VERSION2);
    for (auto i = 0; i < vn2.nversions(); ++i) {
      REQUIRE(vn2.versions()[i] != QUIC_EXERCISE_VERSION2);
    }
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

TEST_CASE("read_essential_info", "[quic]")
{
  SECTION("Long header packet - INITIAL")
  {
    uint8_t input[] = {
      0xc3,                                           // Long header, Type: INITIAL
      0x11, 0x22, 0x33, 0x44,                         // Version
      0x08,                                           // DCID Len
      0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // Destination Connection ID
      0x08,                                           // SCID Len
      0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, // Source Connection ID
      0x00,                                           // Token Length (i), Token (*)
      0x06,                                           // Length
      0x01, 0x23, 0x45, 0x67,                         // Packet number
      0xff, 0xff,                                     // Payload (dummy)
    };

    QUICConnectionId expected_dcid(input + 6, 8);
    QUICConnectionId expected_scid(input + 15, 8);

    Ptr<IOBufferBlock> input_ibb = make_ptr<IOBufferBlock>(new_IOBufferBlock());
    input_ibb->set_internal(static_cast<void *>(input), sizeof(input), BUFFER_SIZE_NOT_ALLOCATED);

    QUICPacketType type;
    QUICVersion version;
    QUICConnectionId dcid;
    QUICConnectionId scid;
    QUICPacketNumber packet_number;
    QUICKeyPhase key_phase;
    bool result = QUICPacketR::read_essential_info(input_ibb, type, version, dcid, scid, packet_number, 0, key_phase);

    REQUIRE(result);
    CHECK(type == QUICPacketType::INITIAL);
    CHECK(version == 0x11223344);
    CHECK(dcid == expected_dcid);
    CHECK(scid == expected_scid);
    CHECK(packet_number == 0x01234567);
  }

  SECTION("Long header packet - INITIAL - 0 length CID")
  {
    uint8_t input[] = {
      0xc2,                                           // Long header, Type: INITIAL
      0xff, 0x00, 0x00, 0x19,                         // Version
      0x08,                                           // DCID Len
      0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // Destination Connection ID
      0x00,                                           // SCID Len
      0x00,                                           // Token Length (i), Token (*)
      0x42, 0x17,                                     // Length
      0x00, 0x00, 0x00                                // Packet number
    };

    Ptr<IOBufferBlock> input_ibb = make_ptr<IOBufferBlock>(new_IOBufferBlock());
    input_ibb->set_internal(static_cast<void *>(input), sizeof(input), BUFFER_SIZE_NOT_ALLOCATED);

    QUICPacketType type;
    QUICVersion version;
    QUICConnectionId dcid;
    QUICConnectionId scid;
    QUICPacketNumber packet_number;
    QUICKeyPhase key_phase;
    bool result = QUICPacketR::read_essential_info(input_ibb, type, version, dcid, scid, packet_number, 0, key_phase);

    REQUIRE(result);
  }

  SECTION("Long header packet - RETRY")
  {
    uint8_t input[] = {
      0xf0,                                           // Long header, Type: RETRY
      0x11, 0x22, 0x33, 0x44,                         // Version
      0x08,                                           // DCID Len
      0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // Destination Connection ID
      0x08,                                           // SCID Len
      0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, // Source Connection ID
      0x01, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, // Retry Token
      0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, //
      0x10, 0x11, 0x12, 0x13, 0x14, 0xf0, 0xf1, 0xf2, //
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Retry Integrity Tag
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };

    QUICConnectionId expected_dcid(input + 6, 8);
    QUICConnectionId expected_scid(input + 15, 8);

    Ptr<IOBufferBlock> input_ibb = make_ptr<IOBufferBlock>(new_IOBufferBlock());
    input_ibb->set_internal(static_cast<void *>(input), sizeof(input), BUFFER_SIZE_NOT_ALLOCATED);

    QUICPacketType type;
    QUICVersion version;
    QUICConnectionId dcid;
    QUICConnectionId scid;
    QUICPacketNumber packet_number;
    QUICKeyPhase key_phase;
    bool result = QUICPacketR::read_essential_info(input_ibb, type, version, dcid, scid, packet_number, 0, key_phase);

    REQUIRE(result);
    CHECK(type == QUICPacketType::RETRY);
    CHECK(version == 0x11223344);
    CHECK(dcid == expected_dcid);
    CHECK(scid == expected_scid);
  }

  SECTION("Long header packet - Version Negotiation")
  {
    uint8_t input[] = {
      0xd9,                                           // Long header, Type: RETRY
      0x00, 0x00, 0x00, 0x00,                         // Version
      0x08,                                           // DCID Len
      0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // Destination Connection ID
      0x08,                                           // SCID Len
      0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, // Source Connection ID
      0xff, 0x00, 0x00, 0x19,                         // Supported Version 1
      0xa1, 0xa2, 0xa3, 0xa4,                         // Supported Version 2
    };

    QUICConnectionId expected_dcid(input + 6, 8);
    QUICConnectionId expected_scid(input + 15, 8);

    Ptr<IOBufferBlock> input_ibb = make_ptr<IOBufferBlock>(new_IOBufferBlock());
    input_ibb->set_internal(static_cast<void *>(input), sizeof(input), BUFFER_SIZE_NOT_ALLOCATED);

    QUICPacketType type;
    QUICVersion version;
    QUICConnectionId dcid;
    QUICConnectionId scid;
    QUICPacketNumber packet_number;
    QUICKeyPhase key_phase;
    bool result = QUICPacketR::read_essential_info(input_ibb, type, version, dcid, scid, packet_number, 0, key_phase);

    REQUIRE(result);
    CHECK(type == QUICPacketType::VERSION_NEGOTIATION);
    CHECK(version == 0x00);
    CHECK(dcid == expected_dcid);
    CHECK(scid == expected_scid);
  }

  SECTION("Short header packet")
  {
    uint8_t input[] = {
      0x43,                                           // Short header with (K=0)
      0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, // Destination Connection ID (144)
      0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, //
      0x10, 0x11,                                     //
      0x01, 0x23, 0x45, 0x67,                         // Packet number
      0xff, 0xff,                                     // Payload (dummy)
    };

    QUICConnectionId expected_dcid(input + 1, 18);

    Ptr<IOBufferBlock> input_ibb = make_ptr<IOBufferBlock>(new_IOBufferBlock());
    input_ibb->set_internal(static_cast<void *>(input), sizeof(input), BUFFER_SIZE_NOT_ALLOCATED);
    QUICPacketType type;
    QUICVersion version;
    QUICConnectionId dcid;
    QUICConnectionId scid;
    QUICPacketNumber packet_number;
    QUICKeyPhase key_phase;
    bool result = QUICPacketR::read_essential_info(input_ibb, type, version, dcid, scid, packet_number, 0, key_phase);

    REQUIRE(result);
    CHECK(type == QUICPacketType::PROTECTED);
    CHECK(key_phase == QUICKeyPhase::PHASE_0);
    CHECK(dcid == expected_dcid);
    CHECK(packet_number == 0x01234567);
  }

  SECTION("Long header packet - Malformed INITIAL 1")
  {
    uint8_t input[] = {
      0xc3, // Long header, Type: INITIAL
    };

    Ptr<IOBufferBlock> input_ibb = make_ptr<IOBufferBlock>(new_IOBufferBlock());
    input_ibb->set_internal(static_cast<void *>(input), sizeof(input), BUFFER_SIZE_NOT_ALLOCATED);

    QUICPacketType type;
    QUICVersion version;
    QUICConnectionId dcid;
    QUICConnectionId scid;
    QUICPacketNumber packet_number;
    QUICKeyPhase key_phase;
    bool result = QUICPacketR::read_essential_info(input_ibb, type, version, dcid, scid, packet_number, 0, key_phase);

    REQUIRE(!result);
  }

  SECTION("Long header packet - Malformed INITIAL 2")
  {
    uint8_t input[] = {
      0xc3,       // Long header, Type: INITIAL
      0x11, 0x22, // Version
    };

    Ptr<IOBufferBlock> input_ibb = make_ptr<IOBufferBlock>(new_IOBufferBlock());
    input_ibb->set_internal(static_cast<void *>(input), sizeof(input), BUFFER_SIZE_NOT_ALLOCATED);

    QUICPacketType type;
    QUICVersion version;
    QUICConnectionId dcid;
    QUICConnectionId scid;
    QUICPacketNumber packet_number;
    QUICKeyPhase key_phase;
    bool result = QUICPacketR::read_essential_info(input_ibb, type, version, dcid, scid, packet_number, 0, key_phase);

    REQUIRE(!result);
  }

  SECTION("Long header packet - Malformed INITIAL 3")
  {
    uint8_t input[] = {
      0xc3,                   // Long header, Type: INITIAL
      0x11, 0x22, 0x33, 0x44, // Version
    };

    Ptr<IOBufferBlock> input_ibb = make_ptr<IOBufferBlock>(new_IOBufferBlock());
    input_ibb->set_internal(static_cast<void *>(input), sizeof(input), BUFFER_SIZE_NOT_ALLOCATED);

    QUICPacketType type;
    QUICVersion version;
    QUICConnectionId dcid;
    QUICConnectionId scid;
    QUICPacketNumber packet_number;
    QUICKeyPhase key_phase;
    bool result = QUICPacketR::read_essential_info(input_ibb, type, version, dcid, scid, packet_number, 0, key_phase);

    REQUIRE(!result);
  }

  SECTION("Long header packet - Malformed INITIAL 4")
  {
    uint8_t input[] = {
      0xc3,                   // Long header, Type: INITIAL
      0x11, 0x22, 0x33, 0x44, // Version
      0x08,                   // DCID Len
    };

    Ptr<IOBufferBlock> input_ibb = make_ptr<IOBufferBlock>(new_IOBufferBlock());
    input_ibb->set_internal(static_cast<void *>(input), sizeof(input), BUFFER_SIZE_NOT_ALLOCATED);

    QUICPacketType type;
    QUICVersion version;
    QUICConnectionId dcid;
    QUICConnectionId scid;
    QUICPacketNumber packet_number;
    QUICKeyPhase key_phase;
    bool result = QUICPacketR::read_essential_info(input_ibb, type, version, dcid, scid, packet_number, 0, key_phase);

    REQUIRE(!result);
  }

  SECTION("Long header packet - Malformed INITIAL 5")
  {
    uint8_t input[] = {
      0xc3,                         // Long header, Type: INITIAL
      0x11, 0x22, 0x33, 0x44,       // Version
      0x08,                         // DCID Len
      0x01, 0x02, 0x03, 0x04, 0x05, // Destination Connection ID
    };

    Ptr<IOBufferBlock> input_ibb = make_ptr<IOBufferBlock>(new_IOBufferBlock());
    input_ibb->set_internal(static_cast<void *>(input), sizeof(input), BUFFER_SIZE_NOT_ALLOCATED);

    QUICPacketType type;
    QUICVersion version;
    QUICConnectionId dcid;
    QUICConnectionId scid;
    QUICPacketNumber packet_number;
    QUICKeyPhase key_phase;
    bool result = QUICPacketR::read_essential_info(input_ibb, type, version, dcid, scid, packet_number, 0, key_phase);

    REQUIRE(!result);
  }

  SECTION("Long header packet - Malformed INITIAL 6")
  {
    uint8_t input[] = {
      0xc3,                                           // Long header, Type: INITIAL
      0x11, 0x22, 0x33, 0x44,                         // Version
      0x08,                                           // DCID Len
      0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // Destination Connection ID
    };

    Ptr<IOBufferBlock> input_ibb = make_ptr<IOBufferBlock>(new_IOBufferBlock());
    input_ibb->set_internal(static_cast<void *>(input), sizeof(input), BUFFER_SIZE_NOT_ALLOCATED);

    QUICPacketType type;
    QUICVersion version;
    QUICConnectionId dcid;
    QUICConnectionId scid;
    QUICPacketNumber packet_number;
    QUICKeyPhase key_phase;
    bool result = QUICPacketR::read_essential_info(input_ibb, type, version, dcid, scid, packet_number, 0, key_phase);

    REQUIRE(!result);
  }

  SECTION("Long header packet - Malformed INITIAL 7")
  {
    uint8_t input[] = {
      0xc3,                                           // Long header, Type: INITIAL
      0x11, 0x22, 0x33, 0x44,                         // Version
      0x08,                                           // DCID Len
      0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // Destination Connection ID
      0x08,                                           // SCID Len
      0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, // Source Connection ID
    };

    Ptr<IOBufferBlock> input_ibb = make_ptr<IOBufferBlock>(new_IOBufferBlock());
    input_ibb->set_internal(static_cast<void *>(input), sizeof(input), BUFFER_SIZE_NOT_ALLOCATED);

    QUICPacketType type;
    QUICVersion version;
    QUICConnectionId dcid;
    QUICConnectionId scid;
    QUICPacketNumber packet_number;
    QUICKeyPhase key_phase;
    bool result = QUICPacketR::read_essential_info(input_ibb, type, version, dcid, scid, packet_number, 0, key_phase);

    REQUIRE(!result);
  }
  SECTION("Long header packet - Malformed INITIAL 8")
  {
    uint8_t input[] = {
      0xc3,                                           // Long header, Type: INITIAL
      0x11, 0x22, 0x33, 0x44,                         // Version
      0x08,                                           // DCID Len
      0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // Destination Connection ID
      0x08,                                           // SCID Len
      0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, // Source Connection ID
      0x80,                                           // Token Length (i), Token (*)
    };

    Ptr<IOBufferBlock> input_ibb = make_ptr<IOBufferBlock>(new_IOBufferBlock());
    input_ibb->set_internal(static_cast<void *>(input), sizeof(input), BUFFER_SIZE_NOT_ALLOCATED);

    QUICPacketType type;
    QUICVersion version;
    QUICConnectionId dcid;
    QUICConnectionId scid;
    QUICPacketNumber packet_number;
    QUICKeyPhase key_phase;
    bool result = QUICPacketR::read_essential_info(input_ibb, type, version, dcid, scid, packet_number, 0, key_phase);

    REQUIRE(!result);
  }

  SECTION("Long header packet - Malformed INITIAL 9")
  {
    uint8_t input[] = {
      0xc3,                                           // Long header, Type: INITIAL
      0x11, 0x22, 0x33, 0x44,                         // Version
      0x08,                                           // DCID Len
      0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // Destination Connection ID
      0x08,                                           // SCID Len
      0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, // Source Connection ID
      0x00,                                           // Token Length (i), Token (*)
      0x06,                                           // Length
      0x01, 0x23,                                     // Packet number
    };

    Ptr<IOBufferBlock> input_ibb = make_ptr<IOBufferBlock>(new_IOBufferBlock());
    input_ibb->set_internal(static_cast<void *>(input), sizeof(input), BUFFER_SIZE_NOT_ALLOCATED);

    QUICPacketType type;
    QUICVersion version;
    QUICConnectionId dcid;
    QUICConnectionId scid;
    QUICPacketNumber packet_number;
    QUICKeyPhase key_phase;
    bool result = QUICPacketR::read_essential_info(input_ibb, type, version, dcid, scid, packet_number, 0, key_phase);

    REQUIRE(!result);
  }
}

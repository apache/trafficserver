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

#include "QUICTransportParameters.h"

TEST_CASE("QUICTransportParametersInClientHello_read", "[quic]")
{
  SECTION("OK")
  {
    uint8_t buf[] = {
      0x00,                   // parameter id
      0x04,                   // length of value
      0x11, 0x22, 0x33, 0x44, // value
      0x01,                   // parameter id
      0x04,                   // length of value
      0x12, 0x34, 0x56, 0x78, // value
      0x05,                   // parameter id
      0x02,                   // length of value
      0x0a, 0x0b,             // value
      0x03,                   // parameter id
      0x02,                   // length of value
      0x05, 0x67,             // value
    };

    QUICTransportParametersInClientHello params_in_ch(buf, sizeof(buf), QUIC_SUPPORTED_VERSIONS[0]);
    CHECK(params_in_ch.is_valid());

    uint16_t len        = 0;
    const uint8_t *data = nullptr;

    data = params_in_ch.getAsBytes(QUICTransportParameterId::ORIGINAL_DESTINATION_CONNECTION_ID, len);
    CHECK(len == 4);
    CHECK(memcmp(data, "\x11\x22\x33\x44", 4) == 0);

    data = params_in_ch.getAsBytes(QUICTransportParameterId::MAX_IDLE_TIMEOUT, len);
    CHECK(len == 4);
    CHECK(memcmp(data, "\x12\x34\x56\x78", 4) == 0);

    data = params_in_ch.getAsBytes(QUICTransportParameterId::INITIAL_MAX_STREAM_DATA_BIDI_LOCAL, len);
    CHECK(len == 2);
    CHECK(memcmp(data, "\x0a\x0b", 2) == 0);

    data = params_in_ch.getAsBytes(QUICTransportParameterId::MAX_UDP_PAYLOAD_SIZE, len);
    CHECK(len == 2);
    CHECK(memcmp(data, "\x05\x67", 2) == 0);

    data = params_in_ch.getAsBytes(QUICTransportParameterId::ACK_DELAY_EXPONENT, len);
    CHECK(len == 0);
    CHECK(data == nullptr);
  }

  SECTION("Duplicate parameters")
  {
    uint8_t buf[] = {
      0x00,                   // parameter id
      0x04,                   // length of value
      0x11, 0x22, 0x33, 0x44, // value
      0x00,                   // parameter id
      0x04,                   // length of value
      0x12, 0x34, 0x56, 0x78, // value
    };

    QUICTransportParametersInClientHello params_in_ch(buf, sizeof(buf), QUIC_SUPPORTED_VERSIONS[0]);
    CHECK(!params_in_ch.is_valid());
  }
}

TEST_CASE("QUICTransportParametersInClientHello_write", "[quic]")
{
  uint8_t buf[65536];
  uint16_t len;

  uint8_t expected[] = {
    0x02,                                           // parameter id
    0x10,                                           // length of value
    0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, // value
    0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, // value
    0x03,                                           // parameter id
    0x02,                                           // length of value
    0x5b, 0xcd,                                     // value
    0x05,                                           // parameter id
    0x04,                                           // length of value
    0x91, 0x22, 0x33, 0x44,                         // value
  };

  QUICTransportParametersInClientHello params_in_ch;

  uint32_t max_stream_data = 0x11223344;
  params_in_ch.set(QUICTransportParameterId::INITIAL_MAX_STREAM_DATA_BIDI_LOCAL, max_stream_data);

  uint16_t max_packet_size = 0x1bcd;
  params_in_ch.set(QUICTransportParameterId::MAX_UDP_PAYLOAD_SIZE, max_packet_size);

  uint8_t stateless_reset_token[16] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                                       0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
  params_in_ch.set(QUICTransportParameterId::STATELESS_RESET_TOKEN, stateless_reset_token, 16);

  params_in_ch.store(buf, &len);
  CHECK(len == 28);
  CHECK(memcmp(buf, expected, len) == 0);
}

TEST_CASE("QUICTransportParametersInEncryptedExtensions_read", "[quic]")
{
  SECTION("OK case")
  {
    uint8_t buf[] = {
      0x01,                   // parameter id
      0x02,                   // length of value
      0x51, 0x23,             // value
      0x02,                   // parameter id
      0x10,                   // length of value
      0x00, 0x10, 0x20, 0x30, // value
      0x40, 0x50, 0x60, 0x70, 0x80, 0x90, 0xa0, 0xb0, 0xc0, 0xd0, 0xe0, 0xf0,
      0x04,                   // parameter id
      0x04,                   // length of value
      0x92, 0x34, 0x56, 0x78, // value
      0x06,                   // parameter id
      0x04,                   // length of value
      0x91, 0x22, 0x33, 0x44, // value
    };

    QUICTransportParametersInEncryptedExtensions params_in_ee(buf, sizeof(buf), QUIC_SUPPORTED_VERSIONS[0]);
    CHECK(params_in_ee.is_valid());

    uint16_t len        = 0;
    const uint8_t *data = nullptr;

    data = params_in_ee.getAsBytes(QUICTransportParameterId::INITIAL_MAX_STREAM_DATA_BIDI_REMOTE, len);
    CHECK(len == 4);
    CHECK(memcmp(data, "\x91\x22\x33\x44", 4) == 0);

    data = params_in_ee.getAsBytes(QUICTransportParameterId::INITIAL_MAX_DATA, len);
    CHECK(len == 4);
    CHECK(memcmp(data, "\x92\x34\x56\x78", 4) == 0);

    data = params_in_ee.getAsBytes(QUICTransportParameterId::MAX_IDLE_TIMEOUT, len);
    CHECK(len == 2);
    CHECK(memcmp(data, "\x51\x23", 2) == 0);

    data = params_in_ee.getAsBytes(QUICTransportParameterId::STATELESS_RESET_TOKEN, len);
    CHECK(len == 16);
    CHECK(memcmp(data, buf + 6, 16) == 0);

    CHECK(!params_in_ee.contains(QUICTransportParameterId::DISABLE_ACTIVE_MIGRATION));
  }

  SECTION("OK case - zero length value")
  {
    uint8_t buf[] = {
      0x01,                   // parameter id
      0x02,                   // length of value
      0x51, 0x23,             // value
      0x04,                   // parameter id
      0x04,                   // length of value
      0xa2, 0x34, 0x56, 0x78, // value
      0x06,                   // parameter id
      0x04,                   // length of value
      0xa1, 0x22, 0x33, 0x44, // value
      0x0c,                   // parameter id
      0x00,                   // length of value
    };

    QUICTransportParametersInEncryptedExtensions params_in_ee(buf, sizeof(buf), QUIC_SUPPORTED_VERSIONS[0]);
    CHECK(params_in_ee.is_valid());

    uint16_t len        = 0;
    const uint8_t *data = nullptr;

    data = params_in_ee.getAsBytes(QUICTransportParameterId::INITIAL_MAX_STREAM_DATA_BIDI_REMOTE, len);
    CHECK(len == 4);
    CHECK(memcmp(data, "\xa1\x22\x33\x44", 4) == 0);

    data = params_in_ee.getAsBytes(QUICTransportParameterId::INITIAL_MAX_DATA, len);
    CHECK(len == 4);
    CHECK(memcmp(data, "\xa2\x34\x56\x78", 4) == 0);

    data = params_in_ee.getAsBytes(QUICTransportParameterId::MAX_IDLE_TIMEOUT, len);
    CHECK(len == 2);
    CHECK(memcmp(data, "\x51\x23", 2) == 0);

    CHECK(params_in_ee.contains(QUICTransportParameterId::DISABLE_ACTIVE_MIGRATION));
  }

  SECTION("Duplicate parameters")
  {
    uint8_t buf[] = {
      0x00,                   // parameter id
      0x04,                   // length of value
      0x01, 0x02, 0x03, 0x04, // value
      0x00,                   // parameter id
      0x04,                   // length of value
      0x12, 0x34, 0x56, 0x78, // value
    };

    QUICTransportParametersInEncryptedExtensions params_in_ee(buf, sizeof(buf), QUIC_SUPPORTED_VERSIONS[0]);
    CHECK(!params_in_ee.is_valid());
  }
}

TEST_CASE("QUICTransportParametersEncryptedExtensions_write", "[quic]")
{
  SECTION("OK cases")
  {
    uint8_t buf[65536];
    uint16_t len;

    uint8_t expected[] = {
      0x03,                   // parameter id
      0x02,                   // length of value
      0x5b, 0xcd,             // value
      0x06,                   // parameter id
      0x04,                   // length of value
      0x91, 0x22, 0x33, 0x44, // value
    };

    QUICTransportParametersInEncryptedExtensions params_in_ee;

    uint32_t max_stream_data = 0x11223344;
    params_in_ee.set(QUICTransportParameterId::INITIAL_MAX_STREAM_DATA_BIDI_REMOTE, max_stream_data);

    uint16_t max_packet_size = 0x1bcd;
    params_in_ee.set(QUICTransportParameterId::MAX_UDP_PAYLOAD_SIZE, max_packet_size);

    params_in_ee.store(buf, &len);
    CHECK(len == 10);
    CHECK(memcmp(buf, expected, len) == 0);
  }

  SECTION("OK cases - include zero length value")
  {
    uint8_t buf[65536];
    uint16_t len;

    uint8_t expected[] = {
      0x03,                   // parameter id
      0x02,                   // length of value
      0x5b, 0xcd,             // value
      0x06,                   // parameter id
      0x04,                   // length of value
      0x91, 0x22, 0x33, 0x44, // value
      0x0c,                   // parameter id
      0x00,                   // length of value
    };

    QUICTransportParametersInEncryptedExtensions params_in_ee;

    uint32_t max_stream_data = 0x11223344;
    params_in_ee.set(QUICTransportParameterId::INITIAL_MAX_STREAM_DATA_BIDI_REMOTE, max_stream_data);

    uint16_t max_packet_size = 0x1bcd;
    params_in_ee.set(QUICTransportParameterId::MAX_UDP_PAYLOAD_SIZE, max_packet_size);
    params_in_ee.set(QUICTransportParameterId::DISABLE_ACTIVE_MIGRATION, nullptr, 0);

    params_in_ee.store(buf, &len);
    CHECK(len == 12);
    CHECK(memcmp(buf, expected, len) == 0);
  }
}

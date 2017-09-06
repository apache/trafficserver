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
  uint8_t buf[] = {
    0x01, 0x02, 0x03, 0x04, // negotiated version
    0x05, 0x06, 0x07, 0x08, // iinitial version
    0x00, 0x1e,             // size of parameters
    0x00, 0x00,             // parameter id
    0x00, 0x04,             // length of value
    0x11, 0x22, 0x33, 0x44, // value
    0x00, 0x01,             // parameter id
    0x00, 0x04,             // length of value
    0x12, 0x34, 0x56, 0x78, // value
    0x00, 0x02,             // parameter id
    0x00, 0x04,             // length of value
    0x0a, 0x0b, 0x0c, 0x0d, // value
    0x00, 0x03,             // parameter id
    0x00, 0x02,             // length of value
    0xab, 0xcd,             // value
  };

  QUICTransportParametersInClientHello params_in_ch(buf, sizeof(buf));
  CHECK(params_in_ch.negotiated_version() == 0x01020304);
  CHECK(params_in_ch.initial_version() == 0x05060708);

  uint16_t len        = 0;
  const uint8_t *data = nullptr;

  data = params_in_ch.get(QUICTransportParameterId::INITIAL_MAX_STREAM_DATA, len);
  CHECK(len == 4);
  CHECK(memcmp(data, "\x11\x22\x33\x44", 4) == 0);

  data = params_in_ch.get(QUICTransportParameterId::INITIAL_MAX_DATA, len);
  CHECK(len == 4);
  CHECK(memcmp(data, "\x12\x34\x56\x78", 4) == 0);

  data = params_in_ch.get(QUICTransportParameterId::INITIAL_MAX_STREAM_ID, len);
  CHECK(len == 4);
  CHECK(memcmp(data, "\x0a\x0b\x0c\x0d", 4) == 0);

  data = params_in_ch.get(QUICTransportParameterId::IDLE_TIMEOUT, len);
  CHECK(len == 2);
  CHECK(memcmp(data, "\xab\xcd", 2) == 0);

  data = params_in_ch.get(QUICTransportParameterId::MAX_PACKET_SIZE, len);
  CHECK(len == 0);
  CHECK(data == nullptr);
}

TEST_CASE("QUICTransportParametersInClientHello_write", "[quic]")
{
  uint8_t buf[65536];
  uint16_t len;

  uint8_t expected[] = {
    0x01, 0x02, 0x03, 0x04,                         // negotiated version
    0x05, 0x06, 0x07, 0x08,                         // iinitial version
    0x00, 0x22,                                     // size of parameters
    0x00, 0x00,                                     // parameter id
    0x00, 0x04,                                     // length of value
    0x11, 0x22, 0x33, 0x44,                         // value
    0x00, 0x05,                                     // parameter id
    0x00, 0x02,                                     // length of value
    0xab, 0xcd,                                     // value
    0x00, 0x06,                                     // parameter id
    0x00, 0x10,                                     // length of value
    0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, // value
    0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, // value
  };

  QUICTransportParametersInClientHello params_in_ch(0x01020304, 0x05060708);

  uint32_t max_stream_data = 0x11223344;
  params_in_ch.add(
    QUICTransportParameterId::INITIAL_MAX_STREAM_DATA,
    std::unique_ptr<QUICTransportParameterValue>(new QUICTransportParameterValue(max_stream_data, sizeof(max_stream_data))));

  uint16_t max_packet_size = 0xabcd;
  params_in_ch.add(
    QUICTransportParameterId::MAX_PACKET_SIZE,
    std::unique_ptr<QUICTransportParameterValue>(new QUICTransportParameterValue(max_packet_size, sizeof(max_packet_size))));

  uint64_t stateless_retry_token[2] = {0x0011223344556677, 0x0011223344556677};
  params_in_ch.add(QUICTransportParameterId::STATELESS_RETRY_TOKEN,
                   std::unique_ptr<QUICTransportParameterValue>(new QUICTransportParameterValue(stateless_retry_token, 16)));

  params_in_ch.store(buf, &len);
  CHECK(len == 44);
  CHECK(memcmp(buf, expected, len) == 0);
}

TEST_CASE("QUICTransportParametersInEncryptedExtensions_read", "[quic]")
{
  uint8_t buf[] = {
    0x04,                   // size of supported versions
    0x01, 0x02, 0x03, 0x04, //
    0x00, 0x1e,             // size of parameters
    0x00, 0x00,             // parameter id
    0x00, 0x04,             // length of value
    0x11, 0x22, 0x33, 0x44, // value
    0x00, 0x01,             // parameter id
    0x00, 0x04,             // length of value
    0x12, 0x34, 0x56, 0x78, // value
    0x00, 0x02,             // parameter id
    0x00, 0x04,             // length of value
    0x0a, 0x0b, 0x0c, 0x0d, // value
    0x00, 0x03,             // parameter id
    0x00, 0x02,             // length of value
    0xab, 0xcd,             // value
  };

  QUICTransportParametersInEncryptedExtensions params_in_ee(buf, sizeof(buf));
  const uint8_t *versions;
  uint16_t vlen;
  versions = params_in_ee.supported_versions_len(&vlen);
  CHECK(vlen == 4);
  CHECK(memcmp(versions, "\x01\x02\x03\x04", 4) == 0);

  uint16_t len        = 0;
  const uint8_t *data = nullptr;

  data = params_in_ee.get(QUICTransportParameterId::INITIAL_MAX_STREAM_DATA, len);
  CHECK(len == 4);
  CHECK(memcmp(data, "\x11\x22\x33\x44", 4) == 0);

  data = params_in_ee.get(QUICTransportParameterId::INITIAL_MAX_DATA, len);
  CHECK(len == 4);
  CHECK(memcmp(data, "\x12\x34\x56\x78", 4) == 0);

  data = params_in_ee.get(QUICTransportParameterId::INITIAL_MAX_STREAM_ID, len);
  CHECK(len == 4);
  CHECK(memcmp(data, "\x0a\x0b\x0c\x0d", 4) == 0);

  data = params_in_ee.get(QUICTransportParameterId::IDLE_TIMEOUT, len);
  CHECK(len == 2);
  CHECK(memcmp(data, "\xab\xcd", 2) == 0);

  data = params_in_ee.get(QUICTransportParameterId::MAX_PACKET_SIZE, len);
  CHECK(len == 0);
  CHECK(data == nullptr);
}

TEST_CASE("QUICTransportParametersEncryptedExtensions_write", "[quic]")
{
  uint8_t buf[65536];
  uint16_t len;

  uint8_t expected[] = {
    0x08,                   // size of supported versions
    0x01, 0x02, 0x03, 0x04, // version 1
    0x05, 0x06, 0x07, 0x08, // version 2
    0x00, 0x0e,             // size of parameters
    0x00, 0x00,             // parameter id
    0x00, 0x04,             // length of value
    0x11, 0x22, 0x33, 0x44, // value
    0x00, 0x05,             // parameter id
    0x00, 0x02,             // length of value
    0xab, 0xcd,             // value
  };

  QUICTransportParametersInEncryptedExtensions params_in_ee;

  uint32_t max_stream_data = 0x11223344;
  params_in_ee.add(
    QUICTransportParameterId::INITIAL_MAX_STREAM_DATA,
    std::unique_ptr<QUICTransportParameterValue>(new QUICTransportParameterValue(max_stream_data, sizeof(max_stream_data))));

  uint16_t max_packet_size = 0xabcd;
  params_in_ee.add(
    QUICTransportParameterId::MAX_PACKET_SIZE,
    std::unique_ptr<QUICTransportParameterValue>(new QUICTransportParameterValue(max_packet_size, sizeof(max_packet_size))));

  params_in_ee.add_version(0x01020304);
  params_in_ee.add_version(0x05060708);
  params_in_ee.store(buf, &len);
  CHECK(len == 25);
  CHECK(memcmp(buf, expected, len) == 0);
}

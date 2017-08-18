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
    0x00, 0x04,             // number of parameters
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
  QUICTransportParameterValue value;
  value = params_in_ch.get(QUICTransportParameterId::INITIAL_MAX_STREAM_DATA);
  CHECK(value.len == 4);
  CHECK(memcmp(value.data, "\x11\x22\x33\x44", 4) == 0);
  value = params_in_ch.get(QUICTransportParameterId::INITIAL_MAX_DATA);
  CHECK(value.len == 4);
  CHECK(memcmp(value.data, "\x12\x34\x56\x78", 4) == 0);
  value = params_in_ch.get(QUICTransportParameterId::INITIAL_MAX_STREAM_ID);
  CHECK(value.len == 4);
  CHECK(memcmp(value.data, "\x0a\x0b\x0c\x0d", 4) == 0);
  value = params_in_ch.get(QUICTransportParameterId::IDLE_TIMEOUT);
  CHECK(value.len == 2);
  CHECK(memcmp(value.data, "\xab\xcd", 2) == 0);
  value = params_in_ch.get(QUICTransportParameterId::MAX_PACKET_SIZE);
  CHECK(value.len == 0);
  CHECK(value.data == nullptr);
}

TEST_CASE("QUICTransportParametersInClientHello_write", "[quic]")
{
  uint8_t buf[65536];
  uint16_t len;

  uint8_t expected[] = {
    0x01, 0x02, 0x03, 0x04, // negotiated version
    0x05, 0x06, 0x07, 0x08, // iinitial version
    0x00, 0x02,             // number of parameters
    0x00, 0x00,             // parameter id
    0x00, 0x04,             // length of value
    0x11, 0x22, 0x33, 0x44, // value
    0x00, 0x05,             // parameter id
    0x00, 0x02,             // length of value
    0xab, 0xcd,             // value
  };

  QUICTransportParametersInClientHello params_in_ch(0x01020304, 0x05060708);
  params_in_ch.add(QUICTransportParameterId::INITIAL_MAX_STREAM_DATA, {reinterpret_cast<const uint8_t *>("\x11\x22\x33\x44"), 4});
  params_in_ch.add(QUICTransportParameterId::MAX_PACKET_SIZE, {reinterpret_cast<const uint8_t *>("\xab\xcd"), 2});
  params_in_ch.store(buf, &len);
  CHECK(len == 24);
  CHECK(memcmp(buf, expected, len) == 0);
}

TEST_CASE("QUICTransportParametersInEncryptedExtensions_read", "[quic]")
{
  uint8_t buf[] = {
    0x00, 0x01,             // number of supported versions
    0x01, 0x02, 0x03, 0x04, //
    0x00, 0x04,             // number of parameters
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
  uint16_t nversion;
  versions = params_in_ee.supported_versions(&nversion);
  CHECK(nversion == 1);
  CHECK(memcmp(versions, "\x01\x02\x03\x04", 4) == 0);
  QUICTransportParameterValue value;
  value = params_in_ee.get(QUICTransportParameterId::INITIAL_MAX_STREAM_DATA);
  CHECK(value.len == 4);
  CHECK(memcmp(value.data, "\x11\x22\x33\x44", 4) == 0);
  value = params_in_ee.get(QUICTransportParameterId::INITIAL_MAX_DATA);
  CHECK(value.len == 4);
  CHECK(memcmp(value.data, "\x12\x34\x56\x78", 4) == 0);
  value = params_in_ee.get(QUICTransportParameterId::INITIAL_MAX_STREAM_ID);
  CHECK(value.len == 4);
  CHECK(memcmp(value.data, "\x0a\x0b\x0c\x0d", 4) == 0);
  value = params_in_ee.get(QUICTransportParameterId::IDLE_TIMEOUT);
  CHECK(value.len == 2);
  CHECK(memcmp(value.data, "\xab\xcd", 2) == 0);
  value = params_in_ee.get(QUICTransportParameterId::MAX_PACKET_SIZE);
  CHECK(value.len == 0);
  CHECK(value.data == nullptr);
}

TEST_CASE("QUICTransportParametersEncryptedExtensions_write", "[quic]")
{
  uint8_t buf[65536];
  uint16_t len;

  uint8_t expected[] = {
    0x00, 0x02,             // number of supported versions
    0x01, 0x02, 0x03, 0x04, // version 1
    0x05, 0x06, 0x07, 0x08, // version 2
    0x00, 0x02,             // number of parameters
    0x00, 0x00,             // parameter id
    0x00, 0x04,             // length of value
    0x11, 0x22, 0x33, 0x44, // value
    0x00, 0x05,             // parameter id
    0x00, 0x02,             // length of value
    0xab, 0xcd,             // value
  };

  QUICTransportParametersInEncryptedExtensions params_in_ee;
  params_in_ee.add(QUICTransportParameterId::INITIAL_MAX_STREAM_DATA, {reinterpret_cast<const uint8_t *>("\x11\x22\x33\x44"), 4});
  params_in_ee.add(QUICTransportParameterId::MAX_PACKET_SIZE, {reinterpret_cast<const uint8_t *>("\xab\xcd"), 2});
  params_in_ee.add_version(0x01020304);
  params_in_ee.add_version(0x05060708);
  params_in_ee.store(buf, &len);
  CHECK(len == 26);
  CHECK(memcmp(buf, expected, len) == 0);
}

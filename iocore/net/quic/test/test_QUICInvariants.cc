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

#include "quic/QUICTypes.h"

TEST_CASE("Long Header - regular case", "[quic]")
{
  const uint8_t raw_dcid[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
  const uint8_t raw_scid[] = {0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18};
  QUICConnectionId expected_dcid(raw_dcid, 8);
  QUICConnectionId expected_scid(raw_scid, 8);

  SECTION("dcid & scid")
  {
    const uint8_t buf[] = {
      0x80,                                           // Long header, Type: NONE
      0x11, 0x22, 0x33, 0x44,                         // Version
      0x08,                                           // DCID Len
      0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // Destination Connection ID
      0x08,                                           // SCID Len
      0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, // Source Connection ID
    };
    uint64_t buf_len = sizeof(buf);

    QUICVersion version   = 0;
    uint8_t dcil          = 0;
    uint8_t scil          = 0;
    QUICConnectionId dcid = QUICConnectionId::ZERO();
    QUICConnectionId scid = QUICConnectionId::ZERO();

    CHECK(QUICInvariants::version(version, buf, buf_len));
    CHECK(version == 0x11223344);

    CHECK(QUICInvariants::dcil(dcil, buf, buf_len));
    CHECK(dcil == sizeof(raw_dcid));
    CHECK(QUICInvariants::dcid(dcid, buf, buf_len));
    CHECK(dcid == expected_dcid);

    CHECK(QUICInvariants::scil(scil, buf, buf_len));
    CHECK(scil == sizeof(raw_scid));
    CHECK(QUICInvariants::scid(scid, buf, buf_len));
    CHECK(scid == expected_scid);
  }

  SECTION("omitted dcid")
  {
    const uint8_t buf[] = {
      0x80,                                           // Long header, Type: NONE
      0x11, 0x22, 0x33, 0x44,                         // Version
      0x00,                                           // DCID Len
      0x08,                                           // SCID Len
      0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, // Source Connection ID
    };
    uint64_t buf_len = sizeof(buf);

    QUICVersion version   = 0;
    uint8_t dcil          = 0;
    uint8_t scil          = 0;
    QUICConnectionId dcid = QUICConnectionId::ZERO();
    QUICConnectionId scid = QUICConnectionId::ZERO();

    CHECK(QUICInvariants::version(version, buf, buf_len));
    CHECK(version == 0x11223344);

    CHECK(QUICInvariants::dcil(dcil, buf, buf_len));
    CHECK(dcil == 0);
    CHECK(QUICInvariants::dcid(dcid, buf, buf_len));
    CHECK(dcid == QUICConnectionId::ZERO());

    CHECK(QUICInvariants::scil(scil, buf, buf_len));
    CHECK(scil == sizeof(raw_scid));
    CHECK(QUICInvariants::scid(scid, buf, buf_len));
    CHECK(scid == expected_scid);
  }

  SECTION("omitted scid")
  {
    const uint8_t buf[] = {
      0x80,                                           // Long header, Type: NONE
      0x11, 0x22, 0x33, 0x44,                         // Version
      0x08,                                           // DCID Len
      0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // Destination Connection ID
      0x00,                                           // SCID Len
    };
    uint64_t buf_len = sizeof(buf);

    QUICVersion version   = 0;
    uint8_t dcil          = 0;
    uint8_t scil          = 0;
    QUICConnectionId dcid = QUICConnectionId::ZERO();
    QUICConnectionId scid = QUICConnectionId::ZERO();

    CHECK(QUICInvariants::version(version, buf, buf_len));
    CHECK(version == 0x11223344);

    CHECK(QUICInvariants::dcil(dcil, buf, buf_len));
    CHECK(dcil == sizeof(raw_dcid));
    CHECK(QUICInvariants::dcid(dcid, buf, buf_len));
    CHECK(dcid == expected_dcid);

    CHECK(QUICInvariants::scil(scil, buf, buf_len));
    CHECK(scil == 0);
    CHECK(QUICInvariants::scid(scid, buf, buf_len));
    CHECK(scid == QUICConnectionId::ZERO());
  }
}

TEST_CASE("Long Header - error cases", "[quic]")
{
  SECTION("version")
  {
    const uint8_t buf[] = {
      0x80, // Long header, Type: NONE
    };
    uint64_t buf_len = sizeof(buf);

    QUICVersion version = 0;

    CHECK(QUICInvariants::version(version, buf, buf_len) == false);
  }

  SECTION("dcid")
  {
    const uint8_t buf[] = {
      0x80,                   // Long header, Type: NONE
      0x11, 0x22, 0x33, 0x44, // Version
      0x08,                   // DCID Len
      0x01, 0x02, 0x03, 0x04, // Invalid Destination Connection ID
      0x00,                   // SCID Len
    };
    uint64_t buf_len = sizeof(buf);

    const uint8_t raw_dcid[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    QUICConnectionId expected_dcid(raw_dcid, 8);

    QUICVersion version   = 0;
    QUICConnectionId dcid = QUICConnectionId::ZERO();

    CHECK(QUICInvariants::version(version, buf, buf_len));
    CHECK(version == 0x11223344);
    CHECK(QUICInvariants::dcid(dcid, buf, buf_len) == false);
  }

  SECTION("scid")
  {
    const uint8_t buf[] = {
      0x80,                                           // Long header, Type: NONE
      0x11, 0x22, 0x33, 0x44,                         // Version
      0x08,                                           // DCID Len
      0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // Destination Connection ID
      0x08,                                           // SCID Len
      0x11, 0x12, 0x13, 0x14,                         // Invalid Source Connection ID
    };
    uint64_t buf_len = sizeof(buf);

    const uint8_t raw_dcid[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    const uint8_t raw_scid[] = {0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18};
    QUICConnectionId expected_dcid(raw_dcid, 8);
    QUICConnectionId expected_scid(raw_scid, 8);

    QUICVersion version   = 0;
    QUICConnectionId dcid = QUICConnectionId::ZERO();
    QUICConnectionId scid = QUICConnectionId::ZERO();

    CHECK(QUICInvariants::version(version, buf, buf_len));
    CHECK(version == 0x11223344);
    CHECK(QUICInvariants::dcid(dcid, buf, buf_len));
    CHECK(dcid == expected_dcid);
    CHECK(QUICInvariants::scid(scid, buf, buf_len) == false);
  }
}

// When ATS change QUICConfigParams::_scid_len shorter, this test should be failed
TEST_CASE("Short Header - regular case", "[quic]")
{
  const uint8_t buf[] = {
    0x00,                                           // Long header, Type: NONE
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, // Destination Connection ID (144)
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, //
    0x10, 0x11,                                     //
  };
  uint64_t buf_len = sizeof(buf);

  const uint8_t raw_dcid[] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, //
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, //
    0x10, 0x11,                                     //
  };
  QUICConnectionId expected_dcid(raw_dcid, sizeof(raw_dcid));

  QUICConnectionId dcid = QUICConnectionId::ZERO();

  CHECK(QUICInvariants::dcid(dcid, buf, buf_len));
  CHECK(dcid == expected_dcid);
}

TEST_CASE("Short Header - error case", "[quic]")
{
  const uint8_t buf[] = {
    0x00,                   // Long header, Type: NONE
    0x01, 0x02, 0x03, 0x04, // Invalid Destination Connection ID
  };
  uint64_t buf_len = sizeof(buf);

  QUICConnectionId dcid = QUICConnectionId::ZERO();
  CHECK(QUICInvariants::dcid(dcid, buf, buf_len) == false);
}

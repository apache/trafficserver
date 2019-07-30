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
#include "quic/QUICIntUtil.h"
#include <memory>

TEST_CASE("QUICTypeUtil", "[quic]")
{
  uint8_t buf[8];
  size_t len;

  QUICIntUtil::write_uint_as_nbytes(0xff, 1, buf, &len);
  INFO("1 byte to 1 byte");
  CHECK(memcmp(buf, "\xff\x00\x00\x00\x00\x00\x00\x00", 1) == 0);

  QUICIntUtil::write_uint_as_nbytes(0xff, 2, buf, &len);
  INFO("1 byte to 2 byte");
  CHECK(memcmp(buf, "\x00\xff\x00\x00\x00\x00\x00\x00", 2) == 0);

  QUICIntUtil::write_uint_as_nbytes(0xff, 4, buf, &len);
  INFO("1 byte to 4 byte");
  CHECK(memcmp(buf, "\x00\x00\x00\xff\x00\x00\x00\x00", 4) == 0);

  QUICIntUtil::write_uint_as_nbytes(0xff, 6, buf, &len);
  INFO("1 byte to 6 byte");
  CHECK(memcmp(buf, "\x00\x00\x00\x00\x00\xff\x00\x00", 6) == 0);

  QUICIntUtil::write_uint_as_nbytes(0xff, 8, buf, &len);
  INFO("1 byte to 8 byte");
  CHECK(memcmp(buf, "\x00\x00\x00\x00\x00\x00\x00\xff", 8) == 0);

  QUICIntUtil::write_uint_as_nbytes(0x11ff, 2, buf, &len);
  INFO("2 byte to 2 byte");
  CHECK(memcmp(buf, "\x11\xff\x00\x00\x00\x00\x00\x00", 2) == 0);

  QUICIntUtil::write_uint_as_nbytes(0x11ff, 4, buf, &len);
  INFO("2 byte to 4 byte");
  CHECK(memcmp(buf, "\x00\x00\x11\xff\x00\x00\x00\x00", 4) == 0);

  QUICIntUtil::write_uint_as_nbytes(0x11ff, 6, buf, &len);
  INFO("2 byte to 6 byte");
  CHECK(memcmp(buf, "\x00\x00\x00\x00\x11\xff\x00\x00", 6) == 0);

  QUICIntUtil::write_uint_as_nbytes(0x11ff, 8, buf, &len);
  INFO("2 byte to 8 byte");
  CHECK(memcmp(buf, "\x00\x00\x00\x00\x00\x00\x11\xff", 8) == 0);
}

TEST_CASE("Variable Length - encoding 1", "[quic]")
{
  uint8_t dst[8]   = {0};
  uint64_t src     = 151288809941952652;
  size_t len       = 0;
  uint8_t expect[] = {0xc2, 0x19, 0x7c, 0x5e, 0xff, 0x14, 0xe8, 0x8c};

  QUICVariableInt::encode(dst, sizeof(dst), len, src);

  CHECK(len == 8);
  CHECK(memcmp(dst, expect, 8) == 0);
}

TEST_CASE("Variable Length - encoding 2", "[quic]")
{
  uint8_t dst[8]   = {0};
  uint64_t src     = 494878333;
  size_t len       = 0;
  uint8_t expect[] = {0x9d, 0x7f, 0x3e, 0x7d};

  QUICVariableInt::encode(dst, sizeof(dst), len, src);

  CHECK(len == 4);
  CHECK(memcmp(dst, expect, 4) == 0);
}

TEST_CASE("Variable Length - encoding 3", "[quic]")
{
  uint8_t dst[8]   = {0};
  uint64_t src     = 15293;
  size_t len       = 0;
  uint8_t expect[] = {0x7b, 0xbd};

  QUICVariableInt::encode(dst, sizeof(dst), len, src);

  CHECK(len == 2);
  CHECK(memcmp(dst, expect, 2) == 0);
}

TEST_CASE("Variable Length - encoding 4", "[quic]")
{
  uint8_t dst[8]   = {0};
  uint64_t src     = 37;
  size_t len       = 0;
  uint8_t expect[] = {0x25};

  QUICVariableInt::encode(dst, sizeof(dst), len, src);

  CHECK(len == 1);
  CHECK(memcmp(dst, expect, 1) == 0);
}

TEST_CASE("Variable Length - decoding 1", "[quic]")
{
  uint8_t src[] = {0xc2, 0x19, 0x7c, 0x5e, 0xff, 0x14, 0xe8, 0x8c};
  uint64_t dst  = 0;
  size_t len    = 0;
  QUICVariableInt::decode(dst, len, src, sizeof(src));

  CHECK(dst == 151288809941952652);
  CHECK(len == 8);
}

TEST_CASE("Variable Length - decoding 2", "[quic]")
{
  uint8_t src[] = {0x9d, 0x7f, 0x3e, 0x7d, 0x00, 0x00, 0x00, 0x00};
  uint64_t dst  = 0;
  size_t len    = 0;
  QUICVariableInt::decode(dst, len, src, sizeof(src));

  CHECK(dst == 494878333);
  CHECK(len == 4);
}

TEST_CASE("Variable Length - decoding 3", "[quic]")
{
  uint8_t src[] = {0x7b, 0xbd, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  uint64_t dst  = 0;
  size_t len    = 0;
  QUICVariableInt::decode(dst, len, src, sizeof(src));

  CHECK(dst == 15293);
  CHECK(len == 2);
}

TEST_CASE("Variable Length - decoding 4", "[quic]")
{
  uint8_t src[] = {0x25, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  uint64_t dst  = 0;
  size_t len    = 0;
  QUICVariableInt::decode(dst, len, src, sizeof(src));

  CHECK(dst == 37);
  CHECK(len == 1);
}

TEST_CASE("Variable Length - decoding 5", "[quic]")
{
  uint8_t src[] = {0x40, 0x25, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  uint64_t dst  = 0;
  size_t len    = 0;
  QUICVariableInt::decode(dst, len, src, sizeof(src));

  CHECK(dst == 37);
  CHECK(len == 2);
}

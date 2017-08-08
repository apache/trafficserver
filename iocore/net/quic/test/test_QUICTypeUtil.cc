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
#include <memory>

TEST_CASE("QUICTypeUtil", "[quic]")
{
  uint8_t buf[8];
  size_t len;

  QUICTypeUtil::write_uint_as_nbytes(0xff, 1, buf, &len);
  INFO("1 byte to 1 byte");
  CHECK(memcmp(buf, "\xff\x00\x00\x00\x00\x00\x00\x00", 1) == 0);

  QUICTypeUtil::write_uint_as_nbytes(0xff, 2, buf, &len);
  INFO("1 byte to 2 byte");
  CHECK(memcmp(buf, "\x00\xff\x00\x00\x00\x00\x00\x00", 2) == 0);

  QUICTypeUtil::write_uint_as_nbytes(0xff, 4, buf, &len);
  INFO("1 byte to 4 byte");
  CHECK(memcmp(buf, "\x00\x00\x00\xff\x00\x00\x00\x00", 4) == 0);

  QUICTypeUtil::write_uint_as_nbytes(0xff, 6, buf, &len);
  INFO("1 byte to 6 byte");
  CHECK(memcmp(buf, "\x00\x00\x00\x00\x00\xff\x00\x00", 6) == 0);

  QUICTypeUtil::write_uint_as_nbytes(0xff, 8, buf, &len);
  INFO("1 byte to 8 byte");
  CHECK(memcmp(buf, "\x00\x00\x00\x00\x00\x00\x00\xff", 8) == 0);

  QUICTypeUtil::write_uint_as_nbytes(0x11ff, 2, buf, &len);
  INFO("2 byte to 2 byte");
  CHECK(memcmp(buf, "\x11\xff\x00\x00\x00\x00\x00\x00", 2) == 0);

  QUICTypeUtil::write_uint_as_nbytes(0x11ff, 4, buf, &len);
  INFO("2 byte to 4 byte");
  CHECK(memcmp(buf, "\x00\x00\x11\xff\x00\x00\x00\x00", 4) == 0);

  QUICTypeUtil::write_uint_as_nbytes(0x11ff, 6, buf, &len);
  INFO("2 byte to 6 byte");
  CHECK(memcmp(buf, "\x00\x00\x00\x00\x11\xff\x00\x00", 6) == 0);

  QUICTypeUtil::write_uint_as_nbytes(0x11ff, 8, buf, &len);
  INFO("2 byte to 8 byte");
  CHECK(memcmp(buf, "\x00\x00\x00\x00\x00\x00\x11\xff", 8) == 0);
}

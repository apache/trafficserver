/** @file

    Unit tests for LocalBuffer

    @section license License

    Licensed to the Apache Software Foundation (ASF) under one
    or more contributor license agreements.  See the NOTICE file
    distributed with this work for additional information
    regarding copyright ownership.  The ASF licenses this file
    to you under the Apache License, Version 2.0 (the
    "License"); you may not use this file except in compliance
    with the License.  You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
 */

#include "catch.hpp"
#include "tscpp/util/LocalBuffer.h"

#include <cstring>

TEST_CASE("LocalBuffer", "[libts][LocalBuffer]")
{
  SECTION("EstSizeBound = default")
  {
    SECTION("size = 0")
    {
      const size_t len = 0;
      ts::LocalBuffer local_buffer(len);
      uint8_t *buf = local_buffer.data();

      CHECK(buf == nullptr);
      CHECK(local_buffer.size() == 0);
    }

    SECTION("size = 1024")
    {
      const size_t len = 1024;
      ts::LocalBuffer local_buffer(len);
      uint8_t *buf = local_buffer.data();

      memset(buf, 0xAA, len);

      CHECK(buf[0] == 0xAA);
      CHECK(buf[len - 1] == 0xAA);
      CHECK(local_buffer.size() == 1024);
    }

    SECTION("size = 2048")
    {
      const size_t len = 2048;
      ts::LocalBuffer local_buffer(len);
      uint8_t *buf = local_buffer.data();

      memset(buf, 0xAA, len);

      CHECK(buf[0] == 0xAA);
      CHECK(buf[len - 1] == 0xAA);
      CHECK(local_buffer.size() == 2048);
    }
  }

  SECTION("EstSizeBound = 2048")
  {
    SECTION("size = 1024")
    {
      const size_t len = 1024;
      ts::LocalBuffer<uint8_t, 2048> local_buffer(len);
      uint8_t *buf = local_buffer.data();

      memset(buf, 0xAA, len);

      CHECK(buf[0] == 0xAA);
      CHECK(buf[len - 1] == 0xAA);
      CHECK(local_buffer.size() == 2048);
    }

    SECTION("size = 2048")
    {
      const size_t len = 2048;
      ts::LocalBuffer<uint8_t, 2048> local_buffer(len);
      uint8_t *buf = local_buffer.data();

      memset(buf, 0xAA, len);

      CHECK(buf[0] == 0xAA);
      CHECK(buf[len - 1] == 0xAA);
      CHECK(local_buffer.size() == 2048);
    }

    SECTION("size = 4096")
    {
      const size_t len = 4096;
      ts::LocalBuffer<uint8_t, 2048> local_buffer(len);
      uint8_t *buf = local_buffer.data();

      memset(buf, 0xAA, len);

      CHECK(buf[0] == 0xAA);
      CHECK(buf[len - 1] == 0xAA);
      CHECK(local_buffer.size() == 4096);
    }
  }
}

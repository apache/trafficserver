/** @file test_utils.cc

  Unit tests for ja3.

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

#include "utils.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("ja3 byte buffer encoding")
{
  unsigned char const buf[]{0x8, 0x3, 0x4};

  SECTION("empty buffer")
  {
    auto got{ja3::encode_byte_buffer(nullptr, 0)};
    CHECK("" == got);
  }

  SECTION("1 value")
  {
    auto got{ja3::encode_byte_buffer(buf, 1)};
    CHECK("8" == got);
  }

  SECTION("3 values")
  {
    auto got{ja3::encode_byte_buffer(buf, 3)};
    CHECK("8-3-4" == got);
  }
}

TEST_CASE("ja3 word buffer encoding")
{
  unsigned char const buf[]{0x0, 0x5, 0x0a, 0x0a, 0x0, 0x8, 0xda, 0xda, 0x1, 0x0};

  SECTION("empty buffer")
  {
    auto got{ja3::encode_word_buffer(nullptr, 0)};
    CHECK("" == got);
  }

  SECTION("nullptr with len 1 - early return must not deref")
  {
    auto got{ja3::encode_word_buffer(nullptr, 1)};
    CHECK("" == got);
  }

  SECTION("1 value")
  {
    auto got{ja3::encode_word_buffer(buf, 2)};
    CHECK("5" == got);
  }

  SECTION("5 values including GREASE values")
  {
    auto got{ja3::encode_word_buffer(buf, 10)};
    CHECK("5-8-256" == got);
  }

  SECTION("all GREASE - skip-loop consumes buffer, no emit")
  {
    unsigned char const grease_buf[]{0x0a, 0x0a, 0xda, 0xda};
    auto                got{ja3::encode_word_buffer(grease_buf, 4)};
    CHECK("" == got);
  }

  SECTION("trailing GREASE - last pair is GREASE, no trailing dash")
  {
    unsigned char const buf2[]{0x00, 0x05, 0x0a, 0x0a};
    auto                got{ja3::encode_word_buffer(buf2, 4)};
    CHECK("5" == got);
  }

  SECTION("odd length 1 - single trailing byte must not be read as a word")
  {
    unsigned char const odd_buf[]{0x42};
    auto                got{ja3::encode_word_buffer(odd_buf, 1)};
    CHECK("" == got);
  }

  SECTION("odd length 3 - last byte without pair must be ignored")
  {
    unsigned char const odd_buf[]{0x00, 0x05, 0x42};
    auto                got{ja3::encode_word_buffer(odd_buf, 3)};
    CHECK("5" == got);
  }

  SECTION("odd length 3 after GREASE - skip-loop must not read past end")
  {
    unsigned char const odd_buf[]{0x0a, 0x0a, 0x42};
    auto                got{ja3::encode_word_buffer(odd_buf, 3)};
    CHECK("" == got);
  }

  SECTION("odd length 5 - tail loop must reject trailing single byte")
  {
    unsigned char const odd_buf[]{0x00, 0x05, 0x00, 0x08, 0x42};
    auto                got{ja3::encode_word_buffer(odd_buf, 5)};
    CHECK("5-8" == got);
  }

  SECTION("supported_groups path: 3-byte extension body, 1-byte tail")
  {
    unsigned char const ext_body[]{0x00, 0x01, 0x02};
    auto                got{ja3::encode_word_buffer(ext_body + 2, 1)};
    CHECK("" == got);
  }
}

TEST_CASE("ja3 integer buffer encoding")
{
  int const buf[]{5, 2570, 8, 56026, 256};

  SECTION("empty buffer")
  {
    auto got{ja3::encode_integer_buffer(nullptr, 0)};
    CHECK("" == got);
  }

  SECTION("1 value")
  {
    auto got{ja3::encode_integer_buffer(buf, 1)};
    CHECK("5" == got);
  }

  SECTION("5 values including GREASE values")
  {
    auto got{ja3::encode_integer_buffer(buf, 5)};
    CHECK("5-8-256" == got);
  }
}

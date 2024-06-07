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

#include "ja3_utils.h"

#define CATCH_CONFIG_MAIN
#include <catch.hpp>

TEST_CASE("ja3 word buffer encoding")
{
  unsigned char const buf[]{0x8, 0x3, 0x4};

  SECTION("empty buffer")
  {
    auto got{ja3::encode_word_buffer(nullptr, 0)};
    CHECK("" == got);
  }

  SECTION("1 value")
  {
    auto got{ja3::encode_word_buffer(buf, 1)};
    CHECK("8" == got);
  }

  SECTION("3 values")
  {
    auto got{ja3::encode_word_buffer(buf, 3)};
    CHECK("8-3-4" == got);
  }
}

TEST_CASE("ja3 dword buffer encoding")
{
  unsigned char const buf[]{0x0, 0x5, 0x0a, 0x0a, 0x0, 0x8, 0xda, 0xda, 0x1, 0x0};

  SECTION("empty buffer")
  {
    auto got{ja3::encode_dword_buffer(nullptr, 0)};
    CHECK("" == got);
  }

  SECTION("1 value")
  {
    auto got{ja3::encode_dword_buffer(buf, 2)};
    CHECK("5" == got);
  }

  SECTION("5 values including GREASE values")
  {
    auto got{ja3::encode_dword_buffer(buf, 10)};
    CHECK("5-8-256" == got);
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

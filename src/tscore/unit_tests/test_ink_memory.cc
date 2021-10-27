/** @file

    ink_memory unit tests.

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

#include <catch.hpp>
#include <cstdint>
#include "tscore/ink_memory.h"

constexpr void
test_can_safely_shift_int8_t()
{
  constexpr int8_t a = 0;
  static_assert(can_safely_shift_left(a, 0) == true, "shifting 0 is safe");
  static_assert(can_safely_shift_left(a, 4) == true, "shifting 0 is safe");
  static_assert(can_safely_shift_left(a, 8) == true, "shifting 0 is safe");

  constexpr int8_t b = 1;
  static_assert(can_safely_shift_left(b, 0) == true, "shifting int8_t 1 0 places is safe");
  static_assert(can_safely_shift_left(b, 1) == true, "shifting int8_t 1 1 places is safe");
  static_assert(can_safely_shift_left(b, 6) == true, "shifting int8_t 1 6 places is safe");
  static_assert(can_safely_shift_left(b, 7) == false, "shifting int8_t 1 7 places becomes negative");
  static_assert(can_safely_shift_left(b, 8) == false, "shifting int8_t 1 8 places overflows");

  constexpr int8_t c = 0xff;
  static_assert(can_safely_shift_left(c, 0) == false, "int8_t 0xff is already negative");
  static_assert(can_safely_shift_left(c, 1) == false, "shifting int8_t 0xff 1 place overflows");
}

constexpr void
test_can_safely_shift_uint8_t()
{
  constexpr uint8_t a = 0;
  static_assert(can_safely_shift_left(a, 0) == true, "shifting 0 is safe");
  static_assert(can_safely_shift_left(a, 4) == true, "shifting 0 is safe");
  static_assert(can_safely_shift_left(a, 8) == true, "shifting 0 is safe");

  constexpr uint8_t b = 1;
  static_assert(can_safely_shift_left(b, 0) == true, "shifting uint8_t 1 0 places is safe");
  static_assert(can_safely_shift_left(b, 1) == true, "shifting uint8_t 1 1 places is safe");
  static_assert(can_safely_shift_left(b, 6) == true, "shifting uint8_t 1 6 places is safe");
  static_assert(can_safely_shift_left(b, 7) == true, "shifting uint8_t 1 7 is safe");
  static_assert(can_safely_shift_left(b, 8) == false, "shifting uint8_t 1 8 places overflows");

  constexpr uint8_t c = 0xff;
  static_assert(can_safely_shift_left(c, 0) == true, "shifting int8_t 0xff 0 places is safe");
  static_assert(can_safely_shift_left(c, 1) == false, "shifting int8_t 0xff 1 place overflows");
}

constexpr void
test_can_safely_shift_int32_t()
{
  constexpr int32_t a = 0;
  static_assert(can_safely_shift_left(a, 4) == true, "shifting 0 is safe");

  constexpr int32_t b = 1;
  static_assert(can_safely_shift_left(b, 4) == true, "shifting 1 is safe");

  constexpr int32_t c = 0x00ff'ffff;
  static_assert(can_safely_shift_left(c, 4) == true, "shifting 0x00ff'ffff is safe");

  constexpr int32_t d = 0x07ff'ffff;
  static_assert(can_safely_shift_left(d, 4) == true, "shifting 0x07ff'ffff is safe");

  constexpr int32_t e = -1;
  static_assert(can_safely_shift_left(e, 4) == false, "shifting -1 will result in truncation");

  constexpr int32_t f = 0x0800'0000;
  static_assert(can_safely_shift_left(f, 4) == false, "shifting 0x0801'0000 will become negative");

  constexpr int32_t g = 0x0fff'ffff;
  static_assert(can_safely_shift_left(g, 4) == false, "shifting 0x0fff'ffff will become negative");

  constexpr int32_t h = 0x1000'0000;
  static_assert(can_safely_shift_left(h, 4) == false, "shifting 0x1000'0000 will overflow");

  constexpr int32_t i = 0xf000'0000;
  static_assert(can_safely_shift_left(i, 4) == false, "shifting 0xf000'0000 will overflow");

  constexpr int32_t j = 0xf800'0000;
  static_assert(can_safely_shift_left(j, 4) == false, "shifting 0xf800'0000 will become negative");
}

constexpr void
test_can_safely_shift_uint32_t()
{
  constexpr uint32_t a = 0;
  static_assert(can_safely_shift_left(a, 4) == true, "shifting 0 is safe");

  constexpr uint32_t b = 1;
  static_assert(can_safely_shift_left(b, 4) == true, "shifting 1 is safe");

  constexpr uint32_t c = 0x00ff'ffff;
  static_assert(can_safely_shift_left(c, 4) == true, "shifting 0x00ff'ffff is safe");

  constexpr uint32_t d = 0x07ff'ffff;
  static_assert(can_safely_shift_left(d, 4) == true, "shifting 0x07ff'ffff is safe");

  constexpr uint32_t e = 0x0800'0000;
  static_assert(can_safely_shift_left(e, 4) == true, "shifting unisgned 0x0800'0000 is safe");

  constexpr uint32_t f = 0x0fff'ffff;
  static_assert(can_safely_shift_left(f, 4) == true, "shifting unsigned 0x0fff'ffff is safe");

  constexpr uint32_t g = 0x1000'0000;
  static_assert(can_safely_shift_left(g, 4) == false, "shifting 0x1000'0000 will overflow");

  constexpr uint32_t h = 0xf000'0000;
  static_assert(can_safely_shift_left(h, 4) == false, "shifting 0xf000'0000 will overflow");

  constexpr uint32_t i = 0xf800'0000;
  static_assert(can_safely_shift_left(i, 4) == false, "shifting 0xf800'0000 will become negative");
}

TEST_CASE("can_safely_shift", "[libts][ink_inet][memory]")
{
  // can_safely_shift_left is a constexpr function, therefore all these checks are
  // done at compile time and REQUIRES calls are not necessary.
  test_can_safely_shift_int8_t();
  test_can_safely_shift_uint8_t();
  test_can_safely_shift_int32_t();
  test_can_safely_shift_uint32_t();
}

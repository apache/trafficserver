/** @file

    Unit tests for IntStr.h.

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

#include "IntStr.h"
#include "IntStr.h"

#include <cstdio>
#include <cstdint>
#include <cinttypes>

#include "catch.hpp"

#include "string_view.h"

// First validate IntStr::MaxSize (at compile time).

constexpr std::uint64_t
pow10(std::size_t exp)
{
  return exp ? (10 * pow10(exp - 1)) : 1;
}

const std::uint64_t One = 1, Biggest = 0 - One;

// Not too big.
static_assert((Biggest / pow10(ts::IntStr::MaxSize - 1)) > 0, "bad sizing");

// Not too small.
static_assert((Biggest / pow10(ts::IntStr::MaxSize - 1)) < 10, "bad sizing");

// Show that negative 64-bit value fits (with prepended minus sign).
static_assert((One << 63) < pow10(ts::IntStr::MaxSize - 1), "bad sizing");

void
test1(std::uint64_t v)
{
  char buf[2 * ts::IntStr::MaxSize];

  std::sprintf(buf, "%" PRIu64, v);

  REQUIRE(ts::string_view(ts::IntStr(v)) == buf);

  std::int64_t v2 = static_cast<std::int64_t>(v);

  std::sprintf(buf, "%" PRId64, v2);

  REQUIRE(ts::string_view(ts::IntStr(v2)) == buf);
}

TEST_CASE("class IntStr", "[IS]")
{
  std::uint64_t i = 3, last = 0;

  test1(0);

  for (; i > last; last = i, i *= 3) {
    test1(i);
  }
}

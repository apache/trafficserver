/** @file

  Unit tests for ts::memcpy_tolower.

  Runs as part of the standard test_tscore binary so the helper's SIMD
  and scalar paths are exercised by ctest in every build, not just when
  ENABLE_BENCHMARKS is set.

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

#include <catch2/catch_test_macros.hpp>

#include "tscore/ink_memcpy_tolower.h"
#include "tscore/ParseRules.h"

#include <array>
#include <cstdint>
#include <random>
#include <vector>

namespace
{

// Same mixed-case ASCII distribution we use in the benchmark, so the unit
// tests exercise inputs that look like real URL/header bytes.
std::vector<char>
make_mixed_case_ascii(std::size_t n, std::uint64_t seed)
{
  std::mt19937_64   rng(seed);
  std::vector<char> v(n);
  for (std::size_t i = 0; i < n; ++i) {
    auto r = static_cast<unsigned>(rng() & 0x3FU);
    if (r < 26U) {
      v[i] = static_cast<char>('A' + r);
    } else if (r < 52U) {
      v[i] = static_cast<char>('a' + (r - 26U));
    } else {
      static constexpr char kNonAlpha[] = "0123456789-_./:";
      v[i]                              = kNonAlpha[r % (sizeof(kNonAlpha) - 1U)];
    }
  }
  return v;
}

// Byte-at-a-time reference, equivalent to the prior static-inline
// memcpy_tolower in URL.cc. Anything ts::memcpy_tolower produces must match
// this for every input we test.
void
memcpy_tolower_reference(char *d, const char *s, std::size_t n) noexcept
{
  while (n--) {
    *d = ParseRules::ink_tolower(*s);
    ++s;
    ++d;
  }
}

} // namespace

TEST_CASE("ts::memcpy_tolower matches scalar reference", "[ts_memcpy_tolower]")
{
  // Bracket every SIMD body width (16/32/64) with both equal-to and
  // offset-from-multiple lengths so the cascade transitions and the
  // AVX-512BW masked tail are all exercised.
  for (std::size_t sz : std::array<std::size_t, 14>{0, 1, 5, 15, 16, 17, 23, 31, 32, 33, 63, 64, 65, 257}) {
    auto              input = make_mixed_case_ascii(sz, 0xC0FFEE + sz);
    std::vector<char> expected(sz);
    std::vector<char> actual(sz);

    memcpy_tolower_reference(expected.data(), input.data(), sz);
    ts::memcpy_tolower(actual.data(), input.data(), sz);

    CAPTURE(sz);
    REQUIRE(actual == expected);
  }
}

TEST_CASE("ts::memcpy_tolower preserves non-ASCII bytes", "[ts_memcpy_tolower]")
{
  // Every byte value 0..255 should round-trip unchanged unless it is in
  // 'A'..'Z', in which case it should map to 'a'..'z'. Guards against any
  // future "speed-up" that widens the case-fold range past ASCII.
  std::array<unsigned char, 256> input;
  for (std::size_t i = 0; i < 256; ++i) {
    input[i] = static_cast<unsigned char>(i);
  }
  std::array<char, 256> output;
  ts::memcpy_tolower(output.data(), reinterpret_cast<const char *>(input.data()), input.size());

  for (std::size_t i = 0; i < 256; ++i) {
    auto in  = static_cast<unsigned char>(i);
    auto out = static_cast<unsigned char>(output[i]);
    auto exp = (in >= 'A' && in <= 'Z') ? static_cast<unsigned char>(in | 0x20) : in;
    CAPTURE(i);
    REQUIRE(out == exp);
  }
}

TEST_CASE("ts::memcpy_tolower supports in-place (dst == src)", "[ts_memcpy_tolower]")
{
  // In-place use must match what an out-of-place call would have produced.
  // Run across the same boundary sizes as the basic correctness case so the
  // SIMD bodies and the AVX-512BW masked load/store are all exercised
  // in-place.
  for (std::size_t sz : std::array<std::size_t, 14>{0, 1, 5, 15, 16, 17, 23, 31, 32, 33, 63, 64, 65, 257}) {
    auto              input = make_mixed_case_ascii(sz, 0xBADF00D + sz);
    std::vector<char> expected(sz);
    std::vector<char> in_place(input);

    memcpy_tolower_reference(expected.data(), input.data(), sz);
    ts::memcpy_tolower(in_place.data(), in_place.data(), sz);

    CAPTURE(sz);
    REQUIRE(in_place == expected);
  }
}

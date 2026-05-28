/** @file

  Micro benchmark for ts::ascii::tolower_copy against a byte-at-a-time
  scalar loop equivalent to the prior URL.cc::memcpy_tolower definition.

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

#define CATCH_CONFIG_ENABLE_BENCHMARKING

#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/benchmark/catch_optimizer.hpp>

#include "tscore/ink_ascii_tolower.h"
#include "tscore/ParseRules.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <random>
#include <string>
#include <vector>

namespace
{

// Sizes chosen to mirror the URL.cc hot path:
//   4-8B   - common HTTP scheme strings ("http", "https")
//   16-32B - typical host names
//   64-256B - long host names / cache-key segments
//   1024B  - stress the inner loop
constexpr std::array<std::size_t, 8> kSizes{4, 8, 16, 24, 32, 64, 256, 1024};

// Same character distribution we expect from URL/host input: ASCII letters
// (mixed case), digits, and the small set of non-alpha bytes that legitimately
// appear in URLs.
std::vector<char>
make_mixed_case_ascii(std::size_t n, std::uint64_t seed = 0xABCDEFULL)
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

// Mirror of the prior static inline memcpy_tolower() from URL.cc, kept here
// as the baseline the SIMD path is expected to beat.
inline void
tolower_scalar(char *d, const char *s, std::size_t n) noexcept
{
  while (n--) {
    *d = ParseRules::ink_tolower(*s);
    ++s;
    ++d;
  }
}

} // namespace

TEST_CASE("active SIMD configuration", "[tolower][config]")
{
  // Print the configuration so the benchmark output makes the selected
  // implementation obvious.
  std::cout << "ts::ascii::tolower_copy implementation: ";
#if TS_HAS_HIGHWAY_DISPATCH
  std::cout << "Highway runtime dispatch (selects best available target at startup)";
#elif defined(__AVX512BW__)
  std::cout << "compile-time cascade — AVX-512BW (64B body + masked tail, gated at n>=64) + AVX2 + SSE2";
#elif defined(__AVX2__)
  std::cout << "compile-time cascade — AVX2 (32B body) + SSE2 (16B drain)";
#elif defined(__SSE2__)
  std::cout << "compile-time cascade — SSE2 (16B body)";
#elif defined(__ARM_NEON) || defined(__aarch64__)
  std::cout << "compile-time cascade — NEON (16B body)";
#else
  std::cout << "compile-time cascade — scalar only";
#endif
  std::cout << '\n';
  SUCCEED();
}

TEST_CASE("tolower throughput", "[bench][tolower]")
{
  for (std::size_t sz : kSizes) {
    auto              input = make_mixed_case_ascii(sz);
    std::vector<char> output_scalar(sz);
    std::vector<char> output_simd(sz);

    // Catch::Benchmark::keep_memory clobbers the buffer in the compiler's
    // model, forcing it to materialize every byte we wrote. Without this an
    // optimizing compiler can shrink or DCE the inline body's stores past
    // the first element we observed.

    std::string scalar_name = "scalar   " + std::to_string(sz) + "B";
    BENCHMARK(scalar_name.c_str())
    {
      tolower_scalar(output_scalar.data(), input.data(), sz);
      Catch::Benchmark::keep_memory(output_scalar.data());
      return output_scalar[0];
    };

    std::string simd_name = "ts::atc  " + std::to_string(sz) + "B";
    BENCHMARK(simd_name.c_str())
    {
      ts::ascii::tolower_copy(output_simd.data(), input.data(), sz);
      Catch::Benchmark::keep_memory(output_simd.data());
      return output_simd[0];
    };
  }
}

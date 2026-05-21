/** @file

  Throughput benchmark for ats_base64_encode / ats_base64_decode comparing
  the scalar path against the simdutf-backed path.

  Sizes bracket both the scalar↔SIMD crossover thresholds (24 bytes for
  encode, 48 bytes for decode) and the typical caller sizes inside ATS
  (8-byte SnowflakeID, 20-32 byte HMACs, ~200 byte OCSP DER requests,
  larger payloads for ceiling measurements). Correctness is covered by
  src/tscore/unit_tests/test_ink_base64.cc, which runs under ctest in
  every build.

  Catch::Benchmark::keep_memory is used around each call to prevent the
  optimizer from DCE-ing the inlined output buffer writes past the first
  observed byte.

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

#include "tscore/ink_base64.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <random>
#include <string>
#include <vector>

namespace
{

// Sizes chosen to mirror real callers and to bracket the scalar↔SIMD
// crossover.
//   8B   - SnowflakeID (uint64_t)
//   16-48B - HMAC-SHA1/SHA256 and crossover region for encode
//   64-96B - crossover region for decode
//   200B - typical OCSP DER request (RFC6960 caps at 255B encoded)
//   512B / 4KB - stress the inner loop where SIMD wins most
constexpr std::array<size_t, 10> kPayloadSizes{8, 16, 24, 32, 48, 64, 96, 200, 512, 4096};

std::vector<unsigned char>
make_random_bytes(size_t n, uint64_t seed = 0xC0FFEEULL)
{
  std::mt19937_64            rng(seed);
  std::vector<unsigned char> v(n);
  for (size_t i = 0; i < n; ++i) {
    v[i] = static_cast<unsigned char>(rng() & 0xFFU);
  }
  return v;
}

std::string
encode_with_ats(const std::vector<unsigned char> &in)
{
  std::string out;
  out.resize(ats_base64_encode_dstlen(in.size()));
  size_t n  = 0;
  bool   ok = ats_base64_encode(in.data(), in.size(), out.data(), out.size(), &n);
  REQUIRE(ok);
  out.resize(n);
  return out;
}

} // namespace

TEST_CASE("active base64 configuration", "[base64][config]")
{
  // Print whether simdutf is wired in so the benchmark output makes the
  // selected configuration obvious.
  std::cout << "ats_base64 compiled with: ";
#if TS_USE_SIMDUTF
  std::cout << "simdutf hybrid (scalar <= 24/48B, simdutf above)";
#else
  std::cout << "scalar only";
#endif
  std::cout << '\n';
  SUCCEED();
}

TEST_CASE("ats_base64_encode throughput", "[bench][base64][encode]")
{
  for (size_t sz : kPayloadSizes) {
    auto              input = make_random_bytes(sz);
    std::vector<char> output(ats_base64_encode_dstlen(sz) + 16);

    std::string name = "encode " + std::to_string(sz) + "B";
    BENCHMARK(name.c_str())
    {
      size_t out_len = 0;
      bool   ok      = ats_base64_encode(input.data(), input.size(), output.data(), output.size(), &out_len);
      Catch::Benchmark::keep_memory(output.data());
      return ok ? out_len : size_t{0};
    };
  }
}

TEST_CASE("ats_base64_decode throughput", "[bench][base64][decode]")
{
  for (size_t sz : kPayloadSizes) {
    auto                       input   = make_random_bytes(sz);
    auto                       encoded = encode_with_ats(input);
    std::vector<unsigned char> output(ats_base64_decode_dstlen(encoded.size()) + 16);

    // Name reports the *plaintext* size so it lines up with the encode bench.
    std::string name = "decode " + std::to_string(sz) + "B (" + std::to_string(encoded.size()) + "B b64)";
    BENCHMARK(name.c_str())
    {
      size_t out_len = 0;
      bool   ok      = ats_base64_decode(encoded.data(), encoded.size(), output.data(), output.size(), &out_len);
      Catch::Benchmark::keep_memory(output.data());
      return ok ? out_len : size_t{0};
    };
  }
}

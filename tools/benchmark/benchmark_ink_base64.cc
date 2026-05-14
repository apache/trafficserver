/** @file

  Micro benchmark for ats_base64_encode / ats_base64_decode and the bulk
  scalar tolower path used by URL canonicalization. Establishes a baseline
  prior to any SIMD work.

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

#include "tscore/ink_base64.h"
#include "tscore/ParseRules.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace
{

// Sizes chosen to mirror real callers and to bracket the scalar↔SIMD
// crossover.
//   8B   - SnowflakeID (uint64_t)
//   16-48B - HMAC-SHA1/SHA256 and crossover region for encode
//   64-128B - crossover region for decode
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

std::vector<char>
make_mixed_case_ascii(size_t n, uint64_t seed = 0xABCDEFULL)
{
  std::mt19937_64   rng(seed);
  std::vector<char> v(n);
  for (size_t i = 0; i < n; ++i) {
    // Mix of uppercase, lowercase, and a few non-letter bytes that should
    // pass through tolower unchanged. Models a URL/header byte stream.
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

// Equivalent of the static inline memcpy_tolower() in src/proxy/hdrs/URL.cc.
// Reproduced here because that definition has internal linkage and isn't
// reachable from this TU.
inline void
memcpy_tolower_scalar(char *d, const char *s, int n)
{
  while (n--) {
    *d = ParseRules::ink_tolower(*s);
    ++s;
    ++d;
  }
}

} // namespace

TEST_CASE("ats_base64 round-trip correctness", "[base64][correctness]")
{
  for (size_t sz : kPayloadSizes) {
    auto                       input   = make_random_bytes(sz);
    auto                       encoded = encode_with_ats(input);
    std::vector<unsigned char> decoded(ats_base64_decode_dstlen(encoded.size()) + 1);
    size_t                     dec_len = 0;
    REQUIRE(ats_base64_decode(encoded.data(), encoded.size(), decoded.data(), decoded.size(), &dec_len));
    REQUIRE(dec_len == sz);
    REQUIRE(std::memcmp(decoded.data(), input.data(), sz) == 0);
  }
}

// Lock the same byte-exact fixture used by InkAPITest's SDK_API_ENCODING
// regression test. Any future implementation swap must keep this passing.
TEST_CASE("ats_base64 InkAPITest fixture", "[base64][correctness][fixture]")
{
  const char *url = "http://www.example.com/foo?fie= \"#%<>[]\\^`{}~&bar={test}&fum=Apache Traffic Server";
  const char *url_b64 =
    "aHR0cDovL3d3dy5leGFtcGxlLmNvbS9mb28/ZmllPSAiIyU8PltdXF5ge31+JmJhcj17dGVzdH0mZnVtPUFwYWNoZSBUcmFmZmljIFNlcnZlcg==";
  const auto url_len     = std::strlen(url);
  const auto url_b64_len = std::strlen(url_b64);

  SECTION("encode produces byte-identical RFC1521 output with '=' padding")
  {
    std::array<char, 1024> buf{};
    size_t                 enc_len = 0;
    REQUIRE(ats_base64_encode(url, url_len, buf.data(), buf.size(), &enc_len));
    REQUIRE(enc_len == url_b64_len);
    REQUIRE(std::strcmp(buf.data(), url_b64) == 0);
  }

  SECTION("decode reproduces the original byte-for-byte")
  {
    std::array<char, 1024> buf{};
    size_t                 dec_len = 0;
    REQUIRE(ats_base64_decode(url_b64, url_b64_len, reinterpret_cast<unsigned char *>(buf.data()), buf.size(), &dec_len));
    REQUIRE(dec_len == url_len);
    REQUIRE(std::strcmp(buf.data(), url) == 0);
  }
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
      // Return a value that depends on the work to prevent DCE.
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
      return ok ? out_len : size_t{0};
    };
  }
}

TEST_CASE("memcpy_tolower throughput", "[bench][tolower]")
{
  // Sizes chosen to model URL paths / header names / cache-key segments.
  constexpr std::array<size_t, 4> kTolowerSizes{16, 64, 256, 1024};

  for (size_t sz : kTolowerSizes) {
    auto              input = make_mixed_case_ascii(sz);
    std::vector<char> output(sz);

    std::string name = "tolower " + std::to_string(sz) + "B";
    BENCHMARK(name.c_str())
    {
      memcpy_tolower_scalar(output.data(), input.data(), static_cast<int>(sz));
      return output[0];
    };
  }
}

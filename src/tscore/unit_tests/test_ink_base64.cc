/** @file

  Unit tests for ats_base64_encode / ats_base64_decode.

  These run in both build configurations. With ENABLE_HIGHWAY_DISPATCH off,
  the public entry points are the scalar path and these checks pin its
  behavior. With it on, large inputs take the Highway SIMD path and every
  check below becomes a byte-for-byte parity test of SIMD vs. the scalar
  primitives (the oracle), which is why the sizes deliberately straddle the
  SIMD thresholds and run up to several KB.

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

#include <cstddef> // size_t, used by ink_base64.h

#include <tscore/ink_base64.h>

#include "../ink_base64_scalar.h" // scalar oracle: encode_scalar / decode_scalar / count_alphabet_prefix

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace
{
// Deterministic pseudo-random bytes (fixed seed -> reproducible).
std::string
prng_bytes(size_t n, uint32_t seed)
{
  std::mt19937                            rng(seed);
  std::uniform_int_distribution<unsigned> d(0, 255);
  std::string                             s(n, '\0');
  for (auto &c : s) {
    c = static_cast<char>(d(rng));
  }
  return s;
}

// Encode via the scalar oracle.
std::string
oracle_encode(const std::string &bin)
{
  std::vector<char> out(ats_base64_encode_dstlen(bin.size()) + 1, '\xCC');
  size_t            n = 0;
  ats::base64::encode_scalar(reinterpret_cast<const unsigned char *>(bin.data()), bin.size(), out.data(), &n);
  return std::string(out.data(), n);
}

// Decode via the scalar oracle (count alphabet prefix, then decode it).
std::string
oracle_decode(const std::string &b64)
{
  const size_t               valid = ats::base64::count_alphabet_prefix(b64.data(), b64.size());
  std::vector<unsigned char> out(ats_base64_decode_dstlen(b64.size()) + 1, 0xCC);
  size_t                     n = 0;
  ats::base64::decode_scalar(b64.data(), valid, out.data(), &n);
  return std::string(reinterpret_cast<char *>(out.data()), n);
}

// Encode via the public entry point (SIMD when enabled).
std::string
public_encode(const std::string &bin)
{
  std::vector<char> out(ats_base64_encode_dstlen(bin.size()) + 1, '\xDD');
  size_t            n  = 0;
  bool              ok = ats_base64_encode(bin.data(), bin.size(), out.data(), out.size(), &n);
  REQUIRE(ok);
  REQUIRE(out[n] == '\0'); // trailing NUL contract
  return std::string(out.data(), n);
}

// Decode via the public entry point (SIMD when enabled).
std::string
public_decode(const std::string &b64)
{
  std::vector<unsigned char> out(ats_base64_decode_dstlen(b64.size()) + 1, 0xDD);
  size_t                     n  = 0;
  bool                       ok = ats_base64_decode(b64.data(), b64.size(), out.data(), out.size(), &n);
  REQUIRE(ok);
  REQUIRE(out[n] == '\0');
  return std::string(reinterpret_cast<char *>(out.data()), n);
}

const std::string kStd = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
const std::string kUrl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
} // namespace

TEST_CASE("ats_base64 known vectors", "[base64]")
{
  // RFC 4648 §10 test vectors.
  CHECK(public_encode("") == "");
  CHECK(public_encode("f") == "Zg==");
  CHECK(public_encode("fo") == "Zm8=");
  CHECK(public_encode("foo") == "Zm9v");
  CHECK(public_encode("foob") == "Zm9vYg==");
  CHECK(public_encode("fooba") == "Zm9vYmE=");
  CHECK(public_encode("foobar") == "Zm9vYmFy");

  CHECK(public_decode("Zg==") == "f");
  CHECK(public_decode("Zm8=") == "fo");
  CHECK(public_decode("Zm9vYmFy") == "foobar");
}

TEST_CASE("ats_base64 encode parity and round-trip across sizes", "[base64]")
{
  // Straddle the SIMD thresholds (encode 24, decode 32) and go well past them.
  std::vector<size_t> sizes;
  for (size_t n = 0; n <= 96; ++n) {
    sizes.push_back(n);
  }
  for (size_t n : {100u, 127u, 128u, 129u, 200u, 255u, 256u, 511u, 512u, 1000u, 4096u, 4099u}) {
    sizes.push_back(n);
  }

  for (size_t n : sizes) {
    const std::string bin = prng_bytes(n, static_cast<uint32_t>(n * 2654435761u + 1));

    const std::string enc_pub = public_encode(bin);
    const std::string enc_ref = oracle_encode(bin);
    INFO("size=" << n);
    CHECK(enc_pub == enc_ref);                               // SIMD encode == scalar encode
    CHECK(public_decode(enc_pub) == bin);                    // round-trips
    CHECK(public_decode(enc_pub) == oracle_decode(enc_pub)); // SIMD decode == scalar decode
  }
}

TEST_CASE("ats_base64 decode parity for standard and URL-safe alphabets", "[base64]")
{
  for (const std::string *alpha : {&kStd, &kUrl}) {
    std::mt19937                       rng(0xBEEF);
    std::uniform_int_distribution<int> pick(0, 63);
    for (size_t n = 0; n <= 300; ++n) {
      std::string s;
      s.reserve(n);
      for (size_t k = 0; k < n; ++k) {
        s.push_back((*alpha)[pick(rng)]);
      }
      INFO("alphabet=" << (alpha == &kUrl ? "url" : "std") << " len=" << n);
      CHECK(public_decode(s) == oracle_decode(s));
    }
  }

  // Both alphabets mixed within one input must decode identically to scalar.
  const std::string mixed = "QWxhZGRpbjpvcGVuIHNlc2FtZQ"
                            "-_+/"
                            "QUJDREVGabcdef0123456789";
  CHECK(public_decode(mixed) == oracle_decode(mixed));
}

TEST_CASE("ats_base64 decode truncates at first non-alphabet byte", "[base64]")
{
  // A non-alphabet byte (whitespace, '=', or garbage) ends the input; the
  // public path must match the scalar oracle exactly, including the SIMD path.
  const char *terminators = " \t\n\r=*@";

  for (size_t len : {1u, 4u, 7u, 16u, 17u, 31u, 32u, 33u, 48u, 63u, 64u, 65u, 96u, 128u, 200u}) {
    std::string base;
    for (size_t k = 0; k < len; ++k) {
      base.push_back(kStd[(k * 7 + 3) % 64]);
    }
    for (size_t pos = 0; pos < len; ++pos) {
      for (const char *t = terminators; *t; ++t) {
        std::string s = base;
        s[pos]        = *t;
        INFO("len=" << len << " pos=" << pos << " term=" << int(*t));
        CHECK(public_decode(s) == oracle_decode(s));
      }
    }
  }
}

TEST_CASE("ats_base64_decode supports in-place (dst == src)", "[base64]")
{
  for (size_t n : {0u, 1u, 2u, 3u, 10u, 33u, 48u, 64u, 100u, 257u, 1000u}) {
    const std::string bin = prng_bytes(n, static_cast<uint32_t>(n + 7));
    const std::string enc = oracle_encode(bin);

    // Decode into a separate buffer for the expected result.
    const std::string expect = public_decode(enc);

    // Decode in place: output overwrites the input buffer.
    std::vector<char> buf(std::max(enc.size(), ats_base64_decode_dstlen(enc.size())) + 1, '\0');
    std::copy(enc.begin(), enc.end(), buf.begin());
    size_t n_out = 0;
    bool   ok    = ats_base64_decode(buf.data(), enc.size(), reinterpret_cast<unsigned char *>(buf.data()), buf.size(), &n_out);
    INFO("size=" << n);
    CHECK(ok);
    CHECK(std::string(buf.data(), n_out) == expect);
  }
}

TEST_CASE("ats_base64 rejects undersized output buffers", "[base64]")
{
  const std::string bin = "hello world, base64";
  const std::string enc = oracle_encode(bin);

  size_t            n = 0;
  std::vector<char> small_enc(ats_base64_encode_dstlen(bin.size()) - 1);
  CHECK_FALSE(ats_base64_encode(bin.data(), bin.size(), small_enc.data(), small_enc.size(), &n));

  std::vector<unsigned char> small_dec(ats_base64_decode_dstlen(enc.size()) - 1);
  CHECK_FALSE(ats_base64_decode(enc.data(), enc.size(), small_dec.data(), small_dec.size(), &n));

  // Exactly the required size must succeed.
  std::vector<char> ok_enc(ats_base64_encode_dstlen(bin.size()));
  CHECK(ats_base64_encode(bin.data(), bin.size(), ok_enc.data(), ok_enc.size(), &n));
}

/** @file

  Unit tests for ats_base64_encode / ats_base64_decode.

  Runs as part of the standard test_tscore binary so the scalar and
  simdutf decode paths are exercised by ctest in every build, not just
  when ENABLE_BENCHMARKS is set. The scenarios bracket the SIMD
  crossover thresholds (24 bytes for encode, 48 bytes for decode) so
  that any future divergence between the two implementations is caught.

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

#include "tscore/ink_base64.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace
{

std::vector<unsigned char>
make_random_bytes(std::size_t n, std::uint64_t seed = 0xC0FFEEULL)
{
  std::mt19937_64            rng(seed);
  std::vector<unsigned char> v(n);
  for (std::size_t i = 0; i < n; ++i) {
    v[i] = static_cast<unsigned char>(rng() & 0xFFU);
  }
  return v;
}

std::string
encode_with_ats(const std::vector<unsigned char> &in)
{
  std::string out;
  out.resize(ats_base64_encode_dstlen(in.size()));
  std::size_t n  = 0;
  bool        ok = ats_base64_encode(in.data(), in.size(), out.data(), out.size(), &n);
  REQUIRE(ok);
  out.resize(n);
  return out;
}

} // namespace

// Random round-trip across sizes that bracket both the encode threshold (24)
// and the decode threshold (48), so the scalar and simdutf paths are both
// exercised every run.
TEST_CASE("ats_base64 round-trip across SIMD thresholds", "[ats_base64]")
{
  for (std::size_t sz : std::array<std::size_t, 10>{0, 1, 8, 23, 24, 25, 47, 48, 49, 4096}) {
    auto                       input   = make_random_bytes(sz, 0xC0FFEE + sz);
    auto                       encoded = encode_with_ats(input);
    std::vector<unsigned char> decoded(ats_base64_decode_dstlen(encoded.size()) + 1);
    std::size_t                dec_len = 0;

    CAPTURE(sz);
    REQUIRE(ats_base64_decode(encoded.data(), encoded.size(), decoded.data(), decoded.size(), &dec_len));
    REQUIRE(dec_len == sz);
    REQUIRE(std::memcmp(decoded.data(), input.data(), sz) == 0);
  }
}

// Byte-exact fixture taken from InkAPITest's SDK_API_ENCODING regression
// test. Any future implementation swap must keep this passing.
TEST_CASE("ats_base64 InkAPITest fixture", "[ats_base64][fixture]")
{
  const char *url = "http://www.example.com/foo?fie= \"#%<>[]\\^`{}~&bar={test}&fum=Apache Traffic Server";
  const char *url_b64 =
    "aHR0cDovL3d3dy5leGFtcGxlLmNvbS9mb28/ZmllPSAiIyU8PltdXF5ge31+JmJhcj17dGVzdH0mZnVtPUFwYWNoZSBUcmFmZmljIFNlcnZlcg==";
  const auto url_len     = std::strlen(url);
  const auto url_b64_len = std::strlen(url_b64);

  SECTION("encode produces byte-identical RFC1521 output with '=' padding")
  {
    std::array<char, 1024> buf{};
    std::size_t            enc_len = 0;
    REQUIRE(ats_base64_encode(url, url_len, buf.data(), buf.size(), &enc_len));
    REQUIRE(enc_len == url_b64_len);
    REQUIRE(std::strcmp(buf.data(), url_b64) == 0);
  }

  SECTION("decode reproduces the original byte-for-byte")
  {
    std::array<char, 1024> buf{};
    std::size_t            dec_len = 0;
    REQUIRE(ats_base64_decode(url_b64, url_b64_len, reinterpret_cast<unsigned char *>(buf.data()), buf.size(), &dec_len));
    REQUIRE(dec_len == url_len);
    REQUIRE(std::strcmp(buf.data(), url) == 0);
  }
}

// The decoder accepts the URL-safe alphabet ('-' for '+', '_' for '/') in
// the same call as standard input. Long enough to exercise the simdutf path.
TEST_CASE("ats_base64_decode accepts URL-safe alphabet", "[ats_base64]")
{
  // Decode the same payload twice, once with standard '+/' and once with
  // URL-safe '-_', and require identical output. The 0xfb/0xbf/0xff pattern
  // produces '+' and '/' in the encoded form, so the URL-safe substitution
  // actually changes bytes.
  const std::vector<unsigned char> payload = {
    0xfb, 0xff, 0xbf, 0xff, 0xfe, 0xbf, 0xfb, 0xff, 0xbf, 0xff, 0xfe, 0xbf, 0xfb, 0xff, 0xbf, 0xff,
    0xfe, 0xbf, 0xfb, 0xff, 0xbf, 0xff, 0xfe, 0xbf, 0xfb, 0xff, 0xbf, 0xff, 0xfe, 0xbf, 0xfb, 0xff,
    0xbf, 0xff, 0xfe, 0xbf, 0xfb, 0xff, 0xbf, 0xff, 0xfe, 0xbf, 0xfb, 0xff, 0xbf, 0xff, 0xfe, 0xbf,
  };

  std::string standard = encode_with_ats(payload);
  REQUIRE(standard.size() > 48); // encoded form must cross BASE64_DECODE_SIMD_THRESHOLD
  std::string url_safe = standard;
  for (auto &c : url_safe) {
    if (c == '+') {
      c = '-';
    } else if (c == '/') {
      c = '_';
    }
  }
  REQUIRE(url_safe != standard); // payload chosen so the swap actually changes bytes

  std::vector<unsigned char> out_std(ats_base64_decode_dstlen(standard.size()) + 1);
  std::vector<unsigned char> out_url(ats_base64_decode_dstlen(url_safe.size()) + 1);
  std::size_t                len_std = 0;
  std::size_t                len_url = 0;
  REQUIRE(ats_base64_decode(standard.data(), standard.size(), out_std.data(), out_std.size(), &len_std));
  REQUIRE(ats_base64_decode(url_safe.data(), url_safe.size(), out_url.data(), out_url.size(), &len_url));

  REQUIRE(len_std == payload.size());
  REQUIRE(len_url == payload.size());
  REQUIRE(std::memcmp(out_std.data(), payload.data(), payload.size()) == 0);
  REQUIRE(std::memcmp(out_url.data(), payload.data(), payload.size()) == 0);
}

// In-place decode (dst == src) must produce the same result as decoding into
// a separate buffer. Used by plugins/experimental/magick.
TEST_CASE("ats_base64_decode supports in-place (dst == src)", "[ats_base64]")
{
  for (std::size_t sz : std::array<std::size_t, 6>{1, 16, 24, 47, 48, 200}) {
    auto              input    = make_random_bytes(sz, 0xBADF00D + sz);
    std::string       encoded  = encode_with_ats(input);
    const std::size_t enc_size = encoded.size();
    // The in-place buffer must hold both the encoded input AND the trailing
    // NUL the decoder writes; encoded.size() is always >= the decoded size,
    // so one extra byte for the NUL is enough.
    std::string in_place = encoded;
    in_place.resize(enc_size + 1);

    std::vector<unsigned char> reference(ats_base64_decode_dstlen(encoded.size()) + 1);
    std::size_t                ref_len = 0;
    std::size_t                ip_len  = 0;

    REQUIRE(ats_base64_decode(encoded.data(), enc_size, reference.data(), reference.size(), &ref_len));
    REQUIRE(
      ats_base64_decode(in_place.data(), enc_size, reinterpret_cast<unsigned char *>(in_place.data()), in_place.size(), &ip_len));

    CAPTURE(sz);
    REQUIRE(ip_len == ref_len);
    REQUIRE(std::memcmp(in_place.data(), reference.data(), ref_len) == 0);
  }
}

// A non-alphabet byte mid-input truncates: the decoder must stop at the
// first such byte and return the bytes decoded up to that point. This was
// the documented behavior of the legacy scalar path and the simdutf wrapper
// preserves it by pre-scanning the input.
TEST_CASE("ats_base64_decode truncates at first non-alphabet byte", "[ats_base64]")
{
  // 28 alphabet bytes then a stop byte then more alphabet (we expect the
  // tail to be ignored). 28 chars decode to 21 bytes. Length is below the
  // simdutf threshold so this exercises the scalar path.
  const char *input = "AAAAAAAAAAAAAAAAAAAAAAAAAAAA!!!IGNORED-TAIL-IGNORED-TAIL";

  std::array<unsigned char, 256> out{};
  std::size_t                    len = 0;
  REQUIRE(ats_base64_decode(input, std::strlen(input), out.data(), out.size(), &len));
  REQUIRE(len == 21);
  for (std::size_t i = 0; i < len; ++i) {
    REQUIRE(out[i] == 0); // 'A' = base64 index 0, so 28 'A's decode to 21 zero bytes
  }
}

// Whitespace in the input should be treated like any other non-alphabet
// byte: the decoder stops at it. This is the property that keeps the SIMD
// and scalar paths aligned regardless of input length, since simdutf would
// otherwise silently skip whitespace and continue.
TEST_CASE("ats_base64_decode stops at ASCII whitespace", "[ats_base64]")
{
  // Construct an input long enough to cross the simdutf threshold so we
  // exercise the wrapper's pre-scan, with a tab byte planted mid-buffer.
  std::string input;
  input.assign(60, 'A'); // 60 'A's -> 45 zero bytes if fully decoded
  input[40] = '\t';      // first whitespace at index 40 -> 30 bytes after truncation

  std::array<unsigned char, 256> out{};
  std::size_t                    len = 0;
  REQUIRE(ats_base64_decode(input.data(), input.size(), out.data(), out.size(), &len));
  REQUIRE(len == 30);
}

// 1, 2, and 3 base64 chars decode to 0, 1, and 2 bytes respectively. Previous
// code had OOB reads in this path; this case guards against a regression.
TEST_CASE("ats_base64_decode handles very short alphabet inputs", "[ats_base64]")
{
  std::array<unsigned char, 16> out{};
  std::size_t                   len = 0;

  // 1 alphabet char: encodes nothing meaningful, decoded length is 0.
  REQUIRE(ats_base64_decode("A", 1, out.data(), out.size(), &len));
  REQUIRE(len == 0);

  // 2 alphabet chars: decoded length is 1.
  REQUIRE(ats_base64_decode("AA", 2, out.data(), out.size(), &len));
  REQUIRE(len == 1);
  REQUIRE(out[0] == 0);

  // 3 alphabet chars: decoded length is 2.
  REQUIRE(ats_base64_decode("AAA", 3, out.data(), out.size(), &len));
  REQUIRE(len == 2);
  REQUIRE(out[0] == 0);
  REQUIRE(out[1] == 0);
}

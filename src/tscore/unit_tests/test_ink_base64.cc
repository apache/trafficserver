/** @file

  Unit tests for ats_base64_encode / ats_base64_decode.

  Includes a regression test for the out-of-bounds read that occurred when the
  decodable prefix length was not a multiple of four. The decode helper places
  the input in an exact-size heap buffer so that, under AddressSanitizer, any
  read past the input (as the old decoder did) is caught.

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

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace
{
// Decode `b64` with the input held in an EXACT-size heap buffer, so a read
// past the input trips AddressSanitizer's red zone (this is what the
// out-of-bounds bug did for prefixes whose length is not a multiple of four).
std::string
decode_tight(const std::string &b64)
{
  std::vector<char> in(b64.size() ? b64.size() : 1);
  if (!b64.empty()) {
    std::memcpy(in.data(), b64.data(), b64.size());
  }

  std::vector<unsigned char> out(ats_base64_decode_dstlen(b64.size()) + 1, 0xCC);
  size_t                     n  = 0;
  bool                       ok = ats_base64_decode(in.data(), b64.size(), out.data(), out.size(), &n);
  REQUIRE(ok);
  REQUIRE(out[n] == '\0'); // trailing NUL contract
  return std::string(reinterpret_cast<char *>(out.data()), n);
}

std::string
encode(const std::string &bin)
{
  std::vector<char> out(ats_base64_encode_dstlen(bin.size()) + 1, '\0');
  size_t            n  = 0;
  bool              ok = ats_base64_encode(bin.data(), bin.size(), out.data(), out.size(), &n);
  REQUIRE(ok);
  REQUIRE(out[n] == '\0');
  return std::string(out.data(), n);
}

const std::string kAlpha = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
} // namespace

TEST_CASE("ats_base64 known vectors", "[base64]")
{
  // RFC 4648 §10.
  CHECK(encode("") == "");
  CHECK(encode("f") == "Zg==");
  CHECK(encode("fo") == "Zm8=");
  CHECK(encode("foo") == "Zm9v");
  CHECK(encode("foob") == "Zm9vYg==");
  CHECK(encode("fooba") == "Zm9vYmE=");
  CHECK(encode("foobar") == "Zm9vYmFy");

  CHECK(decode_tight("Zm9vYmFy") == "foobar");
}

TEST_CASE("ats_base64_decode does not read past a non-4-aligned prefix", "[base64]")
{
  // Regression: a decodable prefix whose length is not a multiple of four made
  // the old decoder run an extra loop iteration past the prefix (out of bounds
  // of the input when the buffer ends there) and read inBuffer[-2]. These
  // unpadded inputs exercise prefix lengths 2, 3, 6, 7 (i.e. 2 and 3 mod 4);
  // decode_tight runs them in exact-size buffers so ASan would catch any
  // over-read.
  CHECK(decode_tight("") == "");
  CHECK(decode_tight("Zg") == "f");          // 2 chars  -> 1 byte
  CHECK(decode_tight("Zm8") == "fo");        // 3 chars  -> 2 bytes
  CHECK(decode_tight("Zm9v") == "foo");      // 4 chars  -> 3 bytes
  CHECK(decode_tight("Zm9vYg") == "foob");   // 6 chars  -> 4 bytes
  CHECK(decode_tight("Zm9vYmE") == "fooba"); // 7 chars  -> 5 bytes

  // A lone trailing character does not encode a full byte and is dropped.
  CHECK(decode_tight("Zm9vYg==").size() == 4);
  CHECK(decode_tight("Q") == "");

  // Sweep every prefix length (hence every length mod 4) in an exact-size
  // buffer; assert the decoded length matches the RFC formula for that many
  // alphabet characters and rely on ASan for over-read detection.
  for (size_t len = 0; len <= 300; ++len) {
    std::string s;
    s.reserve(len);
    for (size_t k = 0; k < len; ++k) {
      s.push_back(kAlpha[(k * 7 + 3) % 64]);
    }
    const size_t rem      = len % 4;
    const size_t expected = (len / 4) * 3 + (rem ? rem - 1 : 0);
    INFO("len=" << len);
    CHECK(decode_tight(s).size() == expected);
  }
}

TEST_CASE("ats_base64 round-trips across sizes", "[base64]")
{
  std::mt19937                            rng(20240601);
  std::uniform_int_distribution<unsigned> byte(0, 255);

  for (size_t n = 0; n <= 256; ++n) {
    std::string bin(n, '\0');
    for (auto &c : bin) {
      c = static_cast<char>(byte(rng));
    }
    INFO("size=" << n);
    CHECK(decode_tight(encode(bin)) == bin);
  }
}

TEST_CASE("ats_base64_decode supports in-place (dst == src)", "[base64]")
{
  std::mt19937                            rng(7);
  std::uniform_int_distribution<unsigned> byte(0, 255);

  for (size_t n : {0u, 1u, 2u, 3u, 10u, 33u, 48u, 64u, 100u, 257u}) {
    std::string bin(n, '\0');
    for (auto &c : bin) {
      c = static_cast<char>(byte(rng));
    }
    const std::string enc    = encode(bin);
    const std::string expect = decode_tight(enc);

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
  const std::string enc = encode(bin);
  size_t            n   = 0;

  std::vector<char> small_enc(ats_base64_encode_dstlen(bin.size()) - 1);
  CHECK_FALSE(ats_base64_encode(bin.data(), bin.size(), small_enc.data(), small_enc.size(), &n));

  std::vector<unsigned char> small_dec(ats_base64_decode_dstlen(enc.size()) - 1);
  CHECK_FALSE(ats_base64_decode(enc.data(), enc.size(), small_dec.data(), small_dec.size(), &n));
}

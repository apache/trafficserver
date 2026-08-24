/** @file

  Benchmark comparing the two ls-hpack Huffman decoders: the original 4-bit
  FSM decoder (lshpack_dec_huff_decode_full) and the 16-bit table decoder
  (lshpack_dec_huff_decode).

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

#include "ls-hpack/lshpack.h"

#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

#define CATCH_CONFIG_ENABLE_BENCHMARKING
#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

namespace
{

// Representative header field values seen on the wire.
constexpr std::string_view SHORT_VALUE  = "text/css";
constexpr std::string_view MEDIUM_VALUE = "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,*/*;q=0.8";
constexpr std::string_view LONG_VALUE =
  "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/126.0.0.0 Safari/537.36";
constexpr std::string_view CORPUS[] = {
  SHORT_VALUE,
  MEDIUM_VALUE,
  LONG_VALUE,
  "/",
  "gzip, deflate, br, zstd",
  "Mon, 21 Oct 2013 20:13:21 GMT",
  "https://www.example.com/images/2026/06/some-article/hero.webp?w=1200&q=75",
  "max-age=31536000, public, immutable",
  "session=8f14e45fceea167a5a36dedd4bea2543; theme=dark; region=us-west-2; ab_bucket=37",
  "ATS/10.1.0",
};

std::vector<uint8_t>
encode(std::string_view text)
{
  std::vector<uint8_t> encoded(text.size() * 4 + 8);
  int64_t              len = litespeed::lshpack_enc_huff_encode(reinterpret_cast<const uint8_t *>(text.data()), //
                                                                reinterpret_cast<const uint8_t *>(text.data()) + text.size(), encoded.data(),
                                                                static_cast<uint32_t>(encoded.size()));
  REQUIRE(len > 0);
  encoded.resize(len);
  return encoded;
}

} // namespace

TEST_CASE("huffman decode: full vs fast", "[bench][huffman]")
{
  char dst[4096];

  // Sanity: both decoders agree on the corpus.
  for (auto text : CORPUS) {
    auto    encoded = encode(text);
    int64_t full    = litespeed::lshpack_dec_huff_decode_full(encoded.data(), encoded.size(), dst, sizeof(dst));
    REQUIRE(full == static_cast<int64_t>(text.size()));
    REQUIRE(memcmp(dst, text.data(), text.size()) == 0);
    int64_t fast = litespeed::lshpack_dec_huff_decode(encoded.data(), encoded.size(), dst, sizeof(dst));
    REQUIRE(fast == full);
    REQUIRE(memcmp(dst, text.data(), text.size()) == 0);
  }

  auto short_enc  = encode(SHORT_VALUE);
  auto medium_enc = encode(MEDIUM_VALUE);
  auto long_enc   = encode(LONG_VALUE);

  BENCHMARK("full: short (8B)")
  {
    return litespeed::lshpack_dec_huff_decode_full(short_enc.data(), short_enc.size(), dst, sizeof(dst));
  };
  BENCHMARK("fast: short (8B)")
  {
    return litespeed::lshpack_dec_huff_decode(short_enc.data(), short_enc.size(), dst, sizeof(dst));
  };

  BENCHMARK("full: medium (86B)")
  {
    return litespeed::lshpack_dec_huff_decode_full(medium_enc.data(), medium_enc.size(), dst, sizeof(dst));
  };
  BENCHMARK("fast: medium (86B)")
  {
    return litespeed::lshpack_dec_huff_decode(medium_enc.data(), medium_enc.size(), dst, sizeof(dst));
  };

  BENCHMARK("full: long (113B)")
  {
    return litespeed::lshpack_dec_huff_decode_full(long_enc.data(), long_enc.size(), dst, sizeof(dst));
  };
  BENCHMARK("fast: long (113B)")
  {
    return litespeed::lshpack_dec_huff_decode(long_enc.data(), long_enc.size(), dst, sizeof(dst));
  };

  std::vector<std::vector<uint8_t>> corpus_enc;
  size_t                            corpus_bytes = 0;
  for (auto text : CORPUS) {
    corpus_enc.push_back(encode(text));
    corpus_bytes += text.size();
  }
  WARN("corpus: " << corpus_bytes << " decoded bytes per iteration");

  BENCHMARK("full: corpus")
  {
    int64_t acc = 0;
    for (auto const &encoded : corpus_enc) {
      acc += litespeed::lshpack_dec_huff_decode_full(encoded.data(), encoded.size(), dst, sizeof(dst));
    }
    return acc;
  };
  BENCHMARK("fast: corpus")
  {
    int64_t acc = 0;
    for (auto const &encoded : corpus_enc) {
      acc += litespeed::lshpack_dec_huff_decode(encoded.data(), encoded.size(), dst, sizeof(dst));
    }
    return acc;
  };
}

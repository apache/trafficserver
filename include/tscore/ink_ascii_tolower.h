/** @file

  SIMD-accelerated bulk ASCII tolower copy.

  Used on the URL canonicalization fast path for cache-key digests
  (src/proxy/hdrs/URL.cc::url_CryptoHash_get_fast) and any other place
  that needs to fold ASCII to lowercase over a small-to-moderate
  buffer. The scalar byte-at-a-time loop is the bottleneck for hosts
  and schemes long enough to vectorize; for shorter inputs the scalar
  tail handles them with no SIMD overhead.

  Semantics match a byte-at-a-time loop using ParseRules::ink_tolower():

    - Bytes in 'A'..'Z' (0x41..0x5A) have bit 5 set, mapping them to
      'a'..'z'. All other bytes (including 0x80..0xFF) pass through
      unchanged. There is no UTF-8 case folding.

    - In-place use (dst == src) is supported on every path. Each SIMD
      body loads a full block into a register before storing back at
      the same offset, and the AVX-512BW masked tail does masked-load
      / masked-store at the same offset. Partial overlap where
      dst != src but the ranges intersect is not supported.

  Two implementations exist, gated by the ENABLE_HIGHWAY_DISPATCH
  CMake option (TS_HAS_HIGHWAY_DISPATCH at compile time):

    - OFF (default): the compile-time cascade defined below. Selection
      is purely compile-time; no runtime dispatch. Body is fully
      inlined into every caller. Best when the build target is broad
      enough to enable the widest SIMD the production hosts support.

    - ON: an out-of-line dispatched implementation in
      src/tscore/ink_ascii_tolower_dispatch.cc, built against Google
      Highway. The header degenerates to a one-line forward shim. At
      runtime the highest SIMD target supported by the live CPU is
      selected once and cached. Best when the build target is
      intentionally conservative (e.g. -march=westmere) but the
      production CPU fleet is heterogeneous.

  Compile-time cascade (default-off path). Bodies are stacked
  widest-first.

    - AVX-512BW builds: when n >= 64, a 64-byte main loop handles the
      bulk and a single masked load/store finishes any 1..63-byte tail,
      then we return. When n < 64, we fall through to the AVX2 + SSE2
      cascade below so tiny inputs avoid the masked tail's fixed setup
      cost.

    - AVX2 builds: a 32-byte main loop drains to a 16-byte SSE2 step
      and then to a scalar tail of 0..15 bytes.

    - SSE2 / NEON builds: a single 16-byte main loop drains to a
      scalar tail.

    - Other targets: scalar only.

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
#pragma once

#include <cstddef>
#include <cstdint>

#include "tscore/ink_config.h"

#if TS_HAS_HIGHWAY_DISPATCH

namespace ts::ascii
{

// Out-of-line, runtime-dispatched implementation. Defined in
// src/tscore/ink_ascii_tolower_dispatch.cc via Highway HWY_EXPORT.
void tolower_copy_dispatch(char *dst, const char *src, std::size_t n) noexcept;

inline void
tolower_copy(char *dst, const char *src, std::size_t n) noexcept
{
  tolower_copy_dispatch(dst, src, n);
}

inline void
tolower_inplace(char *buf, std::size_t n) noexcept
{
  tolower_copy_dispatch(buf, buf, n);
}

} // namespace ts::ascii

#else // !TS_HAS_HIGHWAY_DISPATCH — compile-time cascade

#if defined(__AVX512BW__) || defined(__AVX2__) || defined(__SSE2__)
#include <immintrin.h>
#elif defined(__ARM_NEON) || defined(__aarch64__)
#include <arm_neon.h>
#endif

namespace ts::ascii
{

inline void
tolower_copy(char *dst, const char *src, std::size_t n) noexcept
{
#if defined(__AVX512BW__)
  // AVX-512BW: 64 bytes per iteration with two key optimizations over the
  // narrower paths:
  //   - _mm512_mask_add_epi8 fuses the "+0x20 where upper" into a single
  //     op (no separate maskz_set1 + or).
  //   - A masked load/store handles the 1..63-byte tail in a single SIMD
  //     pass, so we don't need to cascade to AVX2/SSE2 to drain the
  //     remainder.
  //
  // The masked tail does carry ~7 ns of fixed setup cost, which loses to
  // the cascade on short inputs. Gating the whole block on n >= 64 means
  // tiny inputs fall through to the AVX2/SSE2 path below, where they keep
  // the speedup that path already provides.
  //
  // Adapted from Tony Finch's copytolower64.c (see NOTICE).
  if (n >= 64) {
    const __m512i A_vec = _mm512_set1_epi8('A');
    const __m512i Z_vec = _mm512_set1_epi8('Z');
    const __m512i delta = _mm512_set1_epi8('a' - 'A');
    do {
      __m512i   bytes    = _mm512_loadu_epi8(src);
      __mmask64 is_upper = _mm512_cmpge_epi8_mask(bytes, A_vec) & _mm512_cmple_epi8_mask(bytes, Z_vec);
      _mm512_storeu_epi8(dst, _mm512_mask_add_epi8(bytes, is_upper, bytes, delta));
      src += 64;
      dst += 64;
      n   -= 64;
    } while (n >= 64);
    if (n != 0) {
      auto      len_mask = static_cast<__mmask64>((~0ULL) >> (64 - n));
      __m512i   bytes    = _mm512_maskz_loadu_epi8(len_mask, src);
      __mmask64 is_upper = _mm512_cmpge_epi8_mask(bytes, A_vec) & _mm512_cmple_epi8_mask(bytes, Z_vec);
      _mm512_mask_storeu_epi8(dst, len_mask, _mm512_mask_add_epi8(bytes, is_upper, bytes, delta));
    }
    return;
  }
#endif

#if defined(__AVX2__)
  // 32 bytes per iteration. Same compare-and-OR pattern as SSE2.
  {
    const __m256i a_minus_one = _mm256_set1_epi8('A' - 1);
    const __m256i z_plus_one  = _mm256_set1_epi8('Z' + 1);
    const __m256i bit5        = _mm256_set1_epi8(0x20);
    while (n >= 32) {
      __m256i bytes = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(src));
      __m256i ge_A  = _mm256_cmpgt_epi8(bytes, a_minus_one);
      __m256i le_Z  = _mm256_cmpgt_epi8(z_plus_one, bytes);
      __m256i mask  = _mm256_and_si256(_mm256_and_si256(ge_A, le_Z), bit5);
      _mm256_storeu_si256(reinterpret_cast<__m256i *>(dst), _mm256_or_si256(bytes, mask));
      src += 32;
      dst += 32;
      n   -= 32;
    }
  }
#endif

#if defined(__SSE2__)
  // 16 bytes per iteration. Signed compare works for ASCII A-Z because all
  // letters live below 0x80; high bytes (0x80..0xFF) compare as negative
  // and correctly miss the [A,Z] range so they pass through unchanged.
  {
    const __m128i a_minus_one = _mm_set1_epi8('A' - 1);
    const __m128i z_plus_one  = _mm_set1_epi8('Z' + 1);
    const __m128i bit5        = _mm_set1_epi8(0x20);
    while (n >= 16) {
      __m128i bytes = _mm_loadu_si128(reinterpret_cast<const __m128i *>(src));
      __m128i ge_A  = _mm_cmpgt_epi8(bytes, a_minus_one);
      __m128i le_Z  = _mm_cmpgt_epi8(z_plus_one, bytes);
      __m128i mask  = _mm_and_si128(_mm_and_si128(ge_A, le_Z), bit5);
      _mm_storeu_si128(reinterpret_cast<__m128i *>(dst), _mm_or_si128(bytes, mask));
      src += 16;
      dst += 16;
      n   -= 16;
    }
  }
#elif defined(__ARM_NEON) || defined(__aarch64__)
  // 16 bytes per iteration; unsigned compare available natively.
  {
    const uint8x16_t a_minus_one = vdupq_n_u8('A' - 1);
    const uint8x16_t z_plus_one  = vdupq_n_u8('Z' + 1);
    const uint8x16_t bit5        = vdupq_n_u8(0x20);
    while (n >= 16) {
      uint8x16_t bytes = vld1q_u8(reinterpret_cast<const uint8_t *>(src));
      uint8x16_t ge_A  = vcgtq_u8(bytes, a_minus_one);
      uint8x16_t le_Z  = vcltq_u8(bytes, z_plus_one);
      uint8x16_t mask  = vandq_u8(vandq_u8(ge_A, le_Z), bit5);
      vst1q_u8(reinterpret_cast<uint8_t *>(dst), vorrq_u8(bytes, mask));
      src += 16;
      dst += 16;
      n   -= 16;
    }
  }
#endif

  while (n--) {
    auto c = static_cast<unsigned char>(*src++);
    *dst++ = (c >= 'A' && c <= 'Z') ? static_cast<char>(c | 0x20) : static_cast<char>(c);
  }
}

// Thin sugar over tolower_copy for the in-place case. Makes call sites
// like ts::ascii::tolower_inplace(buf, n) read naturally instead of
// ts::ascii::tolower_copy(buf, buf, n).
inline void
tolower_inplace(char *buf, std::size_t n) noexcept
{
  tolower_copy(buf, buf, n);
}

} // namespace ts::ascii

#endif // TS_HAS_HIGHWAY_DISPATCH

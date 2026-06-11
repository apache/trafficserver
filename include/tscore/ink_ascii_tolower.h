/** @file

  Bulk ASCII tolower copy.

  Used on the URL canonicalization fast path for cache-key digests
  (src/proxy/hdrs/URL.cc::url_CryptoHash_get_fast) and any other place that
  needs to fold ASCII to lowercase over a small-to-moderate buffer.

  Semantics match a byte-at-a-time loop using ParseRules::ink_tolower():

    - Bytes in 'A'..'Z' (0x41..0x5A) are folded to 'a'..'z' (bit 5 set). All
      other bytes (including 0x80..0xFF) pass through unchanged. There is no
      UTF-8 case folding.

    - In-place use (dst == src) is supported. Partial overlap where dst != src
      but the ranges intersect is not supported.

  Two implementations, gated by the ENABLE_HIGHWAY_DISPATCH CMake option
  (TS_HAS_HIGHWAY_DISPATCH at compile time):

    - ON: the runtime-dispatched SIMD implementation in
      src/tscore/ink_ascii_tolower_dispatch.cc, built against Google Highway.
      The highest SIMD target supported by the live CPU is selected once and
      cached, so a conservatively compiled binary still runs the widest body
      its CPU supports. This is the canonical accelerated path.

    - OFF (default): a portable scalar loop, which the compiler auto-vectorizes
      for the build's target. No hand-written intrinsics.

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

namespace ts::ascii
{

#if TS_HAS_HIGHWAY_DISPATCH

// Out-of-line, runtime-dispatched SIMD implementation. Defined in
// src/tscore/ink_ascii_tolower_dispatch.cc via Highway HWY_EXPORT.
void tolower_copy_dispatch(char *dst, const char *src, std::size_t n) noexcept;

inline void
tolower_copy(char *dst, const char *src, std::size_t n) noexcept
{
  tolower_copy_dispatch(dst, src, n);
}

#else // !TS_HAS_HIGHWAY_DISPATCH — portable scalar fallback

inline void
tolower_copy(char *dst, const char *src, std::size_t n) noexcept
{
  // The unsigned (c - 'A') < 26 test is true only for 'A'..'Z'; every other
  // byte (including 0x80..0xFF) wraps to >= 26 and passes through unchanged.
  // The compiler auto-vectorizes this loop for the build's target.
  for (std::size_t i = 0; i < n; ++i) {
    auto c = static_cast<unsigned char>(src[i]);
    dst[i] = (static_cast<unsigned char>(c - 'A') < 26) ? static_cast<char>(c | 0x20) : static_cast<char>(c);
  }
}

#endif // TS_HAS_HIGHWAY_DISPATCH

// Thin sugar over tolower_copy for the in-place case. Makes call sites like
// ts::ascii::tolower_inplace(buf, n) read naturally instead of
// ts::ascii::tolower_copy(buf, buf, n).
inline void
tolower_inplace(char *buf, std::size_t n) noexcept
{
  tolower_copy(buf, buf, n);
}

} // namespace ts::ascii

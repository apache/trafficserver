/** @file

  Runtime-dispatched implementation of ts::ascii::tolower_copy.

  Enabled via ENABLE_HIGHWAY_DISPATCH=ON at build time. When the option
  is off, ink_ascii_tolower.h provides the original compile-time cascade
  and this translation unit is not compiled.

  Why dispatch: when the build target is intentionally conservative
  (e.g. -march=westmere for binary portability), the compile-time
  cascade in ink_ascii_tolower.h is locked to SSE2 and cannot exploit
  AVX2 or AVX-512 on hosts that support them. Highway's foreach_target
  mechanism emits one body per enabled target inside this TU using
  per-function GCC target attributes, regardless of the TU's own
  -march. At runtime, the highest target supported by the live CPU is
  selected and its function pointer is cached for direct reuse. Effect:
  a Westmere-compiled binary still runs the AVX-512 body on Ice Lake.

  Per-call cost vs the fully-inlined cascade: ~1.3 ns flat (one direct
  call to this function, then one indirect call through the cached
  pointer). The trade for that overhead is unlocking the wider vector
  path on bulk inputs, which can be ~2x faster on modern CPUs even from
  a conservative build target.

  Correctness is bit-for-bit identical to the cascade (see the
  test_ink_ascii_tolower parity suite in src/tscore/unit_tests).

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

#include "tscore/ink_ascii_tolower.h"

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "ink_ascii_tolower_dispatch.cc"
#include <hwy/foreach_target.h>
#include <hwy/highway.h>

HWY_BEFORE_NAMESPACE();
namespace ts::ascii
{
namespace HWY_NAMESPACE
{
  namespace hn = hwy::HWY_NAMESPACE;

  // Templated chunk worker, forced inline so the small-N branch below
  // stays a per-call-site dead-code-eliminable check rather than a real
  // jump table. Body is the same as the static-dispatch reference: single
  // compare via the unsigned(v - 'A') < 26 trick, fused masked add via
  // MaskedAddOr (lowers to _mm512_mask_add_epi8 on AVX3; falls back to
  // IfThenElse(Add) on targets without native masked-add), and a
  // LoadN/StoreN masked tail.
  template <class D>
  HWY_ATTR HWY_INLINE void
  tolower_chunk(D d, char *dst, const char *src, std::size_t n)
  {
    using V              = hn::Vec<D>;
    const V           A  = hn::Set(d, static_cast<hn::TFromD<D>>('A'));
    const V           R  = hn::Set(d, static_cast<hn::TFromD<D>>(26));
    const V           B5 = hn::Set(d, static_cast<hn::TFromD<D>>(0x20));
    const std::size_t N  = hn::Lanes(d);

    const auto *in  = reinterpret_cast<const uint8_t *>(src);
    auto       *out = reinterpret_cast<uint8_t *>(dst);

    std::size_t i = 0;
    if (n >= N) {
      for (; i + N <= n; i += N) {
        const V    v        = hn::LoadU(d, in + i);
        const auto is_upper = hn::Lt(hn::Sub(v, A), R);
        const V    folded   = hn::MaskedAddOr(v, is_upper, v, B5);
        hn::StoreU(folded, d, out + i);
      }
    }
    if (i < n) {
      const std::size_t rem      = n - i;
      const V           v        = hn::LoadN(d, in + i, rem);
      const auto        is_upper = hn::Lt(hn::Sub(v, A), R);
      const V           folded   = hn::MaskedAddOr(v, is_upper, v, B5);
      hn::StoreN(folded, d, out + i, rem);
    }
  }

  // Small-N gate. On wide targets (AVX2 = 32 lanes, AVX3 = 64 lanes) the
  // masked LoadN/StoreN on a sub-vector input pays ~10 ns of fixed setup
  // before doing any work. Dropping to a 16-byte CappedTag for those
  // inputs collapses the gate to SSSE3-class ops, which is what the
  // hand-written cascade falls through to anyway. On targets where the
  // ScalableTag is already 16 bytes (SSE2/NEON), both branches lower to
  // the same body and the gate folds out at compile time.
  HWY_ATTR void
  tolower_copy_target(char *dst, const char *src, std::size_t n)
  {
    const hn::ScalableTag<uint8_t> d_full;
    if (n < hn::Lanes(d_full)) {
      const hn::CappedTag<uint8_t, 16> d16;
      tolower_chunk(d16, dst, src, n);
    } else {
      tolower_chunk(d_full, dst, src, n);
    }
  }

} // namespace HWY_NAMESPACE
} // namespace ts::ascii
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace ts::ascii
{

HWY_EXPORT(tolower_copy_target);

// Cached-pointer dispatch. On first call, the lazy-init lambda invokes
// the dispatch table (which patches the entry to point to the chosen
// target) and then captures the now-resolved pointer for direct reuse.
// Thread-safe via C++11 magic-static semantics; runs the resolver once.
// Per-call cost after init is one indirect call through the cached
// pointer.
void
tolower_copy_dispatch(char *dst, const char *src, std::size_t n) noexcept
{
  using tolower_fn_t           = void (*)(char *, const char *, std::size_t);
  static const tolower_fn_t fn = []() noexcept {
    char dummy_dst = 0, dummy_src = 'A';
    HWY_DYNAMIC_DISPATCH(tolower_copy_target)(&dummy_dst, &dummy_src, 1);
    return HWY_DYNAMIC_POINTER(tolower_copy_target);
  }();
  fn(dst, src, n);
}

} // namespace ts::ascii
#endif // HWY_ONCE

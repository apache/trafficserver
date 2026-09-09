/** @file

  Runtime-dispatched SIMD base64 encode/decode, built against Google Highway.

  Enabled by ENABLE_HIGHWAY_DISPATCH=ON. Highway's foreach_target mechanism
  emits one body per enabled SIMD target from this single source; at runtime
  the best target supported by the live CPU is selected once and cached, so a
  conservatively compiled binary (e.g. -march=x86-64) still runs the AVX-512
  body on capable hardware.

  The SIMD math follows the well-known vectorized base64 algorithms popularized
  by Wojciech Muła and Daniel Lemire and used by the simdutf library, expressed
  here in Highway's portable ops (see NOTICE):

    - decode: aqrit's "default_or_url" classifier translates ASCII to 6-bit
      values for the standard and URL-safe alphabets at once and flags any
      non-alphabet byte; the 4x6-bit groups are packed to 3 bytes with two
      pairwise multiply-adds and a shuffle. Validation is fused into the loop:
      only fully-valid 16-byte blocks are consumed by SIMD, and the remainder
      (including any non-alphabet truncation point) is finished by the scalar
      decoder, so output matches the scalar path exactly.

    - encode: the Muła reshuffle splits each 3-byte group into four 6-bit
      fields with one multiply-high and one multiply-low, then a pshufb-based
      table maps 6-bit values to the standard alphabet. The 1-2 byte tail and
      `=` padding are produced by the scalar encoder.

  In-place decode (out == in) is preserved: each block is fully loaded before
  its bounds-safe StoreN, whose end never passes the next block's load.

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

#include "ink_base64_dispatch.h"
#include "ink_base64_scalar.h"

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "ink_base64_dispatch.cc"
#include <hwy/foreach_target.h>
#include <hwy/highway.h>

HWY_BEFORE_NAMESPACE();
namespace ts::base64
{
namespace HWY_NAMESPACE
{
  namespace hn = hwy::HWY_NAMESPACE;

  // per-128-bit-block constants, replicated to any width via LoadDup128
  alignas(16) static constexpr int8_t kMul1[16]  = {0x40, 0x01, 0x40, 0x01, 0x40, 0x01, 0x40, 0x01,
                                                    0x40, 0x01, 0x40, 0x01, 0x40, 0x01, 0x40, 0x01};
  alignas(16) static constexpr int16_t kMul2[8]  = {0x1000, 0x0001, 0x1000, 0x0001, 0x1000, 0x0001, 0x1000, 0x0001};
  alignas(16) static constexpr uint8_t kPack[16] = {2, 1, 0, 6, 5, 4, 10, 9, 8, 14, 13, 12, 0x80, 0x80, 0x80, 0x80};

  // ---- DECODE ----

  // aqrit's "default_or_url" classifier (from simdutf's
  // to_base64_mask<default_or_url=true>): maps ASCII to 6-bit values for both
  // standard (+ /) and URL-safe (- _) bytes in one pass, matching
  // printableToSixBit exactly. The raw check mask flags whitespace as invalid
  // (we omit simdutf's whitespace-XOR correction), giving ATS's truncate-on-
  // non-alphabet semantics. Sets *ok false if any lane is non-alphabet.
  alignas(16) static constexpr uint8_t kDeltaAsso[16]   = {0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
                                                           0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x00, 0x16};
  alignas(16) static constexpr uint8_t kDeltaValues[16] = {0xBF, 0xE0, 0xB9, 0x13, 0x04, 0xBF, 0xBF, 0xB9,
                                                           0xB9, 0x00, 0xFF, 0x11, 0xFF, 0xBF, 0x10, 0xB9};
  alignas(16) static constexpr uint8_t kCheckAsso[16]   = {0x0D, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
                                                           0x01, 0x01, 0x03, 0x07, 0x0B, 0x0E, 0x0B, 0x06};
  alignas(16) static constexpr uint8_t kCheckValues[16] = {0x80, 0x80, 0x80, 0x80, 0xCF, 0xBF, 0xD5, 0xA6,
                                                           0xB5, 0xA1, 0x00, 0x80, 0x00, 0x80, 0x00, 0x80};

  template <class D>
  HWY_ATTR HWY_INLINE hn::Vec<D>
                      classify(D d, hn::Vec<D> v, bool *ok)
  {
    const hn::RebindToSigned<D>        di;
    const hn::Repartition<uint32_t, D> d32;

    const auto shifted    = hn::BitCast(d, hn::ShiftRight<3>(hn::BitCast(d32, v)));
    auto       delta_hash = hn::AverageRound(hn::TableLookupBytes(hn::LoadDup128(d, kDeltaAsso), v), shifted);
    delta_hash            = hn::And(delta_hash, hn::Set(d, 0x0F));
    const auto check_hash = hn::AverageRound(hn::TableLookupBytes(hn::LoadDup128(d, kCheckAsso), v), shifted);

    const auto out =
      hn::SaturatedAdd(hn::BitCast(di, hn::TableLookupBytes(hn::LoadDup128(d, kDeltaValues), delta_hash)), hn::BitCast(di, v));
    const auto chk =
      hn::SaturatedAdd(hn::BitCast(di, hn::TableLookupBytes(hn::LoadDup128(d, kCheckValues), check_hash)), hn::BitCast(di, v));

    *ok = hn::AllFalse(di, hn::Lt(chk, hn::Zero(di))); // chk sign bit set => invalid
    return hn::BitCast(d, out);
  }

  // Decode one 16-char block -> 12 bytes. Returns false without storing if the
  // block holds a non-alphabet byte. The 12 valid output bytes are contiguous
  // at the front, so a bounds-safe StoreN(12) suffices (no overrun).
  HWY_ATTR HWY_INLINE bool
  decode_block16(const char *in, unsigned char *out)
  {
    const hn::Full128<uint8_t>                  d;
    const hn::RebindToSigned<decltype(d)>       d8i;
    const hn::Repartition<int16_t, decltype(d)> d16;
    const hn::Repartition<int32_t, decltype(d)> d32;

    const auto v = hn::LoadU(d, reinterpret_cast<const uint8_t *>(in));
    bool       ok;
    const auto val = classify(d, v, &ok);
    if (!ok) {
      return false;
    }

    const auto mul1   = hn::BitCast(d8i, hn::Load(d, reinterpret_cast<const uint8_t *>(kMul1)));
    const auto t0     = hn::SatWidenMulPairwiseAdd(d16, val, mul1);             // maddubs
    const auto t1     = hn::WidenMulPairwiseAdd(d32, t0, hn::Load(d16, kMul2)); // madd
    const auto packed = hn::TableLookupBytes(hn::BitCast(d, t1), hn::Load(d, kPack));

    hn::StoreN(packed, d, out, 12);
    return true;
  }

  HWY_ATTR void
  DecodeImpl(const char *in, size_t in_len, unsigned char *out, size_t *out_len)
  {
    size_t i = 0, o = 0;
    for (; i + 16 <= in_len; i += 16, o += 12) {
      if (!decode_block16(in + i, out + o)) {
        break;
      }
    }
    // Scalar finishes the remainder: truncate at the first non-alphabet byte
    // then decode 4-groups + a 2/3 char tail (and write the trailing NUL).
    // Identical to running the scalar decoder over the whole alphabet prefix,
    // because the SIMD loop consumed only fully-valid 4-group-aligned blocks.
    size_t tail_len = 0;
    decode_scalar_prefix(in + i, in_len - i, out + o, &tail_len);
    o += tail_len;
    if (out_len) {
      *out_len = o;
    }
  }

  // ---- ENCODE ----

  // 6-bit value (0..63) -> standard-alphabet ASCII, from simdutf's
  // lookup_pshufb_improved (Muła): reduce to a 4-bit class, look up a per-class
  // offset, add. Standard alphabet (+ /), matching encode_scalar.
#define U8(x) static_cast<uint8_t>(x)
  alignas(16) static constexpr uint8_t kShiftLUT[16] = {U8('a' - 26),
                                                        U8('0' - 52),
                                                        U8('0' - 52),
                                                        U8('0' - 52),
                                                        U8('0' - 52),
                                                        U8('0' - 52),
                                                        U8('0' - 52),
                                                        U8('0' - 52),
                                                        U8('0' - 52),
                                                        U8('0' - 52),
                                                        U8('0' - 52),
                                                        U8('+' - 62),
                                                        U8('/' - 63),
                                                        U8('A'),
                                                        0,
                                                        0};
#undef U8

  template <class D>
  HWY_ATTR HWY_INLINE hn::Vec<D>
                      to_ascii(D d, hn::Vec<D> idx)
  {
    auto       res  = hn::SaturatedSub(idx, hn::Set(d, 51)); // 52..63 -> 1..12, else 0
    const auto less = hn::Lt(idx, hn::Set(d, 26));           // 0..25 (uppercase class)
    res             = hn::Or(res, hn::IfThenElseZero(less, hn::Set(d, 13)));
    res             = hn::TableLookupBytes(hn::LoadDup128(d, kShiftLUT), res);
    return hn::Add(res, idx);
  }

  // Encode 12 input bytes per 16-byte block -> 16 ASCII chars. `in` must have
  // >= 16 readable bytes (over-reads bytes 12..15, only 0..11 used). Muła
  // reshuffle: spread the 3 bytes of each group across a 32-bit lane, then split
  // into four 6-bit fields with one mulhi + one mullo (each a pair of per-16-bit
  // variable shifts).
  template <class D>
  HWY_ATTR HWY_INLINE void
  encode_chunk(D d, const unsigned char *in, char *out)
  {
    const hn::Repartition<uint32_t, D> d32;
    const hn::Repartition<uint16_t, D> d16;

    alignas(16) static constexpr uint8_t kSpread[16] = {1, 0, 2, 1, 4, 3, 5, 4, 7, 6, 8, 7, 10, 9, 11, 10};

    const auto in32 = hn::BitCast(d32, hn::TableLookupBytes(hn::LoadU(d, in), hn::LoadDup128(d, kSpread)));

    const auto t0  = hn::And(in32, hn::Set(d32, 0x0fc0fc00u));
    const auto t1  = hn::MulHigh(hn::BitCast(d16, t0), hn::BitCast(d16, hn::Set(d32, 0x04000040u)));
    const auto t2  = hn::And(in32, hn::Set(d32, 0x003f03f0u));
    const auto t3  = hn::Mul(hn::BitCast(d16, t2), hn::BitCast(d16, hn::Set(d32, 0x01000010u)));
    const auto idx = hn::BitCast(d, hn::Or(t1, t3)); // four 6-bit values per 32-bit lane

    hn::StoreU(to_ascii(d, idx), d, reinterpret_cast<uint8_t *>(out));
  }

  HWY_ATTR void
  EncodeImpl(const unsigned char *in, size_t in_len, char *out, size_t *out_len)
  {
    const hn::Full128<uint8_t> d;
    size_t                     i = 0, o = 0;
    while (in_len - i >= 16) {
      encode_chunk(d, in + i, out + o);
      i += 12;
      o += 16;
    }
    size_t tail = 0;
    encode_scalar(in + i, in_len - i, out + o, &tail);
    o += tail;
    if (out_len) {
      *out_len = o;
    }
  }

} // namespace HWY_NAMESPACE
} // namespace ts::base64
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace ts::base64
{
HWY_EXPORT(DecodeImpl);
HWY_EXPORT(EncodeImpl);

void
decode_dispatch(const char *in, size_t in_len, unsigned char *out, size_t *out_len)
{
  HWY_DYNAMIC_DISPATCH(DecodeImpl)(in, in_len, out, out_len);
}

void
encode_dispatch(const unsigned char *in, size_t in_len, char *out, size_t *out_len)
{
  HWY_DYNAMIC_DISPATCH(EncodeImpl)(in, in_len, out, out_len);
}

} // namespace ts::base64
#endif

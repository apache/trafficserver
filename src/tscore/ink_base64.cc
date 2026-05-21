/** @file

  Base64 encoding and decoding.

  The public entry points (`ats_base64_encode` / `ats_base64_decode`, also
  exposed through `TSBase64Encode` / `TSBase64Decode`) dispatch between two
  internal implementations:

    - A hand-rolled scalar path, always present, used directly when
      TS_USE_SIMDUTF is disabled, and used for inputs below the SIMD
      crossover threshold when TS_USE_SIMDUTF is enabled. The scalar path
      avoids simdutf's runtime ISA dispatch and virtual-call overhead,
      which would otherwise dominate the cost for tiny inputs (e.g. the
      8-byte SnowflakeID encode).

    - simdutf, used for larger inputs when TS_USE_SIMDUTF is enabled.
      simdutf provides SIMD-accelerated kernels and is several times
      faster than the scalar path once the input is big enough to amortize
      its per-call overhead.

  Thresholds were chosen empirically on a 2.1 GHz Broadwell-EP Xeon
  (AVX2) using tools/benchmark/benchmark_ink_base64. The exact crossover
  shifts on different cores but lies within an order of magnitude of these
  values everywhere we've measured.

  Both paths preserve the same public contract:

    - encode: standard RFC 1521 alphabet (`+`, `/`), `=` padding, no line
      breaks, trailing NUL written at outBuffer[length].

    - decode: accepts both standard (`+`, `/`) and URL-safe (`-`, `_`)
      alphabets in the same input; tolerates missing padding; on any
      non-alphabet byte (including ASCII whitespace, '=', or garbage),
      truncates and returns success with whatever was decoded up to that
      point; trailing NUL written at outBuffer[length]; supports in-place
      decode (dst == src).

  Decode whitespace alignment: simdutf's forgiving-base64 mode would
  silently skip ASCII whitespace and continue. To keep TSBase64Decode
  results independent of build configuration and input size, the wrapper
  pre-scans the input with the same printableToSixBit table the scalar
  path uses and truncates inBufferSize at the first non-alphabet byte
  before handing it to either implementation. Both paths therefore see
  the same prefix of valid alphabet bytes and produce identical output.

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
#include "tscore/ink_platform.h"
#include "tscore/ink_base64.h"
#include "tscore/ink_assert.h"

#if TS_USE_SIMDUTF
#include <simdutf.h>

// Inputs at or below these byte counts stay on the scalar path, where they
// outrun simdutf's per-call overhead. inBufferSize for encode is the binary
// plaintext length; for decode it is the base64-encoded length.
constexpr size_t BASE64_ENCODE_SIMD_THRESHOLD = 24;
constexpr size_t BASE64_DECODE_SIMD_THRESHOLD = 48;
#endif

namespace
{

/* Converts a printable character to its six bit representation. */
const unsigned char printableToSixBit[256] = {
  64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
  64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 62, 64, 62, 64, 63, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 64, 64, 64, 64, 64, 64,
  64, 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 64, 64, 64, 64, 63,
  64, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 64, 64, 64, 64, 64,
  64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
  64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
  64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
  64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64};

constexpr unsigned char MAX_PRINT_VAL = 63;

inline unsigned char
decode_byte(char c)
{
  return printableToSixBit[static_cast<uint8_t>(c)];
}

// Count the leading base64-alphabet bytes (standard or URL-safe). The result
// is the prefix length that both decode paths actually consume; any byte at
// or after this index is whitespace, '=', or garbage and is dropped.
inline size_t
count_alphabet_prefix(const char *inBuffer, size_t inBufferSize)
{
  size_t valid = 0;
  while (valid < inBufferSize && decode_byte(inBuffer[valid]) <= MAX_PRINT_VAL) {
    ++valid;
  }
  return valid;
}

// Hand-rolled scalar encode. Caller has already validated outBufSize.
void
encode_scalar(const unsigned char *inBuffer, size_t inBufferSize, char *outBuffer, size_t *length)
{
  static const char _codes[66] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  char             *obuf       = outBuffer;
  char              in_tail[4];

  while (inBufferSize > 2) {
    *obuf++ = _codes[(inBuffer[0] >> 2) & 077];
    *obuf++ = _codes[((inBuffer[0] & 03) << 4) | ((inBuffer[1] >> 4) & 017)];
    *obuf++ = _codes[((inBuffer[1] & 017) << 2) | ((inBuffer[2] >> 6) & 017)];
    *obuf++ = _codes[inBuffer[2] & 077];

    inBufferSize -= 3;
    inBuffer     += 3;
  }

  if (inBufferSize == 0) {
    *obuf = '\0';
    if (length) {
      *length = (obuf - outBuffer);
    }
  } else {
    memset(in_tail, 0, sizeof(in_tail));
    memcpy(in_tail, inBuffer, inBufferSize);

    *(obuf)     = _codes[(in_tail[0] >> 2) & 077];
    *(obuf + 1) = _codes[((in_tail[0] & 03) << 4) | ((in_tail[1] >> 4) & 017)];
    *(obuf + 2) = _codes[((in_tail[1] & 017) << 2) | ((in_tail[2] >> 6) & 017)];
    *(obuf + 3) = _codes[in_tail[2] & 077];

    if (inBufferSize == 1) {
      *(obuf + 2) = '=';
    }
    *(obuf + 3) = '=';
    *(obuf + 4) = '\0';

    if (length) {
      *length = (obuf + 4) - outBuffer;
    }
  }
}

// Hand-rolled scalar decode. Caller has pre-scanned: every byte in
// inBuffer[0..inBufferSize) is in the base64 alphabet (decode_byte() returns
// <= 63). The caller has also validated outBufSize.
//
// This restructures the legacy decode tail handling. The previous code ran
// one extra loop iteration past the alphabet prefix when inBufferSize was in
// {1, 2, 3} (reading inBuffer[2..3] which was either OOB to the caller's
// buffer or past the valid prefix) and then read inBuffer[-2] in the trailing
// adjustment block when no loop iterations had advanced inBuffer. Process
// only complete 4-character groups in the main loop and decode any 2- or
// 3-byte tail explicitly; a 1-byte tail encodes nothing meaningful and is
// dropped, matching what an RFC 4648 decoder is supposed to do.
void
decode_scalar(const char *inBuffer, size_t inBufferSize, unsigned char *outBuffer, size_t *length)
{
  size_t         decodedBytes = 0;
  unsigned char *buf          = outBuffer;

  while (inBufferSize >= 4) {
    buf[0]        = static_cast<unsigned char>(decode_byte(inBuffer[0]) << 2 | decode_byte(inBuffer[1]) >> 4);
    buf[1]        = static_cast<unsigned char>(decode_byte(inBuffer[1]) << 4 | decode_byte(inBuffer[2]) >> 2);
    buf[2]        = static_cast<unsigned char>(decode_byte(inBuffer[2]) << 6 | decode_byte(inBuffer[3]));
    buf          += 3;
    inBuffer     += 4;
    decodedBytes += 3;
    inBufferSize -= 4;
  }

  if (inBufferSize >= 2) {
    buf[0]        = static_cast<unsigned char>(decode_byte(inBuffer[0]) << 2 | decode_byte(inBuffer[1]) >> 4);
    decodedBytes += 1;
    if (inBufferSize >= 3) {
      buf[1]        = static_cast<unsigned char>(decode_byte(inBuffer[1]) << 4 | decode_byte(inBuffer[2]) >> 2);
      decodedBytes += 1;
    }
  }

  outBuffer[decodedBytes] = '\0';
  if (length) {
    *length = decodedBytes;
  }
}

} // namespace

bool
ats_base64_encode(const unsigned char *inBuffer, size_t inBufferSize, char *outBuffer, size_t outBufSize, size_t *length)
{
  if (outBufSize < ats_base64_encode_dstlen(inBufferSize)) {
    return false;
  }

#if TS_USE_SIMDUTF
  if (inBufferSize > BASE64_ENCODE_SIMD_THRESHOLD) {
    size_t written     = simdutf::binary_to_base64(reinterpret_cast<const char *>(inBuffer), inBufferSize, outBuffer);
    outBuffer[written] = '\0';
    if (length) {
      *length = written;
    }
    return true;
  }
#endif

  encode_scalar(inBuffer, inBufferSize, outBuffer, length);
  return true;
}

bool
ats_base64_encode(const char *inBuffer, size_t inBufferSize, char *outBuffer, size_t outBufSize, size_t *length)
{
  return ats_base64_encode(reinterpret_cast<const unsigned char *>(inBuffer), inBufferSize, outBuffer, outBufSize, length);
}

bool
ats_base64_decode(const char *inBuffer, size_t inBufferSize, unsigned char *outBuffer, size_t outBufSize, size_t *length)
{
  if (outBufSize < ats_base64_decode_dstlen(inBufferSize)) {
    return false;
  }

  // Truncate to the leading base64-alphabet prefix. Doing this upfront for
  // both paths is what keeps the SIMD and scalar decoders aligned on inputs
  // that contain ASCII whitespace, '=' padding, or any other non-alphabet
  // byte; otherwise simdutf's forgiving mode would skip whitespace and
  // continue while the scalar would have stopped at it.
  const size_t valid = count_alphabet_prefix(inBuffer, inBufferSize);

#if TS_USE_SIMDUTF
  if (valid > BASE64_DECODE_SIMD_THRESHOLD) {
    // Reserve one byte for the trailing NUL we always emit. The input we
    // pass to simdutf is pure alphabet bytes (no whitespace, no '='), so
    // last_chunk_options::loose handles the unpadded tail and
    // decode_up_to_bad_char never triggers in practice.
    size_t out_len = outBufSize - 1;
    auto   r       = simdutf::base64_to_binary_safe(inBuffer, valid, reinterpret_cast<char *>(outBuffer), out_len,
                                                    simdutf::base64_default_or_url, simdutf::last_chunk_handling_options::loose,
                                                    /*decode_up_to_bad_char=*/true);

    // OUTPUT_BUFFER_TOO_SMALL is impossible given the upfront dstlen check;
    // be defensive anyway.
    if (r.error == simdutf::error_code::OUTPUT_BUFFER_TOO_SMALL) {
      return false;
    }

    outBuffer[out_len] = '\0';
    if (length) {
      *length = out_len;
    }
    return true;
  }
#endif

  decode_scalar(inBuffer, valid, outBuffer, length);
  return true;
}

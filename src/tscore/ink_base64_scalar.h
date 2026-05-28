/** @file

  Scalar base64 encode/decode primitives, shared by the always-present scalar
  path in ink_base64.cc and (when ENABLE_HIGHWAY_DISPATCH is on) the SIMD
  kernel's scalar tail in ink_base64_dispatch.cc. Keeping a single definition
  here guarantees the two paths cannot drift.

  These are the canonical reference semantics for ATS base64:

    - encode: standard RFC 1521 alphabet (`+`, `/`), `=` padding, no line
      breaks, trailing NUL at outBuffer[length].

    - decode: accepts standard (`+`, `/`) and URL-safe (`-`, `_`) alphabets
      mixed in the same input; truncates at the first non-alphabet byte
      (whitespace, `=`, or garbage); tolerates missing padding; trailing NUL
      at outBuffer[length]; supports in-place decode (dst == src).

  decode_scalar restructures the historical tail handling: the previous code
  ran one extra loop iteration past the alphabet prefix when the valid length
  was not a multiple of four (reading bytes beyond the prefix, potentially out
  of bounds) and then read inBuffer[-2]. This version processes only complete
  4-character groups and decodes a 2- or 3-character tail explicitly, dropping
  a lone trailing character. The decoded length and bytes are identical to the
  historical code for every well-defined input; only the out-of-bounds reads
  are removed.

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
#include <cstring>

namespace ats::base64
{
// Converts a printable character to its six-bit representation; 64 marks a
// non-alphabet byte. Both standard (`+`=62, `/`=63) and URL-safe (`-`=62,
// `_`=63) punctuation are accepted.
inline constexpr unsigned char printableToSixBit[256] = {
  64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
  64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 62, 64, 62, 64, 63, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 64, 64, 64, 64, 64, 64,
  64, 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 64, 64, 64, 64, 63,
  64, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 64, 64, 64, 64, 64,
  64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
  64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
  64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
  64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64};

inline constexpr unsigned char MAX_PRINT_VAL = 63;

inline unsigned char
decode_byte(char c)
{
  return printableToSixBit[static_cast<uint8_t>(c)];
}

// Count the leading base64-alphabet bytes (standard or URL-safe). Any byte at
// or after this index is whitespace, `=`, or garbage and terminates the input.
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
inline void
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

// Hand-rolled scalar decode. Caller has pre-scanned with count_alphabet_prefix
// so every byte in inBuffer[0..inBufferSize) is in the base64 alphabet, and
// has validated outBufSize.
inline void
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

} // namespace ats::base64

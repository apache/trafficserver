/** @file

  Implementations of LiteSpeed functions used by ATS for hpack.

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

/*
MIT License

Copyright (c) 2018 - 2023 LiteSpeed Technologies Inc

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

/** ATS
 * This is the minimal implementation from lshpack.c, lshpack.h, and
 * lsxpack_header.h required from LiteSpeed to perform hpack given the ATS API
 * needs.
 */

#include "huff-tables.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>


namespace litespeed {

namespace
{

constexpr int64_t  LSHPACK_ERR_MORE_BUF = -3;
constexpr unsigned SHORTEST_CODE        = 5;

struct decode_status {
  uint8_t state;
  uint8_t eos;
};

enum {
  HPACK_HUFFMAN_FLAG_ACCEPTED = 0x01,
  HPACK_HUFFMAN_FLAG_SYM      = 0x02,
  HPACK_HUFFMAN_FLAG_FAIL     = 0x04,
};

char *
hdec_huff_dec4bits(uint8_t src_4bits, char *dst, struct decode_status *status)
{
  const struct decode_el cur_dec_code = decode_tables[status->state][src_4bits];
  if (cur_dec_code.flags & HPACK_HUFFMAN_FLAG_FAIL) {
    return nullptr; // failed
  }
  if (cur_dec_code.flags & HPACK_HUFFMAN_FLAG_SYM) {
    *dst = cur_dec_code.sym;
    dst++;
  }

  status->state = cur_dec_code.state;
  status->eos   = ((cur_dec_code.flags & HPACK_HUFFMAN_FLAG_ACCEPTED) != 0);
  return dst;
}

} // anonymous namespace

int64_t
lshpack_enc_huff_encode(uint8_t const *src, uint8_t const *const src_end, uint8_t *dst, uint32_t dst_len)
{
  uint8_t           *p_dst   = dst;
  uint8_t           *dst_end = p_dst + dst_len;
  uintptr_t          bits = 0;
  unsigned           bits_used = 0, adj;
  struct encode_el   cur_enc_code;
  const struct henc *henc;
  uint16_t           idx;

  while (src + sizeof(bits) * 8 / 5 + sizeof(idx) < src_end && p_dst + sizeof(bits) <= dst_end) {
    memcpy(&idx, src, 2);
    henc = &hencs[idx];
    src  += 2;
    while (bits_used + henc->lens < sizeof(bits) * 8) {
      bits      <<= henc->lens;
      bits       |= henc->code;
      bits_used  += henc->lens;
      memcpy(&idx, src, 2);
      henc = &hencs[idx];
      src  += 2;
    }
    if (henc->lens < 64) {
      // Prevent undefined behavior of shifting the full number of bits.
      if (bits_used > 0) {
          bits <<= sizeof(bits) * 8 - bits_used;
      }
      bits_used   = henc->lens - (sizeof(bits) * 8 - bits_used);
      bits       |= henc->code >> bits_used;
#if UINTPTR_MAX == 18446744073709551615ull
      *p_dst++ = bits >> 56;
      *p_dst++ = bits >> 48;
      *p_dst++ = bits >> 40;
      *p_dst++ = bits >> 32;
#endif
      *p_dst++ = bits >> 24;
      *p_dst++ = bits >> 16;
      *p_dst++ = bits >> 8;
      *p_dst++ = bits;
      bits     = henc->code; /* OK not to clear high bits */
    } else {
      src -= 2;
      break;
    }
  }

  while (src != src_end) {
    cur_enc_code = encode_table[*src++];
    if (bits_used + cur_enc_code.bits < sizeof(bits) * 8) {
      bits      <<= cur_enc_code.bits;
      bits       |= cur_enc_code.code;
      bits_used  += cur_enc_code.bits;
      continue;
    } else if (p_dst + sizeof(bits) <= dst_end) {
      // Prevent undefined behavior of shifting the full number of bits.
      if (bits_used > 0) {
          bits <<= sizeof(bits) * 8 - bits_used;
      }
      bits_used   = cur_enc_code.bits - (sizeof(bits) * 8 - bits_used);
      bits       |= cur_enc_code.code >> bits_used;
#if UINTPTR_MAX == 18446744073709551615ull
      *p_dst++ = bits >> 56;
      *p_dst++ = bits >> 48;
      *p_dst++ = bits >> 40;
      *p_dst++ = bits >> 32;
#endif
      *p_dst++ = bits >> 24;
      *p_dst++ = bits >> 16;
      *p_dst++ = bits >> 8;
      *p_dst++ = bits;
      bits     = cur_enc_code.code; /* OK not to clear high bits */
    } else {
      return -1;
    }
  }

  adj = bits_used + (-bits_used & 7); /* Round up to 8 */
  if (bits_used && p_dst + (adj >> 3) <= dst_end) {
    bits <<= -bits_used & 7;                /* Align to byte boundary */
    bits  |= ((1 << (-bits_used & 7)) - 1); /* EOF */
    switch (adj >> 3) {                     /* Write out */
#if UINTPTR_MAX == 18446744073709551615ull
    case 8:
      *p_dst++ = bits >> 56;
      [[fallthrough]];
    case 7:
      *p_dst++ = bits >> 48;
      [[fallthrough]];
    case 6:
      *p_dst++ = bits >> 40;
      [[fallthrough]];
    case 5:
      *p_dst++ = bits >> 32;
      [[fallthrough]];
#endif
    case 4:
      *p_dst++ = bits >> 24;
      [[fallthrough]];
    case 3:
      *p_dst++ = bits >> 16;
      [[fallthrough]];
    case 2:
      *p_dst++ = bits >> 8;
      [[fallthrough]];
    default:
      *p_dst++ = bits;
    }
    return p_dst - dst;
  } else if (p_dst + (adj >> 3) <= dst_end) {
    return p_dst - dst;
  } else {
    return -1;
  }
}

// Implementation taken from LiteSpeed:
// lshpack_dec_huff_decode_full
int64_t
lshpack_dec_huff_decode_full(uint8_t const *src, uint32_t src_len, char *dst, uint32_t dst_len)
{
  const uint8_t       *p_src   = src;
  const uint8_t *const src_end = src + src_len;
  char                      *p_dst   = dst;
  char                      *dst_end = dst + dst_len;
  struct decode_status       status  = {0, 1};

  while (p_src != src_end) {
    if (p_dst == dst_end) {
      return LSHPACK_ERR_MORE_BUF;
    }
    if ((p_dst = hdec_huff_dec4bits(*p_src >> 4, p_dst, &status)) == nullptr) {
      return -1;
    }
    if (p_dst == dst_end) {
      return LSHPACK_ERR_MORE_BUF;
    }
    if ((p_dst = hdec_huff_dec4bits(*p_src & 0xf, p_dst, &status)) == nullptr) {
      return -1;
    }
    ++p_src;
  }

  if (!status.eos) {
    return -1;
  }

  return p_dst - dst;
}

// Implementation taken from LiteSpeed:
// lshpack_dec_huff_decode
//
// The decoder is optimized for the common case.  Most of the time, we decode
// data whose encoding is 16 bits or shorter.  This lets us use a 64 KB table
// indexed by two bytes of input and outputs 1, 2, or 3 bytes at a time.
//
// In the case a longer code is encountered, we fall back to the original
// Huffman decoder that supports all code lengths.
int64_t
lshpack_dec_huff_decode(uint8_t const *src, uint32_t src_len, char *dst, uint32_t dst_len)
{
  char *const          orig_dst = dst;
  uint8_t const *const src_end  = src + src_len;
  char *const          dst_end  = dst + dst_len;
  uintptr_t            buf      = 0;
  unsigned             avail_bits, len;
  struct hdec          hdec = {0, {0, 0, 0}};
  uint16_t             idx;
  int64_t              r;

  avail_bits = 0;
  while (true) {
    if (src + sizeof(buf) <= src_end) {
      len         = (sizeof(buf) * 8 - avail_bits) >> 3;
      avail_bits += len << 3;
      switch (len) {
#if UINTPTR_MAX == 18446744073709551615ull
      case 8:
        buf <<= 8;
        buf  |= static_cast<uintptr_t>(*src++);
        [[fallthrough]];
      case 7:
        buf <<= 8;
        buf  |= static_cast<uintptr_t>(*src++);
        [[fallthrough]];
      default:
        buf <<= 48;
        buf  |= static_cast<uintptr_t>(*src++) << 40;
        buf  |= static_cast<uintptr_t>(*src++) << 32;
        buf  |= static_cast<uintptr_t>(*src++) << 24;
        buf  |= static_cast<uintptr_t>(*src++) << 16;
#else
        [[fallthrough]];
      case 4:
        buf <<= 8;
        buf  |= static_cast<uintptr_t>(*src++);
        [[fallthrough]];
      case 3:
        buf <<= 8;
        buf  |= static_cast<uintptr_t>(*src++);
        [[fallthrough]];
      default:
        buf <<= 16;
#endif
        buf |= static_cast<uintptr_t>(*src++) << 8;
        buf |= static_cast<uintptr_t>(*src++) << 0;
      }
    } else if (src < src_end) {
      do {
        buf       <<= 8;
        buf        |= static_cast<uintptr_t>(*src++);
        avail_bits += 8;
      } while (src < src_end && avail_bits <= sizeof(buf) * 8 - 8);
    } else {
      break; // Normal case terminating condition: out of input
    }

    if (dst_end - dst >= static_cast<ptrdiff_t>(8 * sizeof(buf) / SHORTEST_CODE) && avail_bits >= 16) {
      // Fast path: don't check destination bounds
      do {
        idx         = static_cast<uint16_t>(buf >> (avail_bits - 16));
        hdec        = hdecs[idx];
        dst[0]      = static_cast<char>(hdec.out[0]);
        dst[1]      = static_cast<char>(hdec.out[1]);
        dst[2]      = static_cast<char>(hdec.out[2]);
        dst        += hdec.lens & 3;
        avail_bits -= hdec.lens >> 2;
      } while (avail_bits >= 16 && hdec.lens);
      if (avail_bits < 16) {
        continue;
      }
      goto slow_path;
    } else {
      while (avail_bits >= 16) {
        idx  = static_cast<uint16_t>(buf >> (avail_bits - 16));
        hdec = hdecs[idx];
        len  = hdec.lens & 3;
        if (len && dst + len <= dst_end) {
          switch (len) {
          case 3:
            *dst++ = static_cast<char>(hdec.out[0]);
            *dst++ = static_cast<char>(hdec.out[1]);
            *dst++ = static_cast<char>(hdec.out[2]);
            break;
          case 2:
            *dst++ = static_cast<char>(hdec.out[0]);
            *dst++ = static_cast<char>(hdec.out[1]);
            break;
          default:
            *dst++ = static_cast<char>(hdec.out[0]);
            break;
          }
          avail_bits -= hdec.lens >> 2;
        } else if (dst + len > dst_end) {
          r = dst_end - dst - static_cast<ptrdiff_t>(len);
          if (r > LSHPACK_ERR_MORE_BUF) {
            r = LSHPACK_ERR_MORE_BUF;
          }
          return r;
        } else {
          goto slow_path;
        }
      }
    }
  }

  if (avail_bits >= SHORTEST_CODE) {
    idx  = static_cast<uint16_t>(buf << (16 - avail_bits));
    idx |= (1 << (16 - avail_bits)) - 1; // EOF
    if (idx == 0xFFFF && avail_bits < 8) {
      goto end;
    }
    // If a byte or more of input is left, this means there is a valid
    // encoding, not just EOF.
    hdec = hdecs[idx];
    len  = hdec.lens & 3;
    if ((static_cast<unsigned>(hdec.lens) >> 2) > avail_bits) {
      return -1;
    }
    if (len && dst + len <= dst_end) {
      switch (len) {
      case 3:
        *dst++ = static_cast<char>(hdec.out[0]);
        *dst++ = static_cast<char>(hdec.out[1]);
        *dst++ = static_cast<char>(hdec.out[2]);
        break;
      case 2:
        *dst++ = static_cast<char>(hdec.out[0]);
        *dst++ = static_cast<char>(hdec.out[1]);
        break;
      default:
        *dst++ = static_cast<char>(hdec.out[0]);
        break;
      }
      avail_bits -= hdec.lens >> 2;
    } else if (dst + len > dst_end) {
      r = dst_end - dst - static_cast<ptrdiff_t>(len);
      if (r > LSHPACK_ERR_MORE_BUF) {
        r = LSHPACK_ERR_MORE_BUF;
      }
      return r;
    } else {
      // This must be an invalid code, otherwise it would have fit
      return -1;
    }
  }

  if (avail_bits > 0) {
    // ATS: unlike upstream ls-hpack, also reject padding of 8 or more bits
    // (possible here after the final symbol consumed only part of the tail).
    // RFC 7541 5.2: "A padding strictly longer than 7 bits MUST be treated as
    // a decoding error." This keeps the strictness of the 4-bit FSM decoder.
    if (avail_bits >= 8 || ((1u << avail_bits) - 1) != (buf & ((1u << avail_bits) - 1))) {
      return -1; // Not EOF as expected
    }
  }

end:
  return dst - orig_dst;

slow_path:
  // Find previous byte boundary and finish decoding thence.
  while ((avail_bits & 7) && dst > orig_dst) {
    avail_bits += encode_table[static_cast<uint8_t>(*--dst)].bits;
  }
  assert((avail_bits & 7) == 0);
  src -= avail_bits >> 3;
  r = lshpack_dec_huff_decode_full(src, static_cast<uint32_t>(src_end - src), dst, static_cast<uint32_t>(dst_end - dst));
  if (r >= 0) {
    return dst - orig_dst + r;
  }
  return r;
}

} // namespace litespeed

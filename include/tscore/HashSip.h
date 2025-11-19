/** @file

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

#include "tscore/Hash.h"
#include <cstdint>
#include <cstring>

/*
  SipHash is a Hash Message Authentication Code and can take a key.

  If you don't care about MAC use the void constructor and it will use
  a zero key for you.

  Template parameters:
  - c_rounds: number of compression rounds per message block
  - d_rounds: number of finalization rounds
*/

#define SIP_BLOCK_SIZE 8

#define ROTL64(a, b) (((a) << (b)) | ((a) >> (64 - b)))

static inline std::uint64_t
U8TO64_LE(const std::uint8_t *p)
{
  std::uint64_t result;
  std::memcpy(&result, p, sizeof(result));
  return result;
}

#define SIPCOMPRESS(x0, x1, x2, x3) \
  x0 += x1;                         \
  x2 += x3;                         \
  x1  = ROTL64(x1, 13);             \
  x3  = ROTL64(x3, 16);             \
  x1 ^= x0;                         \
  x3 ^= x2;                         \
  x0  = ROTL64(x0, 32);             \
  x2 += x1;                         \
  x0 += x3;                         \
  x1  = ROTL64(x1, 17);             \
  x3  = ROTL64(x3, 21);             \
  x1 ^= x2;                         \
  x3 ^= x0;                         \
  x2  = ROTL64(x2, 32);

template <int c_rounds, int d_rounds> struct ATSHashSip : ATSHash64 {
  ATSHashSip() { this->clear(); }

  ATSHashSip(const unsigned char key[16]) : k0(U8TO64_LE(key)), k1(U8TO64_LE(key + sizeof(k0))) { this->clear(); }

  ATSHashSip(std::uint64_t key0, std::uint64_t key1) : k0(key0), k1(key1) { this->clear(); }

  void
  update(const void *data, std::size_t len) override
  {
    std::size_t    i, blocks;
    unsigned char *m;
    std::uint64_t  mi;
    std::uint8_t   block_off = 0;

    if (!finalized) {
      m          = (unsigned char *)data;
      total_len += len;

      if (len + block_buffer_len < SIP_BLOCK_SIZE) {
        std::memcpy(block_buffer + block_buffer_len, m, len);
        block_buffer_len += len;
      } else {
        if (block_buffer_len > 0) {
          block_off = SIP_BLOCK_SIZE - block_buffer_len;
          std::memcpy(block_buffer + block_buffer_len, m, block_off);

          mi  = U8TO64_LE(block_buffer);
          v3 ^= mi;
          for (int r = 0; r < c_rounds; r++) {
            SIPCOMPRESS(v0, v1, v2, v3);
          }
          v0 ^= mi;
        }

        for (i = block_off, blocks = ((len - block_off) & ~(SIP_BLOCK_SIZE - 1)); i < blocks; i += SIP_BLOCK_SIZE) {
          mi  = U8TO64_LE(m + i);
          v3 ^= mi;
          for (int r = 0; r < c_rounds; r++) {
            SIPCOMPRESS(v0, v1, v2, v3);
          }
          v0 ^= mi;
        }

        block_buffer_len = (len - block_off) & (SIP_BLOCK_SIZE - 1);
        std::memcpy(block_buffer, m + block_off + blocks, block_buffer_len);
      }
    }
  }

  void
  final() override
  {
    std::uint64_t last7;
    int           i;

    if (!finalized) {
      last7 = static_cast<std::uint64_t>(total_len & 0xff) << 56;

      for (i = block_buffer_len - 1; i >= 0; i--) {
        last7 |= static_cast<std::uint64_t>(block_buffer[i]) << (i * 8);
      }

      v3 ^= last7;
      for (int r = 0; r < c_rounds; r++) {
        SIPCOMPRESS(v0, v1, v2, v3);
      }
      v0 ^= last7;
      v2 ^= 0xff;
      for (int r = 0; r < d_rounds; r++) {
        SIPCOMPRESS(v0, v1, v2, v3);
      }
      hfinal    = v0 ^ v1 ^ v2 ^ v3;
      finalized = true;
    }
  }

  std::uint64_t
  get() const override
  {
    if (finalized) {
      return hfinal;
    } else {
      return 0;
    }
  }

  void
  clear() override
  {
    v0               = k0 ^ 0x736f6d6570736575ull;
    v1               = k1 ^ 0x646f72616e646f6dull;
    v2               = k0 ^ 0x6c7967656e657261ull;
    v3               = k1 ^ 0x7465646279746573ull;
    finalized        = false;
    total_len        = 0;
    block_buffer_len = 0;
  }

private:
  unsigned char block_buffer[8]  = {0};
  std::uint8_t  block_buffer_len = 0;
  std::uint64_t k0               = 0;
  std::uint64_t k1               = 0;
  std::uint64_t v0               = 0;
  std::uint64_t v1               = 0;
  std::uint64_t v2               = 0;
  std::uint64_t v3               = 0;
  std::uint64_t hfinal           = 0;
  std::size_t   total_len        = 0;
  bool          finalized        = false;
};

// Standard SipHash variants
using ATSHash64Sip24 = ATSHashSip<2, 4>;
using ATSHash64Sip13 = ATSHashSip<1, 3>;

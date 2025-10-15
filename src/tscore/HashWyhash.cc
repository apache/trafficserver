/** @file

  Wyhash v4.1 implementation

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

  @section details Details

  Algorithm Info: https://github.com/wangyi-fudan/wyhash
  Wyhash v4.1 - Fast non-cryptographic hash with DoS resistance
  Original algorithm: Public Domain / Unlicense

 */

#include "tscore/HashWyhash.h"
#include <cstring>

#if defined(_MSC_VER) && defined(_M_X64)
#include <intrin.h>
#endif

using namespace std;

static const uint64_t _wyp0 = 0xa0761d6478bd642full;
static const uint64_t _wyp1 = 0xe7037ed1a0b428dbull;
static const uint64_t _wyp2 = 0x8ebc6af09c88c6e3ull;
static const uint64_t _wyp3 = 0x589965cc75374cc3ull;
static const uint64_t _wyp4 = 0x1d8e4e27c47d124full;

// Portable 64-bit arithmetic fallback for 64x64->128 bit multiplication
static inline uint64_t
_wymix_portable(uint64_t A, uint64_t B)
{
  uint64_t ha = A >> 32, hb = B >> 32;
  uint64_t la = static_cast<uint32_t>(A), lb = static_cast<uint32_t>(B);
  uint64_t rh   = ha * hb;
  uint64_t rm0  = ha * lb;
  uint64_t rm1  = hb * la;
  uint64_t rl   = la * lb;
  uint64_t t    = rl + (rm0 << 32);
  uint64_t c    = t < rl;
  uint64_t lo   = t + (rm1 << 32);
  c            += lo < t;
  uint64_t hi   = rh + (rm0 >> 32) + (rm1 >> 32) + c;
  return hi ^ lo;
}

static inline uint64_t
_wymix(uint64_t A, uint64_t B)
{
#ifdef __SIZEOF_INT128__
  __uint128_t r  = A;
  r             *= B;
  return (r >> 64) ^ r;
#elif defined(_MSC_VER) && defined(_M_X64)
  // MSVC intrinsic for 64x64->128 bit multiply
  uint64_t high;
  uint64_t low = _umul128(A, B, &high);
  return high ^ low;
#else
  return _wymix_portable(A, B);
#endif
}

static inline uint64_t
_wyr8(const uint8_t *p)
{
  uint64_t v;
  memcpy(&v, p, 8);
  return v;
}

static inline uint64_t
_wyr4(const uint8_t *p)
{
  uint32_t v;
  memcpy(&v, p, 4);
  return v;
}

static inline uint64_t
_wyr3(const uint8_t *p, size_t k)
{
  return (((uint64_t)p[0]) << 16) | (((uint64_t)p[k >> 1]) << 8) | p[k - 1];
}

ATSHash64Wyhash::ATSHash64Wyhash() : seed(0)
{
  this->clear();
}

ATSHash64Wyhash::ATSHash64Wyhash(uint64_t s) : seed(s)
{
  this->clear();
}

void
ATSHash64Wyhash::update(const void *data, size_t len)
{
  if (finalized) {
    return;
  }

  const uint8_t *p  = static_cast<const uint8_t *>(data);
  total_len        += len;

  if (buffer_len + len < 32) {
    memcpy(buffer + buffer_len, p, len);
    buffer_len += len;
    return;
  }

  if (buffer_len > 0) {
    size_t to_copy = 32 - buffer_len;
    memcpy(buffer + buffer_len, p, to_copy);
    state =
      _wymix(_wyr8(buffer) ^ _wyp0, _wyr8(buffer + 8) ^ state) ^ _wymix(_wyr8(buffer + 16) ^ _wyp1, _wyr8(buffer + 24) ^ state);
    p          += to_copy;
    len        -= to_copy;
    buffer_len  = 0;
  }

  while (len >= 32) {
    state  = _wymix(_wyr8(p) ^ _wyp0, _wyr8(p + 8) ^ state) ^ _wymix(_wyr8(p + 16) ^ _wyp1, _wyr8(p + 24) ^ state);
    p     += 32;
    len   -= 32;
  }

  if (len > 0) {
    memcpy(buffer, p, len);
    buffer_len = len;
  }
}

void
ATSHash64Wyhash::final()
{
  if (finalized) {
    return;
  }

  uint64_t       a = 0, b = 0;
  const uint8_t *p   = buffer;
  size_t         len = buffer_len;

  if (len <= 16) {
    if (len >= 4) {
      a = (_wyr4(p) << 32) | _wyr4(p + ((len >> 3) << 2));
      b = (_wyr4(p + len - 4) << 32) | _wyr4(p + len - 4 - ((len >> 3) << 2));
    } else if (len > 0) {
      a = _wyr3(p, len);
    }
  } else {
    size_t i = len;
    if (i > 32) {
      i = 32;
    }
    a = _wyr8(p);
    b = _wyr8(p + 8);
    if (i > 16) {
      a ^= _wyr8(p + i - 16);
      b ^= _wyr8(p + i - 8);
    }
  }

  hfinal    = _wymix(state ^ _wyp4, total_len ^ (_wymix(a ^ _wyp2, b ^ _wyp3) ^ seed));
  finalized = true;
}

uint64_t
ATSHash64Wyhash::get() const
{
  if (finalized) {
    return hfinal;
  } else {
    return 0;
  }
}

void
ATSHash64Wyhash::clear()
{
  state      = seed;
  total_len  = 0;
  hfinal     = 0;
  finalized  = false;
  buffer_len = 0;
  memset(buffer, 0, sizeof(buffer));
}

// Test-only accessor for the platform's native multiplication
std::uint64_t
wyhash_test_wymix(std::uint64_t A, std::uint64_t B)
{
  return _wymix(A, B);
}

// Test-only accessor for portable multiplication
std::uint64_t
wyhash_test_wymix_portable(std::uint64_t A, std::uint64_t B)
{
  return _wymix_portable(A, B);
}

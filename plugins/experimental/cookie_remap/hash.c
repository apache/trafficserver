/*
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

#include "hash.h"
#include <string.h>
#include <ctype.h>

////////////////////////////////////////////////////////////////////////////////
//        Implementation of the Fowler–Noll–Vo hash function                  //
//        More Details at: http://www.isthe.com/chongo/tech/comp/fnv/         //
////////////////////////////////////////////////////////////////////////////////

/*
 * 32/64 bit magic FNV primes
 * The main secret of the algorithm is in these prime numbers
 * and their special relation to 2^32 (or 2^64) [a word]
 * and 2^8 [a byte].
 */
#define FNV_32_PRIME ((uint32_t)0x01000193UL)
#define FNV_64_PRIME ((uint64_t)0x100000001b3ULL)

/*
 * The init value is quite arbitrary, but these seem to perform
 * well on both web2 and sequential integers represented as strings.
 */
#define FNV1_32_INIT ((uint32_t)33554467UL)
#define FNV1_64_INIT ((uint64_t)0xcbf29ce484222325ULL)

#define MAX_UINT32 (~((uint32_t)0))
#define MAX_UINT64 (~((uint64_t)0))

#define MASK(x) (((uint32_t)1 << (x)) - 1)

static uint32_t
fnv32_nbits(const char *buf, int len, int nbits)
{
  uint32_t hash;
  hash = hash_fnv32_buf(buf, len);

  if (nbits <= 16) {
    hash = ((hash >> nbits) ^ hash) & MASK(nbits);
  } else {
    hash = (hash >> nbits) ^ (hash & MASK(nbits));
  }

  return hash;
}

uint32_t
hash_fnv32_buckets(const char *buf, size_t len, uint32_t num_buckets)
{
  uint32_t hash;
  uint32_t retry;
  int first_bit;

  if (num_buckets < 1) {
    return 0;
  }

  first_bit = ffs(num_buckets);

  if (num_buckets >> first_bit == 0) { /* Power of two */
    /* Yay we can xor fold */
    hash = fnv32_nbits(buf, len, first_bit - 1);
    return hash;
  }

  /* Can't xor fold so use the retry method */

  hash = hash_fnv32_buf(buf, len);

  /* This code ensures there is no bias against larger values */
  retry = (MAX_UINT32 / num_buckets) * num_buckets;
  while (hash >= retry) {
    hash = (hash * FNV_32_PRIME) + FNV1_32_INIT;
  }

  hash %= num_buckets;
  return hash;
}

/* 32-bit version */
uint32_t
hash_fnv32_buf(const char *buf, size_t len)
{
  uint32_t val; /* initial hash value */

  for (val = FNV1_32_INIT; len > 0; --len) {
    val *= FNV_32_PRIME;
    val ^= (uint32_t)(*buf);
    ++buf;
  }

  return val;
}

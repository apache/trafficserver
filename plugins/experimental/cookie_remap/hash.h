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

#ifndef _CKREMAP_HASH_H_
#define _CKREMAP_HASH_H_

#include <stddef.h>
#include <sys/types.h>
#include <inttypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 32-bit Fowler / Noll / Vo (FNV) Hash.
 *
 * see http://www.isthe.com/chongo/tech/comp/fnv/index.html
 */
uint32_t hash_fnv32_buf(const char *buf, size_t len);

/**
 * Computes an fnv32 hash whose value is less than num_buckets
 *
 * This functions computes an fnv32 between zero and num_buckets - 1.
 * It computes an fnv32 hash and collapses that hash into a smaller
 * range using techniques which avoid the bias in a simple mod
 * operation.
 *
 * This function has the best performance (speed and hash distribution)
 * if num_buckets is a power of two.
 */
uint32_t hash_fnv32_buckets(const char *buf, size_t len, uint32_t num_buckets);

#ifdef __cplusplus
}
#endif

#endif /* _CKREMAP_HASH_H_ */

/** @file

  Really really scary code to store a "version" number in unused bits in a pointer.

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

/*
  For information on the structure of the x86_64 memory map:

  http://en.wikipedia.org/wiki/X86-64#Linux

  Essentially, in the current 48-bit implementations, the
  top bit as well as the  lower 47 bits are used, leaving
  the upper-but one 16 bits free to be used for the version.
  We will use the top-but-one 15 and sign extend when generating
  the pointer was required by the standard.
*/

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

void ink_queue_load_64(void *dst, void *src);

#ifdef __x86_64__
#define INK_QUEUE_LD64(dst, src) *((uint64_t *)&(dst)) = *((uint64_t *)&(src))
#else
#define INK_QUEUE_LD64(dst, src) (ink_queue_load_64((void *)&(dst), (void *)&(src)))
#endif

#if TS_HAS_128BIT_CAS
#define INK_QUEUE_LD(dst, src)                                                       \
  do {                                                                               \
    *(__int128_t *)&(dst) = __sync_val_compare_and_swap((__int128_t *)&(src), 0, 0); \
  } while (0)
#else
#define INK_QUEUE_LD(dst, src) INK_QUEUE_LD64(dst, src)
#endif

// Warning: head_p is read and written in multiple threads without a
// lock, use INK_QUEUE_LD to read safely.  This type was formerly used as a linked list
// head pointer.
union head_p {
  head_p() : data(){};

#if (defined(__i386__) || defined(__arm__) || defined(__mips__)) && (SIZEOF_VOIDP == 4)
  typedef int32_t version_type;
  typedef int64_t data_type;
#elif TS_HAS_128BIT_CAS
  typedef int64_t version_type;
  typedef __int128_t data_type;
#else
  typedef int64_t version_type;
  typedef int64_t data_type;
#endif

  struct {
    void *pointer;
    version_type version;
  } s;

  data_type data;
};

#if (defined(__i386__) || defined(__arm__) || defined(__mips__)) && (SIZEOF_VOIDP == 4)
#define FREELIST_POINTER(_x) (_x).s.pointer
#define FREELIST_VERSION(_x) (_x).s.version
#define SET_FREELIST_POINTER_VERSION(_x, _p, _v) \
  (_x).s.pointer = _p;                           \
  (_x).s.version = _v
#elif TS_HAS_128BIT_CAS
#define FREELIST_POINTER(_x) (_x).s.pointer
#define FREELIST_VERSION(_x) (_x).s.version
#define SET_FREELIST_POINTER_VERSION(_x, _p, _v) \
  (_x).s.pointer = _p;                           \
  (_x).s.version = _v
#elif defined(__x86_64__) || defined(__ia64__) || defined(__powerpc64__) || defined(__aarch64__) || defined(__mips64)
/* Layout of FREELIST_POINTER
 *
 *  0 ~ 47 bits : 48 bits, Virtual Address (47 bits for AMD64 and 48 bits for AArch64)
 * 48 ~ 62 bits : 15 bits, Freelist Version
 *      63 bits :  1 bits, The type of Virtual Address (0 = user space, 1 = kernel space)
 */
/* Detect which shift is implemented by the simple expression ((~0 >> 1) < 0):
 *
 * If the shift is 'logical' the highest order bit of the left side of the comparison is 0 so the result is positive.
 * If the shift is 'arithmetic' the highest order bit of the left side is 1 so the result is negative.
 */
#if ((~0 >> 1) < 0)
/* the shift is `arithmetic' */
#define FREELIST_POINTER(_x) \
  ((void *)((((intptr_t)(_x).data) & 0x0000FFFFFFFFFFFFLL) | ((((intptr_t)(_x).data) >> 63) << 48))) // sign extend
#else
/* the shift is `logical' */
#define FREELIST_POINTER(_x) \
  ((void *)((((intptr_t)(_x).data) & 0x0000FFFFFFFFFFFFLL) | (((~((((intptr_t)(_x).data) >> 63) - 1)) >> 48) << 48)))
#endif

#define FREELIST_VERSION(_x) ((((intptr_t)(_x).data) & 0x7FFF000000000000LL) >> 48)
#define SET_FREELIST_POINTER_VERSION(_x, _p, _v) (_x).data = ((((intptr_t)(_p)) & 0x8000FFFFFFFFFFFFLL) | (((_v)&0x7FFFLL) << 48))
#else
#error "unsupported processor"
#endif

#ifdef __cplusplus
}
#endif /* __cplusplus */

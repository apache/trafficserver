/** @file

  A brief file description

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

/****************************************************************************

  ink_atomic.h

  This file defines atomic memory operations.

  On a Sparc V9, ink_atomic.c must be compiled with gcc and requires
  the argument "-Wa,-xarch=v8plus" to get the assembler to emit V9
  instructions.


 ****************************************************************************/

#ifndef _ink_atomic_h_
#define	_ink_atomic_h_

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include "ink_port.h"

#include "ink_apidefs.h"

typedef volatile ink32 vink32;
typedef volatile ink64 vink64;
typedef volatile void *vvoidp;
typedef vink32 *pvink32;
typedef vink64 *pvink64;
typedef vvoidp *pvvoidp;

#if defined(__SUNPRO_CC)

typedef volatile inku32 vinku32;
#if __WORDSIZE == 64
typedef unsigned long inku64_s;
#else
typedef inku64 inku64_s;
#endif
typedef volatile inku64_s vinku64_s;
typedef vinku32 *pvinku32;
typedef vinku64_s *pvinku64_s;


#include <atomic.h>

static inline ink32 ink_atomic_swap(pvink32 mem, ink32 value) { return (ink32)atomic_swap_32((pvinku32)mem, (inku32)value); }
static inline ink64 ink_atomic_swap64(pvink64 mem, ink64 value) { return (ink64)atomic_swap_64((pvinku64_s)mem, (inku64_s)value); }
static inline void *ink_atomic_swap_ptr(vvoidp mem, void *value) { return atomic_swap_ptr((vvoidp)mem, value); }
static inline int ink_atomic_cas(pvink32 mem, int old, int new_value) { return atomic_cas_32((pvinku32)mem, (inku32)old, (inku32)new_value) == old; }
static inline int ink_atomic_cas64(pvink64 mem, ink64 old, ink64 new_value) { return atomic_cas_64((pvinku64_s)mem, (inku64_s)old, (inku64_s)new_value) == old; }
static inline int ink_atomic_cas_ptr(pvvoidp mem, void* old, void* new_value) { return atomic_cas_ptr((vvoidp)mem, old, new_value) == old; }
static inline int ink_atomic_increment(pvink32 mem, int value) { return ((inku32)atomic_add_32_nv((pvinku32)mem, (inku32)value)) - value; }
static inline ink64 ink_atomic_increment64(pvink64 mem, ink64 value) { return ((inku64_s)atomic_add_64_nv((pvinku64_s)mem, (inku64_s)value)) - value; }
static inline void *ink_atomic_increment_ptr(pvvoidp mem, intptr_t value) { return (void*)(((char*)atomic_add_ptr_nv((vvoidp)mem, (ssize_t)value)) - value); }

/* not used for Intel Processors or Sparc which are mostly sequentally consistent */
#define INK_WRITE_MEMORY_BARRIER
#define INK_MEMORY_BARRIER

#else

#if defined(__GNUC__) && (__GNUC__ >= 4) && (__GNUC_MINOR__ >= 1)

/* see http://gcc.gnu.org/onlinedocs/gcc-4.1.2/gcc/Atomic-Builtins.html */

static inline ink32 ink_atomic_swap(pvink32 mem, ink32 value) { return __sync_lock_test_and_set(mem, value); }
static inline ink64 ink_atomic_swap64(pvink64 mem, ink64 value) { return __sync_lock_test_and_set(mem, value); }
static inline void *ink_atomic_swap_ptr(vvoidp mem, void *value) { return __sync_lock_test_and_set((void**)mem, value); }
static inline int ink_atomic_cas(pvink32 mem, int old, int new_value) { return __sync_bool_compare_and_swap(mem, old, new_value); }
static inline ink64 ink_atomic_cas64(pvink64 mem, ink64 old, ink64 new_value) { return __sync_bool_compare_and_swap(mem, old, new_value); }
static inline int ink_atomic_cas_ptr(pvvoidp mem, void* old, void* new_value) { return __sync_bool_compare_and_swap(mem, old, new_value); }
static inline int ink_atomic_increment(pvink32 mem, int value) { return __sync_fetch_and_add(mem, value); }
static inline ink64 ink_atomic_increment64(pvink64 mem, ink64 value) { return __sync_fetch_and_add(mem, value); }
static inline void *ink_atomic_increment_ptr(pvvoidp mem, intptr_t value) { return __sync_fetch_and_add((void**)mem, value); }

/* not used for Intel Processors which have sequential(esque) consistency */
#define INK_WRITE_MEMORY_BARRIER
#define INK_MEMORY_BARRIER

#else

#ifdef __cplusplus
extern "C"
{
#endif                          /* __cplusplus */

/*===========================================================================*

     Atomic Memory Operations

 *===========================================================================*/

/* atomic swap 32-bit value */
  ink32 ink_atomic_swap(pvink32 mem, ink32 value);

/* atomic swap a pointer */
  void *ink_atomic_swap_ptr(vvoidp mem, void *value);

  ink64 ink_atomic_swap64(pvink64 mem, ink64 value);

#if (HOST_OS == freebsd)

  static inline int ink_atomic_cas(pvink32 mem, int old, int new_value)
  {
    int result;
    __asm __volatile("/* %0 %1 */; lock; cmpxchg %2,(%3)":"=a"(result)
                     :"a"(old), "r"(new_value), "r"(mem)
      );
      return old == result;
  }
  static inline int ink_atomic_cas_ptr(pvvoidp mem, void *old, void *new_value)
  {
    return ink_atomic_cas((int *) mem, (int) old, (int) new_value);
  }

  static inline int ink_atomic_increment(pvink32 mem, int value)
  {
    volatile int *memp = mem;
    int old;
    do {
      old = *memp;
    } while (!ink_atomic_cas(mem, old, old + value));
    return old;
  }
  static inline void *ink_atomic_increment_ptr(pvvoidp mem, int value)
  {
    return (void *) ink_atomic_increment((int *) mem, value);
  }
#else  /* non-freebsd for the "else" */
/* Atomic compare and swap 32-bit.
   if (*mem == old) *mem = new_value;
   Returns TRUE if swap was successful. */
  int ink_atomic_cas(pvink32 mem, ink32 old, ink32 new_value);
/* Atomic compare and swap of pointers */
  int ink_atomic_cas_ptr(pvvoidp mem, void *old, void *new_value);
/* Atomic increment/decrement to a pointer.  Adds 'value' bytes to the
   pointer.  Returns the old value of the pointer. */
  void *ink_atomic_increment_ptr(pvvoidp mem, int value);

/* Atomic increment/decrement.  Returns the old value */
  int ink_atomic_increment(pvink32 mem, int value);
#endif  /* freebsd vs not freebsd check */

/* Atomic 64-bit compare and swap
   THIS IS NOT DEFINED for x86 */
#if (HOST_OS == freebsd)

  static inline int ink_atomic_cas64(pvink64 az, ink64 ax, ink64 ay)
  {
    unsigned long x1 = (inku64) ax;
    unsigned long x2 = ((inku64) ax) >> 32;
    unsigned long y1 = (inku64) ay;
    unsigned long y2 = ((inku64) ay) >> 32;
    register pvink64 z asm("edi") = az;
    int result;
    __asm __volatile("lock\n" "     cmpxchg8b (%1)\n" "     setz %%al\n" "     and $255,%%eax":"=a"(result)
                     :"r"(z), "a"(x1), "d"(x2), "b"(y1), "c"(y2)
                     :"cc");
      return result;
  }

  static inline ink64 ink_atomic_increment64(pvink64 mem, ink64 value)
  {
    volatile ink64 *memp = mem;
    ink64 old;
    do {
      old = *memp;
    } while (!ink_atomic_cas64(mem, old, old + value));
    return old;
  }
#else  /* non-freebsd for the "else" */
  int ink_atomic_cas64(pvink64 mem, ink64 old, ink64 new_value);
  ink64 ink_atomic_increment64(pvink64 mem, ink64 value);
#endif  /* freebsd vs not freebsd check */

#define INK_WRITE_MEMORY_BARRIER
#define INK_MEMORY_BARRIER

#ifdef __cplusplus
}
#endif                          /* __cplusplus */
#endif
#endif
#endif                          /* _ink_atomic_h_ */

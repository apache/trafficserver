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

#pragma once

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cassert>

#include "ts/ink_defs.h"
#include "ts/ink_apidefs.h"
#include "ts/ink_mutex.h"

/* GCC compiler >= 4.1 */
#if defined(__GNUC__) && (((__GNUC__ == 4) && (__GNUC_MINOR__ >= 1)) || (__GNUC__ >= 5))

/* see http://gcc.gnu.org/onlinedocs/gcc-4.1.2/gcc/Atomic-Builtins.html */

// ink_atomic_swap(ptr, value)
// Writes @value into @ptr, returning the previous value.
template <typename T>
static inline T
ink_atomic_swap(T *mem, T value)
{
  return __sync_lock_test_and_set(mem, value);
}

// ink_atomic_cas(mem, prev, next)
// Atomically store the value @next into the pointer @mem, but only if the current value at @mem is @prev.
// Returns true if @next was successfully stored.
template <typename T>
static inline bool
ink_atomic_cas(T *mem, T prev, T next)
{
  return __sync_bool_compare_and_swap(mem, prev, next);
}

// ink_atomic_increment(ptr, count)
// Increment @ptr by @count, returning the previous value.
template <typename Type, typename Amount>
static inline Type
ink_atomic_increment(Type *mem, Amount count)
{
  return __sync_fetch_and_add(mem, (Type)count);
}

// ink_atomic_decrement(ptr, count)
// Decrement @ptr by @count, returning the previous value.
template <typename Type, typename Amount>
static inline Type
ink_atomic_decrement(Type *mem, Amount count)
{
  return __sync_fetch_and_sub(mem, (Type)count);
}

// Special hacks for ARM 32-bit
#if (defined(__arm__) || defined(__mips__)) && (SIZEOF_VOIDP == 4)
extern ink_mutex __global_death;

template <>
inline int64_t
ink_atomic_swap<int64_t>(pvint64 mem, int64_t value)
{
  int64_t old;
  ink_mutex_acquire(&__global_death);
  old  = *mem;
  *mem = value;
  ink_mutex_release(&__global_death);
  return old;
}

template <>
inline bool
ink_atomic_cas<int64_t>(pvint64 mem, int64_t old, int64_t new_value)
{
  int64_t curr;
  ink_mutex_acquire(&__global_death);
  curr = *mem;
  if (old == curr)
    *mem = new_value;
  ink_mutex_release(&__global_death);
  if (old == curr)
    return 1;
  return 0;
}

template <typename Amount>
static inline int64_t
ink_atomic_increment(pvint64 mem, Amount value)
{
  int64_t curr;
  ink_mutex_acquire(&__global_death);
  curr = *mem;
  *mem = curr + value;
  ink_mutex_release(&__global_death);
  return curr;
}

template <typename Amount>
static inline int64_t
ink_atomic_decrement(pvint64 mem, Amount value)
{
  int64_t curr;
  ink_mutex_acquire(&__global_death);
  curr = *mem;
  *mem = curr - value;
  ink_mutex_release(&__global_death);
  return curr;
}

template <typename Amount>
static inline uint64_t
ink_atomic_increment(pvuint64 mem, Amount value)
{
  uint64_t curr;
  ink_mutex_acquire(&__global_death);
  curr = *mem;
  *mem = curr + value;
  ink_mutex_release(&__global_death);
  return curr;
}

template <typename Amount>
static inline uint64_t
ink_atomic_decrement(pvuint64 mem, Amount value)
{
  uint64_t curr;
  ink_mutex_acquire(&__global_death);
  curr = *mem;
  *mem = curr - value;
  ink_mutex_release(&__global_death);
  return curr;
}

#endif /* Special hacks for ARM 32-bit */

/* not used for Intel Processors which have sequential(esque) consistency */
#define INK_WRITE_MEMORY_BARRIER
#define INK_MEMORY_BARRIER

#else /* not gcc > v4.1.2 */
#error Need a compiler / libc that supports atomic operations, e.g. gcc v4.1.2 or later
#endif

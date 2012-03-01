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


//////////////////////////////////////////////////////////////////////////////////////////////
// 
// Implement the classes for the various types of hash keys we support.
//
#ifndef __LULU_H__
#define __LULU_H__ 1

// Define UNUSED properly.
#if ((__GNUC__ >= 3) || ((__GNUC__ == 2) && (__GNUC_MINOR__ >= 7)))
#define UNUSED __attribute__ ((unused))
#else
#define UNUSED
#endif /* #if ((__GNUC__ >= 3) || ((__GNUC__ == 2) && (__GNUC_MINOR__ >= 7))) */

#include <sys/types.h>

// Memory barriers on i386 / linux / gcc
#if defined(__i386__)
#define mb()  __asm__ __volatile__ ( "lock; addl $0,0(%%esp)" : : : "memory" )
#define rmb() __asm__ __volatile__ ( "lock; addl $0,0(%%esp)" : : : "memory" )
#define wmb() __asm__ __volatile__ ( "" : : : "memory")
#elif defined(__x86_64__)
#define mb()  __asm__ __volatile__ ( "mfence" : : : "memory")
#define rmb() __asm__ __volatile__ ( "lfence" : : : "memory")
#define wmb() __asm__ __volatile__ ( "" : : : "memory")
#else
#error "Define barriers"
#endif

// Used for Debug etc.
static const char* PLUGIN_NAME = "header_filter";
static const char* PLUGIN_NAME_DBG = "header_filter_dbg";


// From the atomic portions of ATS
typedef int int32;
typedef unsigned int intu32;
typedef long long int64;
typedef unsigned long long intu64;

typedef volatile int32 vint32;
typedef volatile int64 vint64;
typedef volatile void *vvoidp;
typedef vint32 *pvint32;
typedef vint64 *pvint64;
typedef vvoidp *pvvoidp;

#if defined(__GNUC__) && (__GNUC__ >= 4) && (__GNUC_MINOR__ >= 1)

/* see http://gcc.gnu.org/onlinedocs/gcc-4.1.2/gcc/Atomic-Builtins.html */
static inline int32 ink_atomic_swap(pvint32 mem, int32 value) { return __sync_lock_test_and_set(mem, value); }
static inline int64 ink_atomic_swap64(pvint64 mem, int64 value) { return __sync_lock_test_and_set(mem, value); }
static inline void *ink_atomic_swap_ptr(vvoidp mem, void *value) { return __sync_lock_test_and_set((void**)mem, value); }
static inline int ink_atomic_cas(pvint32 mem, int old, int new_value) { return __sync_bool_compare_and_swap(mem, old, new_value); }
static inline int64 ink_atomic_cas64(pvint64 mem, int64 old, int64 new_value) { return __sync_bool_compare_and_swap(mem, old, new_value); }
static inline int ink_atomic_cas_ptr(pvvoidp mem, void* old, void* new_value) { return __sync_bool_compare_and_swap(mem, old, new_value); }
static inline int ink_atomic_increment(pvint32 mem, int value) { return __sync_fetch_and_add(mem, value); }
static inline int64 ink_atomic_increment64(pvint64 mem, int64 value) { return __sync_fetch_and_add(mem, value); }
static inline void *ink_atomic_increment_ptr(pvvoidp mem, intptr_t value) { return __sync_fetch_and_add((void**)mem, value); }
#else
// TODO: Deal with this case?
#failure
#endif


// From google styleguide: http://google-styleguide.googlecode.com/svn/trunk/cppguide.xml
#define DISALLOW_COPY_AND_ASSIGN(TypeName)      \
  TypeName(const TypeName&);                    \
  void operator=(const TypeName&)


#endif // __LULU_H__



/*
  local variables:
  mode: C++
  indent-tabs-mode: nil
  c-basic-offset: 2
  c-comment-only-line-offset: 0
  c-file-offsets: ((statement-block-intro . +)
  (label . 0)
  (statement-cont . +)
  (innamespace . 0))
  end:

  Indent with: /usr/bin/indent -ncs -nut -npcs -l 120 logstats.cc
*/

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

  ProxyAllocator.h



*****************************************************************************/
#pragma once

#include <new>
#include <utility>

#include "tscore/ink_platform.h"
#include "tscore/Allocator.h"

class EThread;

extern int thread_freelist_high_watermark;
extern int thread_freelist_low_watermark;
extern int cmd_disable_pfreelist;

struct ProxyAllocator {
  int   allocated = 0;
  void *freelist  = nullptr;

  ProxyAllocator() {}
};

template <class CAlloc, typename... Args>
typename CAlloc::Value_type *
thread_alloc(CAlloc &a, ProxyAllocator &l, Args &&...args)
{
  if (!cmd_disable_pfreelist && l.freelist) {
    void *v    = l.freelist;
    l.freelist = *reinterpret_cast<void **>(l.freelist);
    --(l.allocated);
    ::new (v) typename CAlloc::Value_type(std::forward<Args>(args)...);
    return static_cast<typename CAlloc::Value_type *>(v);
  }
  return a.alloc(std::forward<Args>(args)...);
}

void *thread_alloc(Allocator &a, ProxyAllocator &l);

void thread_freeup(Allocator &a, ProxyAllocator &l);

#if 1

// Potentially empty variable arguments -- non-standard GCC way
//
#define THREAD_ALLOC(_a, _t, ...)      thread_alloc(::_a, _t->_a, ##__VA_ARGS__)
#define THREAD_ALLOC_INIT(_a, _t, ...) thread_alloc(::_a, _t->_a, ##__VA_ARGS__)
#define SAFE_THREAD_ALLOC(_a, _t, ...) (_t ? thread_alloc(::_a, _t->_a, ##__VA_ARGS__) : ::_a.alloc(##__VA_ARGS__))

#else

// Potentially empty variable arguments -- Standard C++20 way
//
#define THREAD_ALLOC(_a, _t, ...)      thread_alloc(::_a, _t->_a __VA_OPT__(, ) __VA_ARGS__)
#define THREAD_ALLOC_INIT(_a, _t, ...) thread_alloc(::_a, _t->_a __VA_OPT__(, ) __VA_ARGS__)

#endif

class Thread;

#define THREAD_FREE(_p, _a, _tin) thread_free(_p, ::_a, _tin->_a, _tin)

template <class T>
void
thread_free(T *p, Allocator &global_allocator, ProxyAllocator &thread_allocator, Thread *tin)
{
  global_allocator.destroy_if_enabled(p);
  if (tin && !cmd_disable_pfreelist) {
    *(reinterpret_cast<char **>(p)) = reinterpret_cast<char *>(thread_allocator.freelist);
    thread_allocator.freelist       = p;
    thread_allocator.allocated++;
    if (thread_freelist_high_watermark > 0 && thread_allocator.allocated > thread_freelist_high_watermark) {
      thread_freeup(global_allocator.raw(), thread_allocator);
    }
  } else {
    global_allocator.raw().free_void(p);
  }
}

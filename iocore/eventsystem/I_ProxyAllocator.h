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

#include "ts/ink_platform.h"

class EThread;

extern int thread_freelist_high_watermark;
extern int thread_freelist_low_watermark;
extern int cmd_disable_pfreelist;

struct ProxyAllocator {
  int allocated;
  void *freelist;

  ProxyAllocator() : allocated(0), freelist(nullptr) {}
};

template <class C>
inline C *
thread_alloc(ClassAllocator<C> &a, ProxyAllocator &l)
{
  if (!cmd_disable_pfreelist && l.freelist) {
    C *v       = (C *)l.freelist;
    l.freelist = *(C **)l.freelist;
    --(l.allocated);
    *(void **)v = *(void **)&a.proto.typeObject;
    return v;
  }
  return a.alloc();
}

template <class C>
inline C *
thread_alloc_init(ClassAllocator<C> &a, ProxyAllocator &l)
{
  if (!cmd_disable_pfreelist && l.freelist) {
    C *v       = (C *)l.freelist;
    l.freelist = *(C **)l.freelist;
    --(l.allocated);
    memcpy((void *)v, (void *)&a.proto.typeObject, sizeof(C));
    return v;
  }
  return a.alloc();
}

template <class C>
inline void
thread_free(ClassAllocator<C> &a, C *p)
{
  a.free(p);
}

static inline void
thread_free(Allocator &a, void *p)
{
  a.free_void(p);
}

template <class C>
inline void
thread_freeup(ClassAllocator<C> &a, ProxyAllocator &l)
{
  C *head      = (C *)l.freelist;
  C *tail      = (C *)l.freelist;
  size_t count = 0;
  while (l.freelist && l.allocated > thread_freelist_low_watermark) {
    tail       = (C *)l.freelist;
    l.freelist = *(C **)l.freelist;
    --(l.allocated);
    ++count;
  }

  if (unlikely(count == 1)) {
    a.free(tail);
  } else if (count > 0) {
    a.free_bulk(head, tail, count);
  }

  ink_assert(l.allocated >= thread_freelist_low_watermark);
}

void *thread_alloc(Allocator &a, ProxyAllocator &l);
void thread_freeup(Allocator &a, ProxyAllocator &l);

#define THREAD_ALLOC(_a, _t) thread_alloc(::_a, _t->_a)
#define THREAD_ALLOC_INIT(_a, _t) thread_alloc_init(::_a, _t->_a)
#define THREAD_FREE(_p, _a, _t)                              \
  if (!cmd_disable_pfreelist) {                              \
    do {                                                     \
      *(char **)_p    = (char *)_t->_a.freelist;             \
      _t->_a.freelist = _p;                                  \
      _t->_a.allocated++;                                    \
      if (_t->_a.allocated > thread_freelist_high_watermark) \
        thread_freeup(::_a, _t->_a);                         \
    } while (0);                                             \
  } else {                                                   \
    thread_free(::_a, _p);                                   \
  }

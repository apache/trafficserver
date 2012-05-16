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
#ifndef _I_ProxyAllocator_h_
#define _I_ProxyAllocator_h_

#include "libts.h"

class EThread;

#define MAX_ON_THREAD_FREELIST 512

struct ProxyAllocator
{
  int allocated;
  void *freelist;

  ProxyAllocator():allocated(0), freelist(0) { }
};

template<class C> inline C * thread_alloc(ClassAllocator<C> &a, ProxyAllocator & l)
{
  if (l.freelist) {
    C *v = (C *) l.freelist;
    l.freelist = *(C **) l.freelist;
    l.allocated--;
    *(void **) v = *(void **) &a.proto.typeObject;
    return v;
  }
  return a.alloc();
}

template<class C> inline C * thread_alloc_init(ClassAllocator<C> &a, ProxyAllocator & l)
{
  if (l.freelist) {
    C *v = (C *) l.freelist;
    l.freelist = *(C **) l.freelist;
    l.allocated--;
    memcpy((void *) v, (void *) &a.proto.typeObject, sizeof(C));
    return v;
  }
  return a.alloc();
}

template<class C> inline void
thread_freeup(ClassAllocator<C> &a, ProxyAllocator & l)
{
  while (l.freelist) {
    C *v = (C *) l.freelist;
    l.freelist = *(C **) l.freelist;
    l.allocated--;
    a.free(v);                  // we could use a bulk free here
  }
  ink_assert(!l.allocated);
}

#if defined(TS_USE_FREELIST)
#define THREAD_ALLOC(_a, _t) thread_alloc(::_a, _t->_a)
#define THREAD_ALLOC_INIT(_a, _t) thread_alloc_init(::_a, _t->_a)
#define THREAD_FREE_TO(_p, _a, _t, _m) do { \
  *(char **)_p = (char*)_t->_a.freelist;    \
  _t->_a.freelist = _p;                     \
  _t->_a.allocated++;                       \
  if (_t->_a.allocated > _m)                \
    thread_freeup(::_a, _t->_a);            \
} while (0)
#else
#define THREAD_ALLOC(_a, _t) ::_a.alloc()
#define THREAD_ALLOC_INIT(_a, _t) ::_a.alloc()
#define THREAD_FREE_TO(_p, _a, _t, _m) ::_a.free(_p)
#endif
#define THREAD_FREE(_p, _a, _t) \
        THREAD_FREE_TO(_p, _a, _t, MAX_ON_THREAD_FREELIST)

#endif /* _ProxyAllocator_h_ */

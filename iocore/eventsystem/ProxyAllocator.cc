/** @file

    Implementation of the proxy allocators.

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
#include "I_EventSystem.h"

int thread_freelist_size = 512;

void*
thread_alloc(Allocator &a, ProxyAllocator &l)
{
#if TS_USE_FREELIST && !TS_USE_RECLAIMABLE_FREELIST
  if (l.freelist) {
    void *v = (void *) l.freelist;
    l.freelist = *(void **) l.freelist;
    l.allocated--;
    return v;
  }
#else
  (void)l;
#endif
  return a.alloc_void();
}

void
thread_freeup(Allocator &a, ProxyAllocator &l)
{
  while (l.freelist) {
    void *v = (void *) l.freelist;
    l.freelist = *(void **) l.freelist;
    l.allocated--;
    a.free_void(v);                  // we could use a bulk free here
  }
  ink_assert(!l.allocated);
}

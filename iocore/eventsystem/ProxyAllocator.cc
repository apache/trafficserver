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
// must include for defines
#include "ts/ink_config.h"

#undef HAVE_LIBJEMALLOC

// safe to use older system
#include "I_ProxyAllocator.h"

int thread_freelist_high_watermark = 512;
int thread_freelist_low_watermark  = 32;

void *
thread_alloc(Allocator &a, ProxyAllocator &l)
{
  if (l.freelist) {
    void *v    = (void *)l.freelist;
    l.freelist = *(void **)l.freelist;
    --(l.allocated);
    return v;
  }
  return a.alloc_void();
}

void
thread_freeup(Allocator &a, ProxyAllocator &l)
{
  void *head   = (void *)l.freelist;
  void *tail   = (void *)l.freelist;
  size_t count = 0;
  while (l.freelist && l.allocated > thread_freelist_low_watermark) {
    tail       = l.freelist;
    l.freelist = *(void **)l.freelist;
    --(l.allocated);
    ++count;
  }

  if (unlikely(count == 1)) {
    a.free_void(tail);
  } else if (count > 0) {
    a.free_void_bulk(head, tail, count);
  }

  ink_assert(l.allocated >= thread_freelist_low_watermark);
}

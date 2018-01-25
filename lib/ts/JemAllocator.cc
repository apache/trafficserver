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

#include "ts/JemAllocator.h"
#include "ts/jemallctl.h"

#include "ts/ink_queue.h"
#include "ts/ink_defs.h"
#include "ts/ink_resource.h"
#include "ts/ink_align.h"
#include "ts/ink_memory.h"
#include "ts/hugepages.h"

#if !HAVE_LIBJEMALLOC
#define MALLOCX_ALIGN(a) 0
#define MALLOCX_ARENA(a) 0
#endif

//
// waits until first alloc ... to be in correct thread / use context
//
void
AlignedAllocator::init_premapped(std::atomic_uint *preMappedP, unsigned len, unsigned align, unsigned chunk_size, unsigned arena)
{
  auto pgsz               = ats_hugepage_enabled() ? ats_hugepage_size() : ats_pagesize();
  unsigned lastPreMapped  = 0;
  unsigned olastPreMapped = lastPreMapped;

  // align up to number that fit into one (or more) pages
  chunk_size = aligned_size(chunk_size * len, pgsz) / len;

  // continue until satisfied or substituted
  do {
    olastPreMapped = *preMappedP;
    // check if value should be substituted

    if (olastPreMapped >= chunk_size) {
      return; // RETURN done by other thread
    }

    lastPreMapped = preMappedP->compare_exchange_strong(olastPreMapped, chunk_size); // try

  } while (lastPreMapped != olastPreMapped); // repeat to test again..

  // value was changed by this thread...

  // updated value to chunk_size .. so pre-map the memory needed
  init_precached(len, align, chunk_size - lastPreMapped, arena); // same process as precache
}

void
AlignedAllocator::init_precached(unsigned len, unsigned align, unsigned chunk_size, unsigned arena)
{
  void *preCached[chunk_size];

  // must shrink alignment if not a power-of-two
  for (auto t = align; (t &= (t - 1));) {
    align = t; // top-set-bit isn't gone
  }

  auto flags = MALLOCX_ALIGN(align) | (arena != ~0U ? MALLOCX_ARENA(arena) : 0);

  for (int n = chunk_size; n--;) {
    preCached[n] = mallocx(len, flags);
  }
  for (int n = chunk_size; n--;) {
    sdallocx(preCached[n], len, flags);
  }
}

void
AlignedAllocator::re_init(const char *name, unsigned int element_size, unsigned int chunk_size, unsigned int alignment, int advice)
{
  _name      = name;
  _chunkSize = chunk_size;
  _sz        = aligned_size(element_size, std::max(sizeof(uint64_t), alignment + 0UL)); // increase to aligned size
  _align     = alignment;

  _arena = 0; // default arena

#if HAVE_LIBJEMALLOC
  if (advice == MADV_DONTDUMP) {
    static int arena_nodump = jemallctl::create_global_nodump_arena();
    _arena                  = arena_nodump;
  } else if (advice != MADV_NORMAL) {
    ink_abort("allocator re_init: unknown madvise() flags: %x", advice);
  }

  init_premapped(&_preMapped, _sz, _align, _chunkSize, _arena);
#endif
}

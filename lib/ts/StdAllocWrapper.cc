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

#include "StdAllocWrapper.h"
#include "ts/jemallctl.h"

#include "ts/ink_queue.h"
#include "ts/ink_defs.h"
#include "ts/ink_resource.h"
#include "ts/ink_align.h"
#include "ts/ink_memory.h"

void
AlignedAllocator::re_init(const char *name, unsigned int element_size, unsigned int chunk_size, unsigned int alignment, int advice)
{
  _name = name;
  _sz   = aligned_spacing(element_size, std::max(sizeof(uint64_t), alignment + 0UL)); // increase to aligned size

  if (advice == MADV_DONTDUMP) {
    static int arena_nodump = numa::create_global_nodump_arena();
    _arena                  = arena_nodump;
  } else if (advice == MADV_NORMAL) {
    _arena = 0; // default arena
  } else {
    ink_abort("allocator re_init: unknown madvise() flags: %x", advice);
  }

  void *preCached[chunk_size];

  for (int n = chunk_size; n--;) {
    preCached[n] = mallocx(_sz, (MALLOCX_ALIGN(_sz) | MALLOCX_ARENA(_arena)));
  }
  for (int n = chunk_size; n--;) {
    deallocate(preCached[n]);
  }
}

AlignedAllocator::AlignedAllocator(const char *name, unsigned int element_size)
  : _name(name), _sz(aligned_spacing(element_size, sizeof(uint64_t)))
{
  // don't pre-allocate before main() is called
}

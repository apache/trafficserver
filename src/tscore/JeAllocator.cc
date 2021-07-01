/*
 * Copyright 2016-present Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <unistd.h>
#include <sys/types.h>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include "tscore/ink_memory.h"
#include "tscore/ink_error.h"
#include "tscore/ink_assert.h"
#include "tscore/ink_align.h"
#include "tscore/JeAllocator.h"
#include "tscore/Diags.h"

namespace jearena
{
#if JEMALLOC_NODUMP_ALLOCATOR_SUPPORTED
thread_local extent_alloc_t *JemallocNodumpAllocator::original_alloc = nullptr;
thread_local extent_hooks_t JemallocNodumpAllocator::extent_hooks;
thread_local int JemallocNodumpAllocator::arena_flags = 0;

void *
JemallocNodumpAllocator::alloc(extent_hooks_t *extent, void *new_addr, size_t size, size_t alignment, bool *zero, bool *commit,
                               unsigned int arena_id)
{
  void *result = original_alloc(extent, new_addr, size, alignment, zero, commit, arena_id);

  if (result != nullptr) {
    // Seems like we don't really care if the advice went through
    // in the original code, so just keeping it the same here.
    ats_madvise((caddr_t)result, size, MADV_DONTDUMP);
  }

  return result;
}

int
JemallocNodumpAllocator::extend_and_setup_arena()
{
  unsigned int arena_id;
  size_t arena_id_len = sizeof(arena_id);
  if (auto ret = mallctl("arenas.create", &arena_id, &arena_id_len, nullptr, 0)) {
    ink_abort("Unable to extend arena: %s", std::strerror(ret));
  }

  int flags = MALLOCX_ARENA(arena_id) | MALLOCX_TCACHE_NONE;

  // Read the existing hooks
  const auto key = "arena." + std::to_string(arena_id) + ".extent_hooks";
  extent_hooks_t *hooks;
  size_t hooks_len = sizeof(hooks);
  if (auto ret = mallctl(key.c_str(), &hooks, &hooks_len, nullptr, 0)) {
    ink_abort("Unable to get the hooks: %s", std::strerror(ret));
  }

  // Set the custom hook
  original_alloc     = hooks->alloc;
  extent_hooks       = *hooks;
  extent_hooks.alloc = &JemallocNodumpAllocator::alloc;

  extent_hooks_t *new_hooks = &extent_hooks;
  if (auto ret = mallctl(key.c_str(), nullptr, nullptr, &new_hooks, sizeof(new_hooks))) {
    ink_abort("Unable to set the hooks: %s", std::strerror(ret));
  }

  Debug("JeAllocator", "arena \"%ud\" created with flags \"%d\"", arena_id, flags);

  arena_flags = flags;

  return flags;
}
#endif /* JEMALLOC_NODUMP_ALLOCATOR_SUPPORTED */

/**
 * This will retain the original functionality if
 * !defined(JEMALLOC_NODUMP_ALLOCATOR_SUPPORTED)
 */
void *
JemallocNodumpAllocator::allocate(InkFreeList *f)
{
#if JEMALLOC_NODUMP_ALLOCATOR_SUPPORTED
  int flags = arena_flags;
  if (unlikely(flags == 0)) {
    flags = extend_and_setup_arena();
  }
#endif

  void *newp = nullptr;
  if (f->advice) {
#if JEMALLOC_NODUMP_ALLOCATOR_SUPPORTED
    if (likely(f->type_size > 0)) {
      flags |= MALLOCX_ALIGN(f->alignment);
      if (unlikely((newp = mallocx(f->type_size, flags)) == nullptr)) {
        ink_abort("couldn't allocate %u bytes", f->type_size);
      }
    }
#else
    newp = ats_memalign(f->alignment, f->type_size);
    if (INK_ALIGN((uint64_t)newp, ats_pagesize()) == (uint64_t)newp) {
      ats_madvise(static_cast<caddr_t>(newp), INK_ALIGN(f->type_size, f->alignment), f->advice);
    }
#endif
  } else {
    newp = ats_memalign(f->alignment, f->type_size);
  }
  return newp;
}

JemallocNodumpAllocator &
globalJemallocNodumpAllocator()
{
  static auto instance = new JemallocNodumpAllocator();
  return *instance;
}
} // namespace jearena

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

// https://github.com/jemalloc/jemalloc/releases
// Requires jemalloc 5.0.0 or above.

#pragma once

#include "tscore/ink_config.h"
#include "tscore/ink_queue.h"
#include <sys/mman.h>
#include <cstddef>
#include <unordered_map>
#include <mutex>
#include <shared_mutex>

#if TS_HAS_JEMALLOC
#include <jemalloc/jemalloc.h>
#if (JEMALLOC_VERSION_MAJOR == 0)
#error jemalloc has bogus version
#endif
#if (JEMALLOC_VERSION_MAJOR == 5) && defined(MADV_DONTDUMP)
#define JEMALLOC_NODUMP_ALLOCATOR_SUPPORTED 1
#else
#define JEMALLOC_NODUMP_ALLOCATOR_SUPPORTED 0
#endif /* MADV_DONTDUMP */
#endif /* TS_HAS_JEMALLOC */

namespace jearena
{
/**
 * An allocator which uses Jemalloc to create an dedicated arena to allocate
 * memory from. The only special property set on the allocated memory is that
 * the memory is not dump-able.
 *
 * This is done by setting MADV_DONTDUMP using the `madvise` system call. A
 * custom hook installed which is called when allocating a new chunk / extent of
 * memory.  All it does is call the original jemalloc hook to allocate the
 * memory and then set the advise on it before returning the pointer to the
 * allocated memory. Jemalloc does not use allocated chunks / extents across
 * different arenas, without `munmap`-ing them first, and the advises are not
 * sticky i.e. they are unset if `munmap` is done. Also this arena can't be used
 * by any other part of the code by just calling `malloc`.
 *
 * If target system doesn't support MADV_DONTDUMP or jemalloc doesn't support
 * custom arena hook, JemallocNodumpAllocator would fall back to using malloc /
 * free. Such behavior can be identified by using
 * !defined(JEMALLOC_NODUMP_ALLOCATOR_SUPPORTED).
 *
 * Similarly, if binary isn't linked with jemalloc, the logic would fall back to
 * malloc / free.
 */
class JemallocNodumpAllocator
{
public:
  void *allocate(InkFreeList *f);
  void deallocate(InkFreeList *f, void *ptr);

private:
#if JEMALLOC_NODUMP_ALLOCATOR_SUPPORTED
  thread_local static extent_alloc_t *original_alloc;
  thread_local static extent_hooks_t extent_hooks;
  thread_local static int arena_flags;

  static void *alloc(extent_hooks_t *extent, void *new_addr, size_t size, size_t alignment, bool *zero, bool *commit,
                     unsigned int arena_id);
  int extend_and_setup_arena();
#endif /* JEMALLOC_NODUMP_ALLOCATOR_SUPPORTED */
};

/**
 * JemallocNodumpAllocator singleton.
 */
JemallocNodumpAllocator &globalJemallocNodumpAllocator();

} /* namespace jearena */

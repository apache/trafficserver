/** @file

  Memory allocation routines for libts

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
#include "ts/jemallctl.h"
#include "ts/hugepages.h"

#include "ts/ink_platform.h"
#include "ts/ink_memory.h"
#include "ts/ink_defs.h"
#include "ts/ink_stack_trace.h"
#include "ts/Diags.h"
#include "ts/ink_atomic.h"
#include "ts/ink_align.h"

#if defined(freebsd)
#include <malloc_np.h> // for malloc_usable_size
#endif

#include <cassert>
#if defined(linux)
// XXX: SHouldn't that be part of CPPFLAGS?
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif
#endif
#include <cstdlib>
#include <cstring>

void *
ats_malloc(size_t size)
{
  void *ptr = nullptr;

  /*
   * There's some nasty code in libts that expects
   * a MALLOC of a zero-sized item to work properly. Rather
   * than allocate any space, we simply return a nullptr to make
   * certain they die quickly & don't trash things.
   */

  // Useful for tracing bad mallocs
  // ink_stack_trace_dump();
  if (likely(size > 0)) {
    if (unlikely((ptr = malloc(size)) == nullptr)) {
#ifndef HAVE_LIBJEMALLOC
      ink_abort("couldn't allocate %zu bytes", size);
#else
      auto arena = jemallctl::thread_arena();
      ink_warning("ats_malloc: couldn't allocate %zu bytes in arena %d", size, arena);
      ink_warning("ats_malloc: current alloced: %#lx", jemallctl::stats_allocated());
      ink_warning("ats_malloc: current active: %#lx", jemallctl::stats_cactive()->operator uint64_t());
      ink_abort("couldn't allocate %zu bytes", size);
#endif
    }
  }
  return ptr;
} /* End ats_malloc */

void *
ats_calloc(size_t nelem, size_t elsize)
{
  void *ptr = calloc(nelem, elsize);
  if (unlikely(ptr == nullptr)) {
    ink_abort("couldn't allocate %zu %zu byte elements", nelem, elsize);
  }
  return ptr;
} /* End ats_calloc */

void *
ats_realloc(void *ptr, size_t size)
{
  void *newptr = realloc(ptr, size);
  if (unlikely(newptr == nullptr)) {
    ink_abort("couldn't reallocate %zu bytes", size);
  }
  return newptr;
} /* End ats_realloc */

// TODO: For Win32 platforms, we need to figure out what to do with memalign.
// The older code had ifdef's around such calls, turning them into ats_malloc().
void *
ats_memalign(size_t alignment, size_t size)
{
  void *ptr;

#if HAVE_POSIX_MEMALIGN || TS_HAS_JEMALLOC
  if (alignment <= 8) {
    return ats_malloc(size);
  }

#if defined(openbsd)
  if (alignment > PAGE_SIZE)
    alignment = PAGE_SIZE;
#endif

  int retcode = posix_memalign(&ptr, alignment, size);

  if (unlikely(retcode)) {
    if (retcode == EINVAL) {
      ink_abort("couldn't allocate %zu bytes at alignment %zu - invalid alignment parameter", size, alignment);
    } else if (retcode == ENOMEM) {
      ink_abort("couldn't allocate %zu bytes at alignment %zu - insufficient memory", size, alignment);
    } else {
      ink_abort("couldn't allocate %zu bytes at alignment %zu - unknown error %d", size, alignment, retcode);
    }
  }
#else
  ptr = memalign(alignment, size);
  if (unlikely(ptr == nullptr)) {
    ink_abort("couldn't allocate %zu bytes at alignment %zu", size, alignment);
  }
#endif
  return ptr;
} /* End ats_memalign */

void
ats_free(void *ptr)
{
  if (likely(ptr != nullptr)) {
    free(ptr);
  }
} /* End ats_free */

void *
ats_free_null(void *ptr)
{
  if (likely(ptr != nullptr)) {
    free(ptr);
  }
  return nullptr;
} /* End ats_free_null */

void
ats_memalign_free(void *ptr)
{
  if (likely(ptr)) {
    free(ptr);
  }
}

// This effectively makes mallopt() a no-op (currently) when tcmalloc
// or jemalloc is used. This might break our usage for increasing the
// number of mmap areas (ToDo: Do we still really need that??).
//
// TODO: I think we might be able to get rid of this?
int
ats_mallopt(int param ATS_UNUSED, int value ATS_UNUSED)
{
#if HAVE_LIBJEMALLOC
  // TODO: jemalloc code ?
  return 0;
#elif TS_HAS_TCMALLOC
  // TODO: tcmalloc code ?
  return 0;
#elif defined(linux)
  return mallopt(param, value);
#else
  return 0;
#endif
}

int
ats_msync(caddr_t addr, size_t len, caddr_t end, int flags)
{
  size_t pagesize = ats_pagesize();

  // align start back to page boundary
  caddr_t a = (caddr_t)(((uintptr_t)addr) & ~(pagesize - 1));
  // align length to page boundry covering region
  size_t l = (len + (addr - a) + (pagesize - 1)) & ~(pagesize - 1);
  if ((a + l) > end) {
    l = end - a; // strict limit
  }
#if defined(linux)
/* Fix INKqa06500
   Under Linux, msync(..., MS_SYNC) calls are painfully slow, even on
   non-dirty buffers. This is true as of kernel 2.2.12. We sacrifice
   restartability under OS in order to avoid a nasty performance hit
   from a kernel global lock. */
#if 0
  // this was long long ago
  if (flags & MS_SYNC)
    flags = (flags & ~MS_SYNC) | MS_ASYNC;
#endif
#endif
  int res = msync(a, l, flags);
  return res;
}

int
ats_madvise(caddr_t addr, size_t len, int flags)
{
#if HAVE_POSIX_MADVISE
  return posix_madvise(addr, len, flags);
#else
  return madvise(addr, len, flags);
#endif
}

int
ats_mlock(caddr_t addr, size_t len)
{
  size_t pagesize = ats_pagesize();

  caddr_t a = (caddr_t)(((uintptr_t)addr) & ~(pagesize - 1));
  size_t l  = (len + (addr - a) + pagesize - 1) & ~(pagesize - 1);
  int res   = mlock(a, l);
  return res;
}

void *
ats_track_malloc(size_t size, uint64_t *stat)
{
  void *ptr = ats_malloc(size);
#ifdef HAVE_MALLOC_USABLE_SIZE
  ink_atomic_increment(stat, malloc_usable_size(ptr));
#endif
  return ptr;
}

void *
ats_track_realloc(void *ptr, size_t size, uint64_t *alloc_stat, uint64_t *free_stat)
{
#ifdef HAVE_MALLOC_USABLE_SIZE
  const size_t old_size = malloc_usable_size(ptr);
  ptr                   = ats_realloc(ptr, size);
  const size_t new_size = malloc_usable_size(ptr);
  if (old_size < new_size) {
    // allocating something bigger
    ink_atomic_increment(alloc_stat, new_size - old_size);
  } else if (old_size > new_size) {
    ink_atomic_increment(free_stat, old_size - new_size);
  }
  return ptr;
#else
  return ats_realloc(ptr, size);
#endif
}

void
ats_track_free(void *ptr, uint64_t *stat)
{
  if (ptr == nullptr) {
    return;
  }

#ifdef HAVE_MALLOC_USABLE_SIZE
  ink_atomic_increment(stat, malloc_usable_size(ptr));
#endif
  ats_free(ptr);
}

/*-------------------------------------------------------------------------
  Moved from old ink_resource.h
  -------------------------------------------------------------------------*/
char *
_xstrdup(const char *str, int length, const char * /* path ATS_UNUSED */)
{
  char *newstr;

  if (likely(str)) {
    if (length < 0) {
      length = strlen(str);
    }

    newstr = (char *)ats_malloc(length + 1);
    // If this is a zero length string just null terminate and return.
    if (unlikely(length == 0)) {
      *newstr = '\0';
    } else {
      strncpy(newstr, str, length); // we cannot do length + 1 because the string isn't
      newstr[length] = '\0';        // guaranteeed to be null terminated!
    }
    return newstr;
  }
  return nullptr;
}

void *
ats_alloc_stack(size_t stacksize)
{
  if (!ats_hugepage_enabled()) {
    // get memory that grows down and is not populated until needed
    return mmap(nullptr, stacksize, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_GROWSDOWN | MAP_PRIVATE, -1, 0);
  }

  //    [but prefer hugepage alignment and request if possible]
  auto p = mmap(nullptr, stacksize, PROT_READ | PROT_WRITE, (MAP_ANONYMOUS | MAP_GROWSDOWN | MAP_PRIVATE), -1, 0);
  if (stacksize == aligned_spacing(stacksize, ats_hugepage_size())) {
    madvise(p, stacksize, MADV_HUGEPAGE); // opt in
  }

  return p;
}

#if HAVE_LIBJEMALLOC
namespace numa 
{

namespace
{
  chunk_alloc_t *s_origAllocHook = nullptr; // safe pre-main
}

int
create_global_nodump_arena()
{
  auto origArena = jemallctl::thread_arena();

  // fork from base nodes set (id#0)
  auto newArena = jemallctl::do_arenas_extend();

  jemallctl::set_thread_arena(newArena);

  chunk_hooks_t origHooks = jemallctl::thread_arena_hooks();
  s_origAllocHook         = origHooks.alloc;

  origHooks.alloc = [](void *old, size_t len, size_t aligned, bool *zero, bool *commit, unsigned arena) {
    void *r = (*s_origAllocHook)(old, len, aligned, zero, commit, arena);

    if (r) {
      madvise(r, aligned_spacing(len, aligned), MADV_DONTDUMP);
    }

    return r;
  };

  jemallctl::set_thread_arena_hooks(origHooks);

  jemallctl::set_thread_arena(origArena); // default again
  return newArena;
}

} // namespace numa
#endif

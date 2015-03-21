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

/***********************************************************************

    Reclaimable freelist Implementation

***********************************************************************/

#include "ink_config.h"
#include <assert.h>
#include <memory.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include "ink_thread.h"
#include "ink_atomic.h"
#include "ink_queue.h"
#include "ink_memory.h"
#include "ink_error.h"
#include "ink_assert.h"
#include "ink_stack_trace.h"
#include "ink_queue_ext.h"

#if TS_USE_RECLAIMABLE_FREELIST

#define CEIL(x, y) (((x) + (y)-1L) / (y))
#define ROUND(x, l) (((x) + ((l)-1L)) & ~((l)-1L))
#define ITEM_MAGIC 0xFF

#define MAX_NUM_FREELIST 1024

/*
 * Configurable Variables
 */
float cfg_reclaim_factor = 0.3;
int64_t cfg_max_overage = 10;
int64_t cfg_enable_reclaim = 0;
/*
 * Debug filter bit mask:
 *  bit 0: reclaim in ink_freelist_new
 *  bit 1: reclaim in ink_freelist_free
 *  bit 2: fetch memory from thread cache
 */
int64_t cfg_debug_filter;

static uint32_t nr_freelist;
static uint64_t total_mem_in_byte;
static __thread InkThreadCache *ThreadCaches[MAX_NUM_FREELIST];

#define MAX_CHUNK_BYTE_SIZE (ats_pagesize() << 8)

/*
 * For debug
 */
#define show_info(tag, f, pCache) __show_info(stdout, __FILE__, __LINE__, tag, f, pCache)
#define error_info(tag, f, pCache) __show_info(stderr, __FILE__, __LINE__, tag, f, pCache)

static inline void
__show_info(FILE *fp, const char *file, int line, const char *tag, InkFreeList *f, InkThreadCache *pCache)
{
  fprintf(fp, "[%lx:%02u][%s:%05d][%s] %6.2fM t:%-8uf:%-4u m:%-4u avg:%-6.1f"
              " M:%-4u csbase:%-4u csize:%-4u tsize:%-6u cbsize:%u\n",
          (long)ink_thread_self(), f->thread_cache_idx, file, line, tag, ((double)total_mem_in_byte / 1024 / 1024),
          pCache->nr_total, pCache->nr_free, pCache->nr_min, pCache->nr_average, pCache->nr_malloc, f->chunk_size_base,
          f->chunk_size, f->type_size, f->chunk_byte_size);
}

static inline void
memory_alignment_init(InkFreeList *f, uint32_t type_size, uint32_t chunk_size, uint32_t alignment)
{
  uint32_t chunk_byte_size, user_alignment, user_type_size;

  f->chunk_size_base = chunk_size;
  user_alignment = alignment;
  user_type_size = type_size;
  chunk_size = 1;

#ifdef DEBUG
  /*
   * enlarge type_size to hold a item_magic
   */
  type_size += sizeof(unsigned char);
#endif

  /*
   * limit the size of each chunk and resize alignment.
   * 1) when size of chunk > MAX_CHUNK_BYTE_SIZE:
   *    alignment = page_size;
   * 2) when size of chunk <= MAX_CHUNK_BYTE_SIZE:
   *    alignment = (2^N * page_size),
   *    alignment should not larger than MAX_CHUNK_BYTE_SIZE
   */
  alignment = ats_pagesize();
  chunk_byte_size = ROUND(type_size + sizeof(InkChunkInfo), ats_pagesize());
  if (chunk_byte_size <= MAX_CHUNK_BYTE_SIZE) {
    chunk_byte_size = ROUND(type_size * f->chunk_size_base + sizeof(InkChunkInfo), ats_pagesize());

    if (chunk_byte_size > MAX_CHUNK_BYTE_SIZE) {
      chunk_size = (MAX_CHUNK_BYTE_SIZE - sizeof(InkChunkInfo)) / type_size;
      chunk_byte_size = ROUND(type_size * chunk_size + sizeof(InkChunkInfo), ats_pagesize());
    } else
      chunk_size = (chunk_byte_size - sizeof(InkChunkInfo)) / type_size;

    if (chunk_size > 1) {
      /* make alignment to be (2^N * page_size),
       * but not larger than MAX_CHUNK_BYTE_SIZE */
      while (alignment < chunk_byte_size)
        alignment <<= 1;
    }
  }

  if (user_alignment > alignment) {
    alignment = ats_pagesize();
    while (alignment < user_alignment)
      alignment <<= 1;
  }
  ink_release_assert(alignment <= MAX_CHUNK_BYTE_SIZE);

  f->alignment = alignment;
  f->type_size = user_type_size;
  f->chunk_size = chunk_size;
  f->chunk_addr_mask = ~((uintptr_t)(alignment - 1));
  f->chunk_byte_size = chunk_byte_size;

  return;
}

/*
 * mmap_align allocates _size_ bytes and returns a pointer to the
 * allocated memory, which address will be a multiple of _alignment_.
 *  1)the _size_ must be a multiple of page_size;
 *  2)the _alignment_ must be a power of page_size;
 */
static void *
mmap_align(size_t size, size_t alignment)
{
  uintptr_t ptr;
  size_t adjust, extra = 0;

  ink_assert(size % ats_pagesize() == 0);

  /* ask for extra memory if alignment > page_size */
  if (alignment > ats_pagesize()) {
    extra = alignment - ats_pagesize();
  }
  void *result = mmap(NULL, size + extra, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
  if (result == MAP_FAILED) {
    ink_stack_trace_dump();
    const char *err_str = "Out of memory, or the process's maximum number of "
                          "mappings would have been exceeded(if so, you can "
                          "enlarge 'vm.max_map_count' by sysctl in linux).";
    ink_fatal("Failed to mmap %zu bytes, %s", size, (errno == ENOMEM) ? err_str : strerror(errno));
  }

  /* adjust the return memory so it is aligned */
  adjust = 0;
  ptr = (uintptr_t)result;
  if ((ptr & (alignment - 1)) != 0) {
    adjust = alignment - (ptr & (alignment - 1));
  }

  /* return the unused memory to the system */
  if (adjust > 0) {
    munmap((void *)ptr, adjust);
  }
  if (adjust < extra) {
    munmap((void *)(ptr + adjust + size), extra - adjust);
  }

  ptr += adjust;
  ink_assert((ptr & (alignment - 1)) == 0);
  return (void *)ptr;
}

#ifdef DEBUG
static inline uint32_t
get_chunk_item_magic_idx(InkFreeList *f, void *item, InkChunkInfo **ppChunk, bool do_check = false)
{
  uint32_t idx;
  uintptr_t chunk_addr;

  if (f->chunk_size > 1)
    chunk_addr = (uintptr_t)item & f->chunk_addr_mask;
  else
    chunk_addr = (uintptr_t)item;

  if (*ppChunk == NULL)
    *ppChunk = (InkChunkInfo *)(chunk_addr + f->type_size * f->chunk_size);

  idx = ((uintptr_t)item - chunk_addr) / f->type_size;

  if (do_check && (idx >= f->chunk_size || ((uintptr_t)item - chunk_addr) % f->type_size)) {
    ink_stack_trace_dump();
    ink_fatal("Invalid address:%p, chunk_addr:%p, type_size:%d, chunk_size:%u, idx:%u", item, (void *)chunk_addr, f->type_size,
              f->chunk_size, idx);
  }

  return idx;
}

static inline void
set_chunk_item_magic(InkFreeList *f, InkChunkInfo *pChunk, void *item)
{
  uint32_t idx;

  idx = get_chunk_item_magic_idx(f, item, &pChunk);

  ink_release_assert(pChunk->item_magic[idx] == 0);

  pChunk->item_magic[idx] = ITEM_MAGIC;
}

static inline void
clear_chunk_item_magic(InkFreeList *f, InkChunkInfo *pChunk, void *item)
{
  uint32_t idx;

  idx = get_chunk_item_magic_idx(f, item, &pChunk, true);

  ink_release_assert(pChunk->item_magic[idx] == ITEM_MAGIC);

  pChunk->item_magic[idx] = 0;
}
#else
#define set_chunk_item_magic(a, b, c)
#define clear_chunk_item_magic(a, b, c)
#endif

static inline InkChunkInfo *
get_chunk_info_addr(InkFreeList *f, void *item)
{
  uintptr_t chunk_addr;

  if (f->chunk_size > 1)
    chunk_addr = (uintptr_t)item & f->chunk_addr_mask;
  else
    chunk_addr = (uintptr_t)item;

  return (InkChunkInfo *)(chunk_addr + f->type_size * f->chunk_size);
}

static inline InkChunkInfo *
ink_chunk_create(InkFreeList *f, InkThreadCache *pCache)
{
  uint32_t i;
  uint32_t type_size, chunk_size;
  void *chunk_addr, *curr, *next;
  InkChunkInfo *pChunk;

  chunk_addr = mmap_align(f->chunk_byte_size, f->alignment);
  pChunk = (InkChunkInfo *)((char *)chunk_addr + f->type_size * f->chunk_size);

  type_size = f->type_size;
  chunk_size = f->chunk_size;

  pChunk->tid = ink_thread_self();
  pChunk->head = chunk_addr;
  pChunk->type_size = type_size;
  pChunk->chunk_size = chunk_size;
  pChunk->length = f->chunk_byte_size;
  pChunk->allocated = 0;
  pChunk->pThreadCache = pCache;
  pChunk->link = Link<InkChunkInfo>();

#ifdef DEBUG
/*
 * The content will be initialized to zero when
 * calls mmap() with MAP_ANONYMOUS flag on linux
 * platform.
 */
#if !defined(linux)
  memset(pChunk->item_magic, 0, chunk_size * sizeof(unsigned char));
#endif
#endif

  curr = pChunk->head;
  pChunk->inner_free_list = curr;
  for (i = 1; i < chunk_size; i++) {
    next = (void *)((char *)curr + type_size);
    *(void **)curr = next;
    curr = next;
  }
  *(void **)curr = NULL;

  ink_atomic_increment(&f->allocated, chunk_size);
  ink_atomic_increment(&total_mem_in_byte, f->chunk_byte_size);

  pCache->free_chunk_list.push(pChunk);
  pCache->nr_free_chunks++;
  return pChunk;
}

static inline void
ink_chunk_delete(InkFreeList *f, InkThreadCache *pCache, InkChunkInfo *pChunk)
{
  void *chunk_addr = pChunk->head;

  ink_assert(pChunk->allocated == 0);

  pCache->free_chunk_list.remove(pChunk);
  pCache->nr_free_chunks--;

  if (unlikely(munmap(chunk_addr, f->chunk_byte_size))) {
    ink_stack_trace_dump();
    ink_fatal("Failed to munmap %u bytes, %s", f->chunk_byte_size, strerror(errno));
  }

  ink_atomic_increment((int *)&f->allocated, -f->chunk_size);

  /*
   * TODO: I had used ink_atomic_increment() here, but it would
   * lead to incorrect value in linux OS, I don't know why:
   *  ink_atomic_increment((int64_t *)&total_mem_in_byte, -f->chunk_byte_size);
   *
   * So I create a new wrap, ink_atomic_decrement(), in ink_atomic.h,
   * it works well. But we should create the same wrap for other OS.
   */
  ink_atomic_decrement(&total_mem_in_byte, f->chunk_byte_size);
}

static inline void *
malloc_whole_chunk(InkFreeList *f, InkThreadCache *pCache, InkChunkInfo *pChunk)
{
  uint32_t i;
  uint32_t type_size, chunk_size;
  void *next, *item;

  ink_assert(pChunk->allocated == 0);

  type_size = f->type_size;
  chunk_size = f->chunk_size;

  item = pChunk->head;
  for (i = 1; i < chunk_size; i++) {
    next = (void *)((char *)item + i * type_size);
    ink_atomic_increment(&pCache->nr_free, 1);
    ink_atomiclist_push(&pCache->outer_free_list, next);
  }

  pChunk->allocated += chunk_size;
  pChunk->inner_free_list = NULL;
  pCache->nr_total += chunk_size;

  return item;
}

static inline void *
malloc_from_chunk(InkFreeList * /* f ATS_UNUSED */, InkThreadCache *pCache, InkChunkInfo *pChunk)
{
  void *item;

  if ((item = pChunk->inner_free_list)) {
    pChunk->inner_free_list = *(void **)item;
    pChunk->allocated++;
    pCache->nr_total++;
  }

  return item;
}

static inline void
free_to_chunk(InkFreeList *f, InkThreadCache *pCache, void *item)
{
  InkChunkInfo *pChunk;

  pChunk = get_chunk_info_addr(f, item);
  pChunk->allocated--;
  pCache->nr_total--;

  *(void **)item = pChunk->inner_free_list;
  pChunk->inner_free_list = item;

  if (pChunk->allocated == 0)
    ink_chunk_delete(f, pCache, pChunk);
}

static inline void *
malloc_from_cache(InkFreeList *f, InkThreadCache *pCache, uint32_t nr)
{
  void *item;
  InkChunkInfo *pChunk;

  pChunk = pCache->free_chunk_list.head;
  while (pChunk) {
    while ((item = malloc_from_chunk(f, pCache, pChunk))) {
      if (--nr == 0)
        return item;

      ink_atomic_increment(&pCache->nr_free, 1);
      ink_atomiclist_push(&pCache->outer_free_list, item);
    }
    pChunk = pChunk->link.next;
  }

  pChunk = ink_chunk_create(f, pCache);
  if (nr == f->chunk_size)
    return malloc_whole_chunk(f, pCache, pChunk);

  while ((item = malloc_from_chunk(f, pCache, pChunk))) {
    if (--nr == 0)
      return item;

    ink_atomic_increment(&pCache->nr_free, 1);
    ink_atomiclist_push(&pCache->outer_free_list, item);
  }

  ink_assert(0);
  return NULL;
}

static inline void
free_to_cache(InkFreeList *f, InkThreadCache *pCache, void *item, uint32_t nr)
{
  uint32_t n = nr;

  if (item)
    free_to_chunk(f, pCache, item);

  while (n && (item = ink_atomiclist_pop(&pCache->outer_free_list))) {
    free_to_chunk(f, pCache, item);
    n--;
  }
  ink_atomic_increment((int *)&pCache->nr_free, -(nr - n));
}

static inline void
refresh_average_info(InkThreadCache *pCache)
{
  uint32_t nr_free;
  float nr_average;

  nr_free = pCache->nr_free;
  nr_average = pCache->nr_average;

  if (pCache->status == 1 || nr_free < pCache->nr_min)
    pCache->nr_min = nr_free;

  pCache->nr_average = (nr_average * (1 - cfg_reclaim_factor)) + (nr_free * cfg_reclaim_factor);
}

static inline bool
need_to_reclaim(InkFreeList *f, InkThreadCache *pCache)
{
  if (!cfg_enable_reclaim)
    return false;

  if (pCache->nr_free >= pCache->nr_average && pCache->nr_total > f->chunk_size_base) {
    if (pCache->nr_overage++ >= cfg_max_overage) {
      pCache->nr_overage = 0;
      return true;
    }
    return false;
  }

  pCache->nr_overage = 0;
  return false;
}

void
reclaimable_freelist_init(InkFreeList **fl, const char *name, uint32_t type_size, uint32_t chunk_size, uint32_t alignment)
{
  InkFreeList *f;
  ink_freelist_list *fll = freelists;

  /* quick test for power of 2 */
  ink_assert(!(alignment & (alignment - 1)));

  /* NOTE: it's safe to operate on this global list because
   * ink_freelist_init() is only called from single-threaded
   * initialization code. */
  while (fll) {
    /* Reuse InkFreeList if it has the same type_size. */
    if (fll->fl->type_size == type_size) {
      fll->fl->refcnt++;
      *fl = fll->fl;
      return;
    }
    fll = fll->next;
  }

  f = (InkFreeList *)ats_memalign(alignment, sizeof(InkFreeList));
  fll = (ink_freelist_list *)ats_memalign(alignment, sizeof(ink_freelist_list));
  fll->fl = f;
  fll->next = freelists;
  freelists = fll;

  f->name = name;
  f->used = 0;
  f->allocated = 0;
  f->allocated_base = 0;
  f->used_base = 0;

  memory_alignment_init(f, type_size, chunk_size, alignment);

  f->refcnt = 1;
  f->pThreadCache = NULL;
  f->nr_thread_cache = 0;
  f->thread_cache_idx = nr_freelist++;
  ink_assert(f->thread_cache_idx < MAX_NUM_FREELIST);
  ink_mutex_init(&f->lock, "InkFreeList Lock");

  *fl = f;
}

void *
reclaimable_freelist_new(InkFreeList *f)
{
  void *ptr;
  uint32_t i, nr;
  uint32_t old_value;
  uint32_t num_to_move;
  InkChunkInfo *pChunk = NULL;
  InkThreadCache *pCache, *pNextCache;

  ink_atomic_increment(&f->used, 1);

  /* no thread cache, create it */
  if (unlikely((pCache = ThreadCaches[f->thread_cache_idx]) == NULL)) {
    pCache = (InkThreadCache *)ats_calloc(1, sizeof(InkThreadCache));

    pCache->f = f;
    pCache->free_chunk_list = DLL<InkChunkInfo>();

    /* this lock will only be accessed when initializing
     * thread cache, so it won't damage performance */
    ink_mutex_acquire(&f->lock);
    ink_atomiclist_init(&pCache->outer_free_list, f->name, 0);

    nr = CEIL(f->chunk_size_base, f->chunk_size);
    for (i = 0; i < nr; i++) {
      pChunk = ink_chunk_create(f, pCache);
    }

    pCache->nr_malloc = 1;

    ThreadCaches[f->thread_cache_idx] = pCache;

    if (f->pThreadCache) {
      /* we will loop pCache.next without lock, following
       * statement's sequence is important for us. */
      pCache->next = f->pThreadCache;
      pCache->prev = f->pThreadCache->prev;
      pCache->next->prev = pCache;
      pCache->prev->next = pCache;
    } else {
      pCache->next = pCache;
      pCache->prev = pCache;
    }

    f->pThreadCache = pCache;
    f->nr_thread_cache++;

    ink_mutex_release(&f->lock);

    ptr = malloc_whole_chunk(f, pCache, pChunk);
    set_chunk_item_magic(f, pChunk, ptr);
    return ptr;
  }

  pCache->status = 0;

  /* priority to fetch memory from outer_free_list */
  if ((ptr = ink_atomiclist_pop(&pCache->outer_free_list))) {
    old_value = ink_atomic_increment((int *)&pCache->nr_free, -1);
    ink_release_assert(old_value > 0);
    ink_atomic_increment(&pCache->nr_malloc, 1);
    set_chunk_item_magic(f, NULL, ptr);
    return ptr;
  }

  /* try to steal memory from other thread's outer_free_list */
  pNextCache = pCache->next;
  while (pNextCache != pCache) {
    if ((ptr = ink_atomiclist_pop(&pNextCache->outer_free_list))) {
      old_value = ink_atomic_increment((int *)&pNextCache->nr_free, -1);
      ink_release_assert(old_value > 0);
      ink_atomic_increment(&pNextCache->nr_malloc, 1);
      set_chunk_item_magic(f, NULL, ptr);
      return ptr;
    }
    pNextCache = pNextCache->next;
  }

  /* try to reclaim memory from all caches in the same thread */
  for (i = 0; i < nr_freelist; i++) {
    if ((pNextCache = ThreadCaches[i]) == NULL)
      continue;

    if (need_to_reclaim(pNextCache->f, pNextCache)) {
      if (cfg_debug_filter & 0x1)
        show_info("F", pNextCache->f, pNextCache);

      num_to_move = MIN(pNextCache->nr_average, pNextCache->nr_free);

      free_to_cache(pNextCache->f, pNextCache, NULL, num_to_move);

      if (cfg_debug_filter & 0x1)
        show_info("-", pNextCache->f, pNextCache);

      refresh_average_info(pNextCache);
    }
  }

  /* finally, fetch from thread local cache */
  if (cfg_debug_filter & 0x2)
    show_info("M", f, pCache);
  ptr = malloc_from_cache(f, pCache, f->chunk_size);
  if (cfg_debug_filter & 0x2)
    show_info("+", f, pCache);

  refresh_average_info(pCache);
  ink_atomic_increment(&pCache->nr_malloc, 1);
  set_chunk_item_magic(f, NULL, ptr);
  return ptr;
}

void
reclaimable_freelist_free(InkFreeList *f, void *item)
{
  InkChunkInfo *pChunk;
  InkThreadCache *pCache;

  if (item == NULL)
    return;

  pChunk = get_chunk_info_addr(f, item);
  clear_chunk_item_magic(f, pChunk, item);
  pCache = pChunk->pThreadCache;

  ink_atomic_increment((int *)&pCache->nr_malloc, -1);
  if (ink_atomic_cas((int *)&pCache->status, 0, 1))
    refresh_average_info(pCache);

  ink_atomic_increment(&pCache->nr_free, 1);
  ink_atomiclist_push(&pCache->outer_free_list, item);
  ink_atomic_increment(&f->used, -1);
}
#endif

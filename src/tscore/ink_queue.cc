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

/*****************************************************************************
  ink_queue.cc (This used to be ink_queue.c)

  This implements an atomic push/pop queue, and the freelist memory
  pools that are built from it.

  The change from ink_queue.cc to ink_queue.c resulted in some changes
  in access and store of 64 bit values. This is standardized by using
  the INK_QUEUE_LD64 macro which loads the version before the pointer
  (independent of endianness of native architecture) on 32 bit platforms
  or loads the 64 bit quantity directory on the DECs.


  ****************************************************************************/

#include "tscore/ink_config.h"
#include <cassert>
#include <memory.h>
#include <cstdlib>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include "tscore/ink_atomic.h"
#include "tscore/ink_queue.h"
#include "tscore/ink_memory.h"
#include "tscore/ink_error.h"
#include "tscore/ink_assert.h"
#include "tscore/ink_align.h"
#include "tscore/hugepages.h"
#include "tscore/Diags.h"
#include "tscore/JeAllocator.h"

#define DEBUG_TAG "freelist"

/*
 * SANITY and DEADBEEF are compute-intensive memory debugging to
 * help in diagnosing freelist corruption.  We turn them off in
 * release builds.
 */

#ifdef DEBUG
#define SANITY
#define DEADBEEF
#endif

static auto jna = jearena::globalJemallocNodumpAllocator();

struct ink_freelist_ops {
  void *(*fl_new)(InkFreeList *);
  void (*fl_free)(InkFreeList *, void *);
  void (*fl_bulkfree)(InkFreeList *, void *, void *, size_t);
};

typedef struct _ink_freelist_list {
  InkFreeList *fl;
  struct _ink_freelist_list *next;
} ink_freelist_list;

static void *freelist_new(InkFreeList *f);
static void freelist_free(InkFreeList *f, void *item);
static void freelist_bulkfree(InkFreeList *f, void *head, void *tail, size_t num_item);

static void *malloc_new(InkFreeList *f);
static void malloc_free(InkFreeList *f, void *item);
static void malloc_bulkfree(InkFreeList *f, void *head, void *tail, size_t num_item);

static const ink_freelist_ops malloc_ops   = {malloc_new, malloc_free, malloc_bulkfree};
static const ink_freelist_ops freelist_ops = {freelist_new, freelist_free, freelist_bulkfree};
static const ink_freelist_ops *default_ops = &freelist_ops;

static ink_freelist_list *freelists                = nullptr;
static const ink_freelist_ops *freelist_global_ops = default_ops;

const InkFreeListOps *
ink_freelist_malloc_ops()
{
  return &malloc_ops;
}

const InkFreeListOps *
ink_freelist_freelist_ops()
{
  return &freelist_ops;
}

void
ink_freelist_init_ops(int nofl_class, int nofl_proxy)
{
  // This *MUST* only be called at startup before any freelists allocate anything. We will certainly crash if object
  // allocated from the freelist are freed by malloc.
  ink_release_assert(freelist_global_ops == default_ops);

  freelist_global_ops = (nofl_class || nofl_proxy) ? ink_freelist_malloc_ops() : ink_freelist_freelist_ops();
}

void
ink_freelist_init(InkFreeList **fl, const char *name, uint32_t type_size, uint32_t chunk_size, uint32_t alignment)
{
  InkFreeList *f;
  ink_freelist_list *fll;

  /* its safe to add to this global list because ink_freelist_init()
     is only called from single-threaded initialization code. */
  f = (InkFreeList *)ats_memalign(alignment, sizeof(InkFreeList));
  ink_zero(*f);

  fll       = (ink_freelist_list *)ats_malloc(sizeof(ink_freelist_list));
  fll->fl   = f;
  fll->next = freelists;
  freelists = fll;

  f->name = name;
  /* quick test for power of 2 */
  ink_assert(!(alignment & (alignment - 1)));
  // It is never useful to have alignment requirement looser than a page size
  // so clip it. This makes the item alignment checks in the actual allocator simpler.
  f->alignment = alignment;
  if (f->alignment > ats_pagesize()) {
    f->alignment = ats_pagesize();
  }
  Debug(DEBUG_TAG "_init", "<%s> Alignment request/actual (%" PRIu32 "/%" PRIu32 ")", name, alignment, f->alignment);
  // Make sure we align *all* the objects in the allocation, not just the first one
  f->type_size = INK_ALIGN(type_size, f->alignment);
  Debug(DEBUG_TAG "_init", "<%s> Type Size request/actual (%" PRIu32 "/%" PRIu32 ")", name, type_size, f->type_size);
  if (ats_hugepage_enabled()) {
    f->chunk_size = INK_ALIGN(chunk_size * f->type_size, ats_hugepage_size()) / f->type_size;
  } else {
    f->chunk_size = INK_ALIGN(chunk_size * f->type_size, ats_pagesize()) / f->type_size;
  }
  Debug(DEBUG_TAG "_init", "<%s> Chunk Size request/actual (%" PRIu32 "/%" PRIu32 ")", name, chunk_size, f->chunk_size);
  SET_FREELIST_POINTER_VERSION(f->head, FROM_PTR(0), 0);

  *fl = f;
}

void
ink_freelist_madvise_init(InkFreeList **fl, const char *name, uint32_t type_size, uint32_t chunk_size, uint32_t alignment,
                          int advice)
{
  ink_freelist_init(fl, name, type_size, chunk_size, alignment);
  (*fl)->advice = advice;
}

InkFreeList *
ink_freelist_create(const char *name, uint32_t type_size, uint32_t chunk_size, uint32_t alignment)
{
  InkFreeList *f;

  ink_freelist_init(&f, name, type_size, chunk_size, alignment);
  return f;
}

#define ADDRESS_OF_NEXT(x, offset) ((void **)((char *)x + offset))

#ifdef SANITY
int fake_global_for_ink_queue = 0;
#endif

void *
ink_freelist_new(InkFreeList *f)
{
  void *ptr;

  if (likely(ptr = freelist_global_ops->fl_new(f))) {
    ink_atomic_increment((int *)&f->used, 1);
  }

  return ptr;
}

static void *
freelist_new(InkFreeList *f)
{
  head_p item;
  head_p next;
  int result = 0;

  do {
    INK_QUEUE_LD(item, f->head);
    if (TO_PTR(FREELIST_POINTER(item)) == nullptr) {
      uint32_t i;
      void *newp        = nullptr;
      size_t alloc_size = f->chunk_size * f->type_size;
      size_t alignment  = 0;

      if (ats_hugepage_enabled()) {
        alignment = ats_hugepage_size();
        newp      = ats_alloc_hugepage(alloc_size);
      }

      if (newp == nullptr) {
        alignment = ats_pagesize();
        newp      = ats_memalign(alignment, INK_ALIGN(alloc_size, alignment));
      }

      if (f->advice) {
        ats_madvise((caddr_t)newp, INK_ALIGN(alloc_size, alignment), f->advice);
      }
      SET_FREELIST_POINTER_VERSION(item, newp, 0);

      ink_atomic_increment((int *)&f->allocated, f->chunk_size);

      /* free each of the new elements */
      for (i = 0; i < f->chunk_size; i++) {
        char *a = ((char *)FREELIST_POINTER(item)) + i * f->type_size;
#ifdef DEADBEEF
        const char str[4] = {(char)0xde, (char)0xad, (char)0xbe, (char)0xef};
        for (int j = 0; j < (int)f->type_size; j++)
          a[j] = str[j % 4];
#endif
        freelist_free(f, a);
      }

    } else {
      SET_FREELIST_POINTER_VERSION(next, *ADDRESS_OF_NEXT(TO_PTR(FREELIST_POINTER(item)), 0), FREELIST_VERSION(item) + 1);
      result = ink_atomic_cas(&f->head.data, item.data, next.data);

#ifdef SANITY
      if (result) {
        if (FREELIST_POINTER(item) == TO_PTR(FREELIST_POINTER(next)))
          ink_abort("ink_freelist_new: loop detected");
        if (((uintptr_t)(TO_PTR(FREELIST_POINTER(next)))) & 3)
          ink_abort("ink_freelist_new: bad list");
        if (TO_PTR(FREELIST_POINTER(next)))
          fake_global_for_ink_queue = *(int *)TO_PTR(FREELIST_POINTER(next));
      }
#endif /* SANITY */
    }
  } while (result == 0);
  ink_assert(!((uintptr_t)TO_PTR(FREELIST_POINTER(item)) & (((uintptr_t)f->alignment) - 1)));

  return TO_PTR(FREELIST_POINTER(item));
}

static void *
malloc_new(InkFreeList *f)
{
  void *newp = nullptr;

  if (f->alignment) {
    newp = jna.allocate(f);
  } else {
    newp = ats_malloc(f->type_size);
  }

  return newp;
}

void
ink_freelist_free(InkFreeList *f, void *item)
{
  if (likely(item != nullptr)) {
    ink_assert(f->used != 0);
    freelist_global_ops->fl_free(f, item);
    ink_atomic_decrement((int *)&f->used, 1);
  }
}

static void
freelist_free(InkFreeList *f, void *item)
{
  void **adr_of_next = (void **)ADDRESS_OF_NEXT(item, 0);
  head_p h;
  head_p item_pair;
  int result = 0;

  // ink_assert(!((long)item&(f->alignment-1))); XXX - why is this no longer working? -bcall

#ifdef DEADBEEF
  {
    static const char str[4] = {(char)0xde, (char)0xad, (char)0xbe, (char)0xef};

    // set the entire item to DEADBEEF
    for (int j = 0; j < (int)f->type_size; j++)
      ((char *)item)[j] = str[j % 4];
  }
#endif /* DEADBEEF */

  while (!result) {
    INK_QUEUE_LD(h, f->head);
#ifdef SANITY
    if (TO_PTR(FREELIST_POINTER(h)) == item)
      ink_abort("ink_freelist_free: trying to free item twice");
    if (((uintptr_t)(TO_PTR(FREELIST_POINTER(h)))) & 3)
      ink_abort("ink_freelist_free: bad list");
    if (TO_PTR(FREELIST_POINTER(h)))
      fake_global_for_ink_queue = *(int *)TO_PTR(FREELIST_POINTER(h));
#endif /* SANITY */
    *adr_of_next = FREELIST_POINTER(h);
    SET_FREELIST_POINTER_VERSION(item_pair, FROM_PTR(item), FREELIST_VERSION(h));
    INK_MEMORY_BARRIER;
    result = ink_atomic_cas(&f->head.data, h.data, item_pair.data);
  }
}

static void
malloc_free(InkFreeList *f, void *item)
{
  if (f->alignment) {
    jna.deallocate(f, item);
  } else {
    ats_free(item);
  }
}

void
ink_freelist_free_bulk(InkFreeList *f, void *head, void *tail, size_t num_item)
{
  ink_assert(f->used >= num_item);

  freelist_global_ops->fl_bulkfree(f, head, tail, num_item);
  ink_atomic_decrement((int *)&f->used, num_item);
}

static void
freelist_bulkfree(InkFreeList *f, void *head, void *tail, size_t num_item)
{
  void **adr_of_next = (void **)ADDRESS_OF_NEXT(tail, 0);
  head_p h;
  head_p item_pair;
  int result = 0;

  // ink_assert(!((long)item&(f->alignment-1))); XXX - why is this no longer working? -bcall

#ifdef DEADBEEF
  {
    static const char str[4] = {(char)0xde, (char)0xad, (char)0xbe, (char)0xef};

    // set the entire item to DEADBEEF;
    void *temp = head;
    for (size_t i = 0; i < num_item; i++) {
      for (int j = sizeof(void *); j < (int)f->type_size; j++)
        ((char *)temp)[j] = str[j % 4];
      *ADDRESS_OF_NEXT(temp, 0) = FROM_PTR(*ADDRESS_OF_NEXT(temp, 0));
      temp                      = TO_PTR(*ADDRESS_OF_NEXT(temp, 0));
    }
  }
#endif /* DEADBEEF */

  while (!result) {
    INK_QUEUE_LD(h, f->head);
#ifdef SANITY
    if (TO_PTR(FREELIST_POINTER(h)) == head)
      ink_abort("ink_freelist_free: trying to free item twice");
    if (((uintptr_t)(TO_PTR(FREELIST_POINTER(h)))) & 3)
      ink_abort("ink_freelist_free: bad list");
    if (TO_PTR(FREELIST_POINTER(h)))
      fake_global_for_ink_queue = *(int *)TO_PTR(FREELIST_POINTER(h));
#endif /* SANITY */
    *adr_of_next = FREELIST_POINTER(h);
    SET_FREELIST_POINTER_VERSION(item_pair, FROM_PTR(head), FREELIST_VERSION(h));
    INK_MEMORY_BARRIER;
    result = ink_atomic_cas(&f->head.data, h.data, item_pair.data);
  }
}

static void
malloc_bulkfree(InkFreeList *f, void *head, void *tail, size_t num_item)
{
  void *item = head;
  void *next;

  // Avoid compiler warnings
  (void)tail;

  if (f->alignment) {
    for (size_t i = 0; i < num_item && item; ++i, item = next) {
      next = *(void **)item; // find next item before freeing current item
      jna.deallocate(f, item);
    }
  } else {
    for (size_t i = 0; i < num_item && item; ++i, item = next) {
      next = *(void **)item; // find next item before freeing current item
      ats_free(item);
    }
  }
}

void
ink_freelists_snap_baseline()
{
  ink_freelist_list *fll;
  fll = freelists;
  while (fll) {
    fll->fl->allocated_base = fll->fl->allocated;
    fll->fl->used_base      = fll->fl->used;
    fll                     = fll->next;
  }
}

void
ink_freelists_dump_baselinerel(FILE *f)
{
  ink_freelist_list *fll;
  if (f == nullptr) {
    f = stderr;
  }

  fprintf(f, "     allocated      |       in-use       |  count  | type size  |   free list name\n");
  fprintf(f, "  relative to base  |  relative to base  |         |            |                 \n");
  fprintf(f, "--------------------|--------------------|---------|------------|----------------------------------\n");

  fll = freelists;
  while (fll) {
    int a = fll->fl->allocated - fll->fl->allocated_base;
    if (a != 0) {
      fprintf(f, " %18" PRIu64 " | %18" PRIu64 " | %7u | %10u | memory/%s\n",
              (uint64_t)(fll->fl->allocated - fll->fl->allocated_base) * (uint64_t)fll->fl->type_size,
              (uint64_t)(fll->fl->used - fll->fl->used_base) * (uint64_t)fll->fl->type_size, fll->fl->used - fll->fl->used_base,
              fll->fl->type_size, fll->fl->name ? fll->fl->name : "<unknown>");
    }
    fll = fll->next;
  }
  fprintf(f, "-----------------------------------------------------------------------------------------\n");
}

void
ink_freelists_dump(FILE *f)
{
  ink_freelist_list *fll;
  if (f == nullptr) {
    f = stderr;
  }

  fprintf(f, "     Allocated      |        In-Use      | Type Size  |   Free List Name\n");
  fprintf(f, "--------------------|--------------------|------------|----------------------------------\n");

  uint64_t total_allocated = 0;
  uint64_t total_used      = 0;
  fll                      = freelists;
  while (fll) {
    fprintf(f, " %18" PRIu64 " | %18" PRIu64 " | %10u | memory/%s\n", (uint64_t)fll->fl->allocated * (uint64_t)fll->fl->type_size,
            (uint64_t)fll->fl->used * (uint64_t)fll->fl->type_size, fll->fl->type_size,
            fll->fl->name ? fll->fl->name : "<unknown>");
    total_allocated += (uint64_t)fll->fl->allocated * (uint64_t)fll->fl->type_size;
    total_used += (uint64_t)fll->fl->used * (uint64_t)fll->fl->type_size;
    fll = fll->next;
  }
  fprintf(f, " %18" PRIu64 " | %18" PRIu64 " |            | TOTAL\n", total_allocated, total_used);
  fprintf(f, "-----------------------------------------------------------------------------------------\n");
}

void
ink_atomiclist_init(InkAtomicList *l, const char *name, uint32_t offset_to_next)
{
  l->name   = name;
  l->offset = offset_to_next;
  SET_FREELIST_POINTER_VERSION(l->head, FROM_PTR(0), 0);
}

void *
ink_atomiclist_pop(InkAtomicList *l)
{
  head_p item;
  head_p next;
  int result = 0;
  do {
    INK_QUEUE_LD(item, l->head);
    if (TO_PTR(FREELIST_POINTER(item)) == nullptr) {
      return nullptr;
    }
    SET_FREELIST_POINTER_VERSION(next, *ADDRESS_OF_NEXT(TO_PTR(FREELIST_POINTER(item)), l->offset), FREELIST_VERSION(item) + 1);
    result = ink_atomic_cas(&l->head.data, item.data, next.data);
  } while (result == 0);
  {
    void *ret                        = TO_PTR(FREELIST_POINTER(item));
    *ADDRESS_OF_NEXT(ret, l->offset) = nullptr;
    return ret;
  }
}

void *
ink_atomiclist_popall(InkAtomicList *l)
{
  head_p item;
  head_p next;
  int result = 0;
  do {
    INK_QUEUE_LD(item, l->head);
    if (TO_PTR(FREELIST_POINTER(item)) == nullptr) {
      return nullptr;
    }
    SET_FREELIST_POINTER_VERSION(next, FROM_PTR(nullptr), FREELIST_VERSION(item) + 1);
    result = ink_atomic_cas(&l->head.data, item.data, next.data);
  } while (result == 0);
  {
    void *ret = TO_PTR(FREELIST_POINTER(item));
    void *e   = ret;
    /* fixup forward pointers */
    while (e) {
      void *n                        = TO_PTR(*ADDRESS_OF_NEXT(e, l->offset));
      *ADDRESS_OF_NEXT(e, l->offset) = n;
      e                              = n;
    }
    return ret;
  }
}

void *
ink_atomiclist_push(InkAtomicList *l, void *item)
{
  void **adr_of_next = (void **)ADDRESS_OF_NEXT(item, l->offset);
  head_p head;
  head_p item_pair;
  int result = 0;
  void *h    = nullptr;
  do {
    INK_QUEUE_LD(head, l->head);
    h            = FREELIST_POINTER(head);
    *adr_of_next = h;
    ink_assert(item != TO_PTR(h));
    SET_FREELIST_POINTER_VERSION(item_pair, FROM_PTR(item), FREELIST_VERSION(head));
    INK_MEMORY_BARRIER;
    result = ink_atomic_cas(&l->head.data, head.data, item_pair.data);
  } while (result == 0);

  return TO_PTR(h);
}

void *
ink_atomiclist_remove(InkAtomicList *l, void *item)
{
  head_p head;
  void *prev       = nullptr;
  void **addr_next = ADDRESS_OF_NEXT(item, l->offset);
  void *item_next  = *addr_next;
  int result       = 0;

  /*
   * first, try to pop it if it is first
   */
  INK_QUEUE_LD(head, l->head);
  while (TO_PTR(FREELIST_POINTER(head)) == item) {
    head_p next;
    SET_FREELIST_POINTER_VERSION(next, item_next, FREELIST_VERSION(head) + 1);
    result = ink_atomic_cas(&l->head.data, head.data, next.data);

    if (result) {
      *addr_next = nullptr;
      return item;
    }
    INK_QUEUE_LD(head, l->head);
  }

  /*
   * then, go down the list, trying to remove it
   */
  prev = TO_PTR(FREELIST_POINTER(head));
  while (prev) {
    void **prev_adr_of_next = ADDRESS_OF_NEXT(prev, l->offset);
    void *prev_prev         = prev;
    prev                    = TO_PTR(*prev_adr_of_next);
    if (prev == item) {
      ink_assert(prev_prev != item_next);
      *prev_adr_of_next = item_next;
      *addr_next        = nullptr;
      return item;
    }
  }
  return nullptr;
}

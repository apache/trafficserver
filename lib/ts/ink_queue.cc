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

#include "ink_config.h"
#include <assert.h>
#include <memory.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include "ink_atomic.h"
#include "ink_queue.h"
#include "ink_memory.h"
#include "ink_error.h"
#include "ink_assert.h"
#include "ink_queue_ext.h"
#include "ink_align.h"
#include "hugepages.h"

inkcoreapi volatile int64_t fastalloc_mem_in_use = 0;
inkcoreapi volatile int64_t fastalloc_mem_total = 0;

/*
 * SANITY and DEADBEEF are compute-intensive memory debugging to
 * help in diagnosing freelist corruption.  We turn them off in
 * release builds.
 */

#ifdef DEBUG
#define SANITY
#define DEADBEEF
#endif

// #define MEMPROTECT 1

#define MEMPROTECT_SIZE 0x200

#ifdef MEMPROTECT
static const int page_size = ats_pagesize();
#endif

ink_freelist_list *freelists = NULL;

inkcoreapi volatile int64_t freelist_allocated_mem = 0;

#define fl_memadd(_x_) ink_atomic_increment(&freelist_allocated_mem, (int64_t)(_x_));

void
ink_freelist_init(InkFreeList **fl, const char *name, uint32_t type_size, uint32_t chunk_size, uint32_t alignment)
{
#if TS_USE_RECLAIMABLE_FREELIST
  return reclaimable_freelist_init(fl, name, type_size, chunk_size, alignment);
#else
  InkFreeList *f;
  ink_freelist_list *fll;

  /* its safe to add to this global list because ink_freelist_init()
     is only called from single-threaded initialization code. */
  f = (InkFreeList *)ats_memalign(alignment, sizeof(InkFreeList));
  fll = (ink_freelist_list *)ats_malloc(sizeof(ink_freelist_list));
  fll->fl = f;
  fll->next = freelists;
  freelists = fll;

  f->name = name;
  /* quick test for power of 2 */
  ink_assert(!(alignment & (alignment - 1)));
  f->alignment = alignment;
  // Make sure we align *all* the objects in the allocation, not just the first one
  f->type_size = INK_ALIGN(type_size, alignment);
  if (ats_hugepage_enabled()) {
    f->chunk_size = INK_ALIGN(chunk_size * f->type_size, ats_hugepage_size()) / f->type_size;
  } else {
    f->chunk_size = chunk_size;
  }
  SET_FREELIST_POINTER_VERSION(f->head, FROM_PTR(0), 0);

  f->used = 0;
  f->allocated = 0;
  f->allocated_base = 0;
  f->used_base = 0;
  f->advice = 0;
  *fl = f;
#endif
}

void
ink_freelist_madvise_init(InkFreeList **fl, const char *name, uint32_t type_size, uint32_t chunk_size, uint32_t alignment,
                          int advice)
{
  ink_freelist_init(fl, name, type_size, chunk_size, alignment);
#if TS_USE_RECLAIMABLE_FREELIST
  (void)advice;
#else
  (*fl)->advice = advice;
#endif
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

int fastmemtotal = 0;
void *
ink_freelist_new(InkFreeList *f)
{
#if TS_USE_FREELIST
#if TS_USE_RECLAIMABLE_FREELIST
  return reclaimable_freelist_new(f);
#else
  head_p item;
  head_p next;
  int result = 0;

  do {
    INK_QUEUE_LD(item, f->head);
    if (TO_PTR(FREELIST_POINTER(item)) == NULL) {
      uint32_t type_size = f->type_size;
      uint32_t i;

#ifdef MEMPROTECT
      if (type_size >= MEMPROTECT_SIZE) {
        if (f->alignment < page_size)
          f->alignment = page_size;
        type_size = ((type_size + page_size - 1) / page_size) * page_size * 2;
      }
#endif /* MEMPROTECT */

      void *newp = NULL;
#ifdef DEBUG
      char *oldsbrk = (char *)sbrk(0), *newsbrk = NULL;
#endif
      if (ats_hugepage_enabled())
        newp = ats_alloc_hugepage(f->chunk_size * type_size);

      if (newp == NULL) {
        if (f->alignment)
          newp = ats_memalign(f->alignment, f->chunk_size * type_size);
        else
          newp = ats_malloc(f->chunk_size * type_size);
      }
      ats_madvise((caddr_t)newp, f->chunk_size * type_size, f->advice);
      fl_memadd(f->chunk_size * type_size);
#ifdef DEBUG
      newsbrk = (char *)sbrk(0);
      ink_atomic_increment(&fastmemtotal, newsbrk - oldsbrk);
/*      printf("fastmem %d, %d, %d\n", f->chunk_size * type_size,
   newsbrk - oldsbrk, fastmemtotal); */
#endif
      SET_FREELIST_POINTER_VERSION(item, newp, 0);

      ink_atomic_increment((int *)&f->allocated, f->chunk_size);
      ink_atomic_increment(&fastalloc_mem_total, (int64_t)f->chunk_size * f->type_size);

      /* free each of the new elements */
      for (i = 0; i < f->chunk_size; i++) {
        char *a = ((char *)FREELIST_POINTER(item)) + i * type_size;
#ifdef DEADBEEF
        const char str[4] = {(char)0xde, (char)0xad, (char)0xbe, (char)0xef};
        for (int j = 0; j < (int)type_size; j++)
          a[j] = str[j % 4];
#endif
        ink_freelist_free(f, a);
#ifdef MEMPROTECT
        if (f->type_size >= MEMPROTECT_SIZE) {
          a += type_size - page_size;
          if (mprotect(a, page_size, PROT_NONE) < 0)
            perror("mprotect");
        }
#endif /* MEMPROTECT */
      }
      ink_atomic_increment((int *)&f->used, f->chunk_size);
      ink_atomic_increment(&fastalloc_mem_in_use, (int64_t)f->chunk_size * f->type_size);

    } else {
      SET_FREELIST_POINTER_VERSION(next, *ADDRESS_OF_NEXT(TO_PTR(FREELIST_POINTER(item)), 0), FREELIST_VERSION(item) + 1);
#if TS_HAS_128BIT_CAS
      result = ink_atomic_cas((__int128_t *)&f->head.data, item.data, next.data);
#else
      result = ink_atomic_cas((int64_t *)&f->head.data, item.data, next.data);
#endif

#ifdef SANITY
      if (result) {
        if (FREELIST_POINTER(item) == TO_PTR(FREELIST_POINTER(next)))
          ink_fatal("ink_freelist_new: loop detected");
        if (((uintptr_t)(TO_PTR(FREELIST_POINTER(next)))) & 3)
          ink_fatal("ink_freelist_new: bad list");
        if (TO_PTR(FREELIST_POINTER(next)))
          fake_global_for_ink_queue = *(int *)TO_PTR(FREELIST_POINTER(next));
      }
#endif /* SANITY */
    }
  } while (result == 0);
  ink_assert(!((uintptr_t)TO_PTR(FREELIST_POINTER(item)) & (((uintptr_t)f->alignment) - 1)));

  ink_atomic_increment((int *)&f->used, 1);
  ink_atomic_increment(&fastalloc_mem_in_use, (int64_t)f->type_size);

  return TO_PTR(FREELIST_POINTER(item));
#endif /* TS_USE_RECLAIMABLE_FREELIST */
#else  // ! TS_USE_FREELIST
  void *newp = NULL;

  if (f->alignment)
    newp = ats_memalign(f->alignment, f->type_size);
  else
    newp = ats_malloc(f->type_size);
  ats_madvise((caddr_t)newp, f->type_size, f->advice);
  return newp;
#endif
}

void
ink_freelist_free(InkFreeList *f, void *item)
{
#if TS_USE_FREELIST
#if TS_USE_RECLAIMABLE_FREELIST
  return reclaimable_freelist_free(f, item);
#else
  volatile void **adr_of_next = (volatile void **)ADDRESS_OF_NEXT(item, 0);
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
      ink_fatal("ink_freelist_free: trying to free item twice");
    if (((uintptr_t)(TO_PTR(FREELIST_POINTER(h)))) & 3)
      ink_fatal("ink_freelist_free: bad list");
    if (TO_PTR(FREELIST_POINTER(h)))
      fake_global_for_ink_queue = *(int *)TO_PTR(FREELIST_POINTER(h));
#endif /* SANITY */
    *adr_of_next = FREELIST_POINTER(h);
    SET_FREELIST_POINTER_VERSION(item_pair, FROM_PTR(item), FREELIST_VERSION(h));
    INK_MEMORY_BARRIER;
#if TS_HAS_128BIT_CAS
    result = ink_atomic_cas((__int128_t *)&f->head, h.data, item_pair.data);
#else
    result = ink_atomic_cas((int64_t *)&f->head, h.data, item_pair.data);
#endif
  }

  ink_atomic_increment((int *)&f->used, -1);
  ink_atomic_increment(&fastalloc_mem_in_use, -(int64_t)f->type_size);
#endif /* TS_USE_RECLAIMABLE_FREELIST */
#else
  if (f->alignment)
    ats_memalign_free(item);
  else
    ats_free(item);
#endif
}

void
ink_freelist_free_bulk(InkFreeList *f, void *head, void *tail, size_t num_item)
{
#if TS_USE_FREELIST
#if !TS_USE_RECLAIMABLE_FREELIST
  volatile void **adr_of_next = (volatile void **)ADDRESS_OF_NEXT(tail, 0);
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
      temp = TO_PTR(*ADDRESS_OF_NEXT(temp, 0));
    }
  }
#endif /* DEADBEEF */

  while (!result) {
    INK_QUEUE_LD(h, f->head);
#ifdef SANITY
    if (TO_PTR(FREELIST_POINTER(h)) == head)
      ink_fatal("ink_freelist_free: trying to free item twice");
    if (((uintptr_t)(TO_PTR(FREELIST_POINTER(h)))) & 3)
      ink_fatal("ink_freelist_free: bad list");
    if (TO_PTR(FREELIST_POINTER(h)))
      fake_global_for_ink_queue = *(int *)TO_PTR(FREELIST_POINTER(h));
#endif /* SANITY */
    *adr_of_next = FREELIST_POINTER(h);
    SET_FREELIST_POINTER_VERSION(item_pair, FROM_PTR(head), FREELIST_VERSION(h));
    INK_MEMORY_BARRIER;
#if TS_HAS_128BIT_CAS
    result = ink_atomic_cas((__int128_t *)&f->head, h.data, item_pair.data);
#else  /* !TS_HAS_128BIT_CAS */
    result = ink_atomic_cas((int64_t *)&f->head, h.data, item_pair.data);
#endif /* TS_HAS_128BIT_CAS */
  }

  ink_atomic_increment((int *)&f->used, -1 * num_item);
  ink_atomic_increment(&fastalloc_mem_in_use, -(int64_t)f->type_size * num_item);
#else  /* TS_USE_RECLAIMABLE_FREELIST */
  // Avoid compiler warnings
  (void)f;
  (void)head;
  (void)tail;
  (void)num_item;
#endif /* !TS_USE_RECLAIMABLE_FREELIST */
#else  /* !TS_USE_FREELIST */
  void *item = head;

  // Avoid compiler warnings
  (void)tail;

  if (f->alignment) {
    for (size_t i = 0; i < num_item && item; ++i, item = *(void **)item) {
      ats_memalign_free(item);
    }
  } else {
    for (size_t i = 0; i < num_item && item; ++i, item = *(void **)item) {
      ats_free(item);
    }
  }
#endif /* TS_USE_FREELIST */
}

void
ink_freelists_snap_baseline()
{
#if TS_USE_FREELIST
  ink_freelist_list *fll;
  fll = freelists;
  while (fll) {
    fll->fl->allocated_base = fll->fl->allocated;
    fll->fl->used_base = fll->fl->used;
    fll = fll->next;
  }
#else // ! TS_USE_FREELIST
// TODO?
#endif
}

void
ink_freelists_dump_baselinerel(FILE *f)
{
#if TS_USE_FREELIST
  ink_freelist_list *fll;
  if (f == NULL)
    f = stderr;

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
#else // ! TS_USE_FREELIST
  (void)f;
#endif
}

void
ink_freelists_dump(FILE *f)
{
#if TS_USE_FREELIST
  ink_freelist_list *fll;
  if (f == NULL)
    f = stderr;

  fprintf(f, "     allocated      |        in-use      | type size  |   free list name\n");
  fprintf(f, "--------------------|--------------------|------------|----------------------------------\n");

  fll = freelists;
  while (fll) {
    fprintf(f, " %18" PRIu64 " | %18" PRIu64 " | %10u | memory/%s\n", (uint64_t)fll->fl->allocated * (uint64_t)fll->fl->type_size,
            (uint64_t)fll->fl->used * (uint64_t)fll->fl->type_size, fll->fl->type_size,
            fll->fl->name ? fll->fl->name : "<unknown>");
    fll = fll->next;
  }
#else // ! TS_USE_FREELIST
  (void)f;
#endif
}


void
ink_atomiclist_init(InkAtomicList *l, const char *name, uint32_t offset_to_next)
{
  l->name = name;
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
    if (TO_PTR(FREELIST_POINTER(item)) == NULL)
      return NULL;
    SET_FREELIST_POINTER_VERSION(next, *ADDRESS_OF_NEXT(TO_PTR(FREELIST_POINTER(item)), l->offset), FREELIST_VERSION(item) + 1);
#if TS_HAS_128BIT_CAS
    result = ink_atomic_cas((__int128_t *)&l->head.data, item.data, next.data);
#else
    result = ink_atomic_cas((int64_t *)&l->head.data, item.data, next.data);
#endif
  } while (result == 0);
  {
    void *ret = TO_PTR(FREELIST_POINTER(item));
    *ADDRESS_OF_NEXT(ret, l->offset) = NULL;
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
    if (TO_PTR(FREELIST_POINTER(item)) == NULL)
      return NULL;
    SET_FREELIST_POINTER_VERSION(next, FROM_PTR(NULL), FREELIST_VERSION(item) + 1);
#if TS_HAS_128BIT_CAS
    result = ink_atomic_cas((__int128_t *)&l->head.data, item.data, next.data);
#else
    result = ink_atomic_cas((int64_t *)&l->head.data, item.data, next.data);
#endif
  } while (result == 0);
  {
    void *ret = TO_PTR(FREELIST_POINTER(item));
    void *e = ret;
    /* fixup forward pointers */
    while (e) {
      void *n = TO_PTR(*ADDRESS_OF_NEXT(e, l->offset));
      *ADDRESS_OF_NEXT(e, l->offset) = n;
      e = n;
    }
    return ret;
  }
}

void *
ink_atomiclist_push(InkAtomicList *l, void *item)
{
  volatile void **adr_of_next = (volatile void **)ADDRESS_OF_NEXT(item, l->offset);
  head_p head;
  head_p item_pair;
  int result = 0;
  volatile void *h = NULL;
  do {
    INK_QUEUE_LD(head, l->head);
    h = FREELIST_POINTER(head);
    *adr_of_next = h;
    ink_assert(item != TO_PTR(h));
    SET_FREELIST_POINTER_VERSION(item_pair, FROM_PTR(item), FREELIST_VERSION(head));
    INK_MEMORY_BARRIER;
#if TS_HAS_128BIT_CAS
    result = ink_atomic_cas((__int128_t *)&l->head, head.data, item_pair.data);
#else
    result = ink_atomic_cas((int64_t *)&l->head, head.data, item_pair.data);
#endif
  } while (result == 0);

  return TO_PTR(h);
}

void *
ink_atomiclist_remove(InkAtomicList *l, void *item)
{
  head_p head;
  void *prev = NULL;
  void **addr_next = ADDRESS_OF_NEXT(item, l->offset);
  void *item_next = *addr_next;
  int result = 0;

  /*
   * first, try to pop it if it is first
   */
  INK_QUEUE_LD(head, l->head);
  while (TO_PTR(FREELIST_POINTER(head)) == item) {
    head_p next;
    SET_FREELIST_POINTER_VERSION(next, item_next, FREELIST_VERSION(head) + 1);
#if TS_HAS_128BIT_CAS
    result = ink_atomic_cas((__int128_t *)&l->head.data, head.data, next.data);
#else
    result = ink_atomic_cas((int64_t *)&l->head.data, head.data, next.data);
#endif

    if (result) {
      *addr_next = NULL;
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
    void *prev_prev = prev;
    prev = TO_PTR(*prev_adr_of_next);
    if (prev == item) {
      ink_assert(prev_prev != item_next);
      *prev_adr_of_next = item_next;
      *addr_next = NULL;
      return item;
    }
  }
  return NULL;
}

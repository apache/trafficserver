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

#include <assert.h>
#include <memory.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/mman.h>
#include "ink_atomic.h"
#include "ink_queue.h"
#include "ink_memory.h"
#include "ink_error.h"
#include "ink_assert.h"
#include "ink_resource.h"


#ifdef __x86_64__
#define INK_QUEUE_LD64(dst,src) *((inku64*)&(dst)) = *((inku64*)&(src))
#else
#define INK_QUEUE_LD64(dst,src) (ink_queue_load_64((void *)&(dst), (void *)&(src)))
#endif

typedef struct _ink_freelist_list
{
  InkFreeList *fl;
  struct _ink_freelist_list *next;
}
ink_freelist_list;

inkcoreapi volatile ink64 fastalloc_mem_in_use = 0;
inkcoreapi volatile ink64 fastalloc_mem_total = 0;

/*
 * SANITY and DEADBEEF are compute-intensive memory debugging to
 * help in diagnosing freelist corruption.  We turn them off in
 * release builds.
 */

#ifdef DEBUG
#define SANITY
#define DEADBEEF
#endif

/* #define MEMPROTECT */

#define MEMPROTECT_SIZE  0x200

#ifdef MEMPROTECT
static long page_size = 8192;   /* sysconf (_SC_PAGESIZE); */
#endif

static ink_freelist_list *freelists = NULL;

inkcoreapi volatile ink64 freelist_allocated_mem = 0;

#define fl_memadd(_x_) \
   ink_atomic_increment64(&freelist_allocated_mem, (ink64) (_x_));

//static void ink_queue_load_64(void *dst, void *src)
//{
//    ink32 src_version =  (*(head_p *) src).s.version;
//    void *src_pointer = (*(head_p *) src).s.pointer;
//
//    (*(head_p *) dst).s.version = src_version;
//    (*(head_p *) dst).s.pointer = src_pointer;
//}


void
ink_freelist_init(InkFreeList * f,
                  const char *name, unsigned type_size, unsigned chunk_size, unsigned offset, unsigned alignment)
{
  ink_freelist_list *fll;

  /* its safe to add to this global list because ink_freelist_init()
     is only called from single-threaded initialization code. */
  if ((fll = (ink_freelist_list *) ink_malloc(sizeof(ink_freelist_list))) != 0) {
    fll->fl = f;
    fll->next = freelists;
    freelists = fll;
  }
  ink_assert(fll != NULL);

  f->name = name;
  f->offset = offset;
  /* quick test for power of 2 */
  ink_assert(!(alignment & (alignment - 1)));
  f->alignment = alignment;
  f->chunk_size = chunk_size;
  f->type_size = type_size;
#if defined(INK_USE_MUTEX_FOR_FREELISTS)
  ink_mutex_init(&(f->inkfreelist_mutex), name);
#endif
#if (defined(USE_SPINLOCK_FOR_FREELIST) || defined(CHECK_FOR_DOUBLE_FREE))
  ink_mutex_init(&(f->freelist_mutex), name);
  f->head = NULL;
#ifdef CHECK_FOR_DOUBLE_FREE
  f->tail = NULL;
#endif
#else
  SET_FREELIST_POINTER_VERSION(f->head, FROM_PTR(NULL), ((unsigned long) 0));
#endif

  f->count = 0;
  f->allocated = 0;
  f->allocated_base = 0;
  f->count_base = 0;
}

InkFreeList *
ink_freelist_create(const char *name, unsigned type_size, unsigned chunk_size, unsigned offset, unsigned alignment)
{
  InkFreeList *f = ink_type_malloc(InkFreeList);
  ink_freelist_init(f, name, type_size, chunk_size, offset, alignment);
  return f;
}

#define ADDRESS_OF_NEXT(x, offset) ((void **)((char *)x + offset))

#ifdef SANITY
int fake_global_for_ink_queue = 0;
#endif

int fastmemtotal = 0;

#if defined(INK_USE_MUTEX_FOR_FREELISTS)
void *
ink_freelist_new_wrap(InkFreeList * f)
#else /* !INK_USE_MUTEX_FOR_FREELISTS */
void *
ink_freelist_new(InkFreeList * f)
#endif                          /* !INK_USE_MUTEX_FOR_FREELISTS */
{                               //static unsigned cntf = 0;


#if (defined(USE_SPINLOCK_FOR_FREELIST) || defined(CHECK_FOR_DOUBLE_FREE))
  void *foo;
  unsigned type_size = f->type_size;

  ink_mutex_acquire(&(f->freelist_mutex));
  ink_assert(f->type_size != 0);

  //printf("ink_freelist_new %d - %d - %u\n",f ->type_size,(f->head != NULL) ? 1 : 0,cntf++);


  if (f->head != NULL) {
    /*
     * We have something on the free list..
     */
    void_p *h = (void_p *) f->head;
#ifdef CHECK_FOR_DOUBLE_FREE
    if (f->head == f->tail)
      f->tail = NULL;
#endif /* CHECK_FOR_DOUBLE_FREE */

    foo = (void *) h;
    f->head = (volatile void_p *) *h;
    f->count += 1;
    ink_mutex_release(&(f->freelist_mutex));
    return foo;
  } else {
    /*
     * Might as well unlock the freelist mutex, since
     * we're just going to do a malloc now..
     */
    unsigned alignment;

#ifdef MEMPROTECT
    if (type_size >= MEMPROTECT_SIZE) {
      if (f->alignment < page_size)
        f->alignment = page_size;
      type_size = ((type_size + page_size - 1) / page_size) * page_size * 2;
    }
#endif /* MEMPROTECT */

    alignment = f->alignment;
    ink_mutex_release(&(f->freelist_mutex));

    if (alignment) {
      foo = ink_memalign(alignment, type_size);
    } else {
      foo = ink_malloc(type_size);
    }
    if (likely(foo))
      fl_memadd(type_size);


#ifdef MEMPROTECT
    if (type_size >= MEMPROTECT_SIZE) {
      if (mprotect((char *) foo + type_size - page_size, page_size, PROT_NONE) < 0)
        perror("mprotect");
    }
#endif /* MEMPROTECT */
    return foo;
  }

#else /* #if (defined(USE_SPINLOCK_FOR_FREELIST) || defined(CHECK_FOR_DOUBLE_FREE)) */
  head_p item;
  head_p next;
  int result = 0;

  //printf("ink_freelist_new %d - %d - %u\n",f ->type_size,(f->head != NULL) ? 1 : 0,cntf++);


  do {
    INK_QUEUE_LD64(item, f->head);
    if (TO_PTR(FREELIST_POINTER(item)) == NULL) {
      unsigned type_size = f->type_size;
      unsigned int i;

#ifdef MEMPROTECT
      if (type_size >= MEMPROTECT_SIZE) {
        if (f->alignment < page_size)
          f->alignment = page_size;
        type_size = ((type_size + page_size - 1) / page_size) * page_size * 2;
      }
#endif /* MEMPROTECT */

      void *newp = NULL;
#ifndef NO_MEMALIGN
#ifdef DEBUG
      char *oldsbrk = (char *) sbrk(0), *newsbrk = NULL;
#endif
      if (f->alignment)
        newp = ink_memalign(f->alignment, f->chunk_size * type_size);
      else
        newp = ink_malloc(f->chunk_size * type_size);
      if (newp)
        fl_memadd(f->chunk_size * type_size);
#ifdef DEBUG
      newsbrk = (char *) sbrk(0);
      ink_atomic_increment(&fastmemtotal, newsbrk - oldsbrk);
      /*      printf("fastmem %d, %d, %d\n", f->chunk_size * type_size,
         newsbrk - oldsbrk, fastmemtotal); */
#endif
      SET_FREELIST_POINTER_VERSION(item, newp, 0);
#else
      unsigned int add;
      unsigned long mask;
      if (f->alignment) {
        add = f->alignment - 1;
        mask = ~(unsigned long) add;
      } else {
        add = 0;
        mask = ~0;
      }
      newp = ink_malloc(f->chunk_size * type_size + add);
      if (newp)
        fl_memadd(f->chunk_size * type_size + add);
      newp = (void *) ((((unsigned long) newp) + add) & mask);
      SET_FREELIST_POINTER_VERSION(item, newp, ((unsigned long) 0));
#endif

#if !defined(INK_USE_MUTEX_FOR_FREELISTS)
      ink_atomic_increment((int *) &f->allocated, f->chunk_size);
      ink_atomic_increment64(&fastalloc_mem_total, (ink64) f->chunk_size * f->type_size);
#else
      f->allocated += f->chunk_size;
      fastalloc_mem_total += f->chunk_size * f->type_size;
#endif

      /* free each of the new elements */
      for (i = 0; i < f->chunk_size; i++) {
        char *a = ((char *) FREELIST_POINTER(item)) + i * type_size;
#ifdef DEADBEEF
        memset(a, 0xDEADCAFE, type_size);
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
#if !defined(INK_USE_MUTEX_FOR_FREELISTS)
      ink_atomic_increment((int *) &f->count, f->chunk_size);
      ink_atomic_increment64(&fastalloc_mem_in_use, (ink64) f->chunk_size * f->type_size);
#else
      f->count += f->chunk_size;
      fastalloc_mem_in_use += f->chunk_size * f->type_size;
#endif

    } else {
      SET_FREELIST_POINTER_VERSION(next, *ADDRESS_OF_NEXT(TO_PTR(FREELIST_POINTER(item)), f->offset),
                                   FREELIST_VERSION(item) + 1);
#ifdef SANITY
      if (item.s.pointer == TO_PTR(next.s.pointer))
        ink_fatal(1, "ink_freelist_new: loop detected");
#endif /* SANITY */
#if !defined(INK_USE_MUTEX_FOR_FREELISTS)
      result = ink_atomic_cas64((ink64 *) & f->head.data, item.data, next.data);
#else
      f->head.data = next.data;
      result = 1;
#endif

#ifdef SANITY
      if (result) {
        if (((uintptr_t) (TO_PTR(next.s.pointer))) & 3)
          ink_fatal(1, "ink_freelist_new: bad list");
        if (TO_PTR(FREELIST_POINTER(next)))
          fake_global_for_ink_queue = *(int *) TO_PTR(FREELIST_POINTER(next));
      }
#endif /* SANITY */

    }
  }
  while (result == 0);
  // ink_assert(!((unsigned long)TO_PTR(FREELIST_POINTER(item))&(f->alignment-1))); XXX - why is this no longer working? -bcall

  ink_atomic_increment((int *) &f->count, 1);
  ink_atomic_increment64(&fastalloc_mem_in_use, (ink64) f->type_size);

  return TO_PTR(FREELIST_POINTER(item));
#endif /* #if (defined(USE_SPINLOCK_FOR_FREELIST) || defined(CHECK_FOR_DOUBLE_FREE)) */
}
typedef volatile void *volatile_void_p;

#if defined(INK_USE_MUTEX_FOR_FREELISTS)
void
ink_freelist_free_wrap(InkFreeList * f, void *item)
#else /* !INK_USE_MUTEX_FOR_FREELISTS */
void
ink_freelist_free(InkFreeList * f, void *item)
#endif                          /* !INK_USE_MUTEX_FOR_FREELISTS */
{
#if (defined(USE_SPINLOCK_FOR_FREELIST) || defined(CHECK_FOR_DOUBLE_FREE))
  void_p *foo;

  //printf("ink_freelist_free\n");
  ink_mutex_acquire(&(f->freelist_mutex));

  foo = (void_p *) item;
#ifdef CHECK_FOR_DOUBLE_FREE
  void_p *p = (void_p *) f->head;
  while (p) {
    if (p == (void_p *) item)
      ink_release_assert(!"ink_freelist_free: Double free");
    p = (void_p *) * p;
  }

  if (f->tail)
    *f->tail = foo;
  *foo = (void_p) NULL;

  if (f->head == NULL)
    f->head = foo;
  f->tail = foo;
#else
  *foo = (void_p) f->head;
  f->head = foo;
#endif
  f->count -= 1;

  ink_mutex_release(&(f->freelist_mutex));
#else

  volatile_void_p *adr_of_next = (volatile_void_p *) ADDRESS_OF_NEXT(item, f->offset);
  head_p h;
  head_p item_pair;
  int result;

  // ink_assert(!((long)item&(f->alignment-1))); XXX - why is this no longer working? -bcall

#ifdef DEADBEEF
  {
    // set string to DEADBEEF
    const char str[4] = { (char) 0xde, (char) 0xad, (char) 0xbe, (char) 0xef };

    // search for DEADBEEF anywhere after a pointer offset in the item
    char *position = (char *) item + sizeof(void *);    // start
    char *end = (char *) item + f->type_size;   // end 

    int i, j;
    for (i = sizeof(void *) & 0x3, j = 0; position < end; ++position) {
      if (i == j) {
        if (*position == str[i]) {
          if (j++ == 3) {
            ink_fatal(1, "ink_freelist_free: trying to free item twice");
          }
        }
      } else {
        j = 0;
      }
      i = (i + 1) & 0x3;
    }

    // set the entire item to DEADBEEF
    memset(item, 0xDEADBEEF, f->type_size);
  }
#endif /* DEADBEEF */

  result = 0;
  do {
    INK_QUEUE_LD64(h, f->head);
#ifdef SANITY
    if (TO_PTR(FREELIST_POINTER(h)) == item)
      ink_fatal(1, "ink_freelist_free: trying to free item twice");
#ifdef __alpha
    if (((unsigned long) (TO_PTR(h.data))) & 3)
#else
    if (((unsigned long) (TO_PTR(h.s.pointer))) & 3)
#endif
      ink_fatal(1, "ink_freelist_free: bad list");
    if (TO_PTR(FREELIST_POINTER(h)))
      fake_global_for_ink_queue = *(int *) TO_PTR(FREELIST_POINTER(h));
#endif /* SANITY */
    *adr_of_next = FREELIST_POINTER(h);
    SET_FREELIST_POINTER_VERSION(item_pair, FROM_PTR(item), FREELIST_VERSION(h));
    INK_MEMORY_BARRIER;
#if !defined(INK_USE_MUTEX_FOR_FREELISTS)
    result = ink_atomic_cas64((ink64 *) & f->head, h.data, item_pair.data);
#else
    f->head.data = item_pair.data;
    result = 1;
#endif

  }
  while (result == 0);

  ink_atomic_increment((int *) &f->count, -1);
  ink_atomic_increment64(&fastalloc_mem_in_use, -(ink64) f->type_size);
#endif
}

void
ink_freelists_snap_baseline()
{
  ink_freelist_list *fll;
  fll = freelists;
  while (fll) {
    fll->fl->allocated_base = fll->fl->allocated;
    fll->fl->count_base = fll->fl->count;
    fll = fll->next;
  }
}

void
ink_freelists_dump_baselinerel(FILE * f)
{
  ink_freelist_list *fll;
  if (f == NULL)
    f = stderr;

  fprintf(f, " allocated  | in-use     |  count  | type size  |   free list name\n");
  fprintf(f, "rel. to base|rel. to base|         |            |                 \n");
  fprintf(f, "------------|------------|---------|------------|----------------------------------\n");

  fll = freelists;
  while (fll) {
    int a = fll->fl->allocated - fll->fl->allocated_base;
    if (a != 0) {
      fprintf(f, " % 10d | % 10d | % 7d | % 10d | memory/%s\n",
              (fll->fl->allocated - fll->fl->allocated_base) * fll->fl->type_size,
              (fll->fl->count - fll->fl->count_base) * fll->fl->type_size,
              fll->fl->count - fll->fl->count_base, fll->fl->type_size, fll->fl->name ? fll->fl->name : "<unknown>");
    }
    fll = fll->next;
  }
}

void
ink_freelists_dump(FILE * f)
{
  ink_freelist_list *fll;
  if (f == NULL)
    f = stderr;

  fprintf(f, " allocated  | in-use     | type size  |   free list name\n");
  fprintf(f, "------------|------------|------------|----------------------------------\n");

  fll = freelists;
  while (fll) {
    fprintf(f, " % 10d | % 10d | % 10d | memory/%s\n",
            fll->fl->allocated * fll->fl->type_size,
            fll->fl->count * fll->fl->type_size, fll->fl->type_size, fll->fl->name ? fll->fl->name : "<unknown>");
    fll = fll->next;
  }
}


#define INK_FREELIST_CREATE(T, n) \
ink_freelist_create("<unknown>", sizeof(T), n, (unsigned)&((T *)0)->next, 4)

void
ink_atomiclist_init(InkAtomicList * l, const char *name, unsigned offset_to_next)
{
#if defined(INK_USE_MUTEX_FOR_ATOMICLISTS)
  ink_mutex_init(&(l->inkatomiclist_mutex), name);
#endif
  l->name = name;
  l->offset = offset_to_next;
  SET_FREELIST_POINTER_VERSION(l->head, FROM_PTR(NULL), ((unsigned long) 0));
}

#if defined(INK_USE_MUTEX_FOR_ATOMICLISTS)
void *
ink_atomiclist_pop_wrap(InkAtomicList * l)
#else /* !INK_USE_MUTEX_FOR_ATOMICLISTS */
void *
ink_atomiclist_pop(InkAtomicList * l)
#endif                          /* !INK_USE_MUTEX_FOR_ATOMICLISTS */
{
  head_p item;
  head_p next;
  int result = 0;
  do {
    INK_QUEUE_LD64(item, l->head);
    if (TO_PTR(FREELIST_POINTER(item)) == NULL)
      return NULL;
    SET_FREELIST_POINTER_VERSION(next, *ADDRESS_OF_NEXT(TO_PTR(FREELIST_POINTER(item)), l->offset),
                                 FREELIST_VERSION(item) + 1);
#if !defined(INK_USE_MUTEX_FOR_ATOMICLISTS)
    result = ink_atomic_cas64((ink64 *) & l->head.data, item.data, next.data);
#else
    l->head.data = next.data;
    result = 1;
#endif

  }
  while (result == 0);
  {
    void *ret = TO_PTR(FREELIST_POINTER(item));
    *ADDRESS_OF_NEXT(ret, l->offset) = NULL;
    return ret;
  }
}

#if defined(INK_USE_MUTEX_FOR_ATOMICLISTS)
void *
ink_atomiclist_popall_wrap(InkAtomicList * l)
#else /* !INK_USE_MUTEX_FOR_ATOMICLISTS */
void *
ink_atomiclist_popall(InkAtomicList * l)
#endif                          /* !INK_USE_MUTEX_FOR_ATOMICLISTS */
{
  head_p item;
  head_p next;
  int result = 0;
  do {
    INK_QUEUE_LD64(item, l->head);
    if (TO_PTR(FREELIST_POINTER(item)) == NULL)
      return NULL;
    SET_FREELIST_POINTER_VERSION(next, FROM_PTR(NULL), FREELIST_VERSION(item) + 1);
#if !defined(INK_USE_MUTEX_FOR_ATOMICLISTS)
    result = ink_atomic_cas64((ink64 *) & l->head.data, item.data, next.data);
#else
    l->head.data = next.data;
    result = 1;
#endif

  }
  while (result == 0);
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

#if defined(INK_USE_MUTEX_FOR_ATOMICLISTS)
void *
ink_atomiclist_push_wrap(InkAtomicList * l, void *item)
#else /* !INK_USE_MUTEX_FOR_ATOMICLISTS */
void *
ink_atomiclist_push(InkAtomicList * l, void *item)
#endif                          /* !INK_USE_MUTEX_FOR_ATOMICLISTS */
{
  volatile_void_p *adr_of_next = (volatile_void_p *) ADDRESS_OF_NEXT(item, l->offset);
  head_p head;
  head_p item_pair;
  int result = 0;
  void *h = NULL;
  ink_assert(*adr_of_next == NULL);
  do {
    INK_QUEUE_LD64(head, l->head);
    h = FREELIST_POINTER(head);
    *adr_of_next = h;
    ink_assert(item != TO_PTR(h));
    SET_FREELIST_POINTER_VERSION(item_pair, FROM_PTR(item), FREELIST_VERSION(head));
    INK_MEMORY_BARRIER;
#if !defined(INK_USE_MUTEX_FOR_ATOMICLISTS)
    result = ink_atomic_cas64((ink64 *) & l->head, head.data, item_pair.data);
#else
    l->head.data = item_pair.data;
    result = 1;
#endif

  }
  while (result == 0);

  return TO_PTR(h);
}

#if defined(INK_USE_MUTEX_FOR_ATOMICLISTS)
void *
ink_atomiclist_remove_wrap(InkAtomicList * l, void *item)
#else /* !INK_USE_MUTEX_FOR_ATOMICLISTS */
void *
ink_atomiclist_remove(InkAtomicList * l, void *item)
#endif                          /* !INK_USE_MUTEX_FOR_ATOMICLISTS */
{
  head_p head;
  void *prev = NULL;
  void **addr_next = ADDRESS_OF_NEXT(item, l->offset);
  void *item_next = *addr_next;
  int result = 0;

  /*
   * first, try to pop it if it is first
   */
  INK_QUEUE_LD64(head, l->head);
  while (TO_PTR(FREELIST_POINTER(head)) == item) {
    head_p next;
    SET_FREELIST_POINTER_VERSION(next, item_next, FREELIST_VERSION(head) + 1);
#if !defined(INK_USE_MUTEX_FOR_ATOMICLISTS)
    result = ink_atomic_cas64((ink64 *) & l->head.data, head.data, next.data);
#else
    l->head.data = next.data;
    result = 1;
#endif
    if (result) {
      *addr_next = NULL;
      return item;
    }
    INK_QUEUE_LD64(head, l->head);
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

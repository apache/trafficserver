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

#ifndef _ink_queue_h_
#define _ink_queue_h_

/***********************************************************************

    Generic Queue Implementation (for pointer data types only)

    Uses atomic memory operations to avoid blocking.
    Intended as a replacement for llqueue.


***********************************************************************/

#include "ink_port.h"
#include "ink_apidefs.h"
#include "ink_unused.h"

/*
  For information on the structure of the x86_64 memory map:

  http://en.wikipedia.org/wiki/X86-64#Linux

  Essentially, in the current 48-bit implementations, the
  top bit as well as the  lower 47 bits are used, leaving
  the upper-but one 16 bits free to be used for the version.
  We will use the top-but-one 15 and sign extend when generating
  the pointer was required by the standard.
*/

/*
#if defined(POSIX_THREAD)
#include <pthread.h>
#include <stdlib.h>
#endif
*/

/* #define USE_SPINLOCK_FOR_FREELIST */
/* #define CHECK_FOR_DOUBLE_FREE */

#ifdef __cplusplus
extern "C"
{
#endif                          /* __cplusplus */

  void ink_queue_load_64(void *dst, void *src);

/*
 * Generic Free List Manager
 */

  typedef union
  {
#if defined(__i386__)
    struct
    {
      void *pointer;
      ink32 version;
    } s;
#endif
    ink64 data;
  } head_p;

/*
 * Why is version required? One scenario is described below
 * Think of a list like this -> A -> C -> D
 * and you are popping from the list
 * Between the time you take the ptr(A) and swap the head pointer
 * the list could start looking like this
 * -> A -> B -> C -> D
 * If the version check is not there, the list will look like
 * -> C -> D after the pop, which will result in the loss of "B"
 */
#define ZERO_HEAD_P(_x)

#ifdef DEBUG
#define FROM_PTR(_x) (void*)(((uintptr_t)_x)+1)
#define TO_PTR(_x) (void*)(((uintptr_t)_x)-1)
#else
#define FROM_PTR(_x) ((void*)(_x))
#define TO_PTR(_x) ((void*)(_x))
#endif

#if defined(__i386__) || defined(__i386)
#define FREELIST_POINTER(_x) (_x).s.pointer
#define FREELIST_VERSION(_x) (_x).s.version
#define SET_FREELIST_POINTER_VERSION(_x,_p,_v) \
(_x).s.pointer = _p; (_x).s.version = _v
#elif defined(__x86_64__)
#define FREELIST_POINTER(_x) ((void*)(((((intptr_t)(_x).data)>>63)<<48)|(((intptr_t)(_x).data)&0xFFFFFFFFFFFFLL)))
#define FREELIST_VERSION(_x) ((((intptr_t)(_x).data)<<1)>>49)
#define SET_FREELIST_POINTER_VERSION(_x,_p,_v) \
  (_x).data = ((((intptr_t)(_p))&0x8000FFFFFFFFFFFFLL) | (((_v)&0x7FFFLL) << 48))
#else
#error "unsupported processor"
#endif

  typedef void *void_p;

  typedef struct
  {
#if (defined(INK_USE_MUTEX_FOR_FREELISTS) || defined(CHECK_FOR_DOUBLE_FREE))
    ink_mutex inkfreelist_mutex;
#endif
#if (defined(USE_SPINLOCK_FOR_FREELIST) || defined(CHECK_FOR_DOUBLE_FREE))
    /*
      This assumes  we will never use anything other than Pthreads
      on alpha
    */
    ink_mutex freelist_mutex;
    volatile void_p *head;
#ifdef CHECK_FOR_DOUBLE_FREE
    volatile void_p *tail;
#endif
#else
    volatile head_p head;
#endif

    const char *name;
    inku32 type_size, chunk_size, count, allocated, offset, alignment;
    inku32 allocated_base, count_base;
  } InkFreeList, *PInkFreeList;

  inkcoreapi extern volatile ink64 fastalloc_mem_in_use;
  inkcoreapi extern volatile ink64 fastalloc_mem_total;
  inkcoreapi extern volatile ink64 freelist_allocated_mem;

/*
 * alignment must be a power of 2
 */
  InkFreeList *ink_freelist_create(const char *name, inku32 type_size,
                                   inku32 chunk_size, inku32 offset_to_next, inku32 alignment);

  inkcoreapi void ink_freelist_init(InkFreeList * fl, const char *name,
                                    inku32 type_size, inku32 chunk_size,
                                    inku32 offset_to_next, inku32 alignment);
#if !defined(INK_USE_MUTEX_FOR_FREELISTS)
  inkcoreapi void *ink_freelist_new(InkFreeList * f);
  inkcoreapi void ink_freelist_free(InkFreeList * f, void *item);
#else                           /* INK_USE_MUTEX_FOR_FREELISTS */
  inkcoreapi void *ink_freelist_new_wrap(InkFreeList * f);
  static inline void *ink_freelist_new(InkFreeList * f)
  {
    void *retval = NULL;
      ink_mutex_acquire(&(f->inkfreelist_mutex));
      retval = ink_freelist_new_wrap(f);
      ink_mutex_release(&(f->inkfreelist_mutex));
      return retval;
  }

  inkcoreapi void ink_freelist_free_wrap(InkFreeList * f, void *item);
  static inline void ink_freelist_free(InkFreeList * f, void *item)
  {
    ink_mutex_acquire(&(f->inkfreelist_mutex));
    ink_freelist_free_wrap(f, item);
    ink_mutex_release(&(f->inkfreelist_mutex));
  }
#endif /* INK_USE_MUTEX_FOR_FREELISTS */
  void ink_freelists_dump(FILE * f);
  void ink_freelists_dump_baselinerel(FILE * f);
  void ink_freelists_snap_baseline();

  typedef struct
  {
#if defined(INK_USE_MUTEX_FOR_ATOMICLISTS)
    ink_mutex inkatomiclist_mutex;
#endif
    volatile head_p head;
    const char *name;
    inku32 offset;
  } InkAtomicList;

#if !defined(INK_QUEUE_NT)
#define INK_ATOMICLIST_EMPTY(_x) (!(TO_PTR(FREELIST_POINTER((_x.head)))))
#else
  /* ink_queue_nt.c doesn't do the FROM/TO pointer swizzling */
#define INK_ATOMICLIST_EMPTY(_x) (!(      (FREELIST_POINTER((_x.head)))))
#endif

  inkcoreapi void ink_atomiclist_init(InkAtomicList * l, const char *name, inku32 offset_to_next);
#if !defined(INK_USE_MUTEX_FOR_ATOMICLISTS)
  inkcoreapi void *ink_atomiclist_push(InkAtomicList * l, void *item);
  void *ink_atomiclist_pop(InkAtomicList * l);
  inkcoreapi void *ink_atomiclist_popall(InkAtomicList * l);
/*
 * WARNING WARNING WARNING WARNING WARNING WARNING WARNING
 * only if only one thread is doing pops it is possible to have a "remove"
 * which only that thread can use as well.
 * WARNING WARNING WARNING WARNING WARNING WARNING WARNING
 */
  void *ink_atomiclist_remove(InkAtomicList * l, void *item);
#else /* INK_USE_MUTEX_FOR_ATOMICLISTS */
  void *ink_atomiclist_push_wrap(InkAtomicList * l, void *item);
  static inline void *ink_atomiclist_push(InkAtomicList * l, void *item)
  {
    void *ret_value = NULL;
    ink_mutex_acquire(&(l->inkatomiclist_mutex));
    ret_value = ink_atomiclist_push_wrap(l, item);
    ink_mutex_release(&(l->inkatomiclist_mutex));
    return ret_value;
  }

  void *ink_atomiclist_pop_wrap(InkAtomicList * l);
  static inline void *ink_atomiclist_pop(InkAtomicList * l)
  {
    void *ret_value = NULL;
    ink_mutex_acquire(&(l->inkatomiclist_mutex));
    ret_value = ink_atomiclist_pop_wrap(l);
    ink_mutex_release(&(l->inkatomiclist_mutex));
    return ret_value;
  }

  void *ink_atomiclist_popall_wrap(InkAtomicList * l);
  static inline void *ink_atomiclist_popall(InkAtomicList * l)
  {
    void *ret_value = NULL;
    ink_mutex_acquire(&(l->inkatomiclist_mutex));
    ret_value = ink_atomiclist_popall_wrap(l);
    ink_mutex_release(&(l->inkatomiclist_mutex));
    return ret_value;
  }

  void *ink_atomiclist_remove_wrap(InkAtomicList * l, void *item);
  static inline void *ink_atomiclist_remove(InkAtomicList * l, void *item)
  {
    void *ret_value = NULL;
    ink_mutex_acquire(&(l->inkatomiclist_mutex));
    ret_value = ink_atomiclist_remove_wrap(l, item);
    ink_mutex_release(&(l->inkatomiclist_mutex));
    return ret_value;
  }
#endif /* INK_USE_MUTEX_FOR_ATOMICLISTS */
#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _ink_queue_h_ */

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

#ifndef _ink_queue_ext_h_
#define _ink_queue_ext_h_

/***********************************************************************

    Head file of Reclaimable freelist

***********************************************************************/

#include "List.h"
#include "ink_queue.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */
#if TS_USE_RECLAIMABLE_FREELIST
struct _InkThreadCache;
struct _InkFreeList;

typedef struct _InkChunkInfo {
  pthread_t tid;

  uint32_t type_size;
  uint32_t chunk_size;
  uint32_t allocated;
  uint32_t length;

  /*
   * inner free list will only be
   * accessed by creator-thread
   */
  void *inner_free_list;
  void *head;

  struct _InkThreadCache *pThreadCache;

  LINK(_InkChunkInfo, link);

#ifdef DEBUG
  /*
   * magic code for each item,
   * it's used to check double-free issue.
   */
  unsigned char item_magic[0];
#endif
} InkChunkInfo;

typedef struct _InkThreadCache {
  struct _InkFreeList *f;

  /* outer free list will be accessed by:
   * - creator-thread, asa producer-thread
   * - consumer-thread
   * - neighbor-thread
   */
  InkAtomicList outer_free_list;

  /* using for memory reclaim algorithm */
  float nr_average;
  uint32_t nr_total;
  uint32_t nr_free;
  uint32_t nr_min;
  uint32_t nr_overage;
  uint32_t nr_malloc;

  /* represent the status(state) of allocator: Malloc-ing(0) or Free-ing(1),
   * I use it as an simple state machine - calculating the minimum of free
   * memory only when the status change from Malloc-ing to Free-ing.
   */
  uint32_t status;

  uint32_t nr_free_chunks;
  DLL<InkChunkInfo> free_chunk_list;

  _InkThreadCache *prev, *next;
} InkThreadCache;

typedef struct _InkFreeList {
  uint32_t thread_cache_idx;

  uint32_t refcnt;
  const char *name;

  uint32_t type_size;
  uint32_t alignment;

  /* number of elements in one chunk */
  uint32_t chunk_size;
  /* total byte size of one chuck */
  uint32_t chunk_byte_size;
  /* chunk_addr = (uintptr_t)ptr & chunk_addr_mask */
  uintptr_t chunk_addr_mask;

  uint32_t used;
  uint32_t allocated;
  uint32_t allocated_base;
  uint32_t used_base;
  uint32_t chunk_size_base;

  uint32_t nr_thread_cache;
  InkThreadCache *pThreadCache;
  ink_mutex lock;
} InkFreeList, *PInkFreeList;

/* reclaimable freelist API */
void reclaimable_freelist_init(InkFreeList **fl, const char *name, uint32_t type_size, uint32_t chunk_size, uint32_t alignment);
void *reclaimable_freelist_new(InkFreeList *f);
void reclaimable_freelist_free(InkFreeList *f, void *item);
#endif /* END OF TS_USE_RECLAIMABLE_FREELIST */
#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _ink_queue_ext_h_ */

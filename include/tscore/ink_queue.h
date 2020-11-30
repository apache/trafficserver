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

#pragma once

/***********************************************************************

    Generic Queue Implementation (for pointer data types only)

    Uses atomic memory operations to avoid blocking.
    Intended as a replacement for llqueue.


***********************************************************************/

#include <atomic>
#include <cstdio>

#include "tscore/ink_apidefs.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

struct _InkFreeList {
  std::atomic<void *> head{nullptr};
  const char *name;
  std::atomic<uint32_t> used, allocated;
  uint32_t type_size, chunk_size, alignment;
  uint32_t allocated_base, used_base;
  int advice;
};

typedef struct ink_freelist_ops InkFreeListOps;
typedef struct _InkFreeList InkFreeList;

const InkFreeListOps *ink_freelist_malloc_ops();
const InkFreeListOps *ink_freelist_freelist_ops();
void ink_freelist_init_ops(int nofl_class, int nofl_proxy);

/*
 * alignment must be a power of 2
 */
InkFreeList *ink_freelist_create(const char *name, uint32_t type_size, uint32_t chunk_size, uint32_t alignment);

inkcoreapi void ink_freelist_init(InkFreeList **fl, const char *name, uint32_t type_size, uint32_t chunk_size, uint32_t alignment);
inkcoreapi void ink_freelist_madvise_init(InkFreeList **fl, const char *name, uint32_t type_size, uint32_t chunk_size,
                                          uint32_t alignment, int advice);
inkcoreapi void *ink_freelist_new(InkFreeList *f);
inkcoreapi void ink_freelist_free(InkFreeList *f, void *item);
inkcoreapi void ink_freelist_free_bulk(InkFreeList *f, void *head, void *tail, size_t num_item);
void ink_freelists_dump(FILE *f);
void ink_freelists_dump_baselinerel(FILE *f);
void ink_freelists_snap_baseline();

struct InkAtomicList {
  InkAtomicList() {}
  std::atomic<void *> head{nullptr};
  const char *name = nullptr;
  uint32_t offset  = 0;
};

#define INK_ATOMICLIST_EMPTY(_x) (_x.head == nullptr)

inkcoreapi void ink_atomiclist_init(InkAtomicList *l, const char *name, uint32_t offset_to_next);
inkcoreapi void *ink_atomiclist_push(InkAtomicList *l, void *item);
void *ink_atomiclist_pop(InkAtomicList *l);
inkcoreapi void *ink_atomiclist_popall(InkAtomicList *l);
inkcoreapi void *ink_atomiclist_next(InkAtomicList *l, void *item);
/*
 * WARNING WARNING WARNING WARNING WARNING WARNING WARNING
 * only if only one thread is doing pops it is possible to have a "remove"
 * which only that thread can use as well.
 * WARNING WARNING WARNING WARNING WARNING WARNING WARNING
 */
void *ink_atomiclist_remove(InkAtomicList *l, void *item);

#ifdef __cplusplus
}
#endif /* __cplusplus */

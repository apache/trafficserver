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

#include <cstdio>
#include <atomic>

#include <tscore/ink_apidefs.h>
#include <tscore/ver_ptr.h>

/*
 * Why is version required? One scenario is described below
 * Think of a list like this -> A -> C -> D
 * and you are popping from the list
 * Between the time you take the ptr(A) and swap the head pointer
 * the list could start looking like this
 * -> A -> B -> C -> D
 * If the version check is not there, the list will look like
 * -> C -> D after the pop, which will result in the loss of "B"
 *
 * For more information, see:  https://en.wikipedia.org/wiki/ABA_problem .
 * (Versioning is a case of the "tagged state reference" workaround.)
 */

struct _InkFreeList {
  ts::Atomic_versioned_ptr head;
  const char *name{nullptr};
  std::atomic<uint32_t> used{0}, allocated{0};
  uint32_t type_size{0}, chunk_size{0}, alignment{0};
  uint32_t allocated_base{0}, used_base{0};
  int advice{0};
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

void ink_freelist_init(InkFreeList **fl, const char *name, uint32_t type_size, uint32_t chunk_size, uint32_t alignment);
void ink_freelist_madvise_init(InkFreeList **fl, const char *name, uint32_t type_size, uint32_t chunk_size, uint32_t alignment,
                               int advice);
void *ink_freelist_new(InkFreeList *f);
void ink_freelist_free(InkFreeList *f, void *item);
void ink_freelist_free_bulk(InkFreeList *f, void *head, void *tail, size_t num_item);
void ink_freelists_dump(FILE *f);
void ink_freelists_dump_baselinerel(FILE *f);
void ink_freelists_snap_baseline();

struct InkAtomicList {
  InkAtomicList() {}
  InkAtomicList(char const *n, uint32_t ofs) : name(n), offset(ofs) {}
  ts::Atomic_versioned_ptr head;
  const char *name = nullptr;
  uint32_t offset  = 0;
};

#define INK_ATOMICLIST_EMPTY(_x) ((_x).head.load().ptr() == nullptr)

void *ink_atomiclist_push(InkAtomicList *l, void *item);
void *ink_atomiclist_pop(InkAtomicList *l);
void *ink_atomiclist_popall(InkAtomicList *l);
void *ink_atomiclist_next(InkAtomicList *l, void *item);
/*
 * WARNING WARNING WARNING WARNING WARNING WARNING WARNING
 * only if only one thread is doing pops it is possible to have a "remove"
 * which only that thread can use as well.
 * WARNING WARNING WARNING WARNING WARNING WARNING WARNING
 */
void *ink_atomiclist_remove(InkAtomicList *l, void *item);

/** @file

  Fast-Allocators

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

  Provides three classes
    - Allocator for allocating memory blocks of fixed size
    - ClassAllocator for allocating objects
    - SpaceClassAllocator for allocating sparse objects (most members uninitialized)

  These class provides a efficient way for handling dynamic allocation.
  The fast allocator maintains its own freepool of objects from
  which it doles out object. Allocated objects when freed go back
  to the free pool.

  @note Fast allocators could accumulate a lot of objects in the
  free pool as a result of bursty demand. Memory used by the objects
  in the free pool never gets freed even if the freelist grows very
  large.

 */

#pragma once

#include <cstdlib>
#include <new>
#include <type_traits>
#include <utility>
#include "tscore/ink_queue.h"
#include "tscore/ink_defs.h"
#include "tscore/ink_resource.h"
#include <execinfo.h>

#define RND16(_x) (((_x) + 15) & ~15)

/** Allocator for fixed size memory blocks. */
class Allocator
{
public:
  /**
    Allocate a block of memory (size specified during construction
    of Allocator.
  */
  void *
  alloc_void()
  {
    return ink_freelist_new(this->fl);
  }

  /**
    Deallocate a block of memory allocated by the Allocator.

    @param ptr pointer to be freed.
  */
  void
  free_void(void *ptr)
  {
    ink_freelist_free(this->fl, ptr);
  }

  /**
    Deallocate blocks of memory allocated by the Allocator.

    @param head pointer to be freed.
    @param tail pointer to be freed.
    @param num_item of blocks to be freed.
  */
  void
  free_void_bulk(void *head, void *tail, size_t num_item)
  {
    ink_freelist_free_bulk(this->fl, head, tail, num_item);
  }

  Allocator() { fl = nullptr; }

  /**
    Creates a new allocator.

    @param name identification tag used for mem tracking .
    @param element_size size of memory blocks to be allocated.
    @param chunk_size number of units to be allocated if free pool is empty.
    @param alignment of objects must be a power of 2.
  */
  Allocator(const char *name, unsigned int element_size, unsigned int chunk_size = 128, unsigned int alignment = 8)
  {
    ink_freelist_init(&fl, name, element_size, chunk_size, alignment);
  }

  /** Re-initialize the parameters of the allocator. */
  void
  re_init(const char *name, unsigned int element_size, unsigned int chunk_size, unsigned int alignment, int advice)
  {
    ink_freelist_madvise_init(&this->fl, name, element_size, chunk_size, alignment, advice);
  }

protected:
  InkFreeList *fl;
};

/**
  Allocator for Class objects.

*/
template <class C, bool Destruct_on_free = false> class ClassAllocator : public Allocator
{
public:
  /** Allocates objects of the templated type.  Arguments are forwarded to the constructor for the object. */
  template <typename... Args>
  C *
  alloc(Args &&... args)
  {
    void *ptr = ink_freelist_new(this->fl);

    ::new (ptr) C(std::forward<Args>(args)...);
    return (C *)ptr;
  }

  /**
    Deallocates objects of the templated type.

    @param ptr pointer to be freed.
  */
  void
  free(C *ptr)
  {
    if (Destruct_on_free) {
      ptr->~C();
    }
    ink_freelist_free(this->fl, ptr);
  }

  /**
    Deallocates objects of the templated type.

    @param head pointer to be freed.
    @param tail pointer to be freed.
    @param num_item of blocks to be freed.
   */
  std::enable_if_t<!Destruct_on_free, void>
  free_bulk(C *head, C *tail, size_t num_item)
  {
    ink_freelist_free_bulk(this->fl, head, tail, num_item);
  }

  /**
    Allocate objects of the templated type via the inherited interface
    using void pointers.
  */
  void *
  alloc_void()
  {
    return alloc();
  }

  /**
    Deallocate objects of the templated type via the inherited
    interface using void pointers.

    @param ptr pointer to be freed.
  */
  std::enable_if_t<!Destruct_on_free, void>
  free_void(void *ptr)
  {
    free((C *)ptr);
  }

  /**
    Deallocate objects of the templated type via the inherited
    interface using void pointers.

    @param head pointer to be freed.
    @param tail pointer to be freed.
    @param num_item of blocks.
  */
  std::enable_if_t<!Destruct_on_free, void>
  free_void_bulk(void *head, void *tail, size_t num_item)
  {
    free_bulk((C *)head, (C *)tail, num_item);
  }

  /**
    Create a new class specific ClassAllocator.

    @param name some identifying name, used for mem tracking purposes.
    @param chunk_size number of units to be allocated if free pool is empty.
    @param alignment of objects must be a power of 2.
  */
  ClassAllocator(const char *name, unsigned int chunk_size = 128, unsigned int alignment = 16)
  {
    ink_freelist_init(&this->fl, name, RND16(sizeof(C)), chunk_size, RND16(alignment));
  }
};

template <class C, bool Destruct_on_free = false> class TrackerClassAllocator : public ClassAllocator<C, Destruct_on_free>
{
public:
  TrackerClassAllocator(const char *name, unsigned int chunk_size = 128, unsigned int alignment = 16)
    : ClassAllocator<C, Destruct_on_free>(name, chunk_size, alignment), allocations(0), trackerLock(PTHREAD_MUTEX_INITIALIZER)
  {
  }

  C *
  alloc()
  {
    void *callstack[3];
    int frames = backtrace(callstack, 3);
    C *ptr     = ClassAllocator<C, Destruct_on_free>::alloc();

    const void *symbol = nullptr;
    if (frames == 3 && callstack[2] != nullptr) {
      symbol = callstack[2];
    }

    tracker.increment(symbol, (int64_t)sizeof(C), this->fl->name);
    ink_mutex_acquire(&trackerLock);
    reverse_lookup[ptr] = symbol;
    ++allocations;
    ink_mutex_release(&trackerLock);

    return ptr;
  }

  void
  free(C *ptr)
  {
    ink_mutex_acquire(&trackerLock);
    std::map<void *, const void *>::iterator it = reverse_lookup.find(ptr);
    if (it != reverse_lookup.end()) {
      tracker.increment((const void *)it->second, (int64_t)sizeof(C) * -1, nullptr);
      reverse_lookup.erase(it);
    }
    ink_mutex_release(&trackerLock);
    ClassAllocator<C, Destruct_on_free>::free(ptr);
  }

private:
  ResourceTracker tracker;
  std::map<void *, const void *> reverse_lookup;
  uint64_t allocations;
  ink_mutex trackerLock;
};

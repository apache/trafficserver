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
    - SpaceClassAllocator for allocating sparce objects (most members uninitialized)

  These class provides a efficient way for handling dynamic allocation.
  The fast allocator maintains its own freepool of objects from
  which it doles out object. Allocated objects when freed go back
  to the free pool.

  @note Fast allocators could accumulate a lot of objects in the
  free pool as a result of bursty demand. Memory used by the objects
  in the free pool never gets freed even if the freelist grows very
  large.

 */

#ifndef _Allocator_h_
#define _Allocator_h_

#include <new>
#include <stdlib.h>
#include "ts/ink_queue.h"
#include "ts/ink_defs.h"
#include "ts/ink_queue.h"
#include "ts/ink_resource.h"

#include <execinfo.h>
#include <new>
#include <cstdlib>

#define RND16(_x) (((_x) + 15) & ~15)

struct _InkFreeList {
  head_p head;
  const char *name;
  uint32_t type_size, chunk_size, used, allocated, alignment;
  uint32_t allocated_base, used_base;
  int advice;
};

typedef struct ink_freelist_ops InkFreeListOps;
typedef struct _InkFreeList InkFreeList;

extern "C" {

const InkFreeListOps *ink_freelist_malloc_ops();
const InkFreeListOps *ink_freelist_freelist_ops();
void ink_freelist_init_ops(const InkFreeListOps *);

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
}

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

  /** Deallocate a block of memory allocated by the Allocator. */
  void
  free_void(void *ptr)
  {
    ink_freelist_free(this->fl, ptr);
  }

  /** Deallocate blocks of memory allocated by the Allocator. */
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
  Allocator for Class objects. It uses a prototype object to do
  fast initialization. Prototype of the template class is created
  when the fast allocator is created. This is instantiated with
  default (no argument) constructor. Constructor is not called for
  the allocated objects. Instead, the prototype is just memory
  copied onto the new objects. This is done for performance reasons.

*/
template <class C> class ClassAllocator : public Allocator
{
public:
  /** Allocates objects of the templated type. */
  C *
  alloc()
  {
    void *ptr = ink_freelist_new(this->fl);

    memcpy(ptr, (void *)&this->proto.typeObject, sizeof(C));
    return (C *)ptr;
  }

  /**
    Deallocates objects of the templated type.

    @param ptr pointer to be freed.
  */
  void
  free(C *ptr)
  {
    ink_freelist_free(this->fl, ptr);
  }

  /**
     Deallocates objects of the templated type.

     @param head pointer to be freed.
     @param tail pointer to be freed.
     @param count of blocks to be freed.
   */
  void
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
    return (void *)alloc();
  }

  /**
    Deallocate objects of the templated type via the inherited
    interface using void pointers.

    @param ptr pointer to be freed.
  */
  void
  free_void(void *ptr)
  {
    free((C *)ptr);
  }

  /**
      Deallocate objects of the templated type via the inherited
      interface using void pointers.

      @param head pointer to be freed.
      @param tail pointer to be freed.
      @param count of blocks
    */
  void
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
    ::new ((void *)&proto.typeObject) C();
    ink_freelist_init(&this->fl, name, RND16(sizeof(C)), chunk_size, RND16(alignment));
  }

  struct {
    uint8_t typeObject[sizeof(C)];
    int64_t space_holder = 0;
  } proto;
};

template <class C> class TrackerClassAllocator : public ClassAllocator<C>
{
public:
  TrackerClassAllocator(const char *name, unsigned int chunk_size = 128, unsigned int alignment = 16)
    : ClassAllocator<C>(name, chunk_size, alignment), allocations(0), trackerLock(PTHREAD_MUTEX_INITIALIZER)
  {
  }

  C *
  alloc()
  {
    void *callstack[3];
    int frames = backtrace(callstack, 3);
    C *ptr     = ClassAllocator<C>::alloc();

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
    ClassAllocator<C>::free(ptr);
  }

private:
  ResourceTracker tracker;
  std::map<void *, const void *> reverse_lookup;
  uint64_t allocations;
  ink_mutex trackerLock;
};

#if HAVE_LIBJEMALLOC

// these calls can be removed
#define ink_freelists_dump(...)
#define ink_freelists_dump_baselinerel(...)
#define ink_freelists_snap_baseline(...)

#define ink_freelist_init_ops(...)

#include "ts/JemAllocator.h"

// cover up use of original Allocators above
#define Allocator AlignedAllocator
#define ClassAllocator ObjAllocator
#define ProxyAllocator ThreadAllocatorStub

class ThreadAllocatorStub
{
};

// define use of for thread Allocators
#define THREAD_ALLOC(allocObj, t) (::allocObj.alloc())
#define THREAD_ALLOC_INIT(allocObj, t) (::allocObj.alloc())
#define THREAD_FREE(ptr, allocObj, t) (::allocObj.free(ptr))

extern int thread_freelist_high_watermark;
extern int thread_freelist_low_watermark;

#endif

#endif // _Allocator_h_

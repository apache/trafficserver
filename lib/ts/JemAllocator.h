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
    - AlignedAllocator for allocating memory blocks of fixed size / alignment
    - ObjAllocator for allocating objects

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

#include "ts/jemallctl.h"

#include "ts/ink_queue.h"
#include "ts/ink_defs.h"
#include "ts/ink_resource.h"
#include "ts/ink_align.h"
#include "ts/ink_memory.h"

#include <atomic>
#include <memory>
#include <new>
#include <cstdlib>

class AlignedAllocator
{
  const char *_name = nullptr;
  unsigned _sz       = 0; // size with alignment from page bound
  unsigned _align    = sizeof(uint64_t); // size with alignment from page bound
  unsigned _arena    = 0; // jemalloc arena
  unsigned _chunkSize = 128; // default
  std::atomic_uint _preMapped{}; // all-process limit

public:
  AlignedAllocator() {}
  AlignedAllocator(const char *name, unsigned int element_size)
    : _name(name), _sz(aligned_size(element_size, sizeof(uint64_t)))
  {
    // no cache-warming until first alloc
  }

  AlignedAllocator(const char *name, unsigned int element_size, unsigned int chunk_size)
    : _name(name), _sz(aligned_size(element_size, sizeof(uint64_t))), _chunkSize(chunk_size)
  {
    // no cache-warming until first alloc
  }

  AlignedAllocator(const char *name, unsigned int element_size, unsigned int chunk_size, unsigned int alignment)
    : _name(name), _sz(aligned_size(element_size, alignment)), _align(alignment), _chunkSize(chunk_size)
  {
    // no cache-warming until first alloc
  }

  void *
  alloc_void()
  {
    if (unlikely( _preMapped < _chunkSize)) {
      init_premapped(&_preMapped,_sz,_align,_chunkSize);
    }
    return allocate();
  }
  void
  free_void(void *ptr)
  {
    deallocate(ptr);
  }
  void *
  alloc()
  {
    if (unlikely( _preMapped < _chunkSize)) {
      init_premapped(&_preMapped,_sz,_align,_chunkSize);
    }
    return alloc_void();
  }
  void
  free(void *ptr)
  {
    free_void(ptr);
  }

  // called to require that mapped / dirty memory pages are held by process (in arena)
  static void init_premapped(std::atomic_uint *preMapped, unsigned len, unsigned align, unsigned chunk_size, unsigned arena = ~0U);

  // called to require that thread-cached memory is ready (in arena for current thread only)
  static void init_precached(unsigned len, unsigned align, unsigned chunk_size, unsigned arena = ~0U);

  void re_init(const char *name, unsigned int element_size, unsigned int chunk_size, unsigned int alignment, int advice);

protected:
  void *
  allocate()
  {
    return mallocx(_sz, (MALLOCX_ALIGN(_sz) | MALLOCX_ZERO | MALLOCX_ARENA(_arena)));
  }
  void
  deallocate(void *p)
  {
    dallocx(p, MALLOCX_ARENA(_arena));
  }
};


template <size_t N_LENGTH, size_t N_ALIGN>
class SizeCacheAllocator
{
public:
  SizeCacheAllocator(unsigned chunk_size = 128) : _chunkSize(128) { }
  
protected:
  void *
  allocate()
  {
    // pre-caching needed?
    if (unlikely( tm_preCached < _chunkSize )) {
      AlignedAllocator::init_precached(N_LENGTH, N_ALIGN, _chunkSize - tm_preCached);
      tm_preCached = _chunkSize; // up to spec now
    }
    // perform the alloc
    return mallocx(N_LENGTH, MALLOCX_ZERO);
  }

  void
  deallocate(void *p)
  {
    sdallocx(p, N_LENGTH, 0);
  }

private:
  static thread_local int32_t tm_preCached; // shared for same-thread + same-size
  uint32_t                    _chunkSize; // per all instances
};

template <size_t N_LENGTH, size_t N_ALIGN>
thread_local int SizeCacheAllocator<N_LENGTH,N_ALIGN>::tm_preCached = 0;

//
// type-specialized wrapper
//
template <class T_OBJECT, size_t N_SIZE = aligned_size(sizeof(T_OBJECT),alignof(T_OBJECT)) > 
class ObjAllocator : public SizeCacheAllocator<N_SIZE,alignof(T_OBJECT)>
{
public:
  using value_type = T_OBJECT;
  using Allocator_t = SizeCacheAllocator<N_SIZE,alignof(T_OBJECT)>;

  ObjAllocator(const char *name, size_t chunk_size = 128)
     : Allocator_t(chunk_size), _name(name)
  {
  }

  void *
  alloc_void()
  {
    return alloc();
  }
  void
  free_void(void *ptr)
  {
    free(static_cast<value_type*>(ptr));
  }

  value_type *
  alloc()
  {
    ++_active; // track total
    auto p = static_cast<value_type *>(Allocator_t::allocate());
    _cxxAllocator.construct(p);
    return p;
  }

  void
  free(value_type *p)
  {
    _cxxAllocator.destroy(p);
    Allocator_t::deallocate(p);
    --_active; // track total
  }

private:
  const char *               _name = nullptr;
  std::atomic_int            _active{}; // init state
  std::allocator<T_OBJECT>   _cxxAllocator; // likely no-memory size
};

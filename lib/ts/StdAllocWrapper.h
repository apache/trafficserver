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

#include "ts/ink_queue.h"
#include "ts/ink_defs.h"
#include "ts/ink_resource.h"
#include "ts/ink_align.h"
#include "ts/ink_memory.h"

#include <execinfo.h> // for backtrace!

#include <new>
#include <memory>
#include <cstdlib>

class AlignedAllocator
{
  const char *_name = nullptr;
  size_t _sz        = 0; // bytes and alignment (both)
  size_t _arena     = 0; // jemalloc arena

public:
  AlignedAllocator() {}
  AlignedAllocator(const char *name, unsigned int element_size);

  void *
  alloc_void()
  {
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
    return alloc_void();
  }
  void
  free(void *ptr)
  {
    free_void(ptr);
  }

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

class ObjAllocatorBase
{
public:
  ObjAllocatorBase(const char *name, unsigned size, unsigned aligned, unsigned chunk_size = 128) : _name(name)
  {
    void *preCached[chunk_size];

    for (int n = chunk_size; n--;) {
      // create correct size and alignment
      preCached[n] = mallocx(size, MALLOCX_ALIGN(aligned));
    }
    for (int n = chunk_size; n--;) {
      deallocate(preCached[n], size);
    }
  }

protected:
  void
  deallocate(void *p, unsigned size)
  {
    sdallocx(p, size, 0);
  }

private:
  const char *_name;
};

//
// type-specialized wrapper
//
template <typename T_OBJECT> class ObjAllocator : public ObjAllocatorBase
{
public:
  using value_type = T_OBJECT;

  ObjAllocator(const char *name, unsigned chunk_size = 128)
    : ObjAllocatorBase(name, sizeof(value_type), alignof(value_type), chunk_size)
  {
  }

  void *
  alloc_void()
  {
    return allocate();
  }
  void
  free_void(void *ptr)
  {
    static_cast<value_type *>(ptr)->~value_type();
    ObjAllocatorBase::deallocate(ptr, sizeof(value_type));
  }
  value_type *
  alloc()
  {
    return allocate();
  }
  void
  free(value_type *ptr)
  {
    ptr->~value_type(); // dtor called
    ObjAllocatorBase::deallocate(ptr, sizeof(value_type));
  }

protected:
  value_type *
  allocate()
  {
    auto p = static_cast<value_type *>(mallocx(sizeof(value_type), MALLOCX_ALIGN(alignof(value_type)) | MALLOCX_ZERO));
    std::allocator<T_OBJECT>().construct(p); // ctor called
    return p;
  }

  void
  deallocate(value_type *p)
  {
    ObjAllocatorBase::deallocate(p, sizeof(value_type));
  }
};

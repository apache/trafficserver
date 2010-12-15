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

#include <stdlib.h>
#include "ink_queue.h"
#include "ink_port.h"
#include "ink_resource.h"

#ifdef USE_DALLOC
#include "DAllocatore.h"
#endif

#define RND16(_x)               (((_x)+15)&~15)

/** Allocator for fixed size memory blocks. */
class Allocator
{
public:
#ifdef USE_DALLOC
  DAllocator da;
#else
  InkFreeList fl;
#endif

  /**
    Allocate a block of memory (size specified during construction
    of Allocator.

  */
  void *alloc_void();

  /** Deallocate a block of memory allocated by the Allocator. */
  void free_void(void *ptr);

    Allocator()
  {
#ifndef USE_DALLOC
    memset(&fl, 0, sizeof fl);
#endif
  }

  /**
    Creates a new allocator.

    @param name identification tag used for mem tracking .
    @param element_size size of memory blocks to be allocated.
    @param chunk_size number of units to be allocated if free pool is empty.
    @param alignment of objects must be a power of 2.

  */
  Allocator(const char *name, unsigned int element_size, unsigned int chunk_size = 128, unsigned int alignment = 8);

  /** Re-initialize the parameters of the allocator. */
  void re_init(const char *name, unsigned int element_size, unsigned int chunk_size, unsigned int alignment);
};

/**
  Allocator for Class objects. It uses a prototype object to do
  fast initialization. Prototype of the template class is created
  when the fast allocator is created. This is instantiated with
  default (no argument) constructor. Constructor is not called for
  the allocated objects. Instead, the prototype is just memory
  copied onto the new objects. This is done for performance reasons.

*/
template<class C> class ClassAllocator:public Allocator {
public:

  /** Allocates objects of the templated type. */
  C * alloc();

  /**
    Deallocates objects of the templated type.

    @param ptr pointer to be freed.

  */
  void free(C * ptr);

  /**
    Allocate objects of the templated type via the inherited interface
    using void pointers.

  */
  void *alloc_void()
  {
    return (void *) alloc();
  }

  /**
    Deallocate objects of the templated type via the inherited
    interface using void pointers.

    @param ptr pointer to be freed.

  */
  void free_void(void *ptr)
  {
    free((C *) ptr);
  }

  /**
    Create a new class specific ClassAllocator.

    @param name some identifying name, used for mem tracking purposes.
    @param chunk_size number of units to be allocated if free pool is empty.
    @param alignment of objects must be a power of 2.

  */
  ClassAllocator(const char *name, unsigned int chunk_size = 128, unsigned int alignment = 16);

  /** Private data. */
  struct _proto
  {
    C typeObject;
    int64_t space_holder;
  } proto;
};

/**
  Allocator for space class, a class with a lot of uninitialized
  space/members. It uses an instantiate fucntion do initialization
  of objects. This is particulary useful if most of the space in
  the objects does not need to be intialized. The inifunction passed
  can be used to intialize a few fields selectively. Using
  ClassAllocator for space objects would unnecessarily initialized
  all of the members.

*/
template<class C> class SparceClassAllocator:public ClassAllocator<C> {
public:

  /** Allocates objects of the templated type. */
  C * alloc();

  /**
    Create a new class specific SparceClassAllocator.

    @param name some identifying name, used for mem tracking purposes.
    @param chunk_size number of units to be allocated if free pool is empty.
    @param alignment of objects must be a power of 2.
    @param instantiate_func

  */
  SparceClassAllocator(const char *name,
                       unsigned int chunk_size = 128,
                       unsigned int alignment = 16, void (*instantiate_func) (C * proto, C * instance) = NULL);
  /** Private data. */
  void (*instantiate) (C * proto, C * instance);
};

inline void
Allocator::re_init(const char *name, unsigned int element_size, unsigned int chunk_size, unsigned int alignment)
{
#ifdef USE_DALLOC
  da.init(name, element_size, alignment);
#else
  ink_freelist_init(&this->fl, name, element_size, chunk_size, 0, alignment);
#endif
}

#if !defined (PURIFY) && !defined (_NO_FREELIST)
inline void *
Allocator::alloc_void()
{
#ifdef USE_DALLOC
  return this->da.alloc();
#else
  return ink_freelist_new(&this->fl);
#endif
}

inline void
Allocator::free_void(void *ptr)
{
#ifdef USE_DALLOC
  this->da.free(ptr);
#else
  ink_freelist_free(&this->fl, ptr);
#endif
}
#else
// no freelist, non WIN32 platform
inline void *
Allocator::alloc_void()
{
  return (void *) ink_memalign(this->fl.alignment, this->fl.type_size);
}
inline void
Allocator::free_void(void *ptr)
{
  if (likely(ptr))
    ::free(ptr);
  return;
}
#endif /* end no freelist */

template<class C> inline
  ClassAllocator<C>::ClassAllocator(const char *name, unsigned int chunk_size, unsigned int alignment)
{
#ifdef USE_DALLOC
  this->da.init(name, RND16(sizeof(C)), RND16(alignment));
#else
#if !defined(_NO_FREELIST)
  ink_freelist_init(&this->fl, name, RND16(sizeof(C)), chunk_size, 0, RND16(alignment));
#endif //_NO_FREELIST
#endif /* USE_DALLOC */
}

template<class C> inline
SparceClassAllocator<C>::SparceClassAllocator(const char *name, unsigned int chunk_size, unsigned int alignment,
                                              void (*instantiate_func) (C * proto, C * instance)) : ClassAllocator <C> (name, chunk_size, alignment)
{
  instantiate = instantiate_func;       // NULL by default
}

#if !defined (PURIFY) && !defined (_NO_FREELIST)

// use freelist
template<class C> inline C * ClassAllocator<C>::alloc()
{
#ifdef USE_DALLOC
  void *ptr = this->da.alloc();
#else
  void *ptr = ink_freelist_new(&this->fl);
#endif
  if (sizeof(C) < 512) {
    for (unsigned int i = 0; i < RND16(sizeof(C)) / sizeof(int64_t); i++)
      ((int64_t *) ptr)[i] = ((int64_t *) &this->proto.typeObject)[i];
  } else
    memcpy(ptr, &this->proto.typeObject, sizeof(C));
  return (C *) ptr;
}

template<class C> inline C * SparceClassAllocator<C>::alloc()
{
#ifdef USE_DALLOC
  void *ptr = this->da.alloc();
#else
  void *ptr = ink_freelist_new(&this->fl);
#endif
  if (!instantiate) {
    if (sizeof(C) < 512) {
      for (unsigned int i = 0; i < RND16(sizeof(C)) / sizeof(int64_t); i++)
        ((int64_t *) ptr)[i] = ((int64_t *) &this->proto.typeObject)[i];
    } else
      memcpy(ptr, &this->proto.typeObject, sizeof(C));
  } else
    (*instantiate) ((C *) &this->proto.typeObject, (C *) ptr);
  return (C *) ptr;
}

template<class C> inline void ClassAllocator<C>::free(C * ptr)
{
#ifdef USE_DALLOC
  this->da.free(ptr);
#else
  ink_freelist_free(&this->fl, ptr);
#endif
  return;
}

#else  // _NO_FREELIST

// no freelist
template<class C> inline C * ClassAllocator<C>::alloc()
{
  void *ptr = (void *) ink_memalign(8, sizeof(C));
  memcpy(ptr, &this->proto.typeObject, sizeof(C));
  return (C *) ptr;
}

template<class C> inline C * SparceClassAllocator<C>::alloc()
{
  void *ptr = (void *) ink_memalign(8, sizeof(C));

  if (instantiate == NULL)
    memcpy(ptr, &this->proto.typeObject, sizeof(C));
  else
    (*instantiate) ((C *) &this->proto.typeObject, (C *) ptr);

  return (C *) ptr;
}

template<class C> inline void ClassAllocator<C>::free(C * ptr)
{
  if (ptr)
    ::free(ptr);
  return;
}
#endif  // _NO_FREELIST
#endif  // _Allocator_h_

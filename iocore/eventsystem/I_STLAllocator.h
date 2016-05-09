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

#ifndef I_STLALLOCATOR_H_
#define I_STLALLOCATOR_H_

#include <string>
#include <vector>
#include <limits>
#include <cmath>
#include <cstdio>
#include "I_IOBuffer.h"

template <typename T> class ts_stl_std_allocator : public std::allocator<T>
{
public:
  typedef size_t size_type;
  typedef T *pointer;
  typedef const T *const_pointer;

  template <typename _Tp1> struct rebind {
    typedef ts_stl_std_allocator<_Tp1> other;
  };

  pointer
  allocate(size_type n, const void *hint = 0)
  {
    pointer p = std::allocator<T>::allocate(n, hint);
    return p;
  }

  void
  deallocate(pointer p, size_type n)
  {
    std::allocator<T>::deallocate(p, n);
  }

  ts_stl_std_allocator() throw() : std::allocator<T>() {}
  ts_stl_std_allocator(const ts_stl_std_allocator &a) throw() : std::allocator<T>(a) {}
  template <class U> ts_stl_std_allocator(const ts_stl_std_allocator<U> &a) throw() : std::allocator<T>(a) {}
  ~ts_stl_std_allocator() throw() {}
};

template <typename T> class ts_stl_iobuf_allocator : public ts_stl_std_allocator<T>
{
public:
  typedef size_t size_type;
  typedef T *pointer;
  typedef const T *const_pointer;

  template <typename _Tp1> struct rebind {
    typedef ts_stl_iobuf_allocator<_Tp1> other;
  };

  pointer
  allocate(size_type n, const void *hint = 0)
  {
    if (unlikely((n * sizeof(T)) > DEFAULT_MAX_BUFFER_SIZE)) {
      // we need to fall back to the ts_stl_std_allocator for large allocations
      return ts_stl_std_allocator<T>::allocate(n, hint);
    } else {
      // we can pull from a iobuf for this allocation
      int64_t iobuffer_index = iobuffer_size_to_index(n * sizeof(T));
      ink_release_assert(iobuffer_index >= 0);

      pointer p = reinterpret_cast<pointer>(ioBufAllocator[iobuffer_index].alloc_void());
      return p;
    }
  }

  void
  deallocate(pointer p, size_type n)
  {
    if (unlikely((n * sizeof(T)) > DEFAULT_MAX_BUFFER_SIZE)) {
      // we need to fall back to the ts_stl_std_allocator for large allocations
      ts_stl_std_allocator<T>::deallocate(p, n);
    } else {
      // we need to return this block to the appropriate iobuffer
      int64_t iobuffer_index = iobuffer_size_to_index(n * sizeof(T));
      ink_release_assert(iobuffer_index >= 0);
      ioBufAllocator[iobuffer_index].free_void(reinterpret_cast<void *>(p));
    }
  }

  ts_stl_iobuf_allocator() throw() : ts_stl_std_allocator<T>() {}
  ts_stl_iobuf_allocator(const ts_stl_iobuf_allocator &a) throw() : ts_stl_std_allocator<T>(a) {}
  template <class U> ts_stl_iobuf_allocator(const ts_stl_iobuf_allocator<U> &a) throw() : ts_stl_std_allocator<T>(a) {}
  ~ts_stl_iobuf_allocator() throw() {}
};

typedef std::basic_string<char, std::char_traits<char>, ts_stl_iobuf_allocator<char>> ts_string;

// with c++11 this is much cleaner as you can do alias templates.
// and then we could define a ts_vector, etc..

#endif /* I_STLALLOCATOR_H_ */

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

/****************************************************************************

   HdrHeap.h

   Description:


 ****************************************************************************/

#pragma once

#include "tscore/Ptr.h"
#include "tscore/ink_defs.h"
#include "tscore/ink_assert.h"
#include "tscore/Arena.h"
#include "tscore/Scalar.h"
#include "HdrToken.h"

// Objects in the heap must currently be aligned to 8 byte boundaries,
// so their (address & HDR_PTR_ALIGNMENT_MASK) == 0

static constexpr size_t HDR_PTR_SIZE           = sizeof(uint64_t);
static constexpr size_t HDR_PTR_ALIGNMENT_MASK = HDR_PTR_SIZE - 1L;
using HdrHeapMarshalBlocks                     = ts::Scalar<HDR_PTR_SIZE>;

// A many of the operations regarding read-only str
//  heaps are hand unrolled in the code.  Chaning
//  this value requires a full pass through HdrBuf.cc
//  to fix the unrolled operations
static constexpr unsigned HDR_BUF_RONLY_HEAPS = 3;

class CoreUtils;
class IOBufferBlock;

enum {
  HDR_HEAP_OBJ_EMPTY            = 0,
  HDR_HEAP_OBJ_RAW              = 1,
  HDR_HEAP_OBJ_URL              = 2,
  HDR_HEAP_OBJ_HTTP_HEADER      = 3,
  HDR_HEAP_OBJ_MIME_HEADER      = 4,
  HDR_HEAP_OBJ_FIELD_BLOCK      = 5,
  HDR_HEAP_OBJ_FIELD_STANDALONE = 6, // not a type that lives in HdrHeaps
  HDR_HEAP_OBJ_FIELD_SDK_HANDLE = 7, // not a type that lives in HdrHeaps

  HDR_HEAP_OBJ_MAGIC = 0x0FEEB1E0
};

struct HdrHeapObjImpl {
  uint32_t m_type : 8;
  uint32_t m_length : 20;
  uint32_t m_obj_flags : 4;
};

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

extern void obj_describe(HdrHeapObjImpl *obj, bool recurse);

inline int
obj_is_aligned(HdrHeapObjImpl *obj)
{
  return (((((uintptr_t)obj) & HDR_PTR_ALIGNMENT_MASK) == 0) && ((obj->m_length & HDR_PTR_ALIGNMENT_MASK) == 0));
}

inline void
obj_clear_data(HdrHeapObjImpl *obj)
{
  char *ptr      = (char *)obj;
  int hdr_length = sizeof(HdrHeapObjImpl);
  memset(ptr + hdr_length, '\0', obj->m_length - hdr_length);
}

inline void
obj_copy_data(HdrHeapObjImpl *s_obj, HdrHeapObjImpl *d_obj)
{
  char *src, *dst;

  ink_assert((s_obj->m_length == d_obj->m_length) && (s_obj->m_type == d_obj->m_type));

  int hdr_length = sizeof(HdrHeapObjImpl);
  src            = (char *)s_obj + hdr_length;
  dst            = (char *)d_obj + hdr_length;
  memcpy(dst, src, d_obj->m_length - hdr_length);
}

inline void
obj_copy(HdrHeapObjImpl *s_obj, char *d_addr)
{
  memcpy(d_addr, (char *)s_obj, s_obj->m_length);
}

inline void
obj_init_header(HdrHeapObjImpl *obj, uint32_t type, uint32_t nbytes, uint32_t obj_flags)
{
  obj->m_type      = type;
  obj->m_length    = nbytes;
  obj->m_obj_flags = obj_flags;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

enum {
  HDR_BUF_MAGIC_ALIVE     = 0xabcdfeed,
  HDR_BUF_MAGIC_MARSHALED = 0xdcbafeed,
  HDR_BUF_MAGIC_DEAD      = 0xabcddead,
  HDR_BUF_MAGIC_CORRUPT   = 0xbadbadcc
};

class HdrStrHeap : public RefCountObj
{
public:
  static constexpr int DEFAULT_SIZE = 2048;

  void free() override;

  char *allocate(int nbytes);
  char *expand(char *ptr, int old_size, int new_size);
  int space_avail();

  uint32_t m_heap_size;
  char *m_free_start;
  uint32_t m_free_size;

  bool contains(const char *str) const;
};

inline bool
HdrStrHeap::contains(const char *str) const
{
  return reinterpret_cast<char const *>(this + 1) <= str && str < reinterpret_cast<char const *>(this) + m_heap_size;
}

struct StrHeapDesc {
  StrHeapDesc() = default;

  Ptr<RefCountObj> m_ref_count_ptr;
  char const *m_heap_start = nullptr;
  int32_t m_heap_len       = 0;
  bool m_locked            = false;

  bool
  contains(const char *str) const
  {
    return (str >= m_heap_start && str < (m_heap_start + m_heap_len));
  }
};

class HdrHeap
{
  friend class CoreUtils;

public:
  static constexpr int DEFAULT_SIZE = 2048;

  void init();
  inkcoreapi void destroy();

  // PtrHeap allocation
  HdrHeapObjImpl *allocate_obj(int nbytes, int type);
  void deallocate_obj(HdrHeapObjImpl *obj);

  // StrHeap allocation
  char *allocate_str(int nbytes);
  char *expand_str(const char *old_str, int old_len, int new_len);
  char *duplicate_str(const char *str, int nbytes);
  void free_string(const char *s, int len);

  // Marshalling
  inkcoreapi int marshal_length();
  inkcoreapi int marshal(char *buf, int length);
  int unmarshal(int buf_length, int obj_type, HdrHeapObjImpl **found_obj, RefCountObj *block_ref);
  /// Computes the valid data size of an unmarshalled instance.
  /// Callers should round up to HDR_PTR_SIZE to get the actual footprint.
  int unmarshal_size() const; // TBD - change this name, it's confusing.
  // One option - overload marshal_length to return this value if @a magic is HDR_BUF_MAGIC_MARSHALED.

  void inherit_string_heaps(const HdrHeap *inherit_from);
  int attach_block(IOBufferBlock *b, const char *use_start);
  void set_ronly_str_heap_end(int slot, const char *end);

  // Lock read only str heaps so that can't be moved around
  //  by a heap consolidation.  Does NOT lock for Multi-Threaed
  //  access!
  void
  lock_ronly_str_heap(int i)
  {
    m_ronly_heap[i].m_locked = true;
  }

  void
  unlock_ronly_str_heap(int i)
  {
    m_ronly_heap[i].m_locked = false;
    // INKqa11238
    // Move slot i to the first available slot in m_ronly_heap[].
    // The move is necessary because the rest of the code assumes
    // heaps are always allocated in order.
    for (int j = 0; j < i; j++) {
      if (m_ronly_heap[j].m_heap_start == nullptr) {
        // move slot i to slot j
        m_ronly_heap[j].m_ref_count_ptr = m_ronly_heap[i].m_ref_count_ptr;
        m_ronly_heap[j].m_heap_start    = m_ronly_heap[i].m_heap_start;
        m_ronly_heap[j].m_heap_len      = m_ronly_heap[i].m_heap_len;
        m_ronly_heap[j].m_locked        = m_ronly_heap[i].m_locked;
        m_ronly_heap[i].m_ref_count_ptr = nullptr;
        m_ronly_heap[i].m_heap_start    = nullptr;
        m_ronly_heap[i].m_heap_len      = 0;
        m_ronly_heap[i].m_locked        = false;
      }
    }
  }

  // Sanity Check Functions
  void sanity_check_strs();
  bool check_marshalled(uint32_t buf_length);

  // Debugging functions
  void dump_heap(int len = -1);

  uint32_t m_magic;
  char *m_free_start;
  char *m_data_start;
  uint32_t m_size;

  bool m_writeable;

  // Overflow block ptr
  //   Overflow blocks are necessary because we can
  //     run out of space in the header heap and the
  //     heap is not rellocatable
  //   Overflow blocks have the HdrHeap full structure
  //    header on them, although only first block can
  //    point to string heaps
  HdrHeap *m_next;

  // HdrBuf heap pointers
  uint32_t m_free_size;

  int demote_rw_str_heap();
  void coalesce_str_heaps(int incoming_size = 0);
  void evacuate_from_str_heaps(HdrStrHeap *new_heap);
  size_t required_space_for_evacuation();
  bool attach_str_heap(char const *h_start, int h_len, RefCountObj *h_ref_obj, int *index);

  /** Struct to prevent garbage collection on heaps.
      This bumps the reference count to the heap containing the pointer
      while the instance of this class exists. When it goes out of scope
      the reference is dropped. This is useful inside a method or block
      to keep the required heap data around until leaving the scope.
  */
  struct HeapGuard {
    /// Construct the protection.
    HeapGuard(HdrHeap *heap, const char *str)
    {
      if (heap->m_read_write_heap && heap->m_read_write_heap->contains(str)) {
        m_ptr = heap->m_read_write_heap.get();
      } else {
        for (auto &i : heap->m_ronly_heap) {
          if (i.contains(str)) {
            m_ptr = i.m_ref_count_ptr;
            break;
          }
        }
      }
    }

    // There's no need to have a destructor here, the default dtor will take care of
    // releasing the (potentially) locked heap.

    /// The heap we protect (if any)
    Ptr<RefCountObj> m_ptr;
  };

  // String Heap access
  Ptr<HdrStrHeap> m_read_write_heap;
  StrHeapDesc m_ronly_heap[HDR_BUF_RONLY_HEAPS];
  int m_lost_string_space;
};

static constexpr HdrHeapMarshalBlocks HDR_HEAP_HDR_SIZE{ts::round_up(sizeof(HdrHeap))};
static constexpr size_t HDR_MAX_ALLOC_SIZE = HdrHeap::DEFAULT_SIZE - HDR_HEAP_HDR_SIZE;

inline void
HdrHeap::free_string(const char *s, int len)
{
  if (s && len > 0) {
    m_lost_string_space += len;
  }
}

inline int
HdrHeap::unmarshal_size() const
{
  return m_size + m_ronly_heap[0].m_heap_len;
}

//
struct MarshalXlate {
  char const *start;
  char const *end;
  char const *offset;
  MarshalXlate() : start(nullptr), end(nullptr), offset(nullptr) {}
};

struct HeapCheck {
  char const *start;
  char const *end;
};

// Nasty macro to do string marshalling
#define HDR_MARSHAL_STR(ptr, table, nentries)                 \
  if (ptr) {                                                  \
    int found = 0;                                            \
    for (int i = 0; i < nentries; i++) {                      \
      if (ptr >= table[i].start && ptr <= table[i].end) {     \
        ptr   = (((char *)ptr) - (uintptr_t)table[i].offset); \
        found = 1;                                            \
        break;                                                \
      }                                                       \
    }                                                         \
    ink_assert(found);                                        \
    if (found == 0) {                                         \
      return -1;                                              \
    }                                                         \
  }

// Nasty macro to do string marshalling
#define HDR_MARSHAL_STR_1(ptr, table)                       \
  if (ptr) {                                                \
    int found = 0;                                          \
    if (ptr >= table[0].start && ptr <= table[0].end) {     \
      ptr   = (((char *)ptr) - (uintptr_t)table[0].offset); \
      found = 1;                                            \
    }                                                       \
    ink_assert(found);                                      \
    if (found == 0) {                                       \
      return -1;                                            \
    }                                                       \
  }

#define HDR_MARSHAL_PTR(ptr, type, table, nentries)                       \
  if (ptr) {                                                              \
    int found = 0;                                                        \
    for (int i = 0; i < nentries; i++) {                                  \
      if ((char *)ptr >= table[i].start && (char *)ptr <= table[i].end) { \
        ptr   = (type *)(((char *)ptr) - (uintptr_t)table[i].offset);     \
        found = 1;                                                        \
        break;                                                            \
      }                                                                   \
    }                                                                     \
    ink_assert(found);                                                    \
    if (found == 0) {                                                     \
      return -1;                                                          \
    }                                                                     \
  }

#define HDR_MARSHAL_PTR_1(ptr, type, table)                             \
  if (ptr) {                                                            \
    int found = 0;                                                      \
    if ((char *)ptr >= table[0].start && (char *)ptr <= table[0].end) { \
      ptr   = (type *)(((char *)ptr) - (uintptr_t)table[0].offset);     \
      found = 1;                                                        \
    }                                                                   \
    ink_assert(found);                                                  \
    if (found == 0) {                                                   \
      return -1;                                                        \
    }                                                                   \
  }

#define HDR_UNMARSHAL_STR(ptr, offset) \
  if (ptr) {                           \
    ptr = ((char *)ptr) + offset;      \
  }

#define HDR_UNMARSHAL_PTR(ptr, type, offset) \
  if (ptr) {                                 \
    ptr = (type *)(((char *)ptr) + offset);  \
  }

// Nasty macro to do string evacuation.  Assumes
//   new heap = new_heap
#define HDR_MOVE_STR(str, len)                 \
  {                                            \
    if (str) {                                 \
      char *new_str = new_heap->allocate(len); \
      if (new_str)                             \
        memcpy(new_str, str, len);             \
      str = new_str;                           \
    }                                          \
  }

// Nasty macro to do verify all strings it
//   in attached heaps
#define CHECK_STR(str, len, _heaps, _num_heaps)                    \
  {                                                                \
    if (str) {                                                     \
      int found = 0;                                               \
      for (int i = 0; i < _num_heaps; i++) {                       \
        if (str >= _heaps[i].start && str + len <= heaps[i].end) { \
          found = 1;                                               \
        }                                                          \
      }                                                            \
      ink_release_assert(found);                                   \
    }                                                              \
  }

// struct HdrHeapSDKHandle()
//
//   Handle to a HdrHeap.
//
//   Intended to be subclassed and contain a
//     object pointer that points into the heap
//
struct HdrHeapSDKHandle {
public:
  HdrHeapSDKHandle() : m_heap(nullptr) {}
  ~HdrHeapSDKHandle() { clear(); }
  // clear() only deallocates chained SDK return values
  //   The underlying MBuffer is left untouched
  void clear();

  // destroy() frees the underlying MBuffer and deallocates all chained
  //    SDK return values
  void destroy();

  void set(const HdrHeapSDKHandle *from);
  const char *make_sdk_string(const char *raw_str, int raw_str_len);

  HdrHeap *m_heap;

  // In order to prevent gratitous refcounting,
  //  automatic C++ copies are disabled!
  HdrHeapSDKHandle(const HdrHeapSDKHandle &r) = delete;
  HdrHeapSDKHandle &operator=(const HdrHeapSDKHandle &r) = delete;
};

inline void
HdrHeapSDKHandle::destroy()
{
  if (m_heap) {
    m_heap->destroy();
  }
  clear();
}

inline void
HdrHeapSDKHandle::clear()
{
  m_heap = nullptr;
}

inline void
HdrHeapSDKHandle::set(const HdrHeapSDKHandle *from)
{
  clear();
  m_heap = from->m_heap;
}

HdrStrHeap *new_HdrStrHeap(int requested_size);
inkcoreapi HdrHeap *new_HdrHeap(int size = HdrHeap::DEFAULT_SIZE);

void hdr_heap_test();

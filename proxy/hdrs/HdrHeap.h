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

#ifndef _HDR_HEAP_H_
#define _HDR_HEAP_H_

#include "Ptr.h"
#include "ink_port.h"
#include "ink_bool.h"
#include "ink_assert.h"
#include "Arena.h"
#include "HdrToken.h"
#include "SDKAllocator.h"

// Objects in the heap must currently be aligned to 8 byte boundaries,
// so their (address & HDR_PTR_ALIGNMENT_MASK) == 0

#define HDR_PTR_SIZE		(sizeof(uint64))
#define	HDR_PTR_ALIGNMENT_MASK	((HDR_PTR_SIZE) - 1L)

#define ROUND(x,l)  (((x) + ((l) - 1L)) & ~((l) - 1L))

// A many of the operations regarding read-only str
//  heaps are hand unrolled in the code.  Chaning
//  this value requires a full pass through HdrBuf.cc
//  to fix the unrolled operations
#define HDR_BUF_RONLY_HEAPS   3

// Changed these so they for sure fit one normal TCP packet full of headers.
#define HDR_HEAP_DEFAULT_SIZE   2048
#define HDR_STR_HEAP_DEFAULT_SIZE   2048

enum
{
  HDR_HEAP_OBJ_EMPTY = 0,
  HDR_HEAP_OBJ_RAW = 1,
  HDR_HEAP_OBJ_URL = 2,
  HDR_HEAP_OBJ_HTTP_HEADER = 3,
  HDR_HEAP_OBJ_MIME_HEADER = 4,
  HDR_HEAP_OBJ_FIELD_BLOCK = 5,
  HDR_HEAP_OBJ_FIELD_STANDALONE = 6,    // not a type that lives in HdrHeaps
  HDR_HEAP_OBJ_FIELD_SDK_HANDLE = 7,    // not a type that lives in HdrHeaps

  HDR_HEAP_OBJ_MAGIC = 0x0FEEB1E0
};

struct HdrHeapObjImpl
{
  uint32 m_type:8;
  uint32 m_length:20;
  uint32 m_obj_flags:4;
};

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

extern void obj_describe(HdrHeapObjImpl * obj, bool recurse);

inline int
obj_is_aligned(HdrHeapObjImpl * obj)
{
  return (((((uintptr_t) obj) & HDR_PTR_ALIGNMENT_MASK) == 0) && ((obj->m_length & HDR_PTR_ALIGNMENT_MASK) == 0));
}

inline void
obj_clear_data(HdrHeapObjImpl * obj)
{
  char *ptr = (char *) obj;
  int hdr_length = sizeof(HdrHeapObjImpl);
  memset(ptr + hdr_length, '\0', obj->m_length - hdr_length);
}

inline void
obj_copy_data(HdrHeapObjImpl * s_obj, HdrHeapObjImpl * d_obj)
{
  char *src, *dst;

  ink_debug_assert((s_obj->m_length == d_obj->m_length) && (s_obj->m_type == d_obj->m_type));

  int hdr_length = sizeof(HdrHeapObjImpl);
  src = (char *) s_obj + hdr_length;
  dst = (char *) d_obj + hdr_length;
  memcpy(dst, src, d_obj->m_length - hdr_length);
}

inline void
obj_copy(HdrHeapObjImpl * s_obj, char *d_addr)
{
  memcpy(d_addr, (char *) s_obj, s_obj->m_length);
}

inline void
obj_init_header(HdrHeapObjImpl * obj, uint32 type, uint32 nbytes, uint32 obj_flags)
{
  obj->m_type = type;
  obj->m_length = nbytes;
  obj->m_obj_flags = obj_flags;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

enum
{
  HDR_BUF_MAGIC_ALIVE = 0xabcdfeed,
  HDR_BUF_MAGIC_MARSHALED = 0xdcbafeed,
  HDR_BUF_MAGIC_DEAD = 0xabcddead,
  HDR_BUF_MAGIC_CORRUPT = 0xbadbadcc
};

struct StrHeapDesc
{
  StrHeapDesc();
  Ptr<RefCountObj> m_ref_count_ptr;
  char *m_heap_start;
  int32 m_heap_len;
  bool m_locked;
};


class IOBufferBlock;

class HdrStrHeap:public RefCountObj
{
public:

  virtual void free();

  char *allocate(int nbytes);
  char *expand(char *ptr, int old_size, int new_size);
  int space_avail();

  uint32 m_heap_size;
  char *m_free_start;
  uint32 m_free_size;
};

class CoreUtils;

class HdrHeap
{
  friend class CoreUtils;
public:
  void init();
  inkcoreapi void destroy();

  // PtrHeap allocation
  HdrHeapObjImpl *allocate_obj(int nbytes, int type);
  void deallocate_obj(HdrHeapObjImpl * obj);

  // StrHeap allocation
  char *allocate_str(int nbytes);
  char *expand_str(const char *old_str, int old_len, int new_len);
  char *duplicate_str(const char *str, int nbytes);
  void free_string(const char *s, int len);

  // Marshalling
  inkcoreapi int marshal_length();
  inkcoreapi int marshal(char *buf, int length);
  int unmarshal(int buf_length, int obj_type, HdrHeapObjImpl ** found_obj, RefCountObj * block_ref);

  void inherit_string_heaps(const HdrHeap * inherit_from);
  int attach_block(IOBufferBlock * b, const char *use_start);
  void set_ronly_str_heap_end(int slot, const char *end);

  // Lock read only str heaps so that can't be moved around
  //  by a heap consolidation.  Does NOT lock for Multi-Threaed
  //  access!
  void lock_ronly_str_heap(int i)
  {
    m_ronly_heap[i].m_locked = true;
  };
  void unlock_ronly_str_heap(int i)
  {
    m_ronly_heap[i].m_locked = false;
    // INKqa11238
    // Move slot i to the first available slot in m_ronly_heap[].
    // The move is necessary because the rest of the code assumes
    // heaps are always allocated in order.
    for (int j = 0; j < i; j++) {
      if (m_ronly_heap[j].m_heap_start == NULL) {
        // move slot i to slot j
        m_ronly_heap[j].m_ref_count_ptr = m_ronly_heap[i].m_ref_count_ptr;
        m_ronly_heap[j].m_heap_start = m_ronly_heap[i].m_heap_start;
        m_ronly_heap[j].m_heap_len = m_ronly_heap[i].m_heap_len;
        m_ronly_heap[j].m_locked = m_ronly_heap[i].m_locked;
        m_ronly_heap[i].m_ref_count_ptr = NULL;
        m_ronly_heap[i].m_heap_start = NULL;
        m_ronly_heap[i].m_heap_len = 0;
        m_ronly_heap[i].m_locked = false;
      }
    }
  };

  // Sanity Check Functions
  void sanity_check_strs();
  bool check_marshalled(uint32 buf_length);

  // Debugging functions
  void dump_heap(int len = -1);

  uint32 m_magic;
  char *m_free_start;
  char *m_data_start;
  uint32 m_size;

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
  uint32 m_free_size;

  int demote_rw_str_heap();
  void coalesce_str_heaps(int incoming_size = 0);
  void evacuate_from_str_heaps(HdrStrHeap * new_heap);
  int attach_str_heap(char *h_start, int h_len, RefCountObj * h_ref_obj, int *index);

  // String Heap access
  Ptr<HdrStrHeap> m_read_write_heap;
  StrHeapDesc m_ronly_heap[HDR_BUF_RONLY_HEAPS];
  int m_lost_string_space;

};

inline void
HdrHeap::free_string(const char *s, int len)
{
  if (s && len > 0) {
    m_lost_string_space += len;
  }
}


//
struct MarshalXlate
{
  char *start;
  char *end;
  char *offset;
};

struct HeapCheck
{
  char *start;
  char *end;
};


// Nasty macro to do string marshalling
#define HDR_MARSHAL_STR(ptr, table, nentries) \
if (ptr) { \
   int found = 0; \
   for (int i = 0; i < nentries; i++) { \
      if (ptr >= table[i].start && \
	  ptr <= table[i].end) { \
          ptr =  (((char*)ptr) - (uintptr_t) table[i].offset); \
          found = 1; \
          break; \
      } \
   } \
   ink_assert(found); \
   if (found == 0) { \
     return -1; \
   } \
}

// Nasty macro to do string marshalling
#define HDR_MARSHAL_STR_1(ptr, table) \
if (ptr) { \
   int found = 0; \
      if (ptr >= table[0].start && \
	  ptr <= table[0].end) { \
          ptr =  (((char*)ptr) - (uintptr_t) table[0].offset); \
          found = 1; \
      } \
   ink_assert(found); \
   if (found == 0) { \
     return -1; \
   } \
}



#define HDR_MARSHAL_PTR(ptr, type, table, nentries) \
if (ptr) { \
   int found = 0; \
   for (int i = 0; i < nentries; i++) { \
      if ((char*) ptr >= table[i].start && \
	  (char*) ptr <= table[i].end) { \
          ptr = (type *) (((char*)ptr) - (uintptr_t) table[i].offset); \
          found = 1; \
          break; \
      } \
   } \
   ink_assert(found); \
   if (found == 0) { \
    return -1; \
   } \
}

#define HDR_MARSHAL_PTR_1(ptr, type, table) \
if (ptr) { \
   int found = 0; \
      if ((char*) ptr >= table[0].start && \
	  (char*) ptr <= table[0].end) { \
          ptr = (type *) (((char*)ptr) - (uintptr_t) table[0].offset); \
          found = 1; \
      } \
   ink_assert(found); \
   if (found == 0) { \
    return -1; \
   } \
}




#define HDR_UNMARSHAL_STR(ptr, offset) \
if (ptr) { \
  ptr = ((char*)ptr) + offset; \
}

#define HDR_UNMARSHAL_PTR(ptr, type, offset) \
if (ptr) { \
  ptr = (type *) (((char*)ptr) + offset); \
}

// Nasty macro to do string evacuation.  Assumes
//   new heap = new_heap
#define HDR_MOVE_STR(str, len) \
{ \
   if (str) { \
     char* new_str = new_heap->allocate(len); \
     if(new_str) \
       memcpy(new_str, str, len); \
     str = new_str; \
   } \
}

// Nasty macro to do verify all strings it
//   in attached heaps
#define CHECK_STR(str, len, _heaps, _num_heaps) \
{ \
   if (str) { \
     int found = 0; \
     for (int i = 0; i < _num_heaps; i++) { \
        if (str >= _heaps[i].start && \
	    str + len <= heaps[i].end) { \
            found = 1; \
	} \
     } \
     ink_release_assert(found); \
   } \
}

// struct HdrHeapSDKHandle()
//
//   Handle to a HdrHeap.
//
//   Intended to be subclassed and contain a
//     object pointer that points into the heap
//
struct HdrHeapSDKHandle
{
public:
  //  For the SDK, we need to copy strings to add
  //    NULL termination, allocate standalone fields
  //    and allocate field handles, we need an allocator.
  //  To maintain compatibility with previous releases,
  //    everything allocated should go away when the
  //    hdr heap is deallocated so we need to keep
  //    a list of the live objects
  //  SDKAllocator accomplishes both these tasks
  SDKAllocator m_sdk_alloc;
  HdrHeap *m_heap;

public:
    HdrHeapSDKHandle();
   ~HdrHeapSDKHandle();

  // clear() only deallocates chained SDK return values
  //   The underlying MBuffer is left untouched
  void clear();

  // destroy() frees the underlying MBuffer and deallocates all chained
  //    SDK return values
  void destroy();

  void set(const HdrHeapSDKHandle * from);
  const char *make_sdk_string(const char *raw_str, int raw_str_len);

private:
  // In order to prevent gratitous refcounting,
  //  automatic C++ copies are disabled!
    HdrHeapSDKHandle(const HdrHeapSDKHandle & r);
    HdrHeapSDKHandle & operator =(const HdrHeapSDKHandle & r);
};


inline
HdrHeapSDKHandle::HdrHeapSDKHandle():
m_sdk_alloc(),
m_heap(NULL)
{
}

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
  m_heap = NULL;

  if (m_sdk_alloc.head) {
    m_sdk_alloc.free_all();
  }
}

inline
HdrHeapSDKHandle::~
HdrHeapSDKHandle()
{
  clear();
}

inline void
HdrHeapSDKHandle::set(const HdrHeapSDKHandle * from)
{
  clear();
  m_heap = from->m_heap;
}

inkcoreapi HdrHeap *new_HdrHeap(int size = HDR_HEAP_DEFAULT_SIZE);

void hdr_heap_test();
#endif

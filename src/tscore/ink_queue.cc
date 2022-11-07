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

/*****************************************************************************
  ink_queue.cc (This used to be ink_queue.c)

  This implements an atomic push/pop queue, and the freelist memory
  pools that are built from it.

  The change from ink_queue.cc to ink_queue.c resulted in some changes
  in access and store of 64 bit values. This is standardized by using
  the INK_QUEUE_LD64 macro which loads the version before the pointer
  (independent of endianness of native architecture) on 32 bit platforms
  or loads the 64 bit quantity directory on the DECs.


  ****************************************************************************/

#include <cstddef>
#include <utility>
#include "tscore/ink_queue.h"
#include "tscore/ink_assert.h"
#include "tscore/ink_align.h"
#include "tscore/hugepages.h"
#include "tscore/Diags.h"
#include "tscore/JeMiAllocator.h"

#define DEBUG_TAG "freelist"

/*
 * SANITY and DEADBEEF are compute-intensive memory debugging to
 * help in diagnosing freelist corruption.  We turn them off in
 * release builds.
 */

#ifdef DEBUG
#define SANITY
#define DEADBEEF
#endif

namespace
{
// Atomic stack, implemented as a linked list.  The link is stored at the offset (given by the base class
// member function link_offset() ) within each stack element.  The stack is empty when the head pointer is null.
//
template <class Base> class AtomicStack_ : private Base
{
public:
  template <typename... Args>
  AtomicStack_(ts::Atomic_versioned_ptr &head, Args &&... args) : Base(std::forward<Args>(args)...), _head(head)
  {
  }

  // Return reference to "next" pointer within a stack element.
  //
  void *&
  link(void *elem)
  {
    ink_assert(elem != nullptr);

    return *reinterpret_cast<void **>(static_cast<char *>(elem) + Base::link_offset());
  }

  // Splice this stack to the tail of another stack (whose head and tail are given by the parameters).
  // Returns previous head.
  //
  void *
  splice_to_top(void *other_head, void *other_tail)
  {
    ts::Versioned_ptr curr_head{_head.load()};

    do {
      link(other_tail) = curr_head.ptr();

    } while (!_head.compare_exchange_weak(curr_head, other_head));

    return curr_head.ptr();
  }

  // Returns previous head.
  //
  void *
  push(void *elem)
  {
    return splice_to_top(elem, elem);
  }

  template <bool All_elements = false>
  void *
  pop()
  {
    ts::Versioned_ptr curr_head{_head.load()};
    void *new_head;

    do {
      if (!curr_head.ptr()) {
        // Stack is empty.
        //
        return nullptr;
      }

      new_head = All_elements ? nullptr : link(curr_head.ptr());

    } while (!_head.compare_exchange_weak(curr_head, new_head));

    if (!All_elements) {
      link(curr_head.ptr()) = nullptr;
    }

    return curr_head.ptr();
  }

  // WARNING:  This is not safe, unless no other thread is popping or removing.
  //
  // If "item" is in list, it is removed from the list, and the function returns true.  The function returns
  // false if "item" is not in the list.  If "item" was in the list, it's link pointer is cleared.
  //
  bool
  remove(void *item)
  {
    ts::Versioned_ptr curr_head{item, _head.load().version()};

    if (_head.compare_exchange_strong(curr_head, link(item))) {
      // Item was at the top of the stack.

      link(item) = nullptr;

      return true;
    }

    void *p = curr_head.ptr();
    if (p) {
      void *p2 = link(p);

      while (p2 != item) {
        if (nullptr == p2) {
          // Item not in stack.
          //
          return false;
        }
        p  = p2;
        p2 = link(p2);
      }
      link(p) = link(p2);

      link(item) = nullptr;

      return true;
    }
    // Stack is empty.
    //
    return false;
  }

private:
  ts::Atomic_versioned_ptr &_head;
};

template <size_t FixedOffset> class AtomicStackBaseFixedOffset
{
protected:
  static constexpr size_t
  link_offset()
  {
    return FixedOffset;
  }
};

using AtomicStackNoOffset = AtomicStack_<AtomicStackBaseFixedOffset<0>>;

class AtomicStackBaseVariableOffset
{
protected:
  AtomicStackBaseVariableOffset(size_t offset) : _link_offset(offset) {}

  size_t
  link_offset() const
  {
    return _link_offset;
  }

private:
  size_t const _link_offset;
};

using AtomicStackVariableOffset = AtomicStack_<AtomicStackBaseVariableOffset>;

auto jma = je_mi_malloc::globalJeMiNodumpAllocator();
} // end anonymous namespace

struct ink_freelist_ops {
  void *(*fl_new)(InkFreeList *);
  void (*fl_free)(InkFreeList *, void *);
  void (*fl_bulkfree)(InkFreeList *, void *, void *, size_t);
};

using ink_freelist_list = struct _ink_freelist_list {
  InkFreeList *fl;
  struct _ink_freelist_list *next;
};

static void *freelist_new(InkFreeList *f);
static void freelist_free(InkFreeList *f, void *item);
static void freelist_bulkfree(InkFreeList *f, void *head, void *tail, size_t num_item);

static void *malloc_new(InkFreeList *f);
static void malloc_free(InkFreeList *f, void *item);
static void malloc_bulkfree(InkFreeList *f, void *head, void *tail, size_t num_item);

static const ink_freelist_ops malloc_ops   = {malloc_new, malloc_free, malloc_bulkfree};
static const ink_freelist_ops freelist_ops = {freelist_new, freelist_free, freelist_bulkfree};
static const ink_freelist_ops *default_ops = &freelist_ops;

static ink_freelist_list *freelists                = nullptr;
static const ink_freelist_ops *freelist_global_ops = default_ops;

const InkFreeListOps *
ink_freelist_malloc_ops()
{
  return &malloc_ops;
}

const InkFreeListOps *
ink_freelist_freelist_ops()
{
  return &freelist_ops;
}

void
ink_freelist_init_ops(int nofl_class, int nofl_proxy)
{
  // This *MUST* only be called at startup before any freelists allocate anything. We will certainly crash if object
  // allocated from the freelist are freed by malloc.
  ink_release_assert(freelist_global_ops == default_ops);

  freelist_global_ops = (nofl_class || nofl_proxy) ? ink_freelist_malloc_ops() : ink_freelist_freelist_ops();
}

void
ink_freelist_init(InkFreeList **fl, const char *name, uint32_t type_size, uint32_t chunk_size, uint32_t alignment)
{
  InkFreeList *f;
  ink_freelist_list *fll;

  /* its safe to add to this global list because ink_freelist_init()
     is only called from single-threaded initialization code. */
  f = static_cast<InkFreeList *>(ats_memalign(alignment, sizeof(InkFreeList)));
  ::new (f) InkFreeList;

  fll       = static_cast<ink_freelist_list *>(ats_malloc(sizeof(ink_freelist_list)));
  fll->fl   = f;
  fll->next = freelists;
  freelists = fll;

  f->name = name;
  /* quick test for power of 2 */
  ink_assert(!(alignment & (alignment - 1)));
  // It is never useful to have alignment requirement looser than a page size
  // so clip it. This makes the item alignment checks in the actual allocator simpler.
  f->alignment = alignment;
  if (f->alignment > ats_pagesize()) {
    f->alignment = ats_pagesize();
  }
  Debug(DEBUG_TAG "_init", "<%s> Alignment request/actual (%" PRIu32 "/%" PRIu32 ")", name, alignment, f->alignment);
  // Make sure we align *all* the objects in the allocation, not just the first one
  f->type_size = INK_ALIGN(type_size, f->alignment);
  Debug(DEBUG_TAG "_init", "<%s> Type Size request/actual (%" PRIu32 "/%" PRIu32 ")", name, type_size, f->type_size);
  if (ats_hugepage_enabled()) {
    f->chunk_size = INK_ALIGN(chunk_size * f->type_size, ats_hugepage_size()) / f->type_size;
  } else {
    f->chunk_size = INK_ALIGN(chunk_size * f->type_size, ats_pagesize()) / f->type_size;
  }
  Debug(DEBUG_TAG "_init", "<%s> Chunk Size request/actual (%" PRIu32 "/%" PRIu32 ")", name, chunk_size, f->chunk_size);

  *fl = f;
}

void
ink_freelist_madvise_init(InkFreeList **fl, const char *name, uint32_t type_size, uint32_t chunk_size, uint32_t alignment,
                          int advice)
{
  ink_freelist_init(fl, name, type_size, chunk_size, alignment);
  (*fl)->advice = advice;
}

InkFreeList *
ink_freelist_create(const char *name, uint32_t type_size, uint32_t chunk_size, uint32_t alignment)
{
  InkFreeList *f;

  ink_freelist_init(&f, name, type_size, chunk_size, alignment);
  return f;
}

//#ifdef SANITY
#if 0 // TEMP
int fake_global_for_ink_queue = 0;
#endif

void *
ink_freelist_new(InkFreeList *f)
{
  void *ptr;

  if (likely(ptr = freelist_global_ops->fl_new(f))) {
    ++(f->used);
  }

  return ptr;
}

static void *
freelist_new(InkFreeList *f)
{
  void *item;

  for (;;) {
    if (INK_ATOMICLIST_EMPTY(*f)) {
      uint32_t i;
      void *newp        = nullptr;
      size_t alloc_size = f->chunk_size * f->type_size;
      size_t alignment  = 0;

      if (ats_hugepage_enabled()) {
        alignment = ats_hugepage_size();
        newp      = ats_alloc_hugepage(alloc_size);
      }

      if (newp == nullptr) {
        alignment = ats_pagesize();
        newp      = ats_memalign(alignment, INK_ALIGN(alloc_size, alignment));
      }

      if (f->advice) {
        ats_madvise(static_cast<caddr_t>(newp), INK_ALIGN(alloc_size, alignment), f->advice);
      }

      f->allocated += f->chunk_size;

      /* free each of the new elements */
      for (i = 0; i < f->chunk_size; i++) {
        char *a = static_cast<char *>(newp) + i * f->type_size;
#ifdef DEADBEEF
        const char str[4] = {static_cast<char>(0xde), static_cast<char>(0xad), static_cast<char>(0xbe), static_cast<char>(0xef)};
        for (int j = 0; j < static_cast<int>(f->type_size); j++) {
          a[j] = str[j % 4];
        }
#endif
        freelist_free(f, a);
      }

    } else {
      item = AtomicStackNoOffset(f->head).pop<>();

      if (item) {
#ifdef SANITY
        void *head = f->head.load().ptr();
        if (head == item) {
          ink_abort("ink_freelist_new: loop detected");
        }
        if (reinterpret_cast<uintptr_t>(head) & 3) {
          ink_abort("ink_freelist_new: bad list");
        }
        if (head) {
          // fake_global_for_ink_queue = *static_cast<int *>(head);
          static_cast<void>(*const_cast<void *volatile *>(&head));
        }
#endif /* SANITY */
        break;
      }
    }
  }
  ink_assert(!(reinterpret_cast<uintptr_t>(item) & (static_cast<uintptr_t>(f->alignment) - 1)));

  return item;
}

static void *
malloc_new(InkFreeList *f)
{
  void *newp = nullptr;

  if (f->alignment) {
    newp = jma.allocate(f);
  } else {
    newp = ats_malloc(f->type_size);
  }

  return newp;
}

void
ink_freelist_free(InkFreeList *f, void *item)
{
  if (likely(item != nullptr)) {
    ink_assert(f->used != 0);
    freelist_global_ops->fl_free(f, item);
    --(f->used);
  }
}

static void
freelist_free(InkFreeList *f, void *item)
{
#ifdef DEADBEEF
  {
    static const char str[4] = {static_cast<char>(0xde), static_cast<char>(0xad), static_cast<char>(0xbe), static_cast<char>(0xef)};

    // set the entire item to DEADBEEF
    for (int j = 0; j < static_cast<int>(f->type_size); j++) {
      (static_cast<char *>(item))[j] = str[j % 4];
    }
  }
#endif /* DEADBEEF */

#ifdef SANITY
  void *head = f->head.load().ptr();
  if (head == item) {
    ink_abort("ink_freelist_free: trying to free item twice");
  }
  if (reinterpret_cast<uintptr_t>(head) & 3) {
    ink_abort("ink_freelist_free: bad list");
  }
  if (head) {
    // fake_global_for_ink_queue = *static_cast<int *>(head);
    static_cast<void>(*const_cast<void *volatile *>(&head));
  }
#endif /* SANITY */

  AtomicStackNoOffset(f->head).push(item);
}

static void
malloc_free(InkFreeList *f, void *item)
{
  if (f->alignment) {
    jma.deallocate(f, item);
  } else {
    ats_free(item);
  }
}

void
ink_freelist_free_bulk(InkFreeList *f, void *head, void *tail, size_t num_item)
{
  ink_assert(f->used >= num_item);

  freelist_global_ops->fl_bulkfree(f, head, tail, num_item);

  f->used -= num_item;
}

static void
freelist_bulkfree(InkFreeList *f, void *head, void *tail,
                  size_t
#ifdef DEADBEEF
                    num_item
#endif
)
{
#ifdef DEADBEEF
  {
    static const char str[4] = {static_cast<char>(0xde), static_cast<char>(0xad), static_cast<char>(0xbe), static_cast<char>(0xef)};

    // set the entire item to DEADBEEF;
    void *temp = head;
    for (size_t i = 0; i < num_item;) {
      for (int j = sizeof(void *); j < static_cast<int>(f->type_size); j++) {
        (static_cast<char *>(temp))[j] = str[j % 4];
      }
      ++i;
      ink_assert((i < num_item) || (temp == tail));
      temp = *static_cast<void **>(temp);
    }
  }
#endif /* DEADBEEF */

#ifdef SANITY
  void *old_head = f->head.load().ptr();
  if (old_head == head) {
    ink_abort("ink_freelist_free: trying to free item twice");
  }
  if (reinterpret_cast<uintptr_t>(old_head) & 3) {
    ink_abort("ink_freelist_free: bad list");
  }
  if (old_head) {
    // fake_global_for_ink_queue = *static_cast<int *>(old_head);
    static_cast<void>(*const_cast<void *volatile *>(&old_head));
  }
#endif /* SANITY */

  AtomicStackNoOffset(f->head).splice_to_top(head, tail);
}

static void
malloc_bulkfree(InkFreeList *f, void *head, void *tail, size_t num_item)
{
  void *item = head;
  void *next;

  // Avoid compiler warnings
  (void)f;
  (void)tail;

  for (size_t i = 0; i < num_item && item; ++i, item = next) {
    next = *static_cast<void **>(item); // find next item before freeing current item
    ats_free(item);
  }
}

void
ink_freelists_snap_baseline()
{
  ink_freelist_list *fll;
  fll = freelists;
  while (fll) {
    fll->fl->allocated_base = fll->fl->allocated;
    fll->fl->used_base      = fll->fl->used;
    fll                     = fll->next;
  }
}

void
ink_freelists_dump_baselinerel(FILE *f)
{
  ink_freelist_list *fll;
  if (f == nullptr) {
    f = stderr;
  }

  fprintf(f, "     allocated      |       in-use       |  count  | type size  |   free list name\n");
  fprintf(f, "  relative to base  |  relative to base  |         |            |                 \n");
  fprintf(f, "--------------------|--------------------|---------|------------|----------------------------------\n");

  fll = freelists;
  while (fll) {
    int a = fll->fl->allocated - fll->fl->allocated_base;
    if (a != 0) {
      fprintf(f, " %18" PRIu64 " | %18" PRIu64 " | %7u | %10u | memory/%s\n",
              static_cast<uint64_t>(fll->fl->allocated - fll->fl->allocated_base) * static_cast<uint64_t>(fll->fl->type_size),
              static_cast<uint64_t>(fll->fl->used - fll->fl->used_base) * static_cast<uint64_t>(fll->fl->type_size),
              fll->fl->used - fll->fl->used_base, fll->fl->type_size, fll->fl->name ? fll->fl->name : "<unknown>");
    }
    fll = fll->next;
  }
  fprintf(f, "-----------------------------------------------------------------------------------------\n");
}

void
ink_freelists_dump(FILE *f)
{
  ink_freelist_list *fll;
  if (f == nullptr) {
    f = stderr;
  }

  fprintf(f, "     Allocated      |        In-Use      | Type Size  |   Free List Name\n");
  fprintf(f, "--------------------|--------------------|------------|----------------------------------\n");

  uint64_t total_allocated = 0;
  uint64_t total_used      = 0;
  fll                      = freelists;
  while (fll) {
    fprintf(f, " %18" PRIu64 " | %18" PRIu64 " | %10u | memory/%s\n",
            static_cast<uint64_t>(fll->fl->allocated) * static_cast<uint64_t>(fll->fl->type_size),
            static_cast<uint64_t>(fll->fl->used) * static_cast<uint64_t>(fll->fl->type_size), fll->fl->type_size,
            fll->fl->name ? fll->fl->name : "<unknown>");
    total_allocated += static_cast<uint64_t>(fll->fl->allocated) * static_cast<uint64_t>(fll->fl->type_size);
    total_used += static_cast<uint64_t>(fll->fl->used) * static_cast<uint64_t>(fll->fl->type_size);
    fll = fll->next;
  }
  fprintf(f, " %18" PRIu64 " | %18" PRIu64 " |            | TOTAL\n", total_allocated, total_used);
  fprintf(f, "-----------------------------------------------------------------------------------------\n");
}

void *
ink_atomiclist_pop(InkAtomicList *l)
{
  return AtomicStackVariableOffset(l->head, l->offset).pop<>();
}

void *
ink_atomiclist_popall(InkAtomicList *l)
{
  return AtomicStackVariableOffset(l->head, l->offset).pop<true>();
}

void *
ink_atomiclist_push(InkAtomicList *l, void *item)
{
  return AtomicStackVariableOffset(l->head, l->offset).push(item);
}

void *
ink_atomiclist_next(InkAtomicList *l, void *item)
{
  return item ? AtomicStackVariableOffset(l->head, l->offset).link(item) : nullptr;
}

void *
ink_atomiclist_remove(InkAtomicList *l, void *item)
{
  return AtomicStackVariableOffset(l->head, l->offset).remove(item) ? item : nullptr;
}

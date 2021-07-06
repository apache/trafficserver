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

  ink_string++.cc

  C++ support for string manipulation.


 ****************************************************************************/

#include "tscore/ink_platform.h"
#include "tscore/ink_string++.h"
#include "tscore/ink_assert.h"
#include "tscore/ink_memory.h"
#include "tscore/ink_align.h"

/***********************************************************************
 *                                                                     *
 *       StrList (doubly-linked list of string/length list cells)      *
 *                                                                     *
 ***********************************************************************/

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
StrList::dump(FILE *fp)
{
  Str *str;

  for (str = head; str != nullptr; str = str->next) {
    str->dump(fp);
  }
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

Str *
StrList::_new_cell(const char *s, int len_not_counting_nul)
{
  Str *cell;
  char *p;
  int l = len_not_counting_nul;

  // allocate a cell from the array or heap
  if (cells_allocated < STRLIST_BASE_CELLS) {
    cell = &(base_cells[cells_allocated]);
  } else {
    p = static_cast<char *>(alloc(sizeof(Str) + 7));
    if (p == nullptr) {
      return (nullptr); // FIX: scale heap
    }
    p    = (char *)((((uintptr_t)p) + 7) & ~7); // round up to multiple of 8
    cell = reinterpret_cast<Str *>(p);
  }
  ++cells_allocated;

  // are we supposed to copy the string?
  if (copy_when_adding_string) {
    char *buf = static_cast<char *>(alloc(l + 1));
    if (buf == nullptr) {
      return (nullptr); // FIX: need to grow heap!
    }
    memcpy(buf, s, l);
    buf[l] = '\0';

    cell->str = (const char *)buf;
  } else {
    cell->str = s;
  }

  cell->len = l;

  return (cell);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void *
StrList::overflow_heap_alloc(int size)
{
  if (!overflow_first) {
    overflow_first = overflow_current = StrListOverflow::create_heap(STRLIST_OVERFLOW_HEAP_SIZE);
  }

  return overflow_current->alloc(size, &overflow_current);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
StrList::overflow_heap_clean()
{
  if (overflow_first) {
    overflow_first->clean();
  }
}

#define INIT_OVERFLOW_ALIGNMENT 8
// XXX: This is basically INK_ALIGN_DEFAULT
const int overflow_head_hdr_size = INK_ALIGN(sizeof(StrListOverflow), INIT_OVERFLOW_ALIGNMENT);

void
StrListOverflow::init()
{
  next      = nullptr;
  heap_size = 0;
  heap_used = 0;
}

void
StrListOverflow::clean()
{
  StrListOverflow *current_free = this;
  StrListOverflow *next_free;

  while (current_free) {
    next_free = current_free->next;
    ats_free(current_free);
    current_free = next_free;
  }
}

void *
StrListOverflow::alloc(int size, StrListOverflow **new_heap_ptr)
{
  if (size > (heap_size - heap_used)) {
    int new_heap_size = heap_size * 2;

    if (new_heap_size < size) {
      new_heap_size = INK_ALIGN(size, 2048);
      ink_release_assert(new_heap_size >= size);
    }

    ink_assert(next == nullptr);
    *new_heap_ptr = next = create_heap(new_heap_size);
    return next->alloc(size, new_heap_ptr);
  }

  char *start = (reinterpret_cast<char *>(this)) + overflow_head_hdr_size;
  char *rval  = start + heap_used;
  heap_used += size;
  ink_assert(heap_used <= heap_size);
  return (void *)rval;
}

StrListOverflow *
StrListOverflow::create_heap(int user_size)
{
  // I'm aligning the first allocation since the old implementation
  //  used to do this by calling ats_malloc.  I assume it doesn't
  //  matter since we are talking about strings but since this is a
  //  last minute emergency bug fix, I'm not take any changes.  If
  //  allocations are not of aligned values then subsequents allocations
  //  aren't aligned, again mirroring the previous implementation
  int total_size = overflow_head_hdr_size + user_size;

  StrListOverflow *o = static_cast<StrListOverflow *>(ats_malloc(total_size));
  o->init();
  o->heap_size = user_size;

  return o;
}

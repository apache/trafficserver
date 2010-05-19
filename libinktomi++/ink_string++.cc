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

#include "inktomi++.h"
#include "ink_unused.h"      /* MAGIC_EDITING_TAG */

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void *
ink_memchr(const void *as, int ac, size_t an)
{
  unsigned char c = (unsigned char) ac;
  unsigned char *s = (unsigned char *) as;

  // initial segment

  int i_len = ((unsigned long) 8 - (unsigned long) as) & 7;

  // too short to concern us

  if ((int) an < i_len) {
    for (int i = 0; i < (int) an; i++)
      if (s[i] == c)
        return &s[i];
    return 0;
  }
  // bytes 0-3

  // Is this right?  I would think this should be (4 - i_len&3)...
  switch (i_len & 3) {
  case 3:
    if (*s++ == c)
      return s - 1;
  case 2:
    if (*s++ == c)
      return s - 1;
  case 1:
    if (*s++ == c)
      return s - 1;
  case 0:
    break;
  }

  // bytes 4-8

  unsigned int ib = c;
  ib |= (ib << 8);
  ib |= (ib << 16);
  unsigned int im = 0x7efefeff;
  if (i_len & 4) {
    unsigned int ibp = *(unsigned int *) s;
    unsigned int ibb = ibp ^ ib;
    ibb = ((ibb + im) ^ ~ibb) & ~im;
    if (ibb) {
      if (s[0] == c)
        return &s[0];
      if (s[1] == c)
        return &s[1];
      if (s[2] == c)
        return &s[2];
      if (s[3] == c)
        return &s[3];
    }
    s += 4;
  }
  // next 8x bytes
  uint64 m = 0x7efefefefefefeffLL;
  uint64 b = ((uint64) ib);
  b |= (b << 32);
  uint64 *p = (uint64 *) s;
  unsigned int n = (unsigned int) ((((unsigned int) an) - (s - (unsigned char *) as)) >> 3);
  uint64 *end = p + n;
  while (p < end) {
    uint64 bp = *p;
    uint64 bb = bp ^ b;
    bb = ((bb + m) ^ ~bb) & ~m;
    if (bb) {
      s = (unsigned char *) p;
      if (s[0] == c)
        return &s[0];
      if (s[1] == c)
        return &s[1];
      if (s[2] == c)
        return &s[2];
      if (s[3] == c)
        return &s[3];
      if (s[4] == c)
        return &s[4];
      if (s[5] == c)
        return &s[5];
      if (s[6] == c)
        return &s[6];
      if (s[7] == c)
        return &s[7];
    }
    p++;
  }

  // terminal segement

  i_len = (int) (an - (((unsigned char *) p) - ((unsigned char *) as)));
  s = (unsigned char *) p;

  // n-(4..8)..n bytes

  if (i_len & 4) {
    unsigned int ibp = *(unsigned int *) s;
    unsigned int ibb = ibp ^ ib;
    ibb = ((ibb + im) ^ ~ibb) & ~im;
    if (ibb) {
      if (s[0] == c)
        return &s[0];
      if (s[1] == c)
        return &s[1];
      if (s[2] == c)
        return &s[2];
      if (s[3] == c)
        return &s[3];
    }
    s += 4;
  }
  // n-(0..3)..n bytes

  switch (i_len & 3) {
  case 3:
    if (*s++ == c)
      return s - 1;
  case 2:
    if (*s++ == c)
      return s - 1;
  case 1:
    if (*s++ == c)
      return s - 1;
  case 0:
    break;
  }
  return 0;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

char *
ink_memcpy_until_char(char *dst, char *src, unsigned int n, unsigned char c)
{
  unsigned int i = 0;
  for (; ((i < n) && (((unsigned char) src[i]) != c)); i++)
    dst[i] = src[i];
  return &src[i];
}

/***********************************************************************
 *                                                                     *
 *       StrList (doubly-linked list of string/length list cells)      *
 *                                                                     *
 ***********************************************************************/

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
StrList::dump(FILE * fp)
{
  Str *str;

  for (str = head; str != NULL; str = str->next)
    str->dump(fp);
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
    p = (char *) alloc(sizeof(Str) + 7);
    if (p == NULL)
      return (NULL);            // FIX: scale heap
    p = (char *) (((long) (p + 7)) & ~7);       // round up to multiple of 8
    cell = (Str *) p;
  }
  ++cells_allocated;

  // are we supposed to copy the string?
  if (copy_when_adding_string) {
    char *buf = (char *) alloc(l + 1);
    if (buf == NULL)
      return (NULL);            // FIX: need to grow heap!
    memcpy(buf, s, l);
    buf[l] = '\0';

    cell->str = (const char *) buf;
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
  if (overflow_first)
    overflow_first->clean();
}


#define INIT_OVERFLOW_ALIGNMENT      8
#define ROUND(x,l)  (((x) + ((l) - 1L)) & ~((l) - 1L))

const int overflow_head_hdr_size = ROUND(sizeof(StrListOverflow), INIT_OVERFLOW_ALIGNMENT);

void
StrListOverflow::init()
{
  next = NULL;
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
    xfree(current_free);
    current_free = next_free;
  }
}

void *
StrListOverflow::alloc(int size, StrListOverflow ** new_heap_ptr)
{

  if (size > (heap_size - heap_used)) {
    int new_heap_size = heap_size * 2;

    if (new_heap_size < size) {
      new_heap_size = ROUND(size, 2048);
      ink_release_assert(new_heap_size >= size);
    }

    ink_assert(next == NULL);
    *new_heap_ptr = next = create_heap(new_heap_size);
    return next->alloc(size, new_heap_ptr);
  }

  char *start = ((char *) this) + overflow_head_hdr_size;
  char *rval = start + heap_used;
  heap_used += size;
  ink_assert(heap_used <= heap_size);
  return (void *) rval;
}

StrListOverflow *
StrListOverflow::create_heap(int user_size)
{
  // I'm aligning the first allocation since the old implementation
  //  used to do this by calling xmalloc.  I assume it doesn't
  //  matter since we are talking about strings but since this is a
  //  last minute emergency bug fix, I'm not take any changes.  If
  //  allocations are not of aligned values then subsequents allocations
  //  aren't aligned, again mirroring the previous implemnetation
  int total_size = overflow_head_hdr_size + user_size;

  StrListOverflow *o = (StrListOverflow *) xmalloc(total_size);
  o->init();
  o->heap_size = user_size;

  return o;
}

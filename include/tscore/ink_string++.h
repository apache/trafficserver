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

  ink_string++.h

  C++ support for string manipulation.


 ****************************************************************************/

#pragma once
#include <cstdio>
#include <strings.h>

/***********************************************************************
 *                                                                     *
 *                     Str (string/length list cell)                   *
 *                                                                     *
 ***********************************************************************/

struct Str {
  const char *str  = nullptr; // string pointer
  size_t len       = 0;       // length of string (not counting NUL)
  struct Str *next = nullptr; // next in list
  struct Str *prev = nullptr; // prev in list

  Str() {}
  Str(char *s)
  {
    str  = s;
    len  = strlen(s);
    next = nullptr;
    prev = nullptr;
  }
  Str(char *s, int l)
  {
    str  = s;
    len  = l;
    next = nullptr;
    prev = nullptr;
  }

  void
  clean()
  {
    str  = nullptr;
    len  = 0;
    next = nullptr;
    prev = nullptr;
  }

  void
  dump(FILE *fp = stderr)
  {
    fprintf(fp, "Str [\"%.*s\", len %d]\n", (int)len, str, (int)len);
  }
};

/***********************************************************************
 *                                                                     *
 *       StrList (doubly-linked list of string/length list cells)      *
 *                                                                     *
 ***********************************************************************/

#define STRLIST_BASE_HEAP_SIZE 128
#define STRLIST_OVERFLOW_HEAP_SIZE 1024
#define STRLIST_BASE_CELLS 5

struct StrListOverflow;

struct StrList {
public:
  int count;
  Str *head;
  Str *tail;

public:
  StrList(bool do_copy_when_adding_string = true);
  ~StrList();

  Str *get_idx(int i);
  void append(Str *str);
  void prepend(Str *str);
  void add_after(Str *prev, Str *str);
  void detach(Str *str);

  Str *new_cell(const char *s, int len_not_counting_nul);
  Str *append_string(const char *s, int len_not_counting_nul);

  void dump(FILE *fp = stderr);

private:
  void init();
  void clean();

  void *base_heap_alloc(int size);
  void *alloc(int size);
  Str *_new_cell(const char *s, int len_not_counting_nul);
  void *overflow_heap_alloc(int size);
  void overflow_heap_clean();

  Str base_cells[STRLIST_BASE_CELLS];
  char base_heap[STRLIST_BASE_HEAP_SIZE];
  int cells_allocated;
  int base_heap_size;
  int base_heap_used;
  StrListOverflow *overflow_current;
  StrListOverflow *overflow_first;
  bool copy_when_adding_string;
};

struct StrListOverflow {
  StrListOverflow *next;
  int heap_size;
  int heap_used;

  void init();
  void clean();
  void *alloc(int size, StrListOverflow **new_heap_ptr);
  static StrListOverflow *create_heap(int user_size);
};

inline void
StrList::init()
{
  count           = 0;
  cells_allocated = 0;
  head = tail      = nullptr;
  base_heap_size   = STRLIST_BASE_HEAP_SIZE;
  base_heap_used   = 0;
  overflow_first   = nullptr;
  overflow_current = nullptr;
}

inline void
StrList::clean()
{
  if (overflow_first) {
    overflow_heap_clean();
  }
  init();
}

inline StrList::StrList(bool do_copy_when_adding_string)
{
  memset(base_heap, 0, sizeof(base_heap));
  copy_when_adding_string = do_copy_when_adding_string;
  init();
}

inline StrList::~StrList()
{
  clean();
}

inline void *
StrList::base_heap_alloc(int size)
{
  char *p;

  if (size <= (base_heap_size - base_heap_used)) {
    p = &(base_heap[base_heap_used]);
    base_heap_used += size;
    return ((void *)p);
  } else {
    return (nullptr);
  }
}

inline void *
StrList::alloc(int size)
{
  void *p = base_heap_alloc(size);
  if (p == nullptr) {
    p = overflow_heap_alloc(size);
  }
  return (p);
}

inline Str *
StrList::new_cell(const char *s, int len_not_counting_nul)
{
  Str *cell;
  int l = len_not_counting_nul;

  // allocate a cell from the array or heap
  if ((cells_allocated < STRLIST_BASE_CELLS) && (!copy_when_adding_string)) {
    cell      = &(base_cells[cells_allocated++]);
    cell->str = s;
    cell->len = l;
    return (cell);
  } else {
    return (_new_cell(s, len_not_counting_nul));
  }
}

inline Str *
StrList::get_idx(int i)
{
  Str *s;

  for (s = head; ((s != nullptr) && i); s = s->next, i--) {
    ;
  }
  return ((i == 0) ? s : nullptr);
}

inline void
StrList::append(Str *str)
{
  // do nothing if str is nullptr to avoid pointer chasing below
  if (str == nullptr) {
    return;
  }
  ++count;
  str->next = nullptr;
  str->prev = tail;

  if (tail == nullptr) {
    head = tail = str;
  } else {
    tail->next = str;
    tail       = str;
  }
}

inline void
StrList::prepend(Str *str)
{
  if (str == nullptr) {
    return;
  }
  ++count;
  str->next = head;
  str->prev = nullptr;

  if (tail == nullptr) {
    head = tail = str;
  } else {
    head->prev = str;
    head       = str;
  }
}

inline void
StrList::add_after(Str *prev, Str *str)
{
  if (str == nullptr || prev == nullptr) {
    return;
  }
  ++count;
  str->next  = prev->next;
  str->prev  = prev;
  prev->next = str;
  if (tail == prev) {
    tail = str;
  }
}

inline void
StrList::detach(Str *str)
{
  if (str == nullptr) {
    return;
  }
  --count;

  if (head == str) {
    head = str->next;
  }
  if (tail == str) {
    tail = str->prev;
  }
  if (str->prev) {
    str->prev->next = str->next;
  }
  if (str->next) {
    str->next->prev = str->prev;
  }
}

inline Str *
StrList::append_string(const char *s, int len_not_counting_nul)
{
  Str *cell;

  cell = new_cell(s, len_not_counting_nul);
  append(cell);
  return (cell);
}

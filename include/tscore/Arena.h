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

#pragma once

#include <sys/types.h>
#include <memory.h>
#include "tscore/ink_assert.h"

struct ArenaBlock {
  ArenaBlock *next;
  char *m_heap_end;
  char *m_water_level;
  char data[8];
};

class Arena
{
public:
  Arena() : m_blocks(nullptr) {}
  ~Arena() { reset(); }
  inkcoreapi void *alloc(size_t size, size_t alignment = sizeof(double));
  void free(void *mem, size_t size);
  size_t str_length(const char *str);
  char *str_alloc(size_t len);
  void str_free(char *str);
  char *str_store(const char *str, size_t len);

  inkcoreapi void reset();

private:
  ArenaBlock *m_blocks;
};

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline size_t
Arena::str_length(const char *str)
{
  unsigned char *s, *e;
  size_t len;

  e = (unsigned char *)str;
  s = e - 1;

  while (*s >= 128) {
    s -= 1;
  }

  len = *s++;
  while (s != e) {
    len = (len * 128) + (255 - *s++);
  }

  return len;
}

/*-------------------------------------------------------------------------
  layout = [length][data]

    length 1                   = [1]
    length 127                 = [127]
    length 128                 = [1][255]
    length 128 + 1             = [1][254]
    length 128 + 2             = [1][253]
    length 128 + 127           = [1][128]
    length 128 + 128           = [2][255]
    length 128 * 128           = [1][255][255]
    length 128 * 128 + 1       = [1][255][254]
    length 128 * 128 + 2       = [1][255][253]
    length 128 * 128 + 127     = [1][255][128]
    length 128 * 128 + 128     = [1][254][255]
  -------------------------------------------------------------------------*/

inline char *
Arena::str_alloc(size_t len)
{
  unsigned char *mem, *p;
  size_t size;
  size_t tmp;

  size = len + 1 + 1;
  tmp  = len;

  while (tmp >= 128) {
    size += 1;
    tmp /= 128;
  }

  mem = (unsigned char *)alloc(size, 1);

  mem += (size - len - 1);
  p   = mem - 1;
  tmp = len;

  while (tmp >= 128) {
    *p-- = (unsigned char)(255 - (tmp % 128));
    tmp /= 128;
  }
  *p = (unsigned char)tmp;

  return (char *)mem;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline void
Arena::str_free(char *str)
{
  unsigned char *p, *s, *e;
  size_t len;

  e = (unsigned char *)str;
  s = e - 1;

  while (*s >= 128) {
    s -= 1;
  }

  p = s;

  len = *s++;
  while (s != e) {
    len = (len * 128) + (255 - *s++);
  }

  len += (e - p) + 1;
  free(p, len);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline char *
Arena::str_store(const char *str, size_t len)
{
  char *mem;

  mem = str_alloc(len);
  memcpy(mem, str, len);
  mem[len] = '\0';

  return mem;
}

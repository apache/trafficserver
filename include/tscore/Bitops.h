/** @file

  Utility functions for efficient bit operations

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

#include <strings.h>

/**
  Find First (bit) Set. Index starts at 1.

  @return zero for zero arg.

*/
static inline int
ink_ffs(int n)
{
  return ffs(n);
}

/**
  Returns the index of the first bit (least significant bit), set "1"
  for each char in the range (start end).

  @param start pointer to the first character.
  @param end pointer to the location after the last character.
  @param p (if not null) returns the location of the first char that
    has a bit set.
  @return index of the first bit set in the first character that has a
    bit turned on. Zero if all bits are zero.

*/
static inline int
bitops_first_set(unsigned char *start, unsigned char *end, unsigned char **p)
{
  extern unsigned char bit_table[];

  int idx;

  idx = 0;
  while (start != end) {
    idx = bit_table[*start];
    if (idx) {
      break;
    }
    start += 1;
  }

  if (p) {
    *p = start;
  }

  return idx;
}

/**
  Returns the index of the first bit (least significant bit), unset "0"
  for each char in the range (start end).

  @param start pointer to the first character.
  @param end pointer to the location after the last character.
  @param p (if not null) returns the location of the first char that
    has a bit unset.
  @return index of the first bit set in the first character that has a
    bit turned off. Zero if all bits are 1.

*/
static inline int
bitops_first_unset(unsigned char *start, unsigned char *end, unsigned char **p)
{
  extern unsigned char bit_table[];

  int idx;

  idx = 0;
  while (start != end) {
    idx = bit_table[~(*start)];
    if (idx) {
      break;
    }
    start += 1;
  }

  if (p) {
    *p = start;
  }

  return idx;
}

/**
  Returns the index of the first bit (least significant bit), set "1"
  for each char in the range (start end).

  @param start pointer to the first character.
  @param end pointer to the location after the last character.
  @param offset
  @return index of the first bit set in the first character that has a
    bit turned on. Zero if all bits are zero.

*/
static inline int
bitops_next_set(unsigned char *start, unsigned char *end, int offset)
{
  extern unsigned char bit_table[];

  unsigned char *p;
  unsigned char c;
  size_t idx;
  int t;

  idx = 0;
  p   = start + offset / 8;
  t   = (offset % 8) + 1;

  while (p != end) {
    idx = bit_table[*p];
    if (idx) {
      c = *p;
      while (idx && (idx <= (size_t)t)) {
        c &= ~(1 << (idx - 1));
        idx = bit_table[c];
      }

      if (idx) {
        break;
      }
    }
    p += 1;
    t = 0;
  }

  if (idx) {
    idx -= 1;
    idx += (p - start) * 8;
  } else {
    idx = (size_t)-1;
  }

  return (int)idx;
}

static inline int
bitops_next_unset(unsigned char *start, unsigned char *end, int offset)
{
  extern unsigned char bit_table[];

  unsigned char *p;
  unsigned char c;
  size_t idx;
  int t;

  idx = 0;
  p   = start + offset / 8;
  t   = (offset % 8) + 1;

  while (p != end) {
    c   = ~(*p);
    idx = bit_table[c];
    if (idx) {
      while (idx && (idx <= (size_t)t)) {
        c &= ~(1 << (idx - 1));
        idx = bit_table[c];
      }

      if (idx) {
        break;
      }
    }
    p += 1;
    t = 0;
  }

  if (idx) {
    idx -= 1;
    idx += (p - start) * 8;
  } else {
    idx = (size_t)-1;
  }

  return (int)idx;
}

static inline int
bitops_count(unsigned char *start, unsigned char *end)
{
  extern unsigned char bit_count_table[];

  int count;

  count = 0;
  while (start != end) {
    count += bit_count_table[*start++];
  }

  return count;
}

static inline void
bitops_union(unsigned char *s1, unsigned char *s2, int len)
{
  int i;

  if (!s1 || !s2) {
    return;
  }

  for (i = 0; i < len; i++) {
    s1[i] |= s2[i];
  }
}

static inline unsigned char
bitops_set(unsigned char val, int bit)
{
  return (val | (1 << bit));
}

static inline void
bitops_set(unsigned char *val, int bit)
{
  int pos = bit >> 3;
  int idx = bit & 0x7;
  val[pos] |= (1 << idx);
}

static inline unsigned char
bitops_unset(unsigned char val, int bit)
{
  return (val & ~(1 << bit));
}

static inline void
bitops_unset(unsigned char *val, int bit)
{
  int pos = bit >> 3;
  int idx = bit & 0x7;
  val[pos] &= ~(1 << idx);
}

static inline int
bitops_isset(unsigned char val, int bit)
{
  return ((val & (1 << bit)) != 0);
}

static inline int
bitops_isset(unsigned char *val, int bit)
{
  int pos = bit / 8;
  int idx = bit % 8;
  return ((val[pos] & (1 << idx)) != 0);
}

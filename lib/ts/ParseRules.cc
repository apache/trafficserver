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

/*************************** -*- Mod: C++ -*- ******************************

   ParseRules.h --


 ****************************************************************************/

#include "libts.h"      /* MAGIC_EDITING_TAG */

const unsigned int parseRulesCType[256] = {
#include "ParseRulesCType"
};
const char parseRulesCTypeToUpper[256] = {
#include "ParseRulesCTypeToUpper"
};
const char parseRulesCTypeToLower[256] = {
#include "ParseRulesCTypeToLower"
};

unsigned char *
ParseRules::scan_while(unsigned char *ptr, unsigned int n, uint32_t bitmask)
{
  unsigned int i;
  uint32_t *wptr;
  unsigned char *align_ptr;
  uintptr_t f_bytes, b_bytes, words, align_off;

  align_off = ((uintptr_t) ptr & 3);
  align_ptr = (unsigned char *) (((uintptr_t) ptr) & ~3);

  f_bytes = (align_off ? 4 - align_off : 0);

  words = (n - f_bytes) >> 2;

  if (words == 0) {
    for (i = 0; i < n; i++)
      if (!is_type(ptr[i], bitmask))
        return (&ptr[i]);
  } else {
    wptr = ((uint32_t *) align_ptr) + (align_off ? 1 : 0);
    switch (align_off) {
    case 1:
      if (!is_type(align_ptr[1], bitmask))
        return (&ptr[1]);
    case 2:
      if (!is_type(align_ptr[2], bitmask))
        return (&ptr[2]);
    case 3:
      if (!is_type(align_ptr[3], bitmask))
        return (&ptr[3]);
      break;
    default:
      break;
    }

    b_bytes = n - ((words << 2) + f_bytes);

    for (i = 0; i < words; i++) {
      uint32_t word = wptr[i];
      uint32_t result = (is_type(((word >> 0) & 0xFF), bitmask) &
                       is_type(((word >> 8) & 0xFF), bitmask) &
                       is_type(((word >> 16) & 0xFF), bitmask) & is_type(((word >> 24) & 0xFF), bitmask));
      if (result == 0) {
        unsigned char *cptr = (unsigned char *) &(wptr[i]);
        if (!is_type(cptr[0], bitmask))
          return (&cptr[0]);
        if (!is_type(cptr[1], bitmask))
          return (&cptr[1]);
        if (!is_type(cptr[2], bitmask))
          return (&cptr[2]);
        return (&cptr[3]);
      }
    }

    align_ptr = (unsigned char *) &(wptr[words]);

    switch (b_bytes) {
    case 1:
      if (!is_type(align_ptr[0], bitmask))
        return (&align_ptr[0]);
      break;
    case 2:
      if (!is_type(align_ptr[0], bitmask))
        return (&align_ptr[0]);
      if (!is_type(align_ptr[1], bitmask))
        return (&align_ptr[1]);
      break;
    case 3:
      if (!is_type(align_ptr[0], bitmask))
        return (&align_ptr[0]);
      if (!is_type(align_ptr[1], bitmask))
        return (&align_ptr[1]);
      if (!is_type(align_ptr[2], bitmask))
        return (&align_ptr[2]);
      break;
    default:
      break;
    }
  }
  return 0;
}


void
ParseRules::ink_tolower_buffer(char *ptr, unsigned int n)
{
  unsigned int i;

  if (n < 8) {
    for (i = 0; i < n; i++)
      ptr[i] = ParseRules::ink_tolower(ptr[i]);
  } else {
    uintptr_t fpad = 4 - ((uintptr_t) ptr & 3);
    uintptr_t words = (n - fpad) >> 2;
    uintptr_t bpad = n - fpad - (words << 2);

    switch (fpad) {
    case 3:
      *ptr = ParseRules::ink_tolower(*ptr);
      ++ptr;
    case 2:
      *ptr = ParseRules::ink_tolower(*ptr);
      ++ptr;
    case 1:
      *ptr = ParseRules::ink_tolower(*ptr);
      ++ptr;
    default:
      break;
    }

    uint32_t *wptr = (uint32_t *) ptr;
    for (i = 0; i < words; i++) {
      uint32_t word = *wptr;
      ((unsigned char *) &word)[0] = ParseRules::ink_tolower(((unsigned char *) &word)[0]);
      ((unsigned char *) &word)[1] = ParseRules::ink_tolower(((unsigned char *) &word)[1]);
      ((unsigned char *) &word)[2] = ParseRules::ink_tolower(((unsigned char *) &word)[2]);
      ((unsigned char *) &word)[3] = ParseRules::ink_tolower(((unsigned char *) &word)[3]);
      *wptr++ = word;
    }

    switch (bpad) {
    case 3:
      *ptr = ParseRules::ink_tolower(*ptr);
      ++ptr;
    case 2:
      *ptr = ParseRules::ink_tolower(*ptr);
      ++ptr;
    case 1:
      *ptr = ParseRules::ink_tolower(*ptr);
      ++ptr;
    default:
      break;
    }
  }
}

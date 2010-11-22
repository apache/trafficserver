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

  ink_memory.c

  Memory allocation routines for libinktomi.a.

 ****************************************************************************/

#include "inktomi++.h"

#include <assert.h>
#if defined(linux)
// XXX: SHouldn't that be part of CPPFLAGS?
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif
#endif
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif


void *
ink_malloc(size_t size)
{
  void *ptr = NULL;

  /*
   * There's some nasty code in libinktomi that expects
   * a MALLOC of a zero-sized item to wotk properly. Rather
   * than allocate any space, we simply return a NULL to make
   * certain they die quickly & don't trash things.
   */

  // Useful for tracing bad mallocs
  //ink_stack_trace_dump();
  if (likely(size > 0)) {
    if (unlikely((ptr = malloc(size)) == NULL)) {
      xdump();
      ink_fatal(1, "ink_malloc: couldn't allocate %d bytes", size);
    }
  }
  return (ptr);
}                               /* End ink_malloc */


void *
ink_calloc(size_t nelem, size_t elsize)
{
  void *ptr = calloc(nelem, elsize);
  if (unlikely(ptr == NULL)) {
    xdump();
    ink_fatal(1, "ink_calloc: couldn't allocate %d %d byte elements", nelem, elsize);
  }
  return (ptr);
}                               /* End ink_calloc */


void *
ink_realloc(void *ptr, size_t size)
{
  void *newptr = realloc(ptr, size);
  if (unlikely(newptr == NULL)) {
    xdump();
    ink_fatal(1, "ink_realloc: couldn't reallocate %d bytes", size);
  }
  return (newptr);
}                               /* End ink_realloc */


void
ink_memalign_free(void *ptr)
{
  if (likely(ptr)) {
    ink_free(ptr);
  }
}


void *
ink_memalign(size_t alignment, size_t size)
{
#ifndef NO_MEMALIGN

  void *ptr;

#if TS_HAS_POSIX_MEMALIGN
  if (alignment <= 8)
    return ink_malloc(size);

  int retcode = posix_memalign(&ptr, alignment, size);
  if (unlikely(retcode)) {
    if (retcode == EINVAL) {
      ink_fatal(1, "ink_memalign: couldn't allocate %d bytes at alignment %d - invalid alignment parameter",
                (int) size, (int) alignment);
    } else if (retcode == ENOMEM) {
      ink_fatal(1, "ink_memalign: couldn't allocate %d bytes at alignment %d - insufficient memory",
                (int) size, (int) alignment);
    } else {
      ink_fatal(1, "ink_memalign: couldn't allocate %d bytes at alignment %d - unknown error %d",
                (int) size, (int) alignment, retcode);
    }
  }
#else
  ptr = memalign(alignment, size);
  if (unlikely(ptr == NULL)) {
    ink_fatal(1, "ink_memalign: couldn't allocate %d bytes at alignment %d", (int) size, (int) alignment);
  }
#endif
  return (ptr);
#else
#if defined(freebsd) || defined(darwin)
  /*
   * DEC malloc calims to align for "any allocatable type",
   * and the following code checks that.
   */
  switch (alignment) {
  case 1:
  case 2:
  case 4:
  case 8:
  case 16:
    return malloc(size);
  case 32:
  case 64:
  case 128:
  case 256:
  case 512:
  case 1024:
  case 2048:
  case 4096:
  case 8192:
    return valloc(size);
  default:
    abort();
    break;
  }
# else
#       error "Need a memalign"
# endif
#endif /* #ifndef NO_MEMALIGN */
  return NULL;
}                               /* End ink_memalign */

void
ink_free(void *ptr)
{
  if (likely(ptr != NULL))
    free(ptr);
  else
    ink_warning("ink_free: freeing a NULL pointer");
}                               /* End ink_free */


/* this routine has been renamed --- this stub is for portability & will disappear */

char *
ink_duplicate_string(char *ptr)
{
  ink_assert(!"don't use this slow code!");
  return (ink_string_duplicate(ptr));
}                               /* End ink_duplicate_string */

void *
ink_memcpy(void *s1, const void *s2, int n)
{
  register int i;
  register char *s, *d;

  s = (char *) s2;
  d = (char *) s1;

  if (n <= 8) {
    switch (n) {
    case 0:
      break;
    case 1:
      d[0] = s[0];
      break;
    case 2:
      d[0] = s[0];
      d[1] = s[1];
      break;
    case 3:
      d[0] = s[0];
      d[1] = s[1];
      d[2] = s[2];
      break;
    case 4:
      d[0] = s[0];
      d[1] = s[1];
      d[2] = s[2];
      d[3] = s[3];
      break;
    case 5:
      d[0] = s[0];
      d[1] = s[1];
      d[2] = s[2];
      d[3] = s[3];
      d[4] = s[4];
      break;
    case 6:
      d[0] = s[0];
      d[1] = s[1];
      d[2] = s[2];
      d[3] = s[3];
      d[4] = s[4];
      d[5] = s[5];
      break;
    case 7:
      d[0] = s[0];
      d[1] = s[1];
      d[2] = s[2];
      d[3] = s[3];
      d[4] = s[4];
      d[5] = s[5];
      d[6] = s[6];
      break;
    case 8:
      d[0] = s[0];
      d[1] = s[1];
      d[2] = s[2];
      d[3] = s[3];
      d[4] = s[4];
      d[5] = s[5];
      d[6] = s[6];
      d[7] = s[7];
      break;
    default:
      ink_assert(0);
    }
  } else if (n < 128) {
    for (i = 0; i + 7 < n; i += 8) {
      d[i + 0] = s[i + 0];
      d[i + 1] = s[i + 1];
      d[i + 2] = s[i + 2];
      d[i + 3] = s[i + 3];
      d[i + 4] = s[i + 4];
      d[i + 5] = s[i + 5];
      d[i + 6] = s[i + 6];
      d[i + 7] = s[i + 7];
    }
    for (i = i; i < n; i++)
      d[i] = s[i];
  } else {
    memcpy(s1, s2, n);
  }

  return (s1);
}                               /* End ink_memcpy */

void
ink_bcopy(void *s1, void *s2, size_t n)
{
  ink_assert(!"don't use this slow code!");
  ink_memcpy(s2, s1, n);
}                               /* End ink_bcopy */

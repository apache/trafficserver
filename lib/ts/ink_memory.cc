/** @file

  Memory allocation routines for libts

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
#include "libts.h"

#include <assert.h>
#if defined(linux)
// XXX: SHouldn't that be part of CPPFLAGS?
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif
#endif
#include <stdlib.h>
#include <string.h>

void *
ats_malloc(size_t size)
{
  void *ptr = NULL;

  /*
   * There's some nasty code in libts that expects
   * a MALLOC of a zero-sized item to work properly. Rather
   * than allocate any space, we simply return a NULL to make
   * certain they die quickly & don't trash things.
   */

  // Useful for tracing bad mallocs
  // ink_stack_trace_dump();
  if (likely(size > 0)) {
    if (unlikely((ptr = malloc(size)) == NULL)) {
      xdump();
      ink_fatal(1, "ats_malloc: couldn't allocate %zu bytes", size);
    }
  }
  return ptr;
}                               /* End ats_malloc */

void *
ats_calloc(size_t nelem, size_t elsize)
{
  void *ptr = calloc(nelem, elsize);
  if (unlikely(ptr == NULL)) {
    xdump();
    ink_fatal(1, "ats_calloc: couldn't allocate %zu %zu byte elements", nelem, elsize);
  }
  return ptr;
}                               /* End ats_calloc */

void *
ats_realloc(void *ptr, size_t size)
{
  void *newptr = realloc(ptr, size);
  if (unlikely(newptr == NULL)) {
    xdump();
    ink_fatal(1, "ats_realloc: couldn't reallocate %zu bytes", size);
  }
  return newptr;
}                               /* End ats_realloc */

// TODO: For Win32 platforms, we need to figure out what to do with memalign.
// The older code had ifdef's around such calls, turning them into ats_malloc().
void *
ats_memalign(size_t alignment, size_t size)
{
  void *ptr;

#if TS_HAS_POSIX_MEMALIGN || TS_HAS_JEMALLOC
  if (alignment <= 8)
    return ats_malloc(size);

#if defined(openbsd)
  if (alignment > PAGE_SIZE)
      alignment = PAGE_SIZE;
#endif

  int retcode = posix_memalign(&ptr, alignment, size);

  if (unlikely(retcode)) {
    if (retcode == EINVAL) {
      ink_fatal(1, "ats_memalign: couldn't allocate %zu bytes at alignment %zu - invalid alignment parameter",
                size, alignment);
    } else if (retcode == ENOMEM) {
      ink_fatal(1, "ats_memalign: couldn't allocate %zu bytes at alignment %zu - insufficient memory",
                size, alignment);
    } else {
      ink_fatal(1, "ats_memalign: couldn't allocate %zu bytes at alignment %zu - unknown error %d",
                size, alignment, retcode);
    }
  }
#else
  ptr = memalign(alignment, size);
  if (unlikely(ptr == NULL)) {
    ink_fatal(1, "ats_memalign: couldn't allocate %zu bytes at alignment %zu",  size,  alignment);
  }
#endif
  return ptr;
}                               /* End ats_memalign */

void
ats_free(void *ptr)
{
  if (likely(ptr != NULL))
    free(ptr);
}                               /* End ats_free */

void*
ats_free_null(void *ptr)
{
  if (likely(ptr != NULL))
    free(ptr);
  return NULL;
}                               /* End ats_free_null */

void
ats_memalign_free(void *ptr)
{
  if (likely(ptr))
    free(ptr);
}

// This effectively makes mallopt() a no-op (currently) when tcmalloc
// or jemalloc is used. This might break our usage for increasing the
// number of mmap areas (ToDo: Do we still really need that??).
//
// TODO: I think we might be able to get rid of this?
int
ats_mallopt(int param, int value)
{
#if TS_HAS_JEMALLOC
// TODO: jemalloc code ?
#else
#if TS_HAS_TCMALLOC
// TODO: tcmalloc code ?
#else
#if defined(linux)
  return mallopt(param, value);
#endif // ! defined(linux)
#endif // ! TS_HAS_TCMALLOC
#endif // ! TS_HAS_JEMALLOC
  return 0;
}

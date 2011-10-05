/** @file

  Memory allocation routines for libts.

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
#ifndef _ink_memory_h_
#define	_ink_memory_h_

#include <ctype.h>
#include <strings.h>

#include "ink_config.h"

#if TS_HAS_JEMALLOC
#include <jemalloc/jemalloc.h>
/* TODO: Should this have a value ? */
#define ATS_MMAP_MAX 0
#else
#if HAVE_MALLOC_H
#include <malloc.h>
#define ATS_MMAP_MAX M_MMAP_MAX
#endif // ! HAVE_MALLOC_H
#endif // ! TS_HAS_JEMALLOC

#ifdef __cplusplus
extern "C"
{
#endif                          /* __cplusplus */
  void *ats_malloc(size_t size);
  void *ats_calloc(size_t nelem, size_t elsize);
  void *ats_realloc(void *ptr, size_t size);
  void *ats_memalign(size_t alignment, size_t size);
  void ats_free(void *ptr);
  void* ats_free_null(void *ptr);
  void ats_memalign_free(void *ptr);
  int ats_mallopt(int param, int value);

#define ats_strdup(p)        _xstrdup((p), -1, NULL)
#define ats_strndup(p,n)     _xstrdup((p), n, NULL)

#ifdef __cplusplus
}

template < typename T >
T& ink_zero(T& t) {
  memset(&t, 0, sizeof(t));
  return t;
}

#endif                          /* __cplusplus */

#endif

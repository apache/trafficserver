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

  ink_memory.h

  Memory allocation routines for libinktomi.a.

 ****************************************************************************/

#ifndef _ink_memory_h_
#define	_ink_memory_h_

#include <ctype.h>
#include <strings.h>

#ifdef __cplusplus
extern "C"
{
#endif                          /* __cplusplus */

  typedef struct
  {
    void *ptr;
    unsigned int length;
  } InkMemoryBlock;

#define ink_type_malloc(type)       (type *)ink_malloc(sizeof(type));
#define ink_type_malloc_n(n,type)   (type *)ink_malloc((n) * sizeof(type));
#define ink_type_calloc(n,type)     (type *)ink_calloc((n),sizeof(type));

  bool ink_memalign_heap_init(long long ram_cache_size);
  void *ink_malloc(size_t size);
  void *ink_calloc(size_t nelem, size_t elsize);
  void *ink_realloc(void *ptr, size_t size);
  void *ink_memalign(size_t alignment, size_t size);
  void ink_free(void *ptr);
  void ink_memalign_free(void *ptr);
  char *ink_duplicate_string(char *ptr);        /* obsoleted by ink_string_duplicate --- don't use */
  void ink_memzero(void *src_arg, int nbytes);
  void *ink_memcpy(void *s1, void *s2, int n);
  void ink_bcopy(void *s1, void *s2, size_t n);

#ifdef __cplusplus
}
#endif                          /* __cplusplus */

#endif

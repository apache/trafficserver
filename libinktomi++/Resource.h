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

#ifndef __RESOURCE_H__
#define __RESOURCE_H__


#include <stdlib.h>
#include "ink_resource.h"

#ifdef TRACK_MEMORY

#define NEW(mem)        _xtrack_helper (mem, RES_MEM_PATH)


#if defined(__alpha)
extern void *operator
new(size_t size)
throw();
extern void operator
delete(void *p)
throw();
#elif defined(__SUNPRO_CC) && (__SUNPRO_CC >= 0x500)
void *operator
new(size_t)
throw(std::bad_alloc);
void operator
delete(void *)
throw();
#else
extern void *operator  new(size_t size);
extern void operator  delete(void *p);
#endif


#ifndef __SUNPRO_CC

#ifdef __alpha
extern void *operator  new[] (size_t size) throw();
extern void operator  delete[] (void *p) throw();
#elif !defined (_WIN64)
extern void *operator  new[] (size_t size);
extern void operator  delete[] (void *p);
#endif

#endif /* __SUNPRO_CC */


template<class T> static inline T *
_xtrack_helper(T * ptr, const char *path)
{
  return (T *) _xtrack(ptr, path);
}

#elif defined(USE_PARTITION_MEMORY)     /* VxWorks */

#define NEW(mem)        mem

inkcoreapi extern void *operator  new(size_t size);
inkcoreapi extern void operator  delete(void *p);
inkcoreapi extern void *operator  new[] (size_t size);
inkcoreapi extern void operator  delete[] (void *p);

#else /* USE_PARTITION_MEMORY */

#define NEW(mem)  mem

#endif /* USE_PARTITION_MEMORY */


#endif /* __RESOURCE_H__ */

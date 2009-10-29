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

#ifndef __INK_RESOURCE_H__
#define __INK_RESOURCE_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ink_apidefs.h"
#include "ink_port.h"
#include "ink_memory.h"

extern volatile ink64 resource_allocated_mem;
extern volatile int res_track_memory;   /* set this to zero to disable resource tracking */

#define __RES_PATH(x)   #x
#define _RES_PATH(x)    __RES_PATH (x)
#define RES_PATH(x)     x __FILE__ ":" _RES_PATH (__LINE__)
#define RES_MEM_PATH    RES_PATH ("memory/")
#define RES_DESC_PATH   RES_PATH ("descriptor/")

struct Resource
{
  void *magic;
  struct Resource *next;
  const char *path;
  ink64 value;
  ink64 snapshot;
  ink64 baseline;
};

//#define TRACK_MEMORY

#if defined(TRACK_MEMORY)
#define RES_TRACK_MEMORY_DEFAULT 1      /* default value for res_track_memory variable */

#define xfree(p)       _xfree(p)
#define xfree_null(p)  _xfree_null(p)
#define xmalloc(s)     _xmalloc((s), RES_MEM_PATH)
#define xrealloc(p,s)  _xrealloc((p), (s), RES_MEM_PATH)
#define xstrdup(p)     _xstrdup((p), -1, RES_MEM_PATH)
#define xstrndup(p,n)  _xstrdup((p), (n), RES_MEM_PATH)
#define xtrack(p)      _xtrack((p), RES_MEM_PATH)
#define xvalidate(p)   _xvalidate((p),__FILE__,__LINE__)

void _xvalidate(void *ptr, char *file, int line);
void _xfree(void *ptr);
void *_xfree_null(void *ptr);
void *_xmalloc(unsigned int size, const char *path);
void *_xrealloc(void *ptr, unsigned int size, const char *path);
char *_xstrdup(const char *str, int length, const char *path);
void *_xtrack(void *ptr, const char *path);
void xdump_snap_baseline();
void xdump_to_file_baseline_rel(FILE * fp);
void xdump_to_file(FILE * fp);
void xdump(void);
void xsnap(void);

#else /* #if defined(TRACK_MEMORY) */
#define RES_TRACK_MEMORY_DEFAULT 0      /* default value for res_track_memory variable */

#ifdef __cplusplus
static inline void
xfree(void *mem)
{
  if (mem)
    ink_free(mem);
}
static inline void *
xfree_null(void *mem)
{
  if (mem)
    ink_free(mem);
  return NULL;
}

//#else
//#define xfree(_mem)  if(_mem){ink_free((_mem));}
//#define xfree_null(_mem)  if(_mem){ink_free((_mem));_mem = NULL;}
#endif

#define xmalloc(s)        ink_malloc ((s))
#define xrealloc(p,s)     ink_realloc ((p),(s))
#define xstrdup(p)        _xstrdup ((p), -1, NULL)
#define xstrndup(p,n)     _xstrdup ((p), n, NULL)
#define xtrack(p)         p
#define xdump_snap_baseline() do { } while (0)
#define xdump_to_file_baseline_rel(f) do { } while (0)
#define xdump_to_file(f)  do { } while (0)
#define xsnap()           do { } while (0)
#define xvalidate(p) do {} while (0)

void *_xmalloc(unsigned int size, const char *path);
void *_xrealloc(void *ptr, unsigned int size, const char *path);
char *_xstrdup(const char *str, int length, const char *path);
void _xfree(void *ptr);
void *_xfree_null(void *ptr);

void xdump(void);


#endif /* TRACK_MEMORY */

#endif /* __INK_RESOURCE_H__ */

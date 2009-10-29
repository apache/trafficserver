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

#include <assert.h>
#if (HOST_OS == linux)
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif
#endif
#include <stdlib.h>
#include <string.h>

#if (HOST_OS != freebsd)
#include <malloc.h>
#endif
#include "inktomi++.h"     /* MAGIC_EDITING_TAG */


class MAMemChunk
{
public:
  MAMemChunk * next;
  void *ptr;
};

class MAHeap
{
private:
  pthread_mutex_t mutex;
  char *heap;
  char *heap_end;
  int size;
  int chunk_size;
  int total_chunks;
  MAMemChunk *chunk_list_free;
  MAMemChunk *chunk_list_used;
public:
    MAHeap(int chunk_size = 0, int total_chunks = 0);
   ~MAHeap();
  bool Init(int chunk_size = 0, int total_chunks = 0);
  void *Get(void);
  bool Free(void *ptr);
};

bool
MAHeap::Init(int _chunk_size, int _total_chunks)
{
  bool retcode = true;
  chunk_size = _chunk_size;
  total_chunks = _total_chunks;
  size = chunk_size * total_chunks;

  if (size > 0) {
    if (!posix_memalign((void **) &heap, 8192, size)) {
      for (int i = 0; i < total_chunks; i++) {
        MAMemChunk *mc = new MAMemChunk();
        mc->next = chunk_list_free;
        chunk_list_free = mc;
        mc->ptr = (void *) &heap[chunk_size * i];
      }
      heap_end = heap + size;
    } else
      retcode = false;
  }
  return retcode;
}

MAHeap::MAHeap(int _chunk_size, int _total_chunks)
{
  chunk_list_free = (chunk_list_used = 0);
  heap_end = (heap = NULL);
  pthread_mutex_init(&mutex, NULL);
  Init(_chunk_size, _total_chunks);
}

MAHeap::~MAHeap()
{
  pthread_mutex_lock(&mutex);
  // free the heap
  if (heap != NULL) {
    free(heap);
  }
  // delete the free list
  MAMemChunk *head = chunk_list_free;
  MAMemChunk *next = NULL;
  while (head != NULL) {
    next = head->next;
    delete head;
    head = next;
  }
  // delete the used list
  head = chunk_list_used;
  next = NULL;
  while (head != NULL) {
    next = head->next;
    delete head;
    head = next;
  }
  pthread_mutex_unlock(&mutex);
}

void *
MAHeap::Get(void)
{
  MAMemChunk *mc;
  void *ptr = 0;
  if (heap) {
    pthread_mutex_lock(&mutex);
    if ((mc = chunk_list_free) != 0) {
      chunk_list_free = mc->next;
      mc->next = chunk_list_used;
      chunk_list_used = mc;
      ptr = mc->ptr;
    }
    pthread_mutex_unlock(&mutex);
  }
  return ptr;
}

// I know that it is not optimal but we are not going to free aligned memory at all
// I wrote it - just in case ...
bool
MAHeap::Free(void *ptr)
{
  bool retcode = false;
  if (ptr && heap && ptr >= heap && ptr < heap_end) {
    MAMemChunk *mc, **mcc;
    retcode = true;
    pthread_mutex_lock(&mutex);
    for (mcc = &chunk_list_used; (mc = *mcc) != 0; mcc = &(mc->next)) {
      if (mc->ptr == ptr) {
        *mcc = mc->next;
        mc->next = chunk_list_free;
        chunk_list_free = mc;
        break;
      }
    }
    pthread_mutex_unlock(&mutex);
  }
  return retcode;
}

static MAHeap *maheap_1m = new MAHeap();
static MAHeap *maheap_512k = new MAHeap();
static MAHeap *maheap_256k = new MAHeap();

bool
ink_memalign_heap_init(long long ram_cache_size)
{
  bool retcode = true;
  int _total_chunks = (int) (ram_cache_size / (1024 * 1024));

  if (_total_chunks > 1024)
    _total_chunks = 1024;

  if (likely(maheap_1m)) {
    retcode = maheap_1m->Init(1024 * 1024, _total_chunks) ? retcode : false;
  }
  if (likely(maheap_512k)) {
    retcode = maheap_512k->Init(512 * 1024, _total_chunks) ? retcode : false;
  }
  if (likely(maheap_256k)) {
    retcode = maheap_256k->Init(256 * 1024, _total_chunks) ? retcode : false;
  }
  return retcode;
}

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
    if (maheap_1m && maheap_1m->Free(ptr))
      return;
    if (maheap_512k && maheap_512k->Free(ptr))
      return;
    if (maheap_256k && maheap_256k->Free(ptr))
      return;
    ink_free(ptr);
  }
}

void *
ink_memalign(size_t alignment, size_t size)
{
#ifndef NO_MEMALIGN

  void *ptr;

#if (HOST_OS == linux)
  if (alignment <= 16)
    return ink_malloc(size);

  if (size == (1024 * 1024)) {
    if (maheap_1m && (ptr = maheap_1m->Get()) != 0)
      return ptr;
  } else if (size == (1024 * 512)) {
    if (maheap_512k && (ptr = maheap_512k->Get()) != 0)
      return ptr;
  } else if (size == (1024 * 256)) {
    if (maheap_256k && (ptr = maheap_256k->Get()) != 0)
      return ptr;
  }

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
#if (HOST_OS == freebsd)
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


void
ink_memzero(void *src_arg, int nbytes)
{
  ink_assert(!"don't use this slow code!");

  char *src = (char *) src_arg;

  ink_assert(nbytes > 0);

  if (nbytes <= 20) {
    switch (nbytes) {
    case 1:
      src[0] = '\0';
      break;
    case 2:
      src[0] = src[1] = '\0';
      break;
    case 3:
      src[0] = src[1] = src[2] = '\0';
      break;
    case 4:
      src[0] = src[1] = src[2] = src[3] = '\0';
      break;
    case 5:
      src[0] = src[1] = src[2] = src[3] = src[4] = '\0';
      break;
    case 6:
      src[0] = src[1] = src[2] = src[3] = src[4] = src[5] = '\0';
      break;
    case 7:
      src[0] = src[1] = src[2] = src[3] = src[4] = src[5] = src[6] = '\0';
      break;
    case 8:
      src[0] = src[1] = src[2] = src[3] = src[4] = src[5] = src[6] = src[7] = '\0';
      break;
    case 9:
      src[0] = src[1] = src[2] = src[3] = src[4] = src[5] = src[6] = src[7] = src[8] = '\0';
      break;
    case 10:
      src[0] = src[1] = src[2] = src[3] = src[4] = src[5] = src[6] = src[7] = src[8] = src[9] = '\0';
      break;
    case 11:
      src[0] = src[1] = src[2] = src[3] = src[4] = src[5] = src[6] = src[7] = src[8] = src[9] = src[10] = '\0';
      break;
    case 12:
      src[0] = src[1] = src[2] = src[3] = src[4] = src[5] = src[6] = src[7] = src[8] = src[9] =
        src[10] = src[11] = '\0';
      break;
    case 13:
      src[0] = src[1] = src[2] = src[3] = src[4] = src[5] = src[6] = src[7] = src[8] = src[9] =
        src[10] = src[11] = src[12] = '\0';
      break;
    case 14:
      src[0] = src[1] = src[2] = src[3] = src[4] = src[5] = src[6] = src[7] = src[8] = src[9] =
        src[10] = src[11] = src[12] = src[13] = '\0';
      break;
    case 15:
      src[0] = src[1] = src[2] = src[3] = src[4] = src[5] = src[6] = src[7] = src[8] = src[9] =
        src[10] = src[11] = src[12] = src[13] = src[14] = '\0';
      break;
    case 16:
      src[0] = src[1] = src[2] = src[3] = src[4] = src[5] = src[6] = src[7] = src[8] = src[9] =
        src[10] = src[11] = src[12] = src[13] = src[14] = src[15] = '\0';
      break;
    case 17:
      src[0] = src[1] = src[2] = src[3] = src[4] = src[5] = src[6] = src[7] = src[8] = src[9] =
        src[10] = src[11] = src[12] = src[13] = src[14] = src[15] = src[16] = '\0';
      break;
    case 18:
      src[0] = src[1] = src[2] = src[3] = src[4] = src[5] = src[6] = src[7] = src[8] = src[9] =
        src[10] = src[11] = src[12] = src[13] = src[14] = src[15] = src[16] = src[17] = '\0';
      break;
    case 19:
      src[0] = src[1] = src[2] = src[3] = src[4] = src[5] = src[6] = src[7] = src[8] = src[9] =
        src[10] = src[11] = src[12] = src[13] = src[14] = src[15] = src[16] = src[17] = src[18] = '\0';
      break;
    case 20:
      src[0] = src[1] = src[2] = src[3] = src[4] = src[5] = src[6] = src[7] = src[8] = src[9] =
        src[10] = src[11] = src[12] = src[13] = src[14] = src[15] = src[16] = src[17] = src[18] = src[19] = '\0';
      break;
    default:
      break;
    }
  } else if (nbytes <= 1000) {
    int i;
    for (i = 0; i < nbytes; i++) {
      src[i] = '\0';
    }
  } else {
    memset(src, '\0', nbytes);
  }
  return;
}


void *
ink_memcpy(void *s1, void *s2, int n)
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

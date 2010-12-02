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

#include "libts.h"

#include "ink_assert.h"
#include "ink_atomic.h"
#include "ink_port.h"
#include "ink_resource.h"
#include "ink_stack_trace.h"

volatile int64 resource_allocated_mem = 0;
volatile int res_track_memory = RES_TRACK_MEMORY_DEFAULT;

#ifdef TRACK_MEMORY

#define FENCE_POST_SIZE   16

// TODO: Move this to ink_align.h
#define ADJUST(mem,x)  (((char*) (mem)) + x)
// TODO: Use INK_ALIGN instead
#define ROUND(x,l)     (((x) + ((l) - 1L)) & ~((l) - 1L))
#define TSIZE          16387

#define MAKE_MAGIC(m)    (((char*) (m)) - 1)
#define CHECK_MAGIC(m,c) ((char*) (m) == MAKE_MAGIC (c))


typedef struct ResMemInfo ResMemInfo;
typedef struct Resource Resource;

struct ResMemInfo
{
  void *magic;
  unsigned int size:31;
  unsigned int fence_post:1;
  Resource *res;
};


static unsigned int res_hash(const char *s);
Resource *res_lookup(const char *path);


static const int res_extra_space = ROUND(sizeof(ResMemInfo), sizeof(double));
static volatile Resource *res_table[TSIZE];

static const char fence_post_pattern[FENCE_POST_SIZE] = {
  (char) 0xde, (char) 0xad, (char) 0xbe, (char) 0xef,
  (char) 0xde, (char) 0xad, (char) 0xbe, (char) 0xef,
  (char) 0xde, (char) 0xad, (char) 0xbe, (char) 0xef,
  (char) 0xde, (char) 0xad, (char) 0xbe, (char) 0xef,
};


volatile int res_zorch_mem = 0;
volatile int res_fence_post = 0;

#define res_memadd(_x_) \
   ink_atomic_increment64(&resource_allocated_mem, (int64) (_x_));

#define res_memsub(_x_) \
   ink_atomic_increment64(&resource_allocated_mem, (int64) -(_x_));

static int
_xres_init()
{
  res_zorch_mem = (getenv("ZORCH_MEM") != NULL);
  res_fence_post = (getenv("FENCE_POST") != NULL);

  if (res_zorch_mem) {
    fprintf(stderr, "memory zorching enabled\n");
  }

  if (res_fence_post) {
    fprintf(stderr, "memory fence posting enabled\n");
  }

  return 0;
}

static int dummy_var = _xres_init();


/* INKqa03012 - Digital OSF seems to issue some bad frees in
 *  the iostream destructor.  These variables and the the exit_cb()
 *  let us know if exit has been called.  If exit has been, called
 *  we will refrain from issuing warnings regarding bad frees
 */
static int exit_called = 0;
static void
exit_cb()
{
  exit_called = 1;
}
static int unused = atexit(exit_cb);


/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

#define res_inc(res,delta) \
   ink_atomic_increment64 (&res->value, delta); \
   res_memadd(delta)

#define res_check(res) \
    if (res && !CHECK_MAGIC ((res)->magic, (res))) { \
        fprintf (stderr, "FATAL: resource table is corrupt [%d]\n", __LINE__); \
        abort (); \
    }

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/


unsigned int
res_hash(const char *s)
{
#if !defined(linux) && !defined(freebsd) && !defined(__i386__)
#define HASH_ONE(h,one)       ((h << 3) + (one) + (h >> 29))
#define WORD_HAS_NULLBYTE(w)  ((((w) - 0x01010101) ^ (w)) & 0x80808080)

  unsigned int h;
  unsigned int ibp;

  ink_assert(!((uintptr_t) s & 3));

  for (h = 0;;) {
    ibp = *(unsigned int *) s;

    if (WORD_HAS_NULLBYTE(ibp)) {
      unsigned char t[4];

      if (!s[0]) {
        return h;
      } else if (!s[1]) {
        *(unsigned int *) &t[0] = ibp;
        t[2] = 0;
        t[3] = 0;
        h = HASH_ONE(h, *(unsigned int *) &t[0]);
        return h;
      } else if (!s[2]) {
        *(unsigned int *) &t[0] = ibp;
        t[3] = 0;
        h = HASH_ONE(h, *(unsigned int *) &t[0]);
        return h;
      } else if (!s[3]) {
        h = HASH_ONE(h, ibp);
        return h;
      }
    } else {
      h = HASH_ONE(h, ibp);
    }
    s += 4;
  }

#undef HASH_ONE
#undef WORD_HAS_NULLBYTE
#else
  unsigned int h = 0, g;

  for (; *s; s++) {
    h = (h << 4) + *s;
    if ((g = h & 0xf0000000))
      h = (h ^ (g >> 24)) ^ g;
  }
  return h;
#endif
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

Resource *
res_lookup(const char *path)
{
  unsigned int hash_val;
  Resource *node;
  Resource *old;

  hash_val = res_hash(path) % TSIZE;

  for (;;) {
    old = (Resource *) res_table[hash_val];
    node = old;

    while (node) {
      res_check(node);
      if ((path == node->path) || (strcmp(path, node->path) == 0)) {
        return node;
      }
      node = node->next;
    }

    if (old == res_table[hash_val]) {
      node = (Resource *) _xmalloc(sizeof(Resource), NULL);
      node->magic = MAKE_MAGIC(node);
      node->path = path;
      node->value = 0;
      node->snapshot = 0;
      node->baseline = 0;
      node->next = old;

      if (ink_atomic_cas_ptr((pvvoidp) & res_table[hash_val], old, node))
        return node;

      _xfree(node);
      node = NULL;
    }
  }
  return NULL;                  // DEC compiler complains
}


/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

static Resource *
res_stat(const char *path, int64 value)
{
  if (path) {
    Resource *res;

    res = res_lookup(path);
    res_inc(res, value);

    return res;
  } else {
    return NULL;
  }
}


#if defined(linux) || defined(freebsd)
static const int magic_array_offset = 0;
#else
#error "I do not know about this platform."
#endif


/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
_xcheck_fence_post(char *mem, unsigned int size)
{
  if (memcmp(mem, fence_post_pattern, FENCE_POST_SIZE) != 0) {
    return 1;
  }
  if (memcmp(mem + size - FENCE_POST_SIZE, fence_post_pattern, FENCE_POST_SIZE) != 0) {
    return 1;
  }
  return 0;
}

void
_xvalidate(void *ptr, char *file, int line)
{
  ResMemInfo *info;

  info = (ResMemInfo *) ADJUST(ptr, -res_extra_space);
  if (!CHECK_MAGIC(info->magic, info)) {
    info = (ResMemInfo *) ADJUST(info, -magic_array_offset);
  }
  if (!CHECK_MAGIC(info->magic, info)) {
    ink_debug_assert(!"bad pointer");
  } else {
    if (info->res) {
      res_check(info->res);
    }
    char *mem = (char *) info;

    if (info->fence_post) {
      mem -= FENCE_POST_SIZE;
      if (_xcheck_fence_post(mem, info->size + res_extra_space + FENCE_POST_SIZE * 2)) {
        fprintf(stderr, "MEMORY: free: fence-post mangled [%s]\n", info->res ? info->res->path : "<unknown>");
        abort();
      }
    }
  }
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
_xfree(void *ptr)
{
  if (!ptr) {
    // fprintf (stderr, "WARNING: freeing NULL pointer\n");
  } else {
    ResMemInfo *info;

    info = (ResMemInfo *) ADJUST(ptr, -res_extra_space);
    if (!CHECK_MAGIC(info->magic, info)) {
      info = (ResMemInfo *) ADJUST(info, -magic_array_offset);
    }

    if (CHECK_MAGIC(info->magic, info)) {
      char *mem;

      if (info->res) {
        res_check(info->res);
        res_inc(info->res, -((int64) info->size));
      }

      mem = (char *) info;

      if (info->fence_post) {
        mem -= FENCE_POST_SIZE;
        if (_xcheck_fence_post(mem, info->size + res_extra_space + FENCE_POST_SIZE * 2)) {
          fprintf(stderr, "MEMORY: free: fence-post mangled [%s]\n", info->res ? info->res->path : "<unknown>");
          abort();
        }
      }

      if (res_zorch_mem) {
        memset(info, 0x81, info->size + res_extra_space);
      }

      memset(info, 0, res_extra_space);

      free(mem);

    } else {
      /* This is a bad free. Let it leak.  Issue a
       *  warning if we are not in an exit routine
       * (INKqa03012)
       */
      if (exit_called == 0) {
        fprintf(stderr, "WARNING: freeing bad pointer\n");
        ink_debug_assert(!"WARNING: freeing bad pointer");
      }
    }
  }
}

void *
_xfree_null(void *ptr)
{
  _xfree(ptr);
  return NULL;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void *
_xmalloc(unsigned int size, const char *path)
{
  ResMemInfo *info;
  char *mem;
  int extra;
  int fence_post;

  extra = res_extra_space;

  fence_post = res_fence_post;

  if (fence_post) {
    extra += FENCE_POST_SIZE * 2;
  }

  mem = (char *) malloc(size + extra);

  if (unlikely(mem == NULL)) {
    fprintf(stderr, "FATAL: _xmalloc could not allocate %u + %u bytes [%s]\n",
            size, extra, path ? path : "memory/anonymous");
    xdump();
    _exit(1);
  }

  if (fence_post) {
    memcpy(mem, fence_post_pattern, FENCE_POST_SIZE);
    memcpy(mem + size + extra - FENCE_POST_SIZE, fence_post_pattern, FENCE_POST_SIZE);
    mem += FENCE_POST_SIZE;
  }

  memset(mem, 0, res_extra_space);

  info = (ResMemInfo *) mem;
  info->magic = MAKE_MAGIC(mem);
  info->size = size;
  info->fence_post = fence_post;

  if (res_track_memory) {
    info->res = res_stat(path, size);
    res_check(info->res);
  } else {
    info->res = NULL;
  }

  mem = mem + res_extra_space;

  return mem;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

char *
_xstrdup(const char *str, int64 length, const char *path)
{
  char *newstr;

  if (unlikely(str == NULL)) {
    return NULL;
  }

  if (length < 0) {
    length = (int) strlen(str);
  }

  newstr = (char *) _xmalloc(length + 1, path);
  strncpy(newstr, str, length);
  newstr[length] = '\0';

  return newstr;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void *
_xrealloc(void *ptr, unsigned int size, const char *path)
{
  if (!ptr) {
    return _xmalloc(size, path);
  } else {
    ResMemInfo *info;
    char *mem;
    int extra;
    int fence_post;

    info = (ResMemInfo *) ADJUST(ptr, -res_extra_space);
    if (!CHECK_MAGIC(info->magic, info)) {
      info = (ResMemInfo *) ADJUST(info, -magic_array_offset);
    }

    if (info->res) {
      res_check(info->res);
      res_inc(info->res, -((int64) info->size));
    }

    mem = (char *) info;
    extra = res_extra_space;

    if (info->fence_post) {
      mem -= FENCE_POST_SIZE;
      if (_xcheck_fence_post(mem, info->size + res_extra_space + FENCE_POST_SIZE * 2)) {
        fprintf(stderr, "MEMORY: realloc: fence-post mangled [%s] [%s]\n",
                info->res ? info->res->path : "<unknown>", path);
        abort();
      }
    }

    memset(info, 0, res_extra_space);

    fence_post = res_fence_post;

    if (fence_post) {
      extra += FENCE_POST_SIZE * 2;
    }

    mem = (char *) realloc(mem, size + extra);
    if (unlikely(mem == NULL)) {
      fprintf(stderr, "FATAL: could not reallocate %u + %u bytes [%s]\n",
              size, extra, path ? path : "memory/anonymous");
      xdump();
      _exit(1);
    }

    if (fence_post) {
      memcpy(mem, fence_post_pattern, FENCE_POST_SIZE);
      memcpy(mem + size + extra - FENCE_POST_SIZE, fence_post_pattern, FENCE_POST_SIZE);
      mem += FENCE_POST_SIZE;
    }

    memset(mem, 0, res_extra_space);

    info = (ResMemInfo *) mem;
    info->magic = MAKE_MAGIC(mem);
    info->size = size;
    info->fence_post = fence_post;

    if (res_track_memory) {
      info->res = res_stat(path, size);
      res_check(info->res);
    } else {
      info->res = NULL;
    }

    mem = mem + res_extra_space;

    return mem;
  }
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void *
_xtrack(void *ptr, const char *path)
{
  if (unlikely(ptr == NULL)) {
    fprintf(stderr, "WARNING: cannot track NULL pointer\n");
    return ptr;
  } else {
    ResMemInfo *info;

    info = (ResMemInfo *) ADJUST(ptr, -res_extra_space);
    if (!CHECK_MAGIC(info->magic, info)) {
      info = (ResMemInfo *) ADJUST(info, -magic_array_offset);
    }

    if (!CHECK_MAGIC(info->magic, info)) {
      return ptr;
    }

    if (info->res) {
      res_check(info->res);
      res_inc(info->res, -((int64) info->size));
    }

    if (res_track_memory) {
      info->res = res_stat(path, info->size);
      res_check(info->res);
    } else {
      info->res = NULL;
    }

    return ptr;
  }
}

void
xdump_snap_baseline()
{
  int i;
  Resource *res;

  for (i = 0; i < TSIZE; i++) {
    res = (Resource *) res_table[i];
    while (res) {
      res_check(res);
      res->baseline = res->value;
      res = res->next;
    }
  }
}

void
xdump_to_file_baseline_rel(FILE * fp)
{
  Resource *res;
  int64 value;
  int64 diff;
  int i;
  struct timeval timestamp;
  char time_string[32], *time_str;
  int length_to_write;

  ink_gethrtimeofday(&timestamp, NULL);
  time_str = ctime((time_t *) & timestamp.tv_sec);

  length_to_write = squid_timestamp_to_buf(time_string, 32, timestamp.tv_sec, timestamp.tv_usec);
  time_string[length_to_write] = '\0';

  fprintf(fp, "PID: %d %s  %s", getpid(), time_string, time_str);
  fprintf(fp, "   value    |   delta    |   location\n");
  fprintf(fp, "rel. to base|            |           \n");
  fprintf(fp, "------------|------------|-----------------------------------------------\n");

  for (i = 0; i < TSIZE; i++) {
    res = (Resource *) res_table[i];
    while (res) {
      res_check(res);

      value = res->value - res->baseline;
      diff = res->value - res->snapshot;
      if (value != 0) {
        fprintf(fp, " % 10d | % 10d | %s\n", (int) value, (int) diff, res->path);
      }

      res->snapshot = res->value;
      res = res->next;
    }
  }
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
xdump_to_file(FILE * fp)
{
  Resource *res;
  int64 value;
  int64 diff;
  int i;
  struct timeval timestamp;
  char time_string[32], *time_str;
  int length_to_write;

  ink_gethrtimeofday(&timestamp, NULL);
  time_str = ctime((time_t *) & timestamp.tv_sec);

  length_to_write = squid_timestamp_to_buf(time_string, 32, timestamp.tv_sec, timestamp.tv_usec);
  time_string[length_to_write] = '\0';

  fprintf(fp, "PID: %d %s  %s", getpid(), time_string, time_str);
  fprintf(fp, "   value    |   delta    |   location\n");
  fprintf(fp, "------------|------------|-----------------------------------------------\n");

  for (i = 0; i < TSIZE; i++) {
    res = (Resource *) res_table[i];
    while (res) {
      res_check(res);
      if (strncmp(res->path, "memory/IOBuffer/", strlen("memory/IOBuffer/"))
          != 0) {
        value = res->value;
        diff = value - res->snapshot;
        if (diff != 0) {
          fprintf(fp, " % 10d | % 10d | %s\n", (int) value, (int) diff, res->path);
        }
        res->snapshot = res->value;
      }
      res = res->next;
    }
  }

  fprintf(fp, "   value    |   delta    |   location\n");
  fprintf(fp, "------------|------------|-----------------------------------------------\n");
  for (i = 0; i < TSIZE; i++) {
    res = (Resource *) res_table[i];
    while (res) {
      res_check(res);
      if (strncmp(res->path, "memory/IOBuffer/", strlen("memory/IOBuffer/"))
          == 0) {
        value = res->value;
        diff = value - res->snapshot;
        if (diff != 0) {
          fprintf(fp, " % 10d | % 10d | %s\n", (int) value, (int) diff, res->path);
        }
        res->snapshot = res->value;
      }
      res = res->next;
    }
  }
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
xdump()
{
  ink_stack_trace_dump();
  xdump_to_file(stderr);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
xsnap()
{
  Resource *res;
  int i;

  for (i = 0; i < TSIZE; i++) {
    res = (Resource *) res_table[i];
    while (res) {
      res_check(res);
      res->snapshot = res->value;
      res = res->next;
    }
  }
}


#else /* TRACK_MEMORY */

/*-------------------------------------------------------------------------
-------------------------------------------------------------------------*/
void
_xfree(void *mem)
{
  if (likely(mem))
    ink_free(mem);
}

void *
_xfree_null(void *mem)
{
  if (likely(mem))
    ink_free(mem);
  return NULL;
}


void *
_xmalloc(unsigned int size, const char *path)
{
  NOWARN_UNUSED(path);
  return ink_malloc(size);
}

void *
_xrealloc(void *ptr, unsigned int size, const char *path)
{
  NOWARN_UNUSED(path);
  return ink_realloc(ptr, size);
}

char *
_xstrdup(const char *str, int length, const char *path)
{
  NOWARN_UNUSED(path);
  char *newstr;

  if (likely(str)) {
    if (length < 0) {
      length = strlen(str);
    }
    newstr = (char *) ink_malloc(length + 1);
    if (likely(newstr != NULL)) {
      strncpy(newstr, str, length);
      newstr[length] = '\0';
      return newstr;
    }
    fprintf(stderr, "FATAL: could not allocate %d bytes in _xstrdup\n", length + 1);
    ink_stack_trace_dump();
    _exit(1);
  }
  return NULL;
}

typedef struct Resource Resource;

Resource *
res_lookup(const char *path)
{
  NOWARN_UNUSED(path);
  return NULL;
}

void
xdump(void)
{
  ink_stack_trace_dump();
}


#endif /* TRACK_MEMORY */

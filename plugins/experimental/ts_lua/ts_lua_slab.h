#ifndef _TS_SLAB_H_INCLUDED_
#define _TS_SLAB_H_INCLUDED_

#include <ts/ts.h>
#include <ts/experimental.h>
#include <ts/remap.h>

#define ts_align_ptr(p, a) (unsigned char *)(((unsigned long)(p) + ((unsigned long)a - 1)) & ~((unsigned long)a - 1))

#define ALIGNMENT_DOWN(a, size) (((unsigned long)a + size - 1) & (~(size - 1)))
#define ALIGNMENT_UP(a, size) ((unsigned long)a & (~(size - 1)))

#define MAX_SHPOOL_SIZE 8

typedef struct ts_slab_page_s ts_slab_page_t;

struct ts_slab_page_s {
  unsigned long slab;
  ts_slab_page_t *next;
  unsigned long prev;
};

typedef struct {
  // ts_shmtx_sh_t    lock;
  const char *name;

  size_t min_size;
  size_t min_shift;

  size_t size;

  ts_slab_page_t *pages;
  ts_slab_page_t *last;
  ts_slab_page_t free;

  unsigned char *start;
  unsigned char *end;

  TSMutex mutex;
  unsigned char *log_ctx;
  unsigned char zero;

  unsigned log_nomem : 1;

  void *data;
  void *addr;
} ts_slab_pool_t;

ts_slab_pool_t *ts_slab_pool_init(size_t s);
void *ts_slab_alloc(ts_slab_pool_t *pool, size_t size);
void ts_slab_free(ts_slab_pool_t *pool, void *p);
ts_slab_pool_t **get_global_pool();
int get_global_pool_len();
// void *ts_slab_alloc(ts_slab_pool_t *pool, size_t size);
void *ts_slab_alloc_locked(ts_slab_pool_t *pool, size_t size);
// void *ts_slab_calloc(ts_slab_pool_t *pool, size_t size);
// void *ts_slab_calloc_locked(ts_slab_pool_t *pool, size_t size);
// void ts_slab_free(ts_slab_pool_t *pool, void *p);
void ts_slab_free_locked(ts_slab_pool_t *pool, void *p);

#endif /* _TS_SLAB_H_INCLUDED_ */

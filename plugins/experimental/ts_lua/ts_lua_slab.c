#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "ts_lua_slab.h"

#define TS_SLAB_PAGE_MASK 3
#define TS_SLAB_PAGE 0
#define TS_SLAB_BIG 1
#define TS_SLAB_EXACT 2
#define TS_SLAB_SMALL 3

#if (TS_PTR_SIZE == 4)

#define TS_SLAB_PAGE_FREE 0
#define TS_SLAB_PAGE_BUSY 0xffffffff
#define TS_SLAB_PAGE_START 0x80000000

#define TS_SLAB_SHIFT_MASK 0x0000000f
#define TS_SLAB_MAP_MASK 0xffff0000
#define TS_SLAB_MAP_SHIFT 16

#define TS_SLAB_BUSY 0xffffffff

#else /* (TS_PTR_SIZE == 8) */

#define TS_SLAB_PAGE_FREE 0
#define TS_SLAB_PAGE_BUSY 0xffffffffffffffff
#define TS_SLAB_PAGE_START 0x8000000000000000

#define TS_SLAB_SHIFT_MASK 0x000000000000000f
#define TS_SLAB_MAP_MASK 0xffffffff00000000
#define TS_SLAB_MAP_SHIFT 32

#define TS_SLAB_BUSY 0xffffffffffffffff

#endif

#if (TS_DEBUG_MALLOC)

#define ts_slab_junk(p, size) ts_memset(p, 0xA5, size)

#elif (TS_HAVE_DEBUG_MALLOC)

#define ts_slab_junk(p, size) \
  if (ts_debug_malloc)        \
  ts_memset(p, 0xA5, size)

#else

#define ts_slab_junk(p, size)

#endif

#define SLAB_TAG "ts_lua_slab"

static ts_slab_pool_t *ts_slab_init(ts_slab_pool_t *pool);
static ts_slab_page_t *ts_slab_alloc_pages(ts_slab_pool_t *pool, unsigned int pages);
static void ts_slab_free_pages(ts_slab_pool_t *pool, ts_slab_page_t *page, unsigned int pages);

static unsigned long ts_slab_max_size;
static unsigned long ts_slab_exact_size;
static unsigned long ts_slab_exact_shift;

static int ts_pagesize       = 0;
static int ts_pagesize_shift = 0;

static ts_slab_pool_t *global_pool[MAX_SHPOOL_SIZE];
static int global_pool_len = 0;

ts_slab_pool_t **
get_global_pool()
{
  return global_pool;
}

int
get_global_pool_len()
{
  return global_pool_len;
}

void
ts_slab_free(ts_slab_pool_t *pool, void *p)
{
  /* todo lock */
  TSMutexLock(pool->mutex);

  ts_slab_free_locked(pool, p);
  /* unlock */
  TSMutexUnlock(pool->mutex);
}

void *
ts_slab_alloc(ts_slab_pool_t *pool, size_t size)
{
  void *p;
  /* todo lock */
  TSMutexLock(pool->mutex);

  p = ts_slab_alloc_locked(pool, size);

  /* unlock */
  TSMutexUnlock(pool->mutex);

  return p;
}

ts_slab_pool_t *
ts_slab_pool_init(size_t s)
{
  char *addr;
  int n;
  size_t size;
  ts_slab_pool_t *shpool;

  if (global_pool_len >= MAX_SHPOOL_SIZE) {
    return NULL;
  }

  ts_pagesize = getpagesize();
  for (n = ts_pagesize; n >>= 1; ts_pagesize_shift++) { /* void */
  }

  size = ALIGNMENT_DOWN(s, 4096);
  addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_ANON | MAP_SHARED, -1, 0);
  if (addr == NULL) {
    TSError("[%s] mmap error; can not allocated shpool->size", SLAB_TAG);
    return NULL;
  }

  shpool                         = (ts_slab_pool_t *)addr;
  shpool->addr                   = addr;
  shpool->size                   = size;
  shpool->min_size               = 8;
  shpool->min_shift              = 3;
  shpool->end                    = shpool->addr + shpool->size;
  shpool->mutex                  = TSMutexCreate();
  global_pool[global_pool_len++] = shpool;

  return ts_slab_init(shpool);
}

static ts_slab_pool_t *
ts_slab_init(ts_slab_pool_t *pool)
{
  unsigned char *p;
  size_t size;
  int m;
  unsigned long i, n, pages;
  ts_slab_page_t *slots;

  /* STUB */
  if (ts_slab_max_size == 0) {
    ts_slab_max_size   = ts_pagesize / 2;
    ts_slab_exact_size = ts_pagesize / (8 * sizeof(unsigned long));
    for (n = ts_slab_exact_size; n >>= 1; ts_slab_exact_shift++) {
      /* void */
    }
  }
  /**/

  pool->min_size = 1 << pool->min_shift;

  p    = (unsigned char *)pool + sizeof(ts_slab_pool_t);
  size = pool->end - p;

  ts_slab_junk(p, size);

  slots = (ts_slab_page_t *)p;
  n     = ts_pagesize_shift - pool->min_shift;

  for (i = 0; i < n; i++) {
    slots[i].slab = 0;
    slots[i].next = &slots[i];
    slots[i].prev = 0;
  }

  p += n * sizeof(ts_slab_page_t);

  pages = (unsigned long)(size / (ts_pagesize + sizeof(ts_slab_page_t)));

  memset(p, 0, pages * sizeof(ts_slab_page_t));

  pool->pages = (ts_slab_page_t *)p;

  pool->free.prev = 0;
  pool->free.next = (ts_slab_page_t *)p;

  pool->pages->slab = pages;
  pool->pages->next = &pool->free;
  pool->pages->prev = (unsigned long)&pool->free;

  pool->start = (unsigned char *)ts_align_ptr((unsigned long)p + pages * sizeof(ts_slab_page_t), ts_pagesize);

  m = pages - (pool->end - pool->start) / ts_pagesize;
  if (m > 0) {
    pages -= m;
    pool->pages->slab = pages;
  }

  pool->last = pool->pages + pages;

  pool->log_nomem = 1;
  pool->log_ctx   = &pool->zero;
  pool->zero      = '\0';

  return pool;
}

static ts_slab_page_t *
ts_slab_alloc_pages(ts_slab_pool_t *pool, unsigned int pages)
{
  ts_slab_page_t *page, *p;

  for (page = pool->free.next; page != &pool->free; page = page->next) {
    if (page->slab >= pages) {
      if (page->slab > pages) {
        page[page->slab - 1].prev = (unsigned long)&page[pages];

        page[pages].slab = page->slab - pages;
        page[pages].next = page->next;
        page[pages].prev = page->prev;

        p                = (ts_slab_page_t *)page->prev;
        p->next          = &page[pages];
        page->next->prev = (unsigned long)&page[pages];

      } else {
        p                = (ts_slab_page_t *)page->prev;
        p->next          = page->next;
        page->next->prev = page->prev;
      }

      page->slab = pages | TS_SLAB_PAGE_START;
      page->next = NULL;
      page->prev = TS_SLAB_PAGE;

      if (--pages == 0) {
        return page;
      }

      for (p = page + 1; pages; pages--) {
        p->slab = TS_SLAB_PAGE_BUSY;
        p->next = NULL;
        p->prev = TS_SLAB_PAGE;
        p++;
      }

      return page;
    }
  }

  if (pool->log_nomem) {
    //       ts_slab_error(pool, TS_LOG_CRIT,
    //                      "ts_slab_alloc() failed: no memory");
  }

  return NULL;
}

void *
ts_slab_alloc_locked(ts_slab_pool_t *pool, size_t size)
{
  size_t s;
  unsigned long p, n, m, mask, *bitmap;
  unsigned int i, slot, shift, map;
  ts_slab_page_t *page, *prev, *slots;

  if (size > ts_slab_max_size) {
    TSDebug(SLAB_TAG, "slab alloc: %ld\n", size);

    page = ts_slab_alloc_pages(pool, (size >> ts_pagesize_shift) + ((size % ts_pagesize) ? 1 : 0));
    if (page) {
      p = (page - pool->pages) << ts_pagesize_shift;
      p += (unsigned long)pool->start;

    } else {
      p = 0;
    }

    goto done;
  }

  if (size > pool->min_size) {
    shift = 1;
    for (s = size - 1; s >>= 1; shift++) { /* void */
    }
    slot = shift - pool->min_shift;

  } else {
    size  = pool->min_size;
    shift = pool->min_shift;
    slot  = 0;
  }

  TSDebug(SLAB_TAG, "slab alloc: %lu slot: %u\n", size, slot);

  slots = (ts_slab_page_t *)((unsigned char *)pool + sizeof(ts_slab_pool_t));
  page  = slots[slot].next;

  if (page->next != page) {
    if (shift < ts_slab_exact_shift) {
      do {
        p      = (page - pool->pages) << ts_pagesize_shift;
        bitmap = (unsigned long *)(pool->start + p);

        map = (1 << (ts_pagesize_shift - shift)) / (sizeof(unsigned long) * 8);

        for (n = 0; n < map; n++) {
          if (bitmap[n] != TS_SLAB_BUSY) {
            for (m = 1, i = 0; m; m <<= 1, i++) {
              if ((bitmap[n] & m)) {
                continue;
              }

              bitmap[n] |= m;

              i = ((n * sizeof(unsigned long) * 8) << shift) + (i << shift);

              if (bitmap[n] == TS_SLAB_BUSY) {
                for (n = n + 1; n < map; n++) {
                  if (bitmap[n] != TS_SLAB_BUSY) {
                    p = (unsigned long)bitmap + i;

                    goto done;
                  }
                }

                prev             = (ts_slab_page_t *)(page->prev & ~TS_SLAB_PAGE_MASK);
                prev->next       = page->next;
                page->next->prev = page->prev;

                page->next = NULL;
                page->prev = TS_SLAB_SMALL;
              }

              p = (unsigned long)bitmap + i;

              goto done;
            }
          }
        }

        page = page->next;

      } while (page);

    } else if (shift == ts_slab_exact_shift) {
      do {
        if (page->slab != TS_SLAB_BUSY) {
          for (m = 1, i = 0; m; m <<= 1, i++) {
            if ((page->slab & m)) {
              continue;
            }

            page->slab |= m;

            if (page->slab == TS_SLAB_BUSY) {
              prev             = (ts_slab_page_t *)(page->prev & ~TS_SLAB_PAGE_MASK);
              prev->next       = page->next;
              page->next->prev = page->prev;

              page->next = NULL;
              page->prev = TS_SLAB_EXACT;
            }

            p = (page - pool->pages) << ts_pagesize_shift;
            p += i << shift;
            p += (unsigned long)pool->start;

            goto done;
          }
        }

        page = page->next;

      } while (page);

    } else { /* shift > ts_slab_exact_shift */

      n    = ts_pagesize_shift - (page->slab & TS_SLAB_SHIFT_MASK);
      n    = 1 << n;
      n    = ((unsigned long)1 << n) - 1;
      mask = n << TS_SLAB_MAP_SHIFT;

      do {
        if ((page->slab & TS_SLAB_MAP_MASK) != mask) {
          for (m = (unsigned long)1 << TS_SLAB_MAP_SHIFT, i = 0; m & mask; m <<= 1, i++) {
            if ((page->slab & m)) {
              continue;
            }

            page->slab |= m;

            if ((page->slab & TS_SLAB_MAP_MASK) == mask) {
              prev             = (ts_slab_page_t *)(page->prev & ~TS_SLAB_PAGE_MASK);
              prev->next       = page->next;
              page->next->prev = page->prev;

              page->next = NULL;
              page->prev = TS_SLAB_BIG;
            }

            p = (page - pool->pages) << ts_pagesize_shift;
            p += i << shift;
            p += (unsigned long)pool->start;

            goto done;
          }
        }

        page = page->next;

      } while (page);
    }
  }

  page = ts_slab_alloc_pages(pool, 1);

  if (page) {
    if (shift < ts_slab_exact_shift) {
      p      = (page - pool->pages) << ts_pagesize_shift;
      bitmap = (unsigned long *)(pool->start + p);

      s = 1 << shift;
      n = (1 << (ts_pagesize_shift - shift)) / 8 / s;

      if (n == 0) {
        n = 1;
      }

      bitmap[0] = (2 << n) - 1;

      map = (1 << (ts_pagesize_shift - shift)) / (sizeof(unsigned long) * 8);

      for (i = 1; i < map; i++) {
        bitmap[i] = 0;
      }

      page->slab = shift;
      page->next = &slots[slot];
      page->prev = (unsigned long)&slots[slot] | TS_SLAB_SMALL;

      slots[slot].next = page;

      p = ((page - pool->pages) << ts_pagesize_shift) + s * n;
      p += (unsigned long)pool->start;

      goto done;

    } else if (shift == ts_slab_exact_shift) {
      page->slab = 1;
      page->next = &slots[slot];
      page->prev = (unsigned long)&slots[slot] | TS_SLAB_EXACT;

      slots[slot].next = page;

      p = (page - pool->pages) << ts_pagesize_shift;
      p += (unsigned long)pool->start;

      goto done;

    } else { /* shift > ts_slab_exact_shift */

      page->slab = ((unsigned long)1 << TS_SLAB_MAP_SHIFT) | shift;
      page->next = &slots[slot];
      page->prev = (unsigned long)&slots[slot] | TS_SLAB_BIG;

      slots[slot].next = page;

      p = (page - pool->pages) << ts_pagesize_shift;
      p += (unsigned long)pool->start;

      goto done;
    }
  }

  p = 0;

done:

  TSDebug(SLAB_TAG, "slab alloc: %p\n", (void *)p);

  return (void *)p;
}

void
ts_slab_free_locked(ts_slab_pool_t *pool, void *p)
{
  size_t size;
  unsigned long slab, m, *bitmap;
  unsigned int n, type, slot, shift, map;
  ts_slab_page_t *slots, *page;

  TSDebug(SLAB_TAG, "slab free: %p\n", p);

  if ((unsigned char *)p < pool->start || (unsigned char *)p > pool->end) {
    TSError("[%s] ts_slab_free(): outside of pool", SLAB_TAG);
    goto fail;
  }

  n    = ((unsigned char *)p - pool->start) >> ts_pagesize_shift;
  page = &pool->pages[n];
  slab = page->slab;
  type = page->prev & TS_SLAB_PAGE_MASK;

  switch (type) {
  case TS_SLAB_SMALL:

    shift = slab & TS_SLAB_SHIFT_MASK;
    size  = 1 << shift;

    if ((unsigned long)p & (size - 1)) {
      goto wrong_chunk;
    }

    n = ((unsigned long)p & (ts_pagesize - 1)) >> shift;
    m = (unsigned long)1 << (n & (sizeof(unsigned long) * 8 - 1));
    n /= (sizeof(unsigned long) * 8);
    bitmap = (unsigned long *)((unsigned long)p & ~((unsigned long)ts_pagesize - 1));

    if (bitmap[n] & m) {
      if (page->next == NULL) {
        slots = (ts_slab_page_t *)((unsigned char *)pool + sizeof(ts_slab_pool_t));
        slot  = shift - pool->min_shift;

        page->next       = slots[slot].next;
        slots[slot].next = page;

        page->prev       = (unsigned long)&slots[slot] | TS_SLAB_SMALL;
        page->next->prev = (unsigned long)page | TS_SLAB_SMALL;
      }

      bitmap[n] &= ~m;

      n = (1 << (ts_pagesize_shift - shift)) / 8 / (1 << shift);

      if (n == 0) {
        n = 1;
      }

      if (bitmap[0] & ~(((unsigned long)1 << n) - 1)) {
        goto done;
      }

      map = (1 << (ts_pagesize_shift - shift)) / (sizeof(unsigned long) * 8);

      for (n = 1; n < map; n++) {
        if (bitmap[n]) {
          goto done;
        }
      }

      ts_slab_free_pages(pool, page, 1);

      goto done;
    }

    goto chunk_already_free;

  case TS_SLAB_EXACT:

    m    = (unsigned long)1 << (((unsigned long)p & (ts_pagesize - 1)) >> ts_slab_exact_shift);
    size = ts_slab_exact_size;

    if ((unsigned long)p & (size - 1)) {
      goto wrong_chunk;
    }

    if (slab & m) {
      if (slab == TS_SLAB_BUSY) {
        slots = (ts_slab_page_t *)((unsigned char *)pool + sizeof(ts_slab_pool_t));
        slot  = ts_slab_exact_shift - pool->min_shift;

        page->next       = slots[slot].next;
        slots[slot].next = page;

        page->prev       = (unsigned long)&slots[slot] | TS_SLAB_EXACT;
        page->next->prev = (unsigned long)page | TS_SLAB_EXACT;
      }

      page->slab &= ~m;

      if (page->slab) {
        goto done;
      }

      ts_slab_free_pages(pool, page, 1);

      goto done;
    }

    goto chunk_already_free;

  case TS_SLAB_BIG:

    shift = slab & TS_SLAB_SHIFT_MASK;
    size  = 1 << shift;

    if ((unsigned long)p & (size - 1)) {
      goto wrong_chunk;
    }

    m = (unsigned long)1 << ((((unsigned long)p & (ts_pagesize - 1)) >> shift) + TS_SLAB_MAP_SHIFT);

    if (slab & m) {
      if (page->next == NULL) {
        slots = (ts_slab_page_t *)((unsigned char *)pool + sizeof(ts_slab_pool_t));
        slot  = shift - pool->min_shift;

        page->next       = slots[slot].next;
        slots[slot].next = page;

        page->prev       = (unsigned long)&slots[slot] | TS_SLAB_BIG;
        page->next->prev = (unsigned long)page | TS_SLAB_BIG;
      }

      page->slab &= ~m;

      if (page->slab & TS_SLAB_MAP_MASK) {
        goto done;
      }

      ts_slab_free_pages(pool, page, 1);

      goto done;
    }

    goto chunk_already_free;

  case TS_SLAB_PAGE:

    if ((unsigned long)p & (ts_pagesize - 1)) {
      goto wrong_chunk;
    }

    if (slab == TS_SLAB_PAGE_FREE) {
      TSError("[%s] ts_slab_free(): page is already free", SLAB_TAG);
      goto fail;
    }

    if (slab == TS_SLAB_PAGE_BUSY) {
      TSError("[%s] ts_slab_free(): pointer to wrong page", SLAB_TAG);
      goto fail;
    }

    n    = ((unsigned char *)p - pool->start) >> ts_pagesize_shift;
    size = slab & ~TS_SLAB_PAGE_START;

    ts_slab_free_pages(pool, &pool->pages[n], size);

    ts_slab_junk(p, size << ts_pagesize_shift);

    return;
  }

  /* not reached */

  return;

done:

  ts_slab_junk(p, size);

  return;

wrong_chunk:

  TSError("[%s] ts_slab_free(): pointer to wrong chunk", SLAB_TAG);

  goto fail;

chunk_already_free:

  TSError("[%s] ts_slab_free(): chunk is already free", SLAB_TAG);

fail:

  return;
}

static void
ts_slab_free_pages(ts_slab_pool_t *pool, ts_slab_page_t *page, unsigned int pages)
{
  unsigned int type;
  ts_slab_page_t *prev, *join;

  page->slab = pages--;

  if (pages) {
    memset(&page[1], 0, pages * sizeof(ts_slab_page_t));
  }

  if (page->next) {
    prev             = (ts_slab_page_t *)(page->prev & ~TS_SLAB_PAGE_MASK);
    prev->next       = page->next;
    page->next->prev = page->prev;
  }

  join = page + page->slab;

  if (join < pool->last) {
    type = join->prev & TS_SLAB_PAGE_MASK;

    if (type == TS_SLAB_PAGE) {
      if (join->next != NULL) {
        pages += join->slab;
        page->slab += join->slab;

        prev             = (ts_slab_page_t *)(join->prev & ~TS_SLAB_PAGE_MASK);
        prev->next       = join->next;
        join->next->prev = join->prev;

        join->slab = TS_SLAB_PAGE_FREE;
        join->next = NULL;
        join->prev = TS_SLAB_PAGE;
      }
    }
  }

  if (page > pool->pages) {
    join = page - 1;
    type = join->prev & TS_SLAB_PAGE_MASK;

    if (type == TS_SLAB_PAGE) {
      if (join->slab == TS_SLAB_PAGE_FREE) {
        join = (ts_slab_page_t *)(join->prev & ~TS_SLAB_PAGE_MASK);
      }

      if (join->next != NULL) {
        pages += join->slab;
        join->slab += page->slab;

        prev             = (ts_slab_page_t *)(join->prev & ~TS_SLAB_PAGE_MASK);
        prev->next       = join->next;
        join->next->prev = join->prev;

        page->slab = TS_SLAB_PAGE_FREE;
        page->next = NULL;
        page->prev = TS_SLAB_PAGE;

        page = join;
      }
    }
  }

  if (pages) {
    page[pages].prev = (unsigned long)page;
  }

  page->prev = (unsigned long)&pool->free;
  page->next = pool->free.next;

  page->next->prev = (unsigned long)page;

  pool->free.next = page;
}

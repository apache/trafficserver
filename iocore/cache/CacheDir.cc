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

#include "P_Cache.h"

#include "tscore/hugepages.h"
#include "tscore/Regression.h"

// #define LOOP_CHECK_MODE 1
#ifdef LOOP_CHECK_MODE
#define DIR_LOOP_THRESHOLD 1000
#endif
#include "tscore/ink_stack_trace.h"

#define CACHE_INC_DIR_USED(_m)                            \
  do {                                                    \
    ProxyMutex *mutex = _m.get();                         \
    CACHE_INCREMENT_DYN_STAT(cache_direntries_used_stat); \
  } while (0)

#define CACHE_DEC_DIR_USED(_m)                            \
  do {                                                    \
    ProxyMutex *mutex = _m.get();                         \
    CACHE_DECREMENT_DYN_STAT(cache_direntries_used_stat); \
  } while (0)

#define CACHE_INC_DIR_COLLISIONS(_m)                                \
  do {                                                              \
    ProxyMutex *mutex = _m.get();                                   \
    CACHE_INCREMENT_DYN_STAT(cache_directory_collision_count_stat); \
  } while (0);

// Globals

ClassAllocator<OpenDirEntry> openDirEntryAllocator("openDirEntry");
Dir empty_dir;

// OpenDir

OpenDir::OpenDir()
{
  SET_HANDLER(&OpenDir::signal_readers);
}

/*
   If allow_if_writers is false, open_write fails if there are other writers.
   max_writers sets the maximum number of concurrent writers that are
   allowed. Only The first writer can set the max_writers. It is ignored
   for later writers.
   Returns 1 on success and 0 on failure.
   */
int
OpenDir::open_write(CacheVC *cont, int allow_if_writers, int max_writers)
{
  ink_assert(cont->vol->mutex->thread_holding == this_ethread());
  unsigned int h = cont->first_key.slice32(0);
  int b          = h % OPEN_DIR_BUCKETS;
  for (OpenDirEntry *d = bucket[b].head; d; d = d->link.next) {
    if (!(d->writers.head->first_key == cont->first_key)) {
      continue;
    }
    if (allow_if_writers && d->num_writers < d->max_writers) {
      d->writers.push(cont);
      d->num_writers++;
      cont->od           = d;
      cont->write_vector = &d->vector;
      return 1;
    }
    return 0;
  }
  OpenDirEntry *od = THREAD_ALLOC(openDirEntryAllocator, cont->mutex->thread_holding);
  od->readers.head = nullptr;
  od->writers.push(cont);
  od->num_writers           = 1;
  od->max_writers           = max_writers;
  od->vector.data.data      = &od->vector.data.fast_data[0];
  od->dont_update_directory = false;
  od->move_resident_alt     = false;
  od->reading_vec           = false;
  od->writing_vec           = false;
  dir_clear(&od->first_dir);
  cont->od           = od;
  cont->write_vector = &od->vector;
  bucket[b].push(od);
  return 1;
}

int
OpenDir::signal_readers(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */)
{
  Queue<CacheVC, Link_CacheVC_opendir_link> newly_delayed_readers;
  EThread *t = mutex->thread_holding;
  CacheVC *c = nullptr;
  while ((c = delayed_readers.dequeue())) {
    CACHE_TRY_LOCK(lock, c->mutex, t);
    if (lock.is_locked()) {
      c->f.open_read_timeout = 0;
      c->handleEvent(EVENT_IMMEDIATE, nullptr);
      continue;
    }
    newly_delayed_readers.push(c);
  }
  if (newly_delayed_readers.head) {
    delayed_readers = newly_delayed_readers;
    EThread *t1     = newly_delayed_readers.head->mutex->thread_holding;
    if (!t1) {
      t1 = mutex->thread_holding;
    }
    t1->schedule_in(this, HRTIME_MSECONDS(cache_config_mutex_retry_delay));
  }
  return 0;
}

int
OpenDir::close_write(CacheVC *cont)
{
  ink_assert(cont->vol->mutex->thread_holding == this_ethread());
  cont->od->writers.remove(cont);
  cont->od->num_writers--;
  if (!cont->od->writers.head) {
    unsigned int h = cont->first_key.slice32(0);
    int b          = h % OPEN_DIR_BUCKETS;
    bucket[b].remove(cont->od);
    delayed_readers.append(cont->od->readers);
    signal_readers(0, nullptr);
    cont->od->vector.clear();
    THREAD_FREE(cont->od, openDirEntryAllocator, cont->mutex->thread_holding);
  }
  cont->od = nullptr;
  return 0;
}

OpenDirEntry *
OpenDir::open_read(const CryptoHash *key)
{
  unsigned int h = key->slice32(0);
  int b          = h % OPEN_DIR_BUCKETS;
  for (OpenDirEntry *d = bucket[b].head; d; d = d->link.next) {
    if (d->writers.head->first_key == *key) {
      return d;
    }
  }
  return nullptr;
}

int
OpenDirEntry::wait(CacheVC *cont, int msec)
{
  ink_assert(cont->vol->mutex->thread_holding == this_ethread());
  cont->f.open_read_timeout = 1;
  ink_assert(!cont->trigger);
  cont->trigger = cont->vol->mutex->thread_holding->schedule_in_local(cont, HRTIME_MSECONDS(msec));
  readers.push(cont);
  return EVENT_CONT;
}

//
// Cache Directory
//

// return value 1 means no loop
// zero indicates loop
int
dir_bucket_loop_check(Dir *start_dir, Dir *seg)
{
  if (start_dir == nullptr) {
    return 1;
  }

  Dir *p1 = start_dir;
  Dir *p2 = start_dir;

  while (p2) {
    // p1 moves by one entry per iteration
    ink_assert(p1);
    p1 = next_dir(p1, seg);
    // p2 moves by two entries per iteration
    p2 = next_dir(p2, seg);
    if (p2) {
      p2 = next_dir(p2, seg);
    } else {
      return 1;
    }

    if (p2 == p1) {
      return 0; // we have a loop
    }
  }
  return 1;
}

// adds all the directory entries
// in a segment to the segment freelist
void
dir_init_segment(int s, Vol *d)
{
  d->header->freelist[s] = 0;
  Dir *seg               = d->dir_segment(s);
  int l, b;
  memset(static_cast<void *>(seg), 0, SIZEOF_DIR * DIR_DEPTH * d->buckets);
  for (l = 1; l < DIR_DEPTH; l++) {
    for (b = 0; b < d->buckets; b++) {
      Dir *bucket = dir_bucket(b, seg);
      dir_free_entry(dir_bucket_row(bucket, l), s, d);
    }
  }
}

// break the infinite loop in directory entries
// Note : abuse of the token bit in dir entries
int
dir_bucket_loop_fix(Dir *start_dir, int s, Vol *d)
{
  if (!dir_bucket_loop_check(start_dir, d->dir_segment(s))) {
    Warning("Dir loop exists, clearing segment %d", s);
    dir_init_segment(s, d);
    return 1;
  }
  return 0;
}

int
dir_freelist_length(Vol *d, int s)
{
  int free = 0;
  Dir *seg = d->dir_segment(s);
  Dir *e   = dir_from_offset(d->header->freelist[s], seg);
  if (dir_bucket_loop_fix(e, s, d)) {
    return (DIR_DEPTH - 1) * d->buckets;
  }
  while (e) {
    free++;
    e = next_dir(e, seg);
  }
  return free;
}

int
dir_bucket_length(Dir *b, int s, Vol *d)
{
  Dir *e   = b;
  int i    = 0;
  Dir *seg = d->dir_segment(s);
#ifdef LOOP_CHECK_MODE
  if (dir_bucket_loop_fix(b, s, d))
    return 1;
#endif
  while (e) {
    i++;
    if (i > 100) {
      return -1;
    }
    e = next_dir(e, seg);
  }
  return i;
}

int
check_dir(Vol *d)
{
  int i, s;
  Debug("cache_check_dir", "inside check dir");
  for (s = 0; s < d->segments; s++) {
    Dir *seg = d->dir_segment(s);
    for (i = 0; i < d->buckets; i++) {
      Dir *b = dir_bucket(i, seg);
      if (!(dir_bucket_length(b, s, d) >= 0)) {
        return 0;
      }
      if (!(!dir_next(b) || dir_offset(b))) {
        return 0;
      }
      if (!(dir_bucket_loop_check(b, seg))) {
        return 0;
      }
    }
  }
  return 1;
}

inline void
unlink_from_freelist(Dir *e, int s, Vol *d)
{
  Dir *seg = d->dir_segment(s);
  Dir *p   = dir_from_offset(dir_prev(e), seg);
  if (p) {
    dir_set_next(p, dir_next(e));
  } else {
    d->header->freelist[s] = dir_next(e);
  }
  Dir *n = dir_from_offset(dir_next(e), seg);
  if (n) {
    dir_set_prev(n, dir_prev(e));
  }
}

inline Dir *
dir_delete_entry(Dir *e, Dir *p, int s, Vol *d)
{
  Dir *seg         = d->dir_segment(s);
  int no           = dir_next(e);
  d->header->dirty = 1;
  if (p) {
    unsigned int fo = d->header->freelist[s];
    unsigned int eo = dir_to_offset(e, seg);
    dir_clear(e);
    dir_set_next(p, no);
    dir_set_next(e, fo);
    if (fo) {
      dir_set_prev(dir_from_offset(fo, seg), eo);
    }
    d->header->freelist[s] = eo;
  } else {
    Dir *n = next_dir(e, seg);
    if (n) {
      dir_assign(e, n);
      dir_delete_entry(n, e, s, d);
      return e;
    } else {
      dir_clear(e);
      return nullptr;
    }
  }
  return dir_from_offset(no, seg);
}

inline void
dir_clean_bucket(Dir *b, int s, Vol *vol)
{
  Dir *e = b, *p = nullptr;
  Dir *seg = vol->dir_segment(s);
#ifdef LOOP_CHECK_MODE
  int loop_count = 0;
#endif
  do {
#ifdef LOOP_CHECK_MODE
    loop_count++;
    if (loop_count > DIR_LOOP_THRESHOLD) {
      if (dir_bucket_loop_fix(b, s, vol))
        return;
    }
#endif
    if (!dir_valid(vol, e) || !dir_offset(e)) {
      if (is_debug_tag_set("dir_clean")) {
        Debug("dir_clean", "cleaning Vol:%s: %p tag %X boffset %" PRId64 " b %p p %p bucket len %d", vol->hash_text.get(), e,
              dir_tag(e), dir_offset(e), b, p, dir_bucket_length(b, s, vol));
      }
      if (dir_offset(e)) {
        CACHE_DEC_DIR_USED(vol->mutex);
      }
      e = dir_delete_entry(e, p, s, vol);
      continue;
    }
    p = e;
    e = next_dir(e, seg);
  } while (e);
}

void
dir_clean_segment(int s, Vol *d)
{
  Dir *seg = d->dir_segment(s);
  for (int64_t i = 0; i < d->buckets; i++) {
    dir_clean_bucket(dir_bucket(i, seg), s, d);
    ink_assert(!dir_next(dir_bucket(i, seg)) || dir_offset(dir_bucket(i, seg)));
  }
}

void
dir_clean_vol(Vol *d)
{
  for (int64_t i = 0; i < d->segments; i++) {
    dir_clean_segment(i, d);
  }
  CHECK_DIR(d);
}

void
dir_clear_range(off_t start, off_t end, Vol *vol)
{
  for (off_t i = 0; i < vol->buckets * DIR_DEPTH * vol->segments; i++) {
    Dir *e = dir_index(vol, i);
    if (!dir_token(e) && dir_offset(e) >= (int64_t)start && dir_offset(e) < (int64_t)end) {
      CACHE_DEC_DIR_USED(vol->mutex);
      dir_set_offset(e, 0); // delete
    }
  }
  dir_clean_vol(vol);
}

void
check_bucket_not_contains(Dir *b, Dir *e, Dir *seg)
{
  Dir *x = b;
  do {
    if (x == e) {
      break;
    }
    x = next_dir(x, seg);
  } while (x);
  ink_assert(!x);
}

void
freelist_clean(int s, Vol *vol)
{
  dir_clean_segment(s, vol);
  if (vol->header->freelist[s]) {
    return;
  }
  Warning("cache directory overflow on '%s' segment %d, purging...", vol->path, s);
  int n    = 0;
  Dir *seg = vol->dir_segment(s);
  for (int bi = 0; bi < vol->buckets; bi++) {
    Dir *b = dir_bucket(bi, seg);
    for (int l = 0; l < DIR_DEPTH; l++) {
      Dir *e = dir_bucket_row(b, l);
      if (dir_head(e) && !(n++ % 10)) {
        CACHE_DEC_DIR_USED(vol->mutex);
        dir_set_offset(e, 0); // delete
      }
    }
  }
  dir_clean_segment(s, vol);
}

inline Dir *
freelist_pop(int s, Vol *d)
{
  Dir *seg = d->dir_segment(s);
  Dir *e   = dir_from_offset(d->header->freelist[s], seg);
  if (!e) {
    freelist_clean(s, d);
    return nullptr;
  }
  d->header->freelist[s] = dir_next(e);
  // if the freelist if bad, punt.
  if (dir_offset(e)) {
    dir_init_segment(s, d);
    return nullptr;
  }
  Dir *h = dir_from_offset(d->header->freelist[s], seg);
  if (h) {
    dir_set_prev(h, 0);
  }
  return e;
}

int
dir_segment_accounted(int s, Vol *d, int offby, int *f, int *u, int *et, int *v, int *av, int *as)
{
  int free = dir_freelist_length(d, s);
  int used = 0, empty = 0;
  int valid = 0, agg_valid = 0;
  int64_t agg_size = 0;
  Dir *seg         = d->dir_segment(s);
  for (int bi = 0; bi < d->buckets; bi++) {
    Dir *b = dir_bucket(bi, seg);
    Dir *e = b;
    while (e) {
      if (!dir_offset(e)) {
        ink_assert(e == b);
        empty++;
      } else {
        used++;
        if (dir_valid(d, e)) {
          valid++;
        }
        if (dir_agg_valid(d, e)) {
          agg_valid++;
        }
        agg_size += dir_approx_size(e);
      }
      e = next_dir(e, seg);
      if (!e) {
        break;
      }
    }
  }
  if (f) {
    *f = free;
  }
  if (u) {
    *u = used;
  }
  if (et) {
    *et = empty;
  }
  if (v) {
    *v = valid;
  }
  if (av) {
    *av = agg_valid;
  }
  if (as) {
    *as = used ? (int)(agg_size / used) : 0;
  }
  ink_assert(d->buckets * DIR_DEPTH - (free + used + empty) <= offby);
  return d->buckets * DIR_DEPTH - (free + used + empty) <= offby;
}

void
dir_free_entry(Dir *e, int s, Vol *d)
{
  Dir *seg        = d->dir_segment(s);
  unsigned int fo = d->header->freelist[s];
  unsigned int eo = dir_to_offset(e, seg);
  dir_set_next(e, fo);
  if (fo) {
    dir_set_prev(dir_from_offset(fo, seg), eo);
  }
  d->header->freelist[s] = eo;
}

int
dir_probe(const CacheKey *key, Vol *d, Dir *result, Dir **last_collision)
{
  ink_assert(d->mutex->thread_holding == this_ethread());
  int s    = key->slice32(0) % d->segments;
  int b    = key->slice32(1) % d->buckets;
  Dir *seg = d->dir_segment(s);
  Dir *e = nullptr, *p = nullptr, *collision = *last_collision;
  Vol *vol = d;
  CHECK_DIR(d);
#ifdef LOOP_CHECK_MODE
  if (dir_bucket_loop_fix(dir_bucket(b, seg), s, d))
    return 0;
#endif
Lagain:
  e = dir_bucket(b, seg);
  if (dir_offset(e)) {
    do {
      if (dir_compare_tag(e, key)) {
        ink_assert(dir_offset(e));
        // Bug: 51680. Need to check collision before checking
        // dir_valid(). In case of a collision, if !dir_valid(), we
        // don't want to call dir_delete_entry.
        if (collision) {
          if (collision == e) {
            collision = nullptr;
            // increment collision stat
            // Note: dir_probe could be called multiple times
            // for the same document and so the collision stat
            // may not accurately reflect the number of documents
            // having the same first_key
            DDebug("cache_stats", "Incrementing dir collisions");
            CACHE_INC_DIR_COLLISIONS(d->mutex);
          }
          goto Lcont;
        }
        if (dir_valid(d, e)) {
          DDebug("dir_probe_hit", "found %X %X vol %d bucket %d boffset %" PRId64 "", key->slice32(0), key->slice32(1), d->fd, b,
                 dir_offset(e));
          dir_assign(result, e);
          *last_collision = e;
          ink_assert(dir_offset(e) * CACHE_BLOCK_SIZE < d->len);
          return 1;
        } else { // delete the invalid entry
          CACHE_DEC_DIR_USED(d->mutex);
          e = dir_delete_entry(e, p, s, d);
          continue;
        }
      } else {
        DDebug("dir_probe_tag", "tag mismatch %p %X vs expected %X", e, dir_tag(e), key->slice32(3));
      }
    Lcont:
      p = e;
      e = next_dir(e, seg);
    } while (e);
  }
  if (collision) { // last collision no longer in the list, retry
    DDebug("cache_stats", "Incrementing dir collisions");
    CACHE_INC_DIR_COLLISIONS(d->mutex);
    collision = nullptr;
    goto Lagain;
  }
  DDebug("dir_probe_miss", "missed %X %X on vol %d bucket %d at %p", key->slice32(0), key->slice32(1), d->fd, b, seg);
  CHECK_DIR(d);
  return 0;
}

int
dir_insert(const CacheKey *key, Vol *d, Dir *to_part)
{
  ink_assert(d->mutex->thread_holding == this_ethread());
  int s  = key->slice32(0) % d->segments, l;
  int bi = key->slice32(1) % d->buckets;
  ink_assert(dir_approx_size(to_part) <= MAX_FRAG_SIZE + sizeof(Doc));
  Dir *seg = d->dir_segment(s);
  Dir *e   = nullptr;
  Dir *b   = dir_bucket(bi, seg);
  Vol *vol = d;
#if defined(DEBUG) && defined(DO_CHECK_DIR_FAST)
  unsigned int t = DIR_MASK_TAG(key->slice32(2));
  Dir *col       = b;
  while (col) {
    ink_assert((dir_tag(col) != t) || (dir_offset(col) != dir_offset(to_part)));
    col = next_dir(col, seg);
  }
#endif
  CHECK_DIR(d);

Lagain:
  // get from this row first
  e = b;
  if (dir_is_empty(e)) {
    goto Lfill;
  }
  for (l = 1; l < DIR_DEPTH; l++) {
    e = dir_bucket_row(b, l);
    if (dir_is_empty(e)) {
      unlink_from_freelist(e, s, d);
      goto Llink;
    }
  }
  // get one from the freelist
  e = freelist_pop(s, d);
  if (!e) {
    goto Lagain;
  }
Llink:
  dir_set_next(e, dir_next(b));
  dir_set_next(b, dir_to_offset(e, seg));
Lfill:
  dir_assign_data(e, to_part);
  dir_set_tag(e, key->slice32(2));
  ink_assert(d->vol_offset(e) < (d->skip + d->len));
  DDebug("dir_insert", "insert %p %X into vol %d bucket %d at %p tag %X %X boffset %" PRId64 "", e, key->slice32(0), d->fd, bi, e,
         key->slice32(1), dir_tag(e), dir_offset(e));
  CHECK_DIR(d);
  d->header->dirty = 1;
  CACHE_INC_DIR_USED(d->mutex);
  return 1;
}

int
dir_overwrite(const CacheKey *key, Vol *d, Dir *dir, Dir *overwrite, bool must_overwrite)
{
  ink_assert(d->mutex->thread_holding == this_ethread());
  int s          = key->slice32(0) % d->segments, l;
  int bi         = key->slice32(1) % d->buckets;
  Dir *seg       = d->dir_segment(s);
  Dir *e         = nullptr;
  Dir *b         = dir_bucket(bi, seg);
  unsigned int t = DIR_MASK_TAG(key->slice32(2));
  int res        = 1;
#ifdef LOOP_CHECK_MODE
  int loop_count     = 0;
  bool loop_possible = true;
#endif
  Vol *vol = d;
  CHECK_DIR(d);

  ink_assert((unsigned int)dir_approx_size(dir) <= (unsigned int)(MAX_FRAG_SIZE + sizeof(Doc))); // XXX - size should be unsigned
Lagain:
  // find entry to overwrite
  e = b;
  if (dir_offset(e)) {
    do {
#ifdef LOOP_CHECK_MODE
      loop_count++;
      if (loop_count > DIR_LOOP_THRESHOLD && loop_possible) {
        if (dir_bucket_loop_fix(b, s, d)) {
          loop_possible = false;
          goto Lagain;
        }
      }
#endif
      if (dir_tag(e) == t && dir_offset(e) == dir_offset(overwrite)) {
        goto Lfill;
      }
      e = next_dir(e, seg);
    } while (e);
  }
  if (must_overwrite) {
    return 0;
  }
  res = 0;
  // get from this row first
  e = b;
  if (dir_is_empty(e)) {
    CACHE_INC_DIR_USED(d->mutex);
    goto Lfill;
  }
  for (l = 1; l < DIR_DEPTH; l++) {
    e = dir_bucket_row(b, l);
    if (dir_is_empty(e)) {
      unlink_from_freelist(e, s, d);
      goto Llink;
    }
  }
  // get one from the freelist
  e = freelist_pop(s, d);
  if (!e) {
    goto Lagain;
  }
Llink:
  CACHE_INC_DIR_USED(d->mutex);
  dir_set_next(e, dir_next(b));
  dir_set_next(b, dir_to_offset(e, seg));
Lfill:
  dir_assign_data(e, dir);
  dir_set_tag(e, t);
  ink_assert(d->vol_offset(e) < d->skip + d->len);
  DDebug("dir_overwrite", "overwrite %p %X into vol %d bucket %d at %p tag %X %X boffset %" PRId64 "", e, key->slice32(0), d->fd,
         bi, e, t, dir_tag(e), dir_offset(e));
  CHECK_DIR(d);
  d->header->dirty = 1;
  return res;
}

int
dir_delete(const CacheKey *key, Vol *d, Dir *del)
{
  ink_assert(d->mutex->thread_holding == this_ethread());
  int s    = key->slice32(0) % d->segments;
  int b    = key->slice32(1) % d->buckets;
  Dir *seg = d->dir_segment(s);
  Dir *e = nullptr, *p = nullptr;
#ifdef LOOP_CHECK_MODE
  int loop_count = 0;
#endif
  Vol *vol = d;
  CHECK_DIR(d);

  e = dir_bucket(b, seg);
  if (dir_offset(e)) {
    do {
#ifdef LOOP_CHECK_MODE
      loop_count++;
      if (loop_count > DIR_LOOP_THRESHOLD) {
        if (dir_bucket_loop_fix(dir_bucket(b, seg), s, d))
          return 0;
      }
#endif
      if (dir_compare_tag(e, key) && dir_offset(e) == dir_offset(del)) {
        CACHE_DEC_DIR_USED(d->mutex);
        dir_delete_entry(e, p, s, d);
        CHECK_DIR(d);
        return 1;
      }
      p = e;
      e = next_dir(e, seg);
    } while (e);
  }
  CHECK_DIR(d);
  return 0;
}

// Lookaside Cache

int
dir_lookaside_probe(const CacheKey *key, Vol *d, Dir *result, EvacuationBlock **eblock)
{
  ink_assert(d->mutex->thread_holding == this_ethread());
  int i              = key->slice32(3) % LOOKASIDE_SIZE;
  EvacuationBlock *b = d->lookaside[i].head;
  while (b) {
    if (b->evac_frags.key == *key) {
      if (dir_valid(d, &b->new_dir)) {
        *result = b->new_dir;
        DDebug("dir_lookaside", "probe %X success", key->slice32(0));
        if (eblock) {
          *eblock = b;
        }
        return 1;
      }
    }
    b = b->link.next;
  }
  DDebug("dir_lookaside", "probe %X failed", key->slice32(0));
  return 0;
}

int
dir_lookaside_insert(EvacuationBlock *eblock, Vol *d, Dir *to)
{
  CacheKey *key = &eblock->evac_frags.earliest_key;
  DDebug("dir_lookaside", "insert %X %X, offset %d phase %d", key->slice32(0), key->slice32(1), (int)dir_offset(to),
         (int)dir_phase(to));
  ink_assert(d->mutex->thread_holding == this_ethread());
  int i                      = key->slice32(3) % LOOKASIDE_SIZE;
  EvacuationBlock *b         = new_EvacuationBlock(d->mutex->thread_holding);
  b->evac_frags.key          = *key;
  b->evac_frags.earliest_key = *key;
  b->earliest_evacuator      = eblock->earliest_evacuator;
  ink_assert(b->earliest_evacuator);
  b->dir     = eblock->dir;
  b->new_dir = *to;
  d->lookaside[i].push(b);
  return 1;
}

int
dir_lookaside_fixup(const CacheKey *key, Vol *d)
{
  ink_assert(d->mutex->thread_holding == this_ethread());
  int i              = key->slice32(3) % LOOKASIDE_SIZE;
  EvacuationBlock *b = d->lookaside[i].head;
  while (b) {
    if (b->evac_frags.key == *key) {
      int res = dir_overwrite(key, d, &b->new_dir, &b->dir, false);
      DDebug("dir_lookaside", "fixup %X %X offset %" PRId64 " phase %d %d", key->slice32(0), key->slice32(1),
             dir_offset(&b->new_dir), dir_phase(&b->new_dir), res);
      int64_t o = dir_offset(&b->dir), n = dir_offset(&b->new_dir);
      d->ram_cache->fixup(key, (uint32_t)(o >> 32), (uint32_t)o, (uint32_t)(n >> 32), (uint32_t)n);
      d->lookaside[i].remove(b);
      free_EvacuationBlock(b, d->mutex->thread_holding);
      return res;
    }
    b = b->link.next;
  }
  DDebug("dir_lookaside", "fixup %X %X failed", key->slice32(0), key->slice32(1));
  return 0;
}

void
dir_lookaside_cleanup(Vol *d)
{
  ink_assert(d->mutex->thread_holding == this_ethread());
  for (auto &i : d->lookaside) {
    EvacuationBlock *b = i.head;
    while (b) {
      if (!dir_valid(d, &b->new_dir)) {
        EvacuationBlock *nb = b->link.next;
        DDebug("dir_lookaside", "cleanup %X %X cleaned up", b->evac_frags.earliest_key.slice32(0),
               b->evac_frags.earliest_key.slice32(1));
        i.remove(b);
        free_CacheVC(b->earliest_evacuator);
        free_EvacuationBlock(b, d->mutex->thread_holding);
        b = nb;
        goto Lagain;
      }
      b = b->link.next;
    Lagain:;
    }
  }
}

void
dir_lookaside_remove(const CacheKey *key, Vol *d)
{
  ink_assert(d->mutex->thread_holding == this_ethread());
  int i              = key->slice32(3) % LOOKASIDE_SIZE;
  EvacuationBlock *b = d->lookaside[i].head;
  while (b) {
    if (b->evac_frags.key == *key) {
      DDebug("dir_lookaside", "remove %X %X offset %" PRId64 " phase %d", key->slice32(0), key->slice32(1), dir_offset(&b->new_dir),
             dir_phase(&b->new_dir));
      d->lookaside[i].remove(b);
      free_EvacuationBlock(b, d->mutex->thread_holding);
      return;
    }
    b = b->link.next;
  }
  DDebug("dir_lookaside", "remove %X %X failed", key->slice32(0), key->slice32(1));
  return;
}

// Cache Sync
//

void
dir_sync_init()
{
  cacheDirSync          = new CacheSync;
  cacheDirSync->trigger = eventProcessor.schedule_in(cacheDirSync, HRTIME_SECONDS(cache_config_dir_sync_frequency));
}

void
CacheSync::aio_write(int fd, char *b, int n, off_t o)
{
  io.aiocb.aio_fildes = fd;
  io.aiocb.aio_offset = o;
  io.aiocb.aio_nbytes = n;
  io.aiocb.aio_buf    = b;
  io.action           = this;
  io.thread           = AIO_CALLBACK_THREAD_ANY;
  ink_assert(ink_aio_write(&io) >= 0);
}

uint64_t
dir_entries_used(Vol *d)
{
  uint64_t full  = 0;
  uint64_t sfull = 0;
  for (int s = 0; s < d->segments; full += sfull, s++) {
    Dir *seg = d->dir_segment(s);
    sfull    = 0;
    for (int b = 0; b < d->buckets; b++) {
      Dir *e = dir_bucket(b, seg);
      if (dir_bucket_loop_fix(e, s, d)) {
        sfull = 0;
        break;
      }
      while (e) {
        if (dir_offset(e)) {
          sfull++;
        }
        e = next_dir(e, seg);
        if (!e) {
          break;
        }
      }
    }
  }
  return full;
}

/*
 * this function flushes the cache meta data to disk when
 * the cache is shutdown. Must *NOT* be used during regular
 * operation.
 */

void
sync_cache_dir_on_shutdown()
{
  Debug("cache_dir_sync", "sync started");
  char *buf     = nullptr;
  size_t buflen = 0;
  bool buf_huge = false;

  EThread *t = (EThread *)0xdeadbeef;
  for (int i = 0; i < gnvol; i++) {
    // the process is going down, do a blocking call
    // dont release the volume's lock, there could
    // be another aggWrite in progress
    MUTEX_TAKE_LOCK(gvol[i]->mutex, t);
    Vol *d = gvol[i];

    if (DISK_BAD(d->disk)) {
      Debug("cache_dir_sync", "Dir %s: ignoring -- bad disk", d->hash_text.get());
      continue;
    }
    size_t dirlen = d->dirlen();
    ink_assert(dirlen > 0); // make clang happy - if not > 0 the vol is seriously messed up
    if (!d->header->dirty && !d->dir_sync_in_progress) {
      Debug("cache_dir_sync", "Dir %s: ignoring -- not dirty", d->hash_text.get());
      continue;
    }
    // recompute hit_evacuate_window
    d->hit_evacuate_window = (d->data_blocks * cache_config_hit_evacuate_percent) / 100;

    // check if we have data in the agg buffer
    // dont worry about the cachevc s in the agg queue
    // directories have not been inserted for these writes
    if (d->agg_buf_pos) {
      Debug("cache_dir_sync", "Dir %s: flushing agg buffer first", d->hash_text.get());

      // set write limit
      d->header->agg_pos = d->header->write_pos + d->agg_buf_pos;

      int r = pwrite(d->fd, d->agg_buffer, d->agg_buf_pos, d->header->write_pos);
      if (r != d->agg_buf_pos) {
        ink_assert(!"flushing agg buffer failed");
        continue;
      }
      d->header->last_write_pos = d->header->write_pos;
      d->header->write_pos += d->agg_buf_pos;
      ink_assert(d->header->write_pos == d->header->agg_pos);
      d->agg_buf_pos = 0;
      d->header->write_serial++;
    }

    if (buflen < dirlen) {
      if (buf) {
        if (buf_huge) {
          ats_free_hugepage(buf, buflen);
        } else {
          ats_memalign_free(buf);
        }
        buf = nullptr;
      }
      buflen = dirlen;
      if (ats_hugepage_enabled()) {
        buf      = (char *)ats_alloc_hugepage(buflen);
        buf_huge = true;
      }
      if (buf == nullptr) {
        buf      = (char *)ats_memalign(ats_pagesize(), buflen);
        buf_huge = false;
      }
    }

    if (!d->dir_sync_in_progress) {
      d->header->sync_serial++;
    } else {
      Debug("cache_dir_sync", "Periodic dir sync in progress -- overwriting");
    }
    d->footer->sync_serial = d->header->sync_serial;

    CHECK_DIR(d);
    memcpy(buf, d->raw_dir, dirlen);
    size_t B    = d->header->sync_serial & 1;
    off_t start = d->skip + (B ? dirlen : 0);
    B           = pwrite(d->fd, buf, dirlen, start);
    ink_assert(B == dirlen);
    Debug("cache_dir_sync", "done syncing dir for vol %s", d->hash_text.get());
  }
  Debug("cache_dir_sync", "sync done");
  if (buf) {
    if (buf_huge) {
      ats_free_hugepage(buf, buflen);
    } else {
      ats_memalign_free(buf);
    }
    buf = nullptr;
  }
}

int
CacheSync::mainEvent(int event, Event *e)
{
  if (trigger) {
    trigger->cancel_action();
    trigger = nullptr;
  }

Lrestart:
  if (vol_idx >= gnvol) {
    vol_idx = 0;
    if (buf) {
      if (buf_huge) {
        ats_free_hugepage(buf, buflen);
      } else {
        ats_memalign_free(buf);
      }
      buflen   = 0;
      buf      = nullptr;
      buf_huge = false;
    }
    Debug("cache_dir_sync", "sync done");
    if (event == EVENT_INTERVAL) {
      trigger = e->ethread->schedule_in(this, HRTIME_SECONDS(cache_config_dir_sync_frequency));
    } else {
      trigger = eventProcessor.schedule_in(this, HRTIME_SECONDS(cache_config_dir_sync_frequency));
    }
    return EVENT_CONT;
  }

  Vol *vol = gvol[vol_idx]; // must be named "vol" to make STAT macros work.

  if (event == AIO_EVENT_DONE) {
    // AIO Thread
    if (io.aio_result != (int64_t)io.aiocb.aio_nbytes) {
      Warning("vol write error during directory sync '%s'", gvol[vol_idx]->hash_text.get());
      event = EVENT_NONE;
      goto Ldone;
    }
    CACHE_SUM_DYN_STAT(cache_directory_sync_bytes_stat, io.aio_result);

    trigger = eventProcessor.schedule_in(this, SYNC_DELAY);
    return EVENT_CONT;
  }
  {
    CACHE_TRY_LOCK(lock, gvol[vol_idx]->mutex, mutex->thread_holding);
    if (!lock.is_locked()) {
      trigger = eventProcessor.schedule_in(this, HRTIME_MSECONDS(cache_config_mutex_retry_delay));
      return EVENT_CONT;
    }

    if (!vol->dir_sync_in_progress) {
      start_time = Thread::get_hrtime();
    }

    // recompute hit_evacuate_window
    vol->hit_evacuate_window = (vol->data_blocks * cache_config_hit_evacuate_percent) / 100;

    if (DISK_BAD(vol->disk)) {
      goto Ldone;
    }

    int headerlen = ROUND_TO_STORE_BLOCK(sizeof(VolHeaderFooter));
    size_t dirlen = vol->dirlen();
    if (!writepos) {
      // start
      Debug("cache_dir_sync", "sync started");
      /* Don't sync the directory to disk if its not dirty. Syncing the
         clean directory to disk is also the cause of INKqa07151. Increasing
         the serial serial causes the cache to recover more data
         than necessary.
         The dirty bit it set in dir_insert, dir_overwrite and dir_delete_entry
       */
      if (!vol->header->dirty) {
        Debug("cache_dir_sync", "Dir %s not dirty", vol->hash_text.get());
        goto Ldone;
      }
      if (vol->is_io_in_progress() || vol->agg_buf_pos) {
        Debug("cache_dir_sync", "Dir %s: waiting for agg buffer", vol->hash_text.get());
        vol->dir_sync_waiting = true;
        if (!vol->is_io_in_progress()) {
          vol->aggWrite(EVENT_IMMEDIATE, nullptr);
        }
        return EVENT_CONT;
      }
      Debug("cache_dir_sync", "pos: %" PRIu64 " Dir %s dirty...syncing to disk", vol->header->write_pos, vol->hash_text.get());
      vol->header->dirty = 0;
      if (buflen < dirlen) {
        if (buf) {
          if (buf_huge) {
            ats_free_hugepage(buf, buflen);
          } else {
            ats_memalign_free(buf);
          }
          buf = nullptr;
        }
        buflen = dirlen;
        if (ats_hugepage_enabled()) {
          buf      = (char *)ats_alloc_hugepage(buflen);
          buf_huge = true;
        }
        if (buf == nullptr) {
          buf      = (char *)ats_memalign(ats_pagesize(), buflen);
          buf_huge = false;
        }
      }
      vol->header->sync_serial++;
      vol->footer->sync_serial = vol->header->sync_serial;
      CHECK_DIR(d);
      memcpy(buf, vol->raw_dir, dirlen);
      vol->dir_sync_in_progress = true;
    }
    size_t B    = vol->header->sync_serial & 1;
    off_t start = vol->skip + (B ? dirlen : 0);

    if (!writepos) {
      // write header
      aio_write(vol->fd, buf + writepos, headerlen, start + writepos);
      writepos += headerlen;
    } else if (writepos < (off_t)dirlen - headerlen) {
      // write part of body
      int l = SYNC_MAX_WRITE;
      if (writepos + l > (off_t)dirlen - headerlen) {
        l = dirlen - headerlen - writepos;
      }
      aio_write(vol->fd, buf + writepos, l, start + writepos);
      writepos += l;
    } else if (writepos < (off_t)dirlen) {
      ink_assert(writepos == (off_t)dirlen - headerlen);
      // write footer
      aio_write(vol->fd, buf + writepos, headerlen, start + writepos);
      writepos += headerlen;
    } else {
      vol->dir_sync_in_progress = false;
      CACHE_INCREMENT_DYN_STAT(cache_directory_sync_count_stat);
      CACHE_SUM_DYN_STAT(cache_directory_sync_time_stat, Thread::get_hrtime() - start_time);
      start_time = 0;
      goto Ldone;
    }
    return EVENT_CONT;
  }
Ldone:
  // done
  writepos = 0;
  ++vol_idx;
  goto Lrestart;
}

namespace
{
int
compare_ushort(void const *a, void const *b)
{
  return *static_cast<unsigned short const *>(a) - *static_cast<unsigned short const *>(b);
}
} // namespace

//
// Check
//

int Vol::dir_check(bool /* fix ATS_UNUSED */) // TODO: we should eliminate this parameter ?
{
  static int const SEGMENT_HISTOGRAM_WIDTH = 16;
  int hist[SEGMENT_HISTOGRAM_WIDTH + 1]    = {0};
  unsigned short chain_tag[MAX_ENTRIES_PER_SEGMENT];
  int32_t chain_mark[MAX_ENTRIES_PER_SEGMENT];
  uint64_t total_buckets = buckets * segments;
  uint64_t total_entries = total_buckets * DIR_DEPTH;
  int frag_demographics[1 << DIR_SIZE_WIDTH][DIR_BLOCK_SIZES];

  int j;
  int stale = 0, in_use = 0, empty = 0;
  int free = 0, head = 0, buckets_in_use = 0;

  int max_chain_length = 0;
  int64_t bytes_in_use = 0;

  ink_zero(frag_demographics);

  printf("Stripe '[%s]'\n", hash_text.get());
  printf("  Directory Bytes: %" PRIu64 "\n", total_buckets * SIZEOF_DIR);
  printf("  Segments:  %d\n", segments);
  printf("  Buckets per segment:   %" PRIu64 "\n", buckets);
  printf("  Entries:   %" PRIu64 "\n", total_entries);

  for (int s = 0; s < segments; s++) {
    Dir *seg               = this->dir_segment(s);
    int seg_chain_max      = 0;
    int seg_empty          = 0;
    int seg_in_use         = 0;
    int seg_stale          = 0;
    int seg_bytes_in_use   = 0;
    int seg_dups           = 0;
    int seg_buckets_in_use = 0;

    ink_zero(chain_tag);
    memset(chain_mark, -1, sizeof(chain_mark));

    for (int b = 0; b < buckets; b++) {
      Dir *root = dir_bucket(b, seg);
      int h     = 0; // chain length starting in this bucket

      // Walk the chain starting in this bucket
      int chain_idx = 0;
      int mark      = 0;
      ++seg_buckets_in_use;
      for (Dir *e = root; e; e = next_dir(e, seg)) {
        if (!dir_offset(e)) {
          ++seg_empty;
          --seg_buckets_in_use;
          // this should only happen on the first dir in a bucket
          ink_assert(nullptr == next_dir(e, seg));
          break;
        } else {
          int e_idx = e - seg;
          ++h;
          chain_tag[chain_idx++] = dir_tag(e);
          if (chain_mark[e_idx] == mark) {
            printf("    - Cycle of length %d detected for bucket %d\n", h, b);
          } else if (chain_mark[e_idx] >= 0) {
            printf("    - Entry %d is in chain %d and %d", e_idx, chain_mark[e_idx], mark);
          } else {
            chain_mark[e_idx] = mark;
          }

          if (!dir_valid(this, e)) {
            ++seg_stale;
          } else {
            uint64_t size = dir_approx_size(e);
            if (dir_head(e)) {
              ++head;
            }
            ++seg_in_use;
            seg_bytes_in_use += size;
            ++frag_demographics[dir_size(e)][dir_big(e)];
          }
        }
      }

      // Check for duplicates (identical tags in the same bucket).
      if (h > 1) {
        unsigned short last;
        qsort(chain_tag, h, sizeof(chain_tag[0]), &compare_ushort);
        last = chain_tag[0];
        for (int k = 1; k < h; ++k) {
          if (last == chain_tag[k]) {
            ++seg_dups;
          }
          last = chain_tag[k];
        }
      }

      ++hist[std::min(h, SEGMENT_HISTOGRAM_WIDTH)];
      seg_chain_max = std::max(seg_chain_max, h);
    }
    int fl_size = dir_freelist_length(this, s);
    in_use += seg_in_use;
    empty += seg_empty;
    stale += seg_stale;
    free += fl_size;
    buckets_in_use += seg_buckets_in_use;
    max_chain_length = std::max(max_chain_length, seg_chain_max);
    bytes_in_use += seg_bytes_in_use;

    printf("  - Segment-%d | Entries: used=%d stale=%d free=%d disk-bytes=%d Buckets: used=%d empty=%d max=%d avg=%.2f dups=%d\n",
           s, seg_in_use, seg_stale, fl_size, seg_bytes_in_use, seg_buckets_in_use, seg_empty, seg_chain_max,
           seg_buckets_in_use ? static_cast<float>(seg_in_use + seg_stale) / seg_buckets_in_use : 0.0, seg_dups);
  }

  printf("  - Stripe | Entries: in-use=%d stale=%d free=%d Buckets: empty=%d max=%d avg=%.2f\n", in_use, stale, free, empty,
         max_chain_length, buckets_in_use ? static_cast<float>(in_use + stale) / buckets_in_use : 0);

  printf("    Chain lengths:  ");
  for (j = 0; j < SEGMENT_HISTOGRAM_WIDTH; ++j) {
    printf(" %d=%d ", j, hist[j]);
  }
  printf(" %d>=%d\n", SEGMENT_HISTOGRAM_WIDTH, hist[SEGMENT_HISTOGRAM_WIDTH]);

  char tt[256];
  printf("    Total Size:      %" PRIu64 "\n", static_cast<uint64_t>(len));
  printf("    Bytes in Use:    %" PRIu64 " [%0.2f%%]\n", bytes_in_use, 100.0 * (static_cast<float>(bytes_in_use) / len));
  printf("    Objects:         %d\n", head);
  printf("    Average Size:    %" PRIu64 "\n", head ? (bytes_in_use / head) : 0);
  printf("    Average Frags:   %.2f\n", head ? static_cast<float>(in_use) / head : 0);
  printf("    Write Position:  %" PRIu64 "\n", header->write_pos - start);
  printf("    Wrap Count:      %d\n", header->cycle);
  printf("    Phase:           %s\n", header->phase ? "true" : "false");
  ink_ctime_r(&header->create_time, tt);
  tt[strlen(tt) - 1] = 0;
  printf("    Sync Serial:     %u\n", header->sync_serial);
  printf("    Write Serial:    %u\n", header->write_serial);
  printf("    Create Time:     %s\n", tt);
  printf("\n");
  printf("  Fragment size demographics\n");
  for (int b = 0; b < DIR_BLOCK_SIZES; ++b) {
    int block_size = DIR_BLOCK_SIZE(b);
    int s          = 0;
    while (s < 1 << DIR_SIZE_WIDTH) {
      for (int j = 0; j < 8; ++j, ++s) {
        // The size markings are redundant. Low values (less than DIR_SHIFT_WIDTH) for larger
        // base block sizes should never be used. Such entries should use the next smaller base block size.
        if (b > 0 && s < 1 << DIR_BLOCK_SHIFT(1)) {
          ink_assert(frag_demographics[s][b] == 0);
          continue;
        }
        printf(" %8d[%2d:%1d]:%06d", (s + 1) * block_size, s, b, frag_demographics[s][b]);
      }
      printf("\n");
    }
  }
  printf("\n");

  return 0;
}

//
// Static Tables
//

// permutation table
uint8_t CacheKey_next_table[256] = {
  21,  53,  167, 51,  255, 126, 241, 151, 115, 66,  155, 174, 226, 215, 80,  188, 12,  95,  8,   24,  162, 201, 46,  104, 79,  172,
  39,  68,  56,  144, 142, 217, 101, 62,  14,  108, 120, 90,  61,  47,  132, 199, 110, 166, 83,  125, 57,  65,  19,  130, 148, 116,
  228, 189, 170, 1,   71,  0,   252, 184, 168, 177, 88,  229, 242, 237, 183, 55,  13,  212, 240, 81,  211, 74,  195, 205, 147, 93,
  30,  87,  86,  63,  135, 102, 233, 106, 118, 163, 107, 10,  243, 136, 160, 119, 43,  161, 206, 141, 203, 78,  175, 36,  37,  140,
  224, 197, 185, 196, 248, 84,  122, 73,  152, 157, 18,  225, 219, 145, 45,  2,   171, 249, 173, 32,  143, 137, 69,  41,  35,  89,
  33,  98,  179, 214, 114, 231, 251, 123, 180, 194, 29,  3,   178, 31,  192, 164, 15,  234, 26,  230, 91,  156, 5,   16,  23,  244,
  58,  50,  4,   67,  134, 165, 60,  235, 250, 7,   138, 216, 49,  139, 191, 154, 11,  52,  239, 59,  111, 245, 9,   64,  25,  129,
  247, 232, 190, 246, 109, 22,  112, 210, 221, 181, 92,  169, 48,  100, 193, 77,  103, 133, 70,  220, 207, 223, 176, 204, 76,  186,
  200, 208, 158, 182, 227, 222, 131, 38,  187, 238, 6,   34,  253, 128, 146, 44,  94,  127, 105, 153, 113, 20,  27,  124, 159, 17,
  72,  218, 96,  149, 213, 42,  28,  254, 202, 40,  117, 82,  97,  209, 54,  236, 121, 75,  85,  150, 99,  198,
};

// permutation table
uint8_t CacheKey_prev_table[256] = {
  57,  55,  119, 141, 158, 152, 218, 165, 18,  178, 89,  172, 16,  68,  34,  146, 153, 233, 114, 48,  229, 0,   187, 154, 19,  180,
  148, 230, 240, 140, 78,  143, 123, 130, 219, 128, 101, 102, 215, 26,  243, 127, 239, 94,  223, 118, 22,  39,  194, 168, 157, 3,
  173, 1,   248, 67,  28,  46,  156, 175, 162, 38,  33,  81,  179, 47,  9,   159, 27,  126, 200, 56,  234, 111, 73,  251, 206, 197,
  99,  24,  14,  71,  245, 44,  109, 252, 80,  79,  62,  129, 37,  150, 192, 77,  224, 17,  236, 246, 131, 254, 195, 32,  83,  198,
  23,  226, 85,  88,  35,  186, 42,  176, 188, 228, 134, 8,   51,  244, 86,  93,  36,  250, 110, 137, 231, 45,  5,   225, 221, 181,
  49,  214, 40,  199, 160, 82,  91,  125, 166, 169, 103, 97,  30,  124, 29,  117, 222, 76,  50,  237, 253, 7,   112, 227, 171, 10,
  151, 113, 210, 232, 92,  95,  20,  87,  145, 161, 43,  2,   60,  193, 54,  120, 25,  122, 11,  100, 204, 61,  142, 132, 138, 191,
  211, 66,  59,  106, 207, 216, 15,  53,  184, 170, 144, 196, 139, 74,  107, 105, 255, 41,  208, 21,  242, 98,  205, 75,  96,  202,
  209, 247, 189, 72,  69,  238, 133, 13,  167, 31,  235, 116, 201, 190, 213, 203, 104, 115, 12,  212, 52,  63,  149, 135, 183, 84,
  147, 163, 249, 65,  217, 174, 70,  6,   64,  90,  155, 177, 185, 182, 108, 121, 164, 136, 58,  220, 241, 4,
};

//
// Regression
//
unsigned int regress_rand_seed = 0;
void
regress_rand_init(unsigned int i)
{
  regress_rand_seed = i;
}

static void
regress_rand_CacheKey(const CacheKey *key)
{
  unsigned int *x = (unsigned int *)key;
  for (int i = 0; i < 4; i++) {
    x[i] = next_rand(&regress_rand_seed);
  }
}

void
dir_corrupt_bucket(Dir *b, int s, Vol *d)
{
  // coverity[dont_call]
  int l    = ((int)(dir_bucket_length(b, s, d) * drand48()));
  Dir *e   = b;
  Dir *seg = d->dir_segment(s);
  for (int i = 0; i < l; i++) {
    ink_release_assert(e);
    e = next_dir(e, seg);
  }
  ink_release_assert(e);
  dir_set_next(e, dir_to_offset(e, seg));
}

EXCLUSIVE_REGRESSION_TEST(Cache_dir)(RegressionTest *t, int /* atype ATS_UNUSED */, int *status)
{
  ink_hrtime ttime;
  int ret = REGRESSION_TEST_PASSED;

  if ((CacheProcessor::IsCacheEnabled() != CACHE_INITIALIZED) || gnvol < 1) {
    rprintf(t, "cache not ready/configured");
    *status = REGRESSION_TEST_FAILED;
    return;
  }
  Vol *d          = gvol[0];
  EThread *thread = this_ethread();
  MUTEX_TRY_LOCK(lock, d->mutex, thread);
  ink_release_assert(lock.is_locked());
  rprintf(t, "clearing vol 0\n", free);
  vol_dir_clear(d);

  // coverity[var_decl]
  Dir dir;
  dir_clear(&dir);
  dir_set_phase(&dir, 0);
  dir_set_head(&dir, true);
  dir_set_offset(&dir, 1);

  d->header->agg_pos = d->header->write_pos += 1024;

  CacheKey key;
  rand_CacheKey(&key, thread->mutex);

  int s    = key.slice32(0) % d->segments, i, j;
  Dir *seg = d->dir_segment(s);

  // test insert
  rprintf(t, "insert test\n", free);
  int inserted = 0;
  int free     = dir_freelist_length(d, s);
  int n        = free;
  rprintf(t, "free: %d\n", free);
  while (n--) {
    if (!dir_insert(&key, d, &dir)) {
      break;
    }
    inserted++;
  }
  rprintf(t, "inserted: %d\n", inserted);
  if ((unsigned int)(inserted - free) > 1) {
    ret = REGRESSION_TEST_FAILED;
  }

  // test delete
  rprintf(t, "delete test\n");
  for (i = 0; i < d->buckets; i++) {
    for (j = 0; j < DIR_DEPTH; j++) {
      dir_set_offset(dir_bucket_row(dir_bucket(i, seg), j), 0); // delete
    }
  }
  dir_clean_segment(s, d);
  int newfree = dir_freelist_length(d, s);
  rprintf(t, "newfree: %d\n", newfree);
  if ((unsigned int)(newfree - free) > 1) {
    ret = REGRESSION_TEST_FAILED;
  }

  // test insert-delete
  rprintf(t, "insert-delete test\n");
  regress_rand_init(13);
  ttime = Thread::get_hrtime_updated();
  for (i = 0; i < newfree; i++) {
    regress_rand_CacheKey(&key);
    dir_insert(&key, d, &dir);
  }
  uint64_t us = (Thread::get_hrtime_updated() - ttime) / HRTIME_USECOND;
  // On windows us is sometimes 0. I don't know why.
  // printout the insert rate only if its not 0
  if (us) {
    rprintf(t, "insert rate = %d / second\n", (int)((newfree * (uint64_t)1000000) / us));
  }
  regress_rand_init(13);
  ttime = Thread::get_hrtime_updated();
  for (i = 0; i < newfree; i++) {
    Dir *last_collision = nullptr;
    regress_rand_CacheKey(&key);
    if (!dir_probe(&key, d, &dir, &last_collision)) {
      ret = REGRESSION_TEST_FAILED;
    }
  }
  us = (Thread::get_hrtime_updated() - ttime) / HRTIME_USECOND;
  // On windows us is sometimes 0. I don't know why.
  // printout the probe rate only if its not 0
  if (us) {
    rprintf(t, "probe rate = %d / second\n", (int)((newfree * (uint64_t)1000000) / us));
  }

  for (int c = 0; c < d->direntries() * 0.75; c++) {
    regress_rand_CacheKey(&key);
    dir_insert(&key, d, &dir);
  }

  Dir dir1;
  memset(static_cast<void *>(&dir1), 0, sizeof(dir1));
  int s1, b1;

  rprintf(t, "corrupt_bucket test\n");
  for (int ntimes = 0; ntimes < 10; ntimes++) {
#ifdef LOOP_CHECK_MODE
    // dir_probe in bucket with loop
    rand_CacheKey(&key, thread->mutex);
    s1 = key.slice32(0) % d->segments;
    b1 = key.slice32(1) % d->buckets;
    dir_corrupt_bucket(dir_bucket(b1, d->dir_segment(s1)), s1, d);
    dir_insert(&key, d, &dir);
    Dir *last_collision = 0;
    dir_probe(&key, d, &dir, &last_collision);

    rand_CacheKey(&key, thread->mutex);
    s1 = key.slice32(0) % d->segments;
    b1 = key.slice32(1) % d->buckets;
    dir_corrupt_bucket(dir_bucket(b1, d->dir_segment(s1)), s1, d);

    last_collision = 0;
    dir_probe(&key, d, &dir, &last_collision);

    // dir_overwrite in bucket with loop
    rand_CacheKey(&key, thread->mutex);
    s1 = key.slice32(0) % d->segments;
    b1 = key.slice32(1) % d->buckets;
    CacheKey key1;
    key1.b[1] = 127;
    dir1      = dir;
    dir_set_offset(&dir1, 23);
    dir_insert(&key1, d, &dir1);
    dir_insert(&key, d, &dir);
    key1.b[1] = 80;
    dir_insert(&key1, d, &dir1);
    dir_corrupt_bucket(dir_bucket(b1, d->dir_segment(s1)), s1, d);
    dir_overwrite(&key, d, &dir, &dir, 1);

    rand_CacheKey(&key, thread->mutex);
    s1       = key.slice32(0) % d->segments;
    b1       = key.slice32(1) % d->buckets;
    key.b[1] = 23;
    dir_insert(&key, d, &dir1);
    dir_corrupt_bucket(dir_bucket(b1, d->dir_segment(s1)), s1, d);
    dir_overwrite(&key, d, &dir, &dir, 0);

    rand_CacheKey(&key, thread->mutex);
    s1        = key.slice32(0) % d->segments;
    Dir *seg1 = d->dir_segment(s1);
    // dir_freelist_length in freelist with loop
    dir_corrupt_bucket(dir_from_offset(d->header->freelist[s], seg1), s1, d);
    dir_freelist_length(d, s1);

    rand_CacheKey(&key, thread->mutex);
    s1 = key.slice32(0) % d->segments;
    b1 = key.slice32(1) % d->buckets;
    // dir_bucket_length in bucket with loop
    dir_corrupt_bucket(dir_bucket(b1, d->dir_segment(s1)), s1, d);
    dir_bucket_length(dir_bucket(b1, d->dir_segment(s1)), s1, d);
    if (!check_dir(d))
      ret = REGRESSION_TEST_FAILED;
#else
    // test corruption detection
    rand_CacheKey(&key, thread->mutex);
    s1 = key.slice32(0) % d->segments;
    b1 = key.slice32(1) % d->buckets;

    dir_insert(&key, d, &dir1);
    dir_insert(&key, d, &dir1);
    dir_insert(&key, d, &dir1);
    dir_insert(&key, d, &dir1);
    dir_insert(&key, d, &dir1);
    dir_corrupt_bucket(dir_bucket(b1, d->dir_segment(s1)), s1, d);
    if (check_dir(d)) {
      ret = REGRESSION_TEST_FAILED;
    }
#endif
  }
  vol_dir_clear(d);
  *status = ret;
}

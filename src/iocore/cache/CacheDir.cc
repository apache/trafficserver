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
#include "P_CacheDir.h"
#include "P_CacheDoc.h"
#include "Stripe.h"

#include "tscore/hugepages.h"
#include "tscore/Random.h"

#ifdef LOOP_CHECK_MODE
#define DIR_LOOP_THRESHOLD 1000
#endif

namespace
{

DbgCtl dbg_ctl_cache_dir_sync{"dir_sync"};
DbgCtl dbg_ctl_cache_check_dir{"cache_check_dir"};
DbgCtl dbg_ctl_dir_clean{"dir_clean"};

#ifdef DEBUG

DbgCtl dbg_ctl_cache_stats{"cache_stats"};
DbgCtl dbg_ctl_dir_probe_hit{"dir_probe_hit"};
DbgCtl dbg_ctl_dir_probe_tag{"dir_probe_tag"};
DbgCtl dbg_ctl_dir_probe_miss{"dir_probe_miss"};
DbgCtl dbg_ctl_dir_insert{"dir_insert"};
DbgCtl dbg_ctl_dir_overwrite{"dir_overwrite"};
DbgCtl dbg_ctl_dir_lookaside{"dir_lookaside"};

#endif

} // end anonymous namespace

// Globals

ClassAllocator<OpenDirEntry> openDirEntryAllocator("openDirEntry");

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
  ink_assert(cont->stripe->mutex->thread_holding == this_ethread());
  unsigned int h = cont->first_key.slice32(0);
  int          b = h % OPEN_DIR_BUCKETS;
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
  EThread                                  *t = mutex->thread_holding;
  CacheVC                                  *c = nullptr;
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
  ink_assert(cont->stripe->mutex->thread_holding == this_ethread());
  cont->od->writers.remove(cont);
  cont->od->num_writers--;
  if (!cont->od->writers.head) {
    unsigned int h = cont->first_key.slice32(0);
    int          b = h % OPEN_DIR_BUCKETS;
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
OpenDir::open_read(const CryptoHash *key) const
{
  unsigned int h = key->slice32(0);
  int          b = h % OPEN_DIR_BUCKETS;
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
  ink_assert(cont->stripe->mutex->thread_holding == this_ethread());
  cont->f.open_read_timeout = 1;
  ink_assert(!cont->trigger);
  cont->trigger = cont->stripe->mutex->thread_holding->schedule_in_local(cont, HRTIME_MSECONDS(msec));
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
dir_init_segment(int s, Stripe *stripe)
{
  stripe->header->freelist[s] = 0;
  Dir *seg                    = stripe->dir_segment(s);
  int  l, b;
  memset(static_cast<void *>(seg), 0, SIZEOF_DIR * DIR_DEPTH * stripe->buckets);
  for (l = 1; l < DIR_DEPTH; l++) {
    for (b = 0; b < stripe->buckets; b++) {
      Dir *bucket = dir_bucket(b, seg);
      dir_free_entry(dir_bucket_row(bucket, l), s, stripe);
    }
  }
}

// break the infinite loop in directory entries
// Note : abuse of the token bit in dir entries
int
dir_bucket_loop_fix(Dir *start_dir, int s, Stripe *stripe)
{
  if (!dir_bucket_loop_check(start_dir, stripe->dir_segment(s))) {
    Warning("Dir loop exists, clearing segment %d", s);
    dir_init_segment(s, stripe);
    return 1;
  }
  return 0;
}

int
dir_freelist_length(Stripe *stripe, int s)
{
  int  free = 0;
  Dir *seg  = stripe->dir_segment(s);
  Dir *e    = dir_from_offset(stripe->header->freelist[s], seg);
  if (dir_bucket_loop_fix(e, s, stripe)) {
    return (DIR_DEPTH - 1) * stripe->buckets;
  }
  while (e) {
    free++;
    e = next_dir(e, seg);
  }
  return free;
}

int
dir_bucket_length(Dir *b, int s, Stripe *stripe)
{
  Dir *e   = b;
  int  i   = 0;
  Dir *seg = stripe->dir_segment(s);
#ifdef LOOP_CHECK_MODE
  if (dir_bucket_loop_fix(b, s, vol))
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
check_dir(Stripe *stripe)
{
  int i, s;
  Dbg(dbg_ctl_cache_check_dir, "inside check dir");
  for (s = 0; s < stripe->segments; s++) {
    Dir *seg = stripe->dir_segment(s);
    for (i = 0; i < stripe->buckets; i++) {
      Dir *b = dir_bucket(i, seg);
      if (!(dir_bucket_length(b, s, stripe) >= 0)) {
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
unlink_from_freelist(Dir *e, int s, Stripe *stripe)
{
  Dir *seg = stripe->dir_segment(s);
  Dir *p   = dir_from_offset(dir_prev(e), seg);
  if (p) {
    dir_set_next(p, dir_next(e));
  } else {
    stripe->header->freelist[s] = dir_next(e);
  }
  Dir *n = dir_from_offset(dir_next(e), seg);
  if (n) {
    dir_set_prev(n, dir_prev(e));
  }
}

inline Dir *
dir_delete_entry(Dir *e, Dir *p, int s, Stripe *stripe)
{
  Dir *seg              = stripe->dir_segment(s);
  int  no               = dir_next(e);
  stripe->header->dirty = 1;
  if (p) {
    unsigned int fo = stripe->header->freelist[s];
    unsigned int eo = dir_to_offset(e, seg);
    dir_clear(e);
    dir_set_next(p, no);
    dir_set_next(e, fo);
    if (fo) {
      dir_set_prev(dir_from_offset(fo, seg), eo);
    }
    stripe->header->freelist[s] = eo;
  } else {
    Dir *n = next_dir(e, seg);
    if (n) {
      dir_assign(e, n);
      dir_delete_entry(n, e, s, stripe);
      return e;
    } else {
      dir_clear(e);
      return nullptr;
    }
  }
  return dir_from_offset(no, seg);
}

inline void
dir_clean_bucket(Dir *b, int s, Stripe *stripe)
{
  Dir *e = b, *p = nullptr;
  Dir *seg = stripe->dir_segment(s);
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
    if (!dir_valid(stripe, e) || !dir_offset(e)) {
      if (dbg_ctl_dir_clean.on()) {
        Dbg(dbg_ctl_dir_clean, "cleaning Stripe:%s: %p tag %X boffset %" PRId64 " b %p p %p bucket len %d", stripe->hash_text.get(),
            e, dir_tag(e), dir_offset(e), b, p, dir_bucket_length(b, s, stripe));
      }
      if (dir_offset(e)) {
        Metrics::Gauge::decrement(cache_rsb.direntries_used);
        Metrics::Gauge::decrement(stripe->cache_vol->vol_rsb.direntries_used);
      }
      e = dir_delete_entry(e, p, s, stripe);
      continue;
    }
    p = e;
    e = next_dir(e, seg);
  } while (e);
}

void
dir_clean_segment(int s, Stripe *stripe)
{
  Dir *seg = stripe->dir_segment(s);
  for (int64_t i = 0; i < stripe->buckets; i++) {
    dir_clean_bucket(dir_bucket(i, seg), s, stripe);
    ink_assert(!dir_next(dir_bucket(i, seg)) || dir_offset(dir_bucket(i, seg)));
  }
}

void
dir_clean_vol(Stripe *stripe)
{
  for (int64_t i = 0; i < stripe->segments; i++) {
    dir_clean_segment(i, stripe);
  }
  CHECK_DIR(d);
}

void
dir_clear_range(off_t start, off_t end, Stripe *stripe)
{
  for (off_t i = 0; i < stripe->buckets * DIR_DEPTH * stripe->segments; i++) {
    Dir *e = dir_index(stripe, i);
    if (dir_offset(e) >= static_cast<int64_t>(start) && dir_offset(e) < static_cast<int64_t>(end)) {
      Metrics::Gauge::decrement(cache_rsb.direntries_used);
      Metrics::Gauge::decrement(stripe->cache_vol->vol_rsb.direntries_used);
      dir_set_offset(e, 0); // delete
    }
  }
  dir_clean_vol(stripe);
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
freelist_clean(int s, Stripe *stripe)
{
  dir_clean_segment(s, stripe);
  if (stripe->header->freelist[s]) {
    return;
  }
  Warning("cache directory overflow on '%s' segment %d, purging...", stripe->path, s);
  int  n   = 0;
  Dir *seg = stripe->dir_segment(s);
  for (int bi = 0; bi < stripe->buckets; bi++) {
    Dir *b = dir_bucket(bi, seg);
    for (int l = 0; l < DIR_DEPTH; l++) {
      Dir *e = dir_bucket_row(b, l);
      if (dir_head(e) && !(n++ % 10)) {
        Metrics::Gauge::decrement(cache_rsb.direntries_used);
        Metrics::Gauge::decrement(stripe->cache_vol->vol_rsb.direntries_used);
        dir_set_offset(e, 0); // delete
      }
    }
  }
  dir_clean_segment(s, stripe);
}

inline Dir *
freelist_pop(int s, Stripe *stripe)
{
  Dir *seg = stripe->dir_segment(s);
  Dir *e   = dir_from_offset(stripe->header->freelist[s], seg);
  if (!e) {
    freelist_clean(s, stripe);
    return nullptr;
  }
  stripe->header->freelist[s] = dir_next(e);
  // if the freelist if bad, punt.
  if (dir_offset(e)) {
    dir_init_segment(s, stripe);
    return nullptr;
  }
  Dir *h = dir_from_offset(stripe->header->freelist[s], seg);
  if (h) {
    dir_set_prev(h, 0);
  }
  return e;
}

int
dir_segment_accounted(int s, Stripe *stripe, int offby, int *f, int *u, int *et, int *v, int *av, int *as)
{
  int     free = dir_freelist_length(stripe, s);
  int     used = 0, empty = 0;
  int     valid = 0, agg_valid = 0;
  int64_t agg_size = 0;
  Dir    *seg      = stripe->dir_segment(s);
  for (int bi = 0; bi < stripe->buckets; bi++) {
    Dir *b = dir_bucket(bi, seg);
    Dir *e = b;
    while (e) {
      if (!dir_offset(e)) {
        ink_assert(e == b);
        empty++;
      } else {
        used++;
        if (dir_valid(stripe, e)) {
          valid++;
        }
        if (dir_agg_valid(stripe, e)) {
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
    *as = used ? static_cast<int>(agg_size / used) : 0;
  }
  ink_assert(stripe->buckets * DIR_DEPTH - (free + used + empty) <= offby);
  return stripe->buckets * DIR_DEPTH - (free + used + empty) <= offby;
}

void
dir_free_entry(Dir *e, int s, Stripe *stripe)
{
  Dir         *seg = stripe->dir_segment(s);
  unsigned int fo  = stripe->header->freelist[s];
  unsigned int eo  = dir_to_offset(e, seg);
  dir_set_next(e, fo);
  if (fo) {
    dir_set_prev(dir_from_offset(fo, seg), eo);
  }
  stripe->header->freelist[s] = eo;
}

int
dir_probe(const CacheKey *key, StripeSM *stripe, Dir *result, Dir **last_collision)
{
  ink_assert(stripe->mutex->thread_holding == this_ethread());
  int  s   = key->slice32(0) % stripe->segments;
  int  b   = key->slice32(1) % stripe->buckets;
  Dir *seg = stripe->dir_segment(s);
  Dir *e = nullptr, *p = nullptr, *collision = *last_collision;
  CHECK_DIR(d);
#ifdef LOOP_CHECK_MODE
  if (dir_bucket_loop_fix(dir_bucket(b, seg), s, vol))
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
            DDbg(dbg_ctl_cache_stats, "Incrementing dir collisions");
            Metrics::Counter::increment(cache_rsb.directory_collision);
            Metrics::Counter::increment(stripe->cache_vol->vol_rsb.directory_collision);
          }
          goto Lcont;
        }
        if (dir_valid(stripe, e)) {
          DDbg(dbg_ctl_dir_probe_hit, "found %X %X vol %d bucket %d boffset %" PRId64 "", key->slice32(0), key->slice32(1),
               stripe->fd, b, dir_offset(e));
          dir_assign(result, e);
          *last_collision = e;
          ink_assert(dir_offset(e) * CACHE_BLOCK_SIZE < stripe->len);
          return 1;
        } else { // delete the invalid entry
          Metrics::Gauge::decrement(cache_rsb.direntries_used);
          Metrics::Gauge::decrement(stripe->cache_vol->vol_rsb.direntries_used);
          e = dir_delete_entry(e, p, s, stripe);
          continue;
        }
      } else {
        DDbg(dbg_ctl_dir_probe_tag, "tag mismatch %p %X vs expected %X", e, dir_tag(e), key->slice32(3));
      }
    Lcont:
      p = e;
      e = next_dir(e, seg);
    } while (e);
  }
  if (collision) { // last collision no longer in the list, retry
    DDbg(dbg_ctl_cache_stats, "Incrementing dir collisions");
    Metrics::Counter::increment(cache_rsb.directory_collision);
    Metrics::Counter::increment(stripe->cache_vol->vol_rsb.directory_collision);
    collision = nullptr;
    goto Lagain;
  }
  DDbg(dbg_ctl_dir_probe_miss, "missed %X %X on vol %d bucket %d at %p", key->slice32(0), key->slice32(1), stripe->fd, b, seg);
  CHECK_DIR(d);
  return 0;
}

int
dir_insert(const CacheKey *key, StripeSM *stripe, Dir *to_part)
{
  ink_assert(stripe->mutex->thread_holding == this_ethread());
  int s  = key->slice32(0) % stripe->segments, l;
  int bi = key->slice32(1) % stripe->buckets;
  ink_assert(dir_approx_size(to_part) <= MAX_FRAG_SIZE + sizeof(Doc));
  Dir *seg = stripe->dir_segment(s);
  Dir *e   = nullptr;
  Dir *b   = dir_bucket(bi, seg);
#if defined(DEBUG) && defined(DO_CHECK_DIR_FAST)
  unsigned int t   = DIR_MASK_TAG(key->slice32(2));
  Dir         *col = b;
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
      unlink_from_freelist(e, s, stripe);
      goto Llink;
    }
  }
  // get one from the freelist
  e = freelist_pop(s, stripe);
  if (!e) {
    goto Lagain;
  }
Llink:
  // dir_probe searches from head to tail of list and resumes from last_collision.
  // Need to insert at the tail of the list so that no entries can be inserted
  // before last_collision. This means walking the entire list on each insert,
  // but at least the lists are completely in memory and should be quite short
  Dir *prev, *last;

  l    = 0;
  last = b;
  do {
    prev = last;
    last = next_dir(last, seg);
  } while (last && (++l <= stripe->buckets * DIR_DEPTH));

  dir_set_next(e, 0);
  dir_set_next(prev, dir_to_offset(e, seg));
Lfill:
  dir_assign_data(e, to_part);
  dir_set_tag(e, key->slice32(2));
  ink_assert(stripe->vol_offset(e) < (stripe->skip + stripe->len));
  DDbg(dbg_ctl_dir_insert, "insert %p %X into vol %d bucket %d at %p tag %X %X boffset %" PRId64 "", e, key->slice32(0), stripe->fd,
       bi, e, key->slice32(1), dir_tag(e), dir_offset(e));
  CHECK_DIR(d);
  stripe->header->dirty = 1;
  Metrics::Gauge::increment(cache_rsb.direntries_used);
  Metrics::Gauge::increment(stripe->cache_vol->vol_rsb.direntries_used);

  return 1;
}

int
dir_overwrite(const CacheKey *key, StripeSM *stripe, Dir *dir, Dir *overwrite, bool must_overwrite)
{
  ink_assert(stripe->mutex->thread_holding == this_ethread());
  int          s   = key->slice32(0) % stripe->segments, l;
  int          bi  = key->slice32(1) % stripe->buckets;
  Dir         *seg = stripe->dir_segment(s);
  Dir         *e   = nullptr;
  Dir         *b   = dir_bucket(bi, seg);
  unsigned int t   = DIR_MASK_TAG(key->slice32(2));
  int          res = 1;
#ifdef LOOP_CHECK_MODE
  int  loop_count    = 0;
  bool loop_possible = true;
#endif
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
        if (dir_bucket_loop_fix(b, s, vol)) {
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
    Metrics::Gauge::increment(cache_rsb.direntries_used);
    Metrics::Gauge::increment(stripe->cache_vol->vol_rsb.direntries_used);
    goto Lfill;
  }
  for (l = 1; l < DIR_DEPTH; l++) {
    e = dir_bucket_row(b, l);
    if (dir_is_empty(e)) {
      unlink_from_freelist(e, s, stripe);
      goto Llink;
    }
  }
  // get one from the freelist
  e = freelist_pop(s, stripe);
  if (!e) {
    goto Lagain;
  }
Llink:
  Metrics::Gauge::increment(cache_rsb.direntries_used);
  Metrics::Gauge::increment(stripe->cache_vol->vol_rsb.direntries_used);
  // as with dir_insert above, need to insert new entries at the tail of the linked list
  Dir *prev, *last;

  l    = 0;
  last = b;
  do {
    prev = last;
    last = next_dir(last, seg);
  } while (last && (++l <= stripe->buckets * DIR_DEPTH));

  dir_set_next(e, 0);
  dir_set_next(prev, dir_to_offset(e, seg));
Lfill:
  dir_assign_data(e, dir);
  dir_set_tag(e, t);
  ink_assert(stripe->vol_offset(e) < stripe->skip + stripe->len);
  DDbg(dbg_ctl_dir_overwrite, "overwrite %p %X into vol %d bucket %d at %p tag %X %X boffset %" PRId64 "", e, key->slice32(0),
       stripe->fd, bi, e, t, dir_tag(e), dir_offset(e));
  CHECK_DIR(d);
  stripe->header->dirty = 1;
  return res;
}

int
dir_delete(const CacheKey *key, StripeSM *stripe, Dir *del)
{
  ink_assert(stripe->mutex->thread_holding == this_ethread());
  int  s   = key->slice32(0) % stripe->segments;
  int  b   = key->slice32(1) % stripe->buckets;
  Dir *seg = stripe->dir_segment(s);
  Dir *e = nullptr, *p = nullptr;
#ifdef LOOP_CHECK_MODE
  int loop_count = 0;
#endif
  CHECK_DIR(vol);

  e = dir_bucket(b, seg);
  if (dir_offset(e)) {
    do {
#ifdef LOOP_CHECK_MODE
      loop_count++;
      if (loop_count > DIR_LOOP_THRESHOLD) {
        if (dir_bucket_loop_fix(dir_bucket(b, seg), s, vol))
          return 0;
      }
#endif
      if (dir_compare_tag(e, key) && dir_offset(e) == dir_offset(del)) {
        Metrics::Gauge::decrement(cache_rsb.direntries_used);
        Metrics::Gauge::decrement(stripe->cache_vol->vol_rsb.direntries_used);
        dir_delete_entry(e, p, s, stripe);
        CHECK_DIR(d);
        return 1;
      }
      p = e;
      e = next_dir(e, seg);
    } while (e);
  }
  CHECK_DIR(vol);
  return 0;
}

// Lookaside Cache

int
dir_lookaside_probe(const CacheKey *key, StripeSM *stripe, Dir *result, EvacuationBlock **eblock)
{
  ink_assert(stripe->mutex->thread_holding == this_ethread());
  int              i = key->slice32(3) % LOOKASIDE_SIZE;
  EvacuationBlock *b = stripe->lookaside[i].head;
  while (b) {
    if (b->evac_frags.key == *key) {
      if (dir_valid(stripe, &b->new_dir)) {
        *result = b->new_dir;
        DDbg(dbg_ctl_dir_lookaside, "probe %X success", key->slice32(0));
        if (eblock) {
          *eblock = b;
        }
        return 1;
      }
    }
    b = b->link.next;
  }
  DDbg(dbg_ctl_dir_lookaside, "probe %X failed", key->slice32(0));
  return 0;
}

int
dir_lookaside_insert(EvacuationBlock *eblock, StripeSM *stripe, Dir *to)
{
  CacheKey *key = &eblock->evac_frags.earliest_key;
  DDbg(dbg_ctl_dir_lookaside, "insert %X %X, offset %d phase %d", key->slice32(0), key->slice32(1), (int)dir_offset(to),
       (int)dir_phase(to));
  ink_assert(stripe->mutex->thread_holding == this_ethread());
  int              i         = key->slice32(3) % LOOKASIDE_SIZE;
  EvacuationBlock *b         = new_EvacuationBlock();
  b->evac_frags.key          = *key;
  b->evac_frags.earliest_key = *key;
  b->earliest_evacuator      = eblock->earliest_evacuator;
  ink_assert(b->earliest_evacuator);
  b->dir     = eblock->dir;
  b->new_dir = *to;
  stripe->lookaside[i].push(b);
  return 1;
}

int
dir_lookaside_fixup(const CacheKey *key, StripeSM *stripe)
{
  ink_assert(stripe->mutex->thread_holding == this_ethread());
  int              i = key->slice32(3) % LOOKASIDE_SIZE;
  EvacuationBlock *b = stripe->lookaside[i].head;
  while (b) {
    if (b->evac_frags.key == *key) {
      int res = dir_overwrite(key, stripe, &b->new_dir, &b->dir, false);
      DDbg(dbg_ctl_dir_lookaside, "fixup %X %X offset %" PRId64 " phase %d %d", key->slice32(0), key->slice32(1),
           dir_offset(&b->new_dir), dir_phase(&b->new_dir), res);
      int64_t o = dir_offset(&b->dir), n = dir_offset(&b->new_dir);
      stripe->ram_cache->fixup(key, static_cast<uint64_t>(o), static_cast<uint64_t>(n));
      stripe->lookaside[i].remove(b);
      free_EvacuationBlock(b);
      return res;
    }
    b = b->link.next;
  }
  DDbg(dbg_ctl_dir_lookaside, "fixup %X %X failed", key->slice32(0), key->slice32(1));
  return 0;
}

void
dir_lookaside_cleanup(StripeSM *stripe)
{
  ink_assert(stripe->mutex->thread_holding == this_ethread());
  for (auto &i : stripe->lookaside) {
    EvacuationBlock *b = i.head;
    while (b) {
      if (!dir_valid(stripe, &b->new_dir)) {
        EvacuationBlock *nb = b->link.next;
        DDbg(dbg_ctl_dir_lookaside, "cleanup %X %X cleaned up", b->evac_frags.earliest_key.slice32(0),
             b->evac_frags.earliest_key.slice32(1));
        i.remove(b);
        free_CacheEvacuateDocVC(b->earliest_evacuator);
        free_EvacuationBlock(b);
        b = nb;
        goto Lagain;
      }
      b = b->link.next;
    Lagain:;
    }
  }
}

void
dir_lookaside_remove(const CacheKey *key, StripeSM *stripe)
{
  ink_assert(stripe->mutex->thread_holding == this_ethread());
  int              i = key->slice32(3) % LOOKASIDE_SIZE;
  EvacuationBlock *b = stripe->lookaside[i].head;
  while (b) {
    if (b->evac_frags.key == *key) {
      DDbg(dbg_ctl_dir_lookaside, "remove %X %X offset %" PRId64 " phase %d", key->slice32(0), key->slice32(1),
           dir_offset(&b->new_dir), dir_phase(&b->new_dir));
      stripe->lookaside[i].remove(b);
      free_EvacuationBlock(b);
      return;
    }
    b = b->link.next;
  }
  DDbg(dbg_ctl_dir_lookaside, "remove %X %X failed", key->slice32(0), key->slice32(1));
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
dir_entries_used(Stripe *stripe)
{
  uint64_t full  = 0;
  uint64_t sfull = 0;
  for (int s = 0; s < stripe->segments; full += sfull, s++) {
    Dir *seg = stripe->dir_segment(s);
    sfull    = 0;
    for (int b = 0; b < stripe->buckets; b++) {
      Dir *e = dir_bucket(b, seg);
      if (dir_bucket_loop_fix(e, s, stripe)) {
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
  Dbg(dbg_ctl_cache_dir_sync, "sync started");
  EThread *t = (EThread *)0xdeadbeef;
  for (int i = 0; i < gnstripes; i++) {
    gstripes[i]->shutdown(t);
  }
  Dbg(dbg_ctl_cache_dir_sync, "sync done");
}

int
CacheSync::mainEvent(int event, Event *e)
{
  if (trigger) {
    trigger->cancel_action();
    trigger = nullptr;
  }

Lrestart:
  if (stripe_index >= gnstripes) {
    stripe_index = 0;
    if (buf) {
      if (buf_huge) {
        ats_free_hugepage(buf, buflen);
      } else {
        ats_free(buf);
      }
      buflen   = 0;
      buf      = nullptr;
      buf_huge = false;
    }
    Dbg(dbg_ctl_cache_dir_sync, "sync done");
    if (event == EVENT_INTERVAL) {
      trigger = e->ethread->schedule_in(this, HRTIME_SECONDS(cache_config_dir_sync_frequency));
    } else {
      trigger = eventProcessor.schedule_in(this, HRTIME_SECONDS(cache_config_dir_sync_frequency));
    }
    return EVENT_CONT;
  }

  StripeSM *stripe = gstripes[stripe_index]; // must be named "vol" to make STAT macros work.

  if (event == AIO_EVENT_DONE) {
    // AIO Thread
    if (!io.ok()) {
      Warning("vol write error during directory sync '%s'", gstripes[stripe_index]->hash_text.get());
      event = EVENT_NONE;
      goto Ldone;
    }
    Metrics::Counter::increment(cache_rsb.directory_sync_bytes, io.aio_result);
    Metrics::Counter::increment(stripe->cache_vol->vol_rsb.directory_sync_bytes, io.aio_result);
    trigger = eventProcessor.schedule_in(this, SYNC_DELAY);
    return EVENT_CONT;
  }
  {
    CACHE_TRY_LOCK(lock, gstripes[stripe_index]->mutex, mutex->thread_holding);
    if (!lock.is_locked()) {
      trigger = eventProcessor.schedule_in(this, HRTIME_MSECONDS(cache_config_mutex_retry_delay));
      return EVENT_CONT;
    }

    if (!stripe->dir_sync_in_progress) {
      start_time = ink_get_hrtime();
    }

    // recompute hit_evacuate_window
    stripe->hit_evacuate_window = (stripe->data_blocks * cache_config_hit_evacuate_percent) / 100;

    if (DISK_BAD(stripe->disk)) {
      goto Ldone;
    }

    int    headerlen = ROUND_TO_STORE_BLOCK(sizeof(StripteHeaderFooter));
    size_t dirlen    = stripe->dirlen();
    if (!writepos) {
      // start
      Dbg(dbg_ctl_cache_dir_sync, "sync started");
      /* Don't sync the directory to disk if its not dirty. Syncing the
         clean directory to disk is also the cause of INKqa07151. Increasing
         the serial number causes the cache to recover more data than necessary.
         The dirty bit it set in dir_insert, dir_overwrite and dir_delete_entry
       */
      if (!stripe->header->dirty) {
        Dbg(dbg_ctl_cache_dir_sync, "Dir %s not dirty", stripe->hash_text.get());
        goto Ldone;
      }
      if (stripe->is_io_in_progress() || stripe->get_agg_buf_pos()) {
        Dbg(dbg_ctl_cache_dir_sync, "Dir %s: waiting for agg buffer", stripe->hash_text.get());
        stripe->dir_sync_waiting = true;
        if (!stripe->is_io_in_progress()) {
          stripe->aggWrite(EVENT_IMMEDIATE, nullptr);
        }
        return EVENT_CONT;
      }
      Dbg(dbg_ctl_cache_dir_sync, "pos: %" PRIu64 " Dir %s dirty...syncing to disk", stripe->header->write_pos,
          stripe->hash_text.get());
      stripe->header->dirty = 0;
      if (buflen < dirlen) {
        if (buf) {
          if (buf_huge) {
            ats_free_hugepage(buf, buflen);
          } else {
            ats_free(buf);
          }
          buf = nullptr;
        }
        buflen = dirlen;
        if (ats_hugepage_enabled()) {
          buf      = static_cast<char *>(ats_alloc_hugepage(buflen));
          buf_huge = true;
        }
        if (buf == nullptr) {
          buf      = static_cast<char *>(ats_memalign(ats_pagesize(), buflen));
          buf_huge = false;
        }
      }
      stripe->header->sync_serial++;
      stripe->footer->sync_serial = stripe->header->sync_serial;
      CHECK_DIR(d);
      memcpy(buf, stripe->raw_dir, dirlen);
      stripe->dir_sync_in_progress = true;
    }
    size_t B     = stripe->header->sync_serial & 1;
    off_t  start = stripe->skip + (B ? dirlen : 0);

    if (!writepos) {
      // write header
      aio_write(stripe->fd, buf + writepos, headerlen, start + writepos);
      writepos += headerlen;
    } else if (writepos < static_cast<off_t>(dirlen) - headerlen) {
      // write part of body
      int l = SYNC_MAX_WRITE;
      if (writepos + l > static_cast<off_t>(dirlen) - headerlen) {
        l = dirlen - headerlen - writepos;
      }
      aio_write(stripe->fd, buf + writepos, l, start + writepos);
      writepos += l;
    } else if (writepos < static_cast<off_t>(dirlen)) {
      ink_assert(writepos == (off_t)dirlen - headerlen);
      // write footer
      aio_write(stripe->fd, buf + writepos, headerlen, start + writepos);
      writepos += headerlen;
    } else {
      stripe->dir_sync_in_progress = false;
      Metrics::Counter::increment(cache_rsb.directory_sync_count);
      Metrics::Counter::increment(stripe->cache_vol->vol_rsb.directory_sync_count);
      Metrics::Counter::increment(cache_rsb.directory_sync_time, ink_get_hrtime() - start_time);
      Metrics::Counter::increment(stripe->cache_vol->vol_rsb.directory_sync_time, ink_get_hrtime() - start_time);
      start_time = 0;
      goto Ldone;
    }
    return EVENT_CONT;
  }
Ldone:
  // done
  writepos = 0;
  ++stripe_index;
  goto Lrestart;
}

//
// Static Tables
//

// permutation table
const uint8_t CacheKey_next_table[256] = {
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
const uint8_t CacheKey_prev_table[256] = {
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

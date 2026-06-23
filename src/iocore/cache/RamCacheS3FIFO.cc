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

// S3-FIFO RAM cache replacement policy.
//
// The design follows Yang, Zhang, Qiu, Yue & Rashmi, "FIFO queues are all you need for cache
// eviction" (SOSP 2023) and https://s3fifo.com, mirroring the libCacheSim reference (S3FIFO.c).
//
// EXPERIMENTAL: three FIFO queues -- a small admission queue S (~10% of capacity), a main queue M
// (~90%, a 2-bit CLOCK), and a ghost queue G that remembers the keys of recently evicted objects
// (no data). New objects enter S; on eviction from S an object that was reused (frequency >= 2) is
// promoted to M, otherwise it is demoted to G. A subsequent miss whose key is in G enters M
// directly. The small queue + ghost give S3-FIFO admission control that filters one-hit-wonders --
// exactly the CDN "quick demotion" property -- at FIFO cost (no per-hit reordering). The ghost
// holds keys, so it costs per-entry metadata at high object cardinality; that metadata is bounded
// and counted against ram_cache.size so total memory stays within the configured budget. Selectable
// as proxy.config.cache.ram_cache.algorithm = 2; the queue split, ghost bounds, and promotion
// threshold are tunable via proxy.config.cache.ram_cache.s3fifo.* (see init() and the admin guide,
// records.yaml.en.rst).

#include "P_RamCache.h"
#include "P_CacheInternal.h"
#include "StripeSM.h"
#include "iocore/eventsystem/IOBuffer.h"
#include "tscore/CryptoHash.h"
#include "tscore/List.h"

#define ENTRY_OVERHEAD 128 // per-entry metadata counted against ram_cache.size
#define FREQ_MAX       3   // 2-bit saturating frequency counter; keep ram_cache.s3fifo.promote_threshold's range in sync

namespace
{
DbgCtl dbg_ctl_ram_cache{"ram_cache"};
} // namespace

enum { SEG_SMALL = 0, SEG_MAIN = 1, SEG_GHOST = 2 };

struct RamCacheS3FIFOEntry {
  CryptoHash key;
  uint64_t   auxkey;
  uint32_t   size; // object bytes; resident entries account size + ENTRY_OVERHEAD against the budget
  uint8_t    seg;  // SEG_SMALL / SEG_MAIN / SEG_GHOST
  uint8_t    freq; // 0..FREQ_MAX
  LINK(RamCacheS3FIFOEntry, lru_link);
  LINK(RamCacheS3FIFOEntry, hash_link);
  Ptr<IOBufferData> data; // null for ghost entries
};

struct RamCacheS3FIFO : public RamCache {
  int     get(CryptoHash *key, Ptr<IOBufferData> *ret_data, uint64_t auxkey = 0) override;
  int     put(CryptoHash *key, IOBufferData *data, uint32_t len, bool copy = false, uint64_t auxkey = 0) override;
  int     fixup(const CryptoHash *key, uint64_t old_auxkey, uint64_t new_auxkey) override;
  int64_t size() const override;
  void    init(int64_t max_bytes, StripeSM *stripe) override;

private:
  int     _main_percent      = 90; // main queue target size, percent of the resident budget (configurable)
  int     _promote_threshold = 2;  // reuse count in the small queue that promotes an object to main (configurable)
  int64_t _max_bytes         = 0;
  int64_t _ghost_size_limit  = 0; // ghost object-size bound (keeps it proportional for big objects)
  int64_t _ghost_max         = 0; // ghost entry-count bound (caps metadata to ghost_mem_percent)
  int64_t _s_bytes           = 0; // resident bytes in the small queue (incl. ENTRY_OVERHEAD per entry)
  int64_t _m_bytes           = 0; // resident bytes in the main queue
  int64_t _g_bytes           = 0; // ghost object-size sum (vs _ghost_size_limit)
  int64_t _g_count           = 0; // ghost entries; each costs ~ENTRY_OVERHEAD, counted in the budget
  int64_t _objects           = 0; // resident objects (S + M)
  int64_t _nentries          = 0; // hash entries (resident + ghost), for sizing the hash table

  Que(RamCacheS3FIFOEntry, lru_link) _seg[3]; // head = oldest, tail = newest, per segment
  DList(RamCacheS3FIFOEntry, hash_link) *_bucket = nullptr;
  int       _nbuckets                            = 0;
  int       _ibuckets                            = 0;
  StripeSM *_stripe                              = nullptr;

  void                 _resize_hashtable();
  RamCacheS3FIFOEntry *_lookup(const CryptoHash *key, uint64_t auxkey);
  void                 _unlink_hash(RamCacheS3FIFOEntry *e);
  void                 _remove(RamCacheS3FIFOEntry *e);
  void                 _to_main(RamCacheS3FIFOEntry *e);
  void                 _to_ghost(RamCacheS3FIFOEntry *e);
  bool                 _evict_ghost_one();
  void                 _enforce_ghost();
  void                 _evict_small();
  void                 _evict_main();
  void                 _evict();
};

ClassAllocator<RamCacheS3FIFOEntry, false> ramCacheS3FIFOEntryAllocator("RamCacheS3FIFOEntry");

static const int bucket_sizes[] = {8191,    16381,   32749,    65521,    131071,   262139,    524287,    1048573,   2097143,
                                   4194301, 8388593, 16777213, 33554393, 67108859, 134217689, 268435399, 536870909, 1073741827};

void
RamCacheS3FIFO::_resize_hashtable()
{
  ink_release_assert(_ibuckets < static_cast<int>(sizeof(bucket_sizes) / sizeof(bucket_sizes[0])));
  int     anbuckets = bucket_sizes[_ibuckets];
  int64_t s         = anbuckets * sizeof(DList(RamCacheS3FIFOEntry, hash_link));

  DList(RamCacheS3FIFOEntry, hash_link) *new_bucket = static_cast<DList(RamCacheS3FIFOEntry, hash_link) *>(ats_malloc(s));
  memset(static_cast<void *>(new_bucket), 0, s);
  if (_bucket) {
    for (int64_t i = 0; i < _nbuckets; i++) {
      RamCacheS3FIFOEntry *e = nullptr;
      while ((e = _bucket[i].pop())) {
        new_bucket[e->key.slice32(3) % anbuckets].push(e);
      }
    }
    ats_free(_bucket);
  }
  _bucket   = new_bucket;
  _nbuckets = anbuckets;
}

void
RamCacheS3FIFO::init(int64_t abytes, StripeSM *astripe)
{
  _stripe    = astripe;
  _max_bytes = abytes;
  if (!_max_bytes) {
    return;
  }

  // The tunables are range-validated in records.yaml (RECC_INT): an out-of-range value is rejected
  // there with a warning and the documented default is used in its place, so the values read here
  // are already within their safe ranges -- no clamping needed.
  _main_percent          = cache_config_ram_cache_s3fifo_main_percent;
  _promote_threshold     = cache_config_ram_cache_s3fifo_promote_threshold;
  int ghost_size_percent = cache_config_ram_cache_s3fifo_ghost_size_percent;
  int ghost_mem_percent  = cache_config_ram_cache_s3fifo_ghost_mem_percent;

  _ghost_size_limit = (_max_bytes * ghost_size_percent) / 100;
  // The ghost stores keys only, but each key still costs ~ENTRY_OVERHEAD of real memory. Bound the
  // ghost by both its object-size sum (keeps it proportional for large objects) and an entry count
  // that caps its metadata at ghost_mem_percent of the configured size; the metadata is counted
  // against the budget (see put) so total memory never exceeds ram_cache.size.
  _ghost_max = ((_max_bytes * ghost_mem_percent) / 100) / ENTRY_OVERHEAD;

  Dbg(dbg_ctl_ram_cache,
      "S3-FIFO init %" PRId64 " bytes: main_percent=%d ghost_size_percent=%d ghost_mem_percent=%d promote_threshold=%d", _max_bytes,
      _main_percent, ghost_size_percent, ghost_mem_percent, _promote_threshold);

  _resize_hashtable();
}

int64_t
RamCacheS3FIFO::size() const
{
  // Memory accounted against ram_cache.size: resident data plus per-entry overhead, plus the
  // ghost's per-key overhead. This matches the budget enforced in put(), and is O(1).
  return _s_bytes + _m_bytes + _g_count * ENTRY_OVERHEAD;
}

RamCacheS3FIFOEntry *
RamCacheS3FIFO::_lookup(const CryptoHash *key, uint64_t auxkey)
{
  uint32_t             i = key->slice32(3) % _nbuckets;
  RamCacheS3FIFOEntry *e = _bucket[i].head;
  while (e) {
    if (e->key == *key && e->auxkey == auxkey) {
      return e;
    }
    e = e->hash_link.next;
  }
  return nullptr;
}

void
RamCacheS3FIFO::_unlink_hash(RamCacheS3FIFOEntry *e)
{
  uint32_t b = e->key.slice32(3) % _nbuckets;
  _bucket[b].remove(e);
}

// Remove an entry from whichever segment holds it, update all accounting, and free it. The single
// removal point used by eviction, ghost trimming, and stale-aux-key discard in put().
void
RamCacheS3FIFO::_remove(RamCacheS3FIFOEntry *e)
{
  _seg[e->seg].remove(e);
  if (e->seg == SEG_GHOST) {
    _g_bytes -= e->size;
    _g_count--;
  } else {
    int64_t resident = ENTRY_OVERHEAD + e->size;
    if (e->seg == SEG_SMALL) {
      _s_bytes -= resident;
    } else {
      _m_bytes -= resident;
    }
    ts::Metrics::Gauge::decrement(cache_rsb.ram_cache_bytes, resident);
    ts::Metrics::Gauge::decrement(_stripe->cache_vol->vol_rsb.ram_cache_bytes, resident);
    _objects--;
  }
  _unlink_hash(e);
  _nentries--;
  e->data = nullptr;
  THREAD_FREE(e, ramCacheS3FIFOEntryAllocator, this_thread());
}

// Promote a small-queue object that has proven popular into the main queue (resident, no data
// change), resetting its frequency for the main-queue CLOCK.
void
RamCacheS3FIFO::_to_main(RamCacheS3FIFOEntry *e)
{
  _seg[SEG_SMALL].remove(e);
  _s_bytes -= ENTRY_OVERHEAD + e->size;
  e->seg    = SEG_MAIN;
  e->freq   = 0;
  _seg[SEG_MAIN].enqueue(e);
  _m_bytes += ENTRY_OVERHEAD + e->size;
}

// Demote a small-queue object to the ghost queue: drop its data (freeing resident bytes) but keep
// its key so a quick re-reference can be admitted straight to the main queue.
void
RamCacheS3FIFO::_to_ghost(RamCacheS3FIFOEntry *e)
{
  _seg[SEG_SMALL].remove(e);
  _s_bytes -= ENTRY_OVERHEAD + e->size;
  ts::Metrics::Gauge::decrement(cache_rsb.ram_cache_bytes, ENTRY_OVERHEAD + e->size);
  ts::Metrics::Gauge::decrement(_stripe->cache_vol->vol_rsb.ram_cache_bytes, ENTRY_OVERHEAD + e->size);
  e->data = nullptr;
  e->seg  = SEG_GHOST;
  e->freq = 0;
  _seg[SEG_GHOST].enqueue(e);
  _g_bytes += e->size;
  _g_count++;
  _objects--;
  _enforce_ghost();
}

// Drop the oldest ghost key; returns false if the ghost was already empty.
bool
RamCacheS3FIFO::_evict_ghost_one()
{
  RamCacheS3FIFOEntry *g = _seg[SEG_GHOST].head;
  if (!g) {
    return false;
  }
  _remove(g);
  return true;
}

void
RamCacheS3FIFO::_enforce_ghost()
{
  while ((_g_bytes > _ghost_size_limit || _g_count > _ghost_max) && _evict_ghost_one()) {}
}

// Evict from the small queue: promote reused objects to main, demote the first un-reused object to
// the ghost (which is what actually frees resident bytes). If everything got promoted the small
// queue empties and the caller falls through to evicting main.
void
RamCacheS3FIFO::_evict_small()
{
  while (_seg[SEG_SMALL].head) {
    RamCacheS3FIFOEntry *c = _seg[SEG_SMALL].head;
    if (c->freq >= _promote_threshold) {
      _to_main(c);
    } else {
      _to_ghost(c);
      return;
    }
  }
}

// Evict from the main queue (2-bit CLOCK): a reused object is reinserted at the tail with its
// frequency decremented; the first object with frequency 0 is evicted.
void
RamCacheS3FIFO::_evict_main()
{
  while (_seg[SEG_MAIN].head) {
    RamCacheS3FIFOEntry *c = _seg[SEG_MAIN].head;
    if (c->freq >= 1) {
      _seg[SEG_MAIN].remove(c);
      c->freq -= 1; // 2-bit clock: a reused object gets another pass (freq is already <= FREQ_MAX)
      _seg[SEG_MAIN].enqueue(c);
    } else {
      _remove(c);
      return;
    }
  }
}

void
RamCacheS3FIFO::_evict()
{
  // Main targets MAIN_PERCENT of the budget actually available to resident data -- the ghost's
  // metadata is reserved out of the configured size, so basing the split on raw _max_bytes would
  // (when the ghost is full) keep _m_bytes below the limit forever and starve the small queue.
  int64_t resident_budget = _max_bytes - _g_count * ENTRY_OVERHEAD;
  if (_m_bytes > resident_budget * _main_percent / 100 || _s_bytes == 0) {
    _evict_main();
  } else {
    _evict_small();
  }
}

int
RamCacheS3FIFO::get(CryptoHash *key, Ptr<IOBufferData> *ret_data, uint64_t auxkey)
{
  if (!_max_bytes) {
    return 0;
  }
  RamCacheS3FIFOEntry *e = _lookup(key, auxkey);
  if (e && e->seg != SEG_GHOST) {
    if (e->freq < FREQ_MAX) {
      e->freq++;
    }
    (*ret_data) = e->data;
    ts::Metrics::Counter::increment(cache_rsb.ram_cache_hits);
    ts::Metrics::Counter::increment(_stripe->cache_vol->vol_rsb.ram_cache_hits);
    return 1;
  }
  ts::Metrics::Counter::increment(cache_rsb.ram_cache_misses);
  ts::Metrics::Counter::increment(_stripe->cache_vol->vol_rsb.ram_cache_misses);
  return 0;
}

int
RamCacheS3FIFO::put(CryptoHash *key, IOBufferData *data, [[maybe_unused]] uint32_t len, bool, uint64_t auxkey)
{
  if (!_max_bytes) {
    return 0;
  }
  uint32_t size = data->block_size();
  uint32_t i    = key->slice32(3) % _nbuckets;

  // Walk the hash chain. A resident hit just counts the reference; a ghost hit is admitted fresh to
  // the main queue; an entry with the same key but a different aux key is stale and is discarded --
  // the same one-resident-copy-per-key contract LRU and CLFUS enforce.
  bool                 ghost_hit = false;
  RamCacheS3FIFOEntry *e         = _bucket[i].head;
  while (e) {
    if (e->key == *key) {
      if (e->auxkey == auxkey) {
        if (e->seg != SEG_GHOST) { // already resident: count the reference (matches LRU's bump)
          if (e->freq < FREQ_MAX) {
            e->freq++;
          }
          return 1;
        }
        RamCacheS3FIFOEntry *next = e->hash_link.next; // ghost hit: admit a fresh copy to main
        _remove(e);
        ghost_hit = true;
        e         = next;
        continue;
      }
      RamCacheS3FIFOEntry *next = e->hash_link.next; // stale aux key: discard the old copy
      _remove(e);
      e = next;
      continue;
    }
    e = e->hash_link.next;
  }

  // Keep total memory within the configured size: resident data+overhead plus the ghost's
  // metadata (ENTRY_OVERHEAD per remembered key) must fit. Evict resident first; if that is
  // exhausted but ghost metadata still pushes over, drop ghost keys too.
  int64_t need = ENTRY_OVERHEAD + size;
  while (_s_bytes + _m_bytes + _g_count * ENTRY_OVERHEAD + need > _max_bytes) {
    if (_s_bytes + _m_bytes > 0) {
      _evict();
    } else if (!_evict_ghost_one()) {
      break;
    }
  }
  if (_s_bytes + _m_bytes + _g_count * ENTRY_OVERHEAD + need > _max_bytes) {
    return 0; // object larger than the whole cache: do not admit, so the budget is never exceeded
  }

  RamCacheS3FIFOEntry *ne = THREAD_ALLOC(ramCacheS3FIFOEntryAllocator, this_ethread());
  ne->key                 = *key;
  ne->auxkey              = auxkey;
  ne->size                = size;
  ne->freq                = 0;
  ne->seg                 = ghost_hit ? SEG_MAIN : SEG_SMALL;
  ne->data                = data;
  _bucket[i].push(ne);
  _seg[ne->seg].enqueue(ne);
  if (ghost_hit) {
    _m_bytes += need;
  } else {
    _s_bytes += need;
  }
  _objects++;
  _nentries++;
  ts::Metrics::Gauge::increment(cache_rsb.ram_cache_bytes, need);
  ts::Metrics::Gauge::increment(_stripe->cache_vol->vol_rsb.ram_cache_bytes, need);

  if (_nentries > _nbuckets * 0.75) {
    ++_ibuckets;
    _resize_hashtable();
  }
  return 1;
}

int
RamCacheS3FIFO::fixup(const CryptoHash *key, uint64_t old_auxkey, uint64_t new_auxkey)
{
  if (!_max_bytes) {
    return 0;
  }
  RamCacheS3FIFOEntry *e = _lookup(key, old_auxkey);
  if (e && e->seg != SEG_GHOST) {
    e->auxkey = new_auxkey;
    return 1;
  }
  return 0;
}

RamCache *
new_RamCacheS3FIFO()
{
  return new RamCacheS3FIFO;
}

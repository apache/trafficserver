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

// Clocked Least Frequently Used by Size (CLFUS) replacement policy
// See https://cwiki.apache.org/confluence/display/TS/RamCache

#include "RamCacheCLFUS.h"
#include "P_RamCache.h"
#include "P_CacheInternal.h"
#include "StripeSM.h"
#include "iocore/eventsystem/IOBuffer.h"
#include "iocore/eventsystem/Tasks.h"
#include "fastlz/fastlz.h"
#include "tscore/CryptoHash.h"
#include "tscore/Regression.h"

#include <zlib.h>
#ifdef HAVE_LZMA_H
#include <lzma.h>
#endif
#ifdef HAVE_LZ4_H
#include <lz4.h>
#endif
#ifdef HAVE_ZSTD_H
#include <zstd.h>
// ZSTD_getErrorCode() and the ZSTD_error_* codes live here, not in zstd.h.
#include <zstd_errors.h>
#include <memory>
constexpr int CLFUS_ZSTD_LEVEL = 3;

namespace
{

// One-shot ZSTD_compress/ZSTD_decompress allocate and free a context on every
// call, so reuse a per-thread context instead. May return nullptr if zstd
// fails to allocate one; that failure is sticky for the life of the thread, so
// warn when it happens. The compression level is a sticky parameter set once
// here; no explicit ZSTD_CCtx_reset() is needed because ZSTD_compress2()
// starts a new session on every call (resets are only for interrupting the
// streaming API or changing sticky parameters).
ZSTD_CCtx *
zstd_cctx()
{
  thread_local std::unique_ptr<ZSTD_CCtx, size_t (*)(ZSTD_CCtx *)> ctx = [] {
    std::unique_ptr<ZSTD_CCtx, size_t (*)(ZSTD_CCtx *)> c{ZSTD_createCCtx(), ZSTD_freeCCtx};
    if (c && ZSTD_isError(ZSTD_CCtx_setParameter(c.get(), ZSTD_c_compressionLevel, CLFUS_ZSTD_LEVEL))) {
      c.reset();
    }
    if (!c) {
      Warning("unable to allocate zstd compression context; RAM cache entries will not be compressed on this thread");
    }
    return c;
  }();
  return ctx.get();
}

ZSTD_DCtx *
zstd_dctx()
{
  thread_local std::unique_ptr<ZSTD_DCtx, size_t (*)(ZSTD_DCtx *)> ctx = [] {
    std::unique_ptr<ZSTD_DCtx, size_t (*)(ZSTD_DCtx *)> c{ZSTD_createDCtx(), ZSTD_freeDCtx};
    if (!c) {
      Warning("unable to allocate zstd decompression context; compressed RAM cache entries will miss on this thread");
    }
    return c;
  }();
  return ctx.get();
}

} // end anonymous namespace
#endif

// #define CHECK_ACOUNTING 1 // very expensive double checking of all sizes

constexpr double   required_compression = 0.9;
constexpr double   required_shrink      = 0.8;
constexpr uint32_t history_hysteria     = 10;
constexpr uint32_t entry_overhead       = 256; // per-entry overhead to consider when computing cache value/size

#ifdef HAVE_LZMA_H
constexpr uint32_t lzma_base_memlimit = 64 * 1024 * 1024;
#endif

constexpr uint32_t average_value_over = 100;
constexpr uint32_t requeue_limit      = 100;

#ifdef DEBUG

namespace
{

DbgCtl dbg_ctl_ram_cache{"ram_cache"};
DbgCtl dbg_ctl_ram_cache_compare{"ram_cache_compare"};

} // end anonymous namespace

#endif

constexpr uint64_t
requeue_hits(const uint64_t hits)
{
  return hits ? (hits - 1) : 0;
}

constexpr double
cache_value_hits_size(const uint64_t hits, const uint32_t size)
{
  return static_cast<double>(hits + 1) / (size + entry_overhead);
}

constexpr double
cache_value(const RamCacheCLFUSEntry *const e)
{
  return cache_value_hits_size(e->hits, e->size);
}

int64_t
RamCacheCLFUS::size() const
{
  int64_t s = 0;
  for (int i = 0; i < 2; i++) {
    forl_LL(RamCacheCLFUSEntry, e, this->_lru[i])
    {
      s += sizeof(*e);
      if (e->data) {
        s += sizeof(*e->data);
        s += e->data->block_size();
      }
    }
  }
  return s;
}

class RamCacheCLFUSCompressor : public Continuation
{
public:
  RamCacheCLFUS *rc;
  int            mainEvent(int event, Event *e);

  RamCacheCLFUSCompressor(RamCacheCLFUS *arc) : rc(arc) { SET_HANDLER(&RamCacheCLFUSCompressor::mainEvent); }
};

int
RamCacheCLFUSCompressor::mainEvent(int /* event ATS_UNUSED */, Event *e)
{
  // The codec is validated once in ink_cache_init(), before any cache exists.
  if (cache_config_ram_cache_compress_percent) {
    rc->compress_entries(e->ethread);
  }
  return EVENT_CONT;
}

ClassAllocator<RamCacheCLFUSEntry, false> ramCacheCLFUSEntryAllocator("RamCacheCLFUSEntry");

static const int bucket_sizes[] = {127,      251,      509,       1021,      2039,      4093,       8191,      16381,   32749,
                                   65521,    131071,   262139,    524287,    1048573,   2097143,    4194301,   8388593, 16777213,
                                   33554393, 67108859, 134217689, 268435399, 536870909, 1073741789, 2147483647};

// Only safe when init() did not schedule the background compressor, i.e. when
// cache_config_ram_cache_compress was CACHE_COMPRESSION_NONE at init() time.
// That scheduled RamCacheCLFUSCompressor holds a raw back-pointer to this
// object and nothing cancels it, so destroying a cache that has one would
// leave it dangling. Cancelling the event here would not be enough either: the
// continuation carries no mutex, so EventProcessor::schedule leaves the event
// with none and it can be running compress_entries() on an ET_TASK thread
// while this destructor runs. Making that safe means giving the compressor its
// own ProxyMutex and cancelling the retained Event under it -- not the stripe
// mutex, because Mutex_unlock() only decrements nthread_holding, so a
// continuation dispatched holding stripe->mutex would keep the stripe locked
// across the codec call and defeat the lock drop in compress_entries(). Not
// worth doing while production never destroys a RamCacheCLFUS -- these live
// for the lifetime of their StripeSM. Unit tests that construct one directly
// must init() with compression off and drive compress_entries() synchronously.
RamCacheCLFUS::~RamCacheCLFUS()
{
  // Entries are pool-allocated without running their destructor, so release the
  // data reference explicitly before returning each one to the allocator, then
  // free the hash table and the seen filter.
  // History entries (lru[1]) hold no data and were never counted.
  while (RamCacheCLFUSEntry *e = this->_lru[0].dequeue()) {
    this->_bytes -= e->size + entry_overhead;
    ts::Metrics::Gauge::decrement(cache_rsb.ram_cache_bytes, e->size);
    ts::Metrics::Gauge::decrement(stripe->cache_vol->vol_rsb.ram_cache_bytes, e->size);
    this->_objects--;
    e->data = nullptr;
    THREAD_FREE(e, ramCacheCLFUSEntryAllocator, this_thread());
  }
  while (RamCacheCLFUSEntry *e = this->_lru[1].dequeue()) {
    this->_history--;
    e->data = nullptr;
    THREAD_FREE(e, ramCacheCLFUSEntryAllocator, this_thread());
  }
  ats_free(this->_bucket);
  ats_free(this->_seen);
}

void
RamCacheCLFUS::_resize_hashtable()
{
  int anbuckets = bucket_sizes[this->_ibuckets];
  DDbg(dbg_ctl_ram_cache, "resize hashtable %d", anbuckets);
  int64_t s                                        = anbuckets * sizeof(DList(RamCacheCLFUSEntry, hash_link));
  DList(RamCacheCLFUSEntry, hash_link) *new_bucket = static_cast<DList(RamCacheCLFUSEntry, hash_link) *>(ats_malloc(s));
  memset(static_cast<void *>(new_bucket), 0, s);
  if (this->_bucket) {
    for (int64_t i = 0; i < this->_nbuckets; i++) {
      RamCacheCLFUSEntry *e = nullptr;
      while ((e = this->_bucket[i].pop())) {
        new_bucket[e->key.slice32(3) % anbuckets].push(e);
      }
    }
    ats_free(this->_bucket);
  }
  this->_bucket   = new_bucket;
  this->_nbuckets = anbuckets;
  ats_free(this->_seen);
  if (cache_config_ram_cache_use_seen_filter) {
    int size    = bucket_sizes[this->_ibuckets] * sizeof(uint16_t);
    this->_seen = static_cast<uint16_t *>(ats_malloc(size));
    memset(this->_seen, 0, size);
  }
}

void
RamCacheCLFUS::init(int64_t abytes, StripeSM *astripe)
{
  ink_assert(astripe != nullptr);
  stripe           = astripe;
  this->_max_bytes = abytes;
  DDbg(dbg_ctl_ram_cache, "initializing ram_cache %" PRId64 " bytes", abytes);
  if (!this->_max_bytes) {
    return;
  }
  this->_resize_hashtable();
  if (cache_config_ram_cache_compress) {
    eventProcessor.schedule_every(new RamCacheCLFUSCompressor(this), HRTIME_SECOND, ET_TASK);
  }
}

#ifdef CHECK_ACOUNTING
static void
check_accounting(RamCacheCLFUS *c)
{
  int64_t             x = 0, xsize = 0, h = 0;
  RamCacheCLFUSEntry *y = c->lru[0].head;
  while (y) {
    x++;
    xsize += y->size + entry_overhead;
    y      = y->lru_link.next;
  }
  y = c->lru[1].head;
  while (y) {
    h++;
    y = y->lru_link.next;
  }
  ink_assert(x == c->objects);
  ink_assert(xsize == c->bytes);
  ink_assert(h == c->history);
}
#else
#define check_accounting(_c)
#endif

namespace
{

// Record a RAM cache decompression failure. This is data corruption or a codec
// error rather than an ordinary miss, so it has to be visible outside a debug
// build: a warning carrying the codec's own diagnosis, which tells a corrupt
// frame apart from a bookkeeping error in e->len, plus the global and
// per-volume counters. Call while the entry is still intact. Throttled through
// the site-wide facility so it honors proxy.config.log.throttling_interval_msec
// and reports its own suppression count.
void
note_decompress_failure(StripeSM *stripe, const CryptoHash *key, const RamCacheCLFUSEntry *e, const char *detail)
{
  SiteThrottledWarning("RAM cache decompression failed: type %d len %u compressed_len %u key %X: %s; entry dropped",
                       static_cast<int>(e->flag_bits.compressed), e->len, e->compressed_len, key->slice32(3), detail);
  ts::Metrics::Counter::increment(cache_rsb.ram_cache_decompress_failures);
  ts::Metrics::Counter::increment(stripe->cache_vol->vol_rsb.ram_cache_decompress_failures);
}

} // end anonymous namespace

int
RamCacheCLFUS::get(CryptoHash *key, Ptr<IOBufferData> *ret_data, uint64_t auxkey)
{
  if (!this->_max_bytes) {
    return 0;
  }
  int64_t             i = key->slice32(3) % this->_nbuckets;
  RamCacheCLFUSEntry *e = this->_bucket[i].head;
  char               *b = nullptr;
  while (e) {
    if (e->key == *key && e->auxkey == auxkey) {
      this->_move_compressed(e);
      if (!e->flag_bits.lru) { // in memory
        if (cache_value(e) > this->_average_value) {
          this->_lru[e->flag_bits.lru].remove(e);
          this->_lru[e->flag_bits.lru].enqueue(e);
        }
        e->hits++;
        uint32_t ram_hit_state = RAM_HIT_COMPRESS_NONE;
        if (e->flag_bits.compressed) {
          b = static_cast<char *>(ats_malloc(e->len));
          switch (e->flag_bits.compressed) {
          default:
            note_decompress_failure(stripe, key, e, "no decoder for this compression type");
            goto Lfailed;
          case CACHE_COMPRESSION_FASTLZ: {
            int l  = static_cast<int>(e->len);
            int rc = fastlz_decompress(e->data->data(), e->compressed_len, b, l);
            if (l != rc) {
              char detail[128];
              snprintf(detail, sizeof(detail), "fastlz_decompress produced %d bytes, expected %d", rc, l);
              note_decompress_failure(stripe, key, e, detail);
              goto Lfailed;
            }
            ram_hit_state = RAM_HIT_COMPRESS_FASTLZ;
            break;
          }
          case CACHE_COMPRESSION_LIBZ: {
            uLongf l  = e->len;
            int    rc = uncompress(reinterpret_cast<Bytef *>(b), &l, reinterpret_cast<Bytef *>(e->data->data()), e->compressed_len);
            if (Z_OK != rc) {
              char detail[128];
              snprintf(detail, sizeof(detail), "uncompress: %s", zError(rc));
              note_decompress_failure(stripe, key, e, detail);
              goto Lfailed;
            }
            ram_hit_state = RAM_HIT_COMPRESS_LIBZ;
            break;
          }
#ifdef HAVE_LZMA_H
          case CACHE_COMPRESSION_LIBLZMA: {
            size_t   l = static_cast<size_t>(e->len), ipos = 0, opos = 0;
            uint64_t memlimit = e->len * 2 + lzma_base_memlimit;
            lzma_ret rc = lzma_stream_buffer_decode(&memlimit, 0, nullptr, reinterpret_cast<uint8_t *>(e->data->data()), &ipos,
                                                    e->compressed_len, reinterpret_cast<uint8_t *>(b), &opos, l);
            if (LZMA_OK != rc) {
              char detail[128];
              snprintf(detail, sizeof(detail), "lzma_stream_buffer_decode returned %d, wrote %zu of %zu output bytes",
                       static_cast<int>(rc), opos, l);
              note_decompress_failure(stripe, key, e, detail);
              goto Lfailed;
            }
            ram_hit_state = RAM_HIT_COMPRESS_LIBLZMA;
            break;
          }
#endif
#ifdef HAVE_LZ4_H
          case CACHE_COMPRESSION_LZ4: {
            int l  = static_cast<int>(e->len);
            int rc = LZ4_decompress_safe(e->data->data(), b, e->compressed_len, l);
            if (l != rc) {
              // A negative return is a malformed frame; a smaller non-negative
              // one means e->len disagrees with the frame's content.
              char detail[128];
              snprintf(detail, sizeof(detail), "LZ4_decompress_safe returned %d, expected %d", rc, l);
              note_decompress_failure(stripe, key, e, detail);
              goto Lfailed;
            }
            ram_hit_state = RAM_HIT_COMPRESS_LZ4;
            break;
          }
#endif
#ifdef HAVE_ZSTD_H
          case CACHE_COMPRESSION_ZSTD: {
            size_t     l    = static_cast<size_t>(e->len);
            ZSTD_DCtx *dctx = zstd_dctx();
            if (dctx == nullptr) {
              // This thread can't decompress, but the entry itself is fine:
              // miss instead of evicting it.
              ats_free(b);
              goto Lerror;
            }
            size_t ll = ZSTD_decompressDCtx(dctx, b, l, e->data->data(), e->compressed_len);
            if (ZSTD_isError(ll)) {
              char detail[128];
              snprintf(detail, sizeof(detail), "ZSTD_decompressDCtx: %s", ZSTD_getErrorName(ll));
              note_decompress_failure(stripe, key, e, detail);
              goto Lfailed;
            }
            if (l != ll) {
              char detail[128];
              snprintf(detail, sizeof(detail), "ZSTD_decompressDCtx produced %zu bytes, expected %zu", ll, l);
              note_decompress_failure(stripe, key, e, detail);
              goto Lfailed;
            }
            ram_hit_state = RAM_HIT_COMPRESS_ZSTD;
            break;
          }
#endif
          }
          IOBufferData *data = new_xmalloc_IOBufferData(b, e->len);
          data->_mem_type    = DEFAULT_ALLOC;
          if (!e->flag_bits.copy) { // don't bother if we have to copy anyway
            int64_t delta  = (static_cast<int64_t>(e->compressed_len)) - static_cast<int64_t>(e->size);
            this->_bytes  += delta;
            ts::Metrics::Gauge::increment(cache_rsb.ram_cache_bytes, delta);
            ts::Metrics::Gauge::increment(stripe->cache_vol->vol_rsb.ram_cache_bytes, delta);
            e->size = e->compressed_len;
            check_accounting(this);
            e->flag_bits.compressed = 0;
            e->data                 = data;
          }
          (*ret_data) = data;
        } else {
          if (e->flag_bits.copy) {
            (*ret_data) = copy_data_out(e->data.get(), e->len);
          } else {
            (*ret_data) = e->data;
          }
        }
        ts::Metrics::Counter::increment(cache_rsb.ram_cache_hits);
        ts::Metrics::Counter::increment(stripe->cache_vol->vol_rsb.ram_cache_hits);
        DDbg(dbg_ctl_ram_cache, "get %X %" PRId64 " size %d HIT", key->slice32(3), auxkey, e->size);
        return ram_hit_state;
      } else {
        ts::Metrics::Counter::increment(cache_rsb.ram_cache_misses);
        ts::Metrics::Counter::increment(stripe->cache_vol->vol_rsb.ram_cache_misses);
        DDbg(dbg_ctl_ram_cache, "get %X %" PRId64 " HISTORY", key->slice32(3), auxkey);
        return 0;
      }
    }
    assert(e != e->hash_link.next);
    e = e->hash_link.next;
  }
  DDbg(dbg_ctl_ram_cache, "get %X %" PRId64 " MISS", key->slice32(3), auxkey);
Lerror:
  ts::Metrics::Counter::increment(cache_rsb.ram_cache_misses);
  ts::Metrics::Counter::increment(stripe->cache_vol->vol_rsb.ram_cache_misses);

  return 0;
Lfailed:
  // Every branch above reported the failure through note_decompress_failure()
  // while the entry was still intact; this only tears it down.
  ats_free(b);
  this->_destroy(e);
  DDbg(dbg_ctl_ram_cache, "get %X %" PRId64 " Z_ERR", key->slice32(3), auxkey);
  goto Lerror;
}

void
RamCacheCLFUS::_tick()
{
  RamCacheCLFUSEntry *e = this->_lru[1].dequeue();
  if (!e) {
    return;
  }
  e->hits >>= 1;
  if (e->hits) {
    e->hits = requeue_hits(e->hits);
    this->_lru[1].enqueue(e);
  } else {
    goto Lfree;
  }
  if (this->_history <= this->_objects + history_hysteria) {
    return;
  }
  e = this->_lru[1].dequeue();
Lfree:
  if (!e) { // e may be nullptr after e= lru[1].dequeue()
    return;
  }
  e->flag_bits.lru = 0;
  this->_history--;
  uint32_t b = e->key.slice32(3) % this->_nbuckets;
  this->_bucket[b].remove(e);
  DDbg(dbg_ctl_ram_cache, "put %X %" PRId64 " size %d FREED", e->key.slice32(3), e->auxkey, e->size);
  THREAD_FREE(e, ramCacheCLFUSEntryAllocator, this_thread());
}

void
RamCacheCLFUS::_victimize(RamCacheCLFUSEntry *e)
{
  this->_objects--;
  DDbg(dbg_ctl_ram_cache, "put %X %" PRId64 " size %d VICTIMIZED", e->key.slice32(3), e->auxkey, e->size);
  e->data          = nullptr;
  e->flag_bits.lru = 1;
  this->_lru[1].enqueue(e);
  this->_history++;
}

void
RamCacheCLFUS::_move_compressed(RamCacheCLFUSEntry *e)
{
  if (e == this->_compressed) {
    if (this->_compressed->lru_link.next) {
      this->_compressed = this->_compressed->lru_link.next;
    } else {
      this->_ncompressed--;
      this->_compressed = this->_compressed->lru_link.prev;
    }
  }
}

RamCacheCLFUSEntry *
RamCacheCLFUS::_destroy(RamCacheCLFUSEntry *e)
{
  RamCacheCLFUSEntry *ret = e->hash_link.next;
  this->_move_compressed(e);
  this->_lru[e->flag_bits.lru].remove(e);
  if (!e->flag_bits.lru) {
    this->_objects--;
    this->_bytes -= e->size + entry_overhead;
    ts::Metrics::Gauge::decrement(cache_rsb.ram_cache_bytes, e->size);
    ts::Metrics::Gauge::decrement(stripe->cache_vol->vol_rsb.ram_cache_bytes, e->size);
    e->data = nullptr;
  } else {
    this->_history--;
  }
  uint32_t b = e->key.slice32(3) % this->_nbuckets;
  this->_bucket[b].remove(e);
  DDbg(dbg_ctl_ram_cache, "put %X %" PRId64 " DESTROYED", e->key.slice32(3), e->auxkey);
  THREAD_FREE(e, ramCacheCLFUSEntryAllocator, this_thread());
  return ret;
}

void
RamCacheCLFUS::compress_entries(EThread *thread, int do_at_most)
{
  if (!cache_config_ram_cache_compress) {
    return;
  }
  ink_assert(stripe != nullptr);
#ifdef HAVE_ZSTD_H
  if (cache_config_ram_cache_compress == CACHE_COMPRESSION_ZSTD && zstd_cctx() == nullptr) {
    // The per-thread context failed to allocate, and that failure is sticky
    // for the life of the thread this cache's compressor is pinned to, so no
    // entry can be compressed on this pass or any later one. Skip the pass
    // rather than walking every entry -- dropping and retaking the stripe lock
    // and allocating a compressBound()-sized buffer for each -- only to fail
    // every time. The entries are left untouched: this says nothing about the
    // data, so they stay eligible. Deliberately not counted in
    // ram_cache.compress.failure: that counter means entries the codec could
    // not compress, and incrementing here once per pass would make it climb
    // once a second per stripe for the life of the thread. The one-time
    // Warning in zstd_cctx() is what reports this condition.
    return;
  }
#endif
  MUTEX_TAKE_LOCK(stripe->mutex, thread);
  if (!this->_compressed) {
    this->_compressed  = this->_lru[0].head;
    this->_ncompressed = 0;
  }
  float target = (cache_config_ram_cache_compress_percent / 100.0) * this->_objects;
  int   n      = 0;
  char *b = nullptr, *bb = nullptr;
  while (this->_compressed && target > this->_ncompressed) {
    RamCacheCLFUSEntry *e = this->_compressed;
    if (e->flag_bits.incompressible || e->flag_bits.compressed) {
      goto Lcontinue;
    }
    n++;
    if (do_at_most < n) {
      break;
    }
    {
      e->compressed_len = e->size;
      uint32_t l        = 0;
      int      ctype    = cache_config_ram_cache_compress;
      switch (ctype) {
      default:
        goto Lcontinue;
      case CACHE_COMPRESSION_FASTLZ:
        if (e->len < 16) {
          // fastlz cannot compress inputs this small; decide while the entry
          // is still lock-protected rather than in the unlocked region below.
          e->flag_bits.incompressible = 1;
          goto Lcontinue;
        }
        l = static_cast<uint32_t>(static_cast<double>(e->len) * 1.05 + 66);
        break;
      case CACHE_COMPRESSION_LIBZ:
        l = static_cast<uint32_t>(compressBound(e->len));
        break;
#ifdef HAVE_LZMA_H
      case CACHE_COMPRESSION_LIBLZMA:
        l = static_cast<uint32_t>(lzma_stream_buffer_bound(e->len));
        break;
#endif
#ifdef HAVE_LZ4_H
      case CACHE_COMPRESSION_LZ4:
        l = static_cast<uint32_t>(LZ4_compressBound(e->len));
        break;
#endif
#ifdef HAVE_ZSTD_H
      case CACHE_COMPRESSION_ZSTD:
        l = static_cast<uint32_t>(ZSTD_compressBound(e->len));
        break;
#endif
      }
      // store transient data for lock release
      Ptr<IOBufferData> edata = e->data;
      uint32_t          elen  = e->len;
      CryptoHash        key   = e->key;
      MUTEX_UNTAKE_LOCK(stripe->mutex, thread);
      b           = static_cast<char *>(ats_malloc(l));
      bool failed = false;
      // Distinguishes a transient, data-independent failure (the codec could
      // not allocate its working memory) from the codec rejecting this data.
      // Only the latter says anything about the entry.
      bool transient = false;
      switch (ctype) {
      default:
        // The bound switch above filtered unknown types; this is unreachable,
        // but must not jump to Lfailed from this unlocked region.
        failed = true;
        break;
      case CACHE_COMPRESSION_FASTLZ:
        if ((l = fastlz_compress(edata->data(), elen, b)) <= 0) {
          failed = true;
        }
        break;
      case CACHE_COMPRESSION_LIBZ: {
        uLongf ll = l;
        int    rc = compress(reinterpret_cast<Bytef *>(b), &ll, reinterpret_cast<Bytef *>(edata->data()), elen);
        if (Z_OK != rc) {
          failed    = true;
          transient = (rc == Z_MEM_ERROR);
        }
        l = static_cast<int>(ll);
        break;
      }
#ifdef HAVE_LZMA_H
      case CACHE_COMPRESSION_LIBLZMA: {
        size_t pos = 0, ll = l;
        if (LZMA_OK != lzma_easy_buffer_encode(LZMA_PRESET_DEFAULT, LZMA_CHECK_NONE, nullptr,
                                               reinterpret_cast<uint8_t *>(edata->data()), elen, reinterpret_cast<uint8_t *>(b),
                                               &pos, ll)) {
          failed = true;
        }
        l = static_cast<int>(pos);
        break;
      }
#endif
#ifdef HAVE_LZ4_H
      case CACHE_COMPRESSION_LZ4: {
        int ll = l;
        if ((l = LZ4_compress_default(edata->data(), b, elen, ll)) == 0) {
          failed = true;
        }
        break;
      }
#endif
#ifdef HAVE_ZSTD_H
      case CACHE_COMPRESSION_ZSTD: {
        // The pass-level check above already proved this thread has a context,
        // and the context is thread_local while the pass never changes thread.
        ZSTD_CCtx *cctx = zstd_cctx();
        ink_assert(cctx != nullptr);
        size_t zret = ZSTD_compress2(cctx, b, l, edata->data(), elen);
        if (ZSTD_isError(zret)) {
          failed = true;
          // ZSTD_createCCtx() allocates only the context struct; the much
          // larger working buffers are allocated on first use and grow with
          // the input, so this is the realistic out-of-memory path.
          transient = (ZSTD_getErrorCode(zret) == ZSTD_error_memory_allocation);
        } else {
          l = static_cast<uint32_t>(zret);
        }
        break;
      }
#endif
      }
      MUTEX_TAKE_LOCK(stripe->mutex, thread);
      // See if the entry is still around; it may have been freed while the
      // lock was dropped, so this must be checked before anything writes to
      // it (including the failure marking below).
      {
        uint32_t            i  = key.slice32(3) % this->_nbuckets;
        RamCacheCLFUSEntry *ee = this->_bucket[i].head;
        while (ee) {
          if (ee->key == key && ee->data == edata) {
            break;
          }
          ee = ee->hash_link.next;
        }
        if (!ee || ee != e) {
          ats_free(b);
          e = this->_compressed;
          if (!e) {
            // The cursor was invalidated while the lock was dropped.
            break;
          }
          goto Lcontinue;
        }
      }
      if (failed) {
        ts::Metrics::Counter::increment(cache_rsb.ram_cache_compress_failures);
        ts::Metrics::Counter::increment(stripe->cache_vol->vol_rsb.ram_cache_compress_failures);
        if (transient) {
          // An allocation failure inside the codec is not a property of the
          // data, so do not record it as permanently incompressible; leave the
          // entry eligible for a later pass.
          ats_free(b);
          goto Lcontinue;
        }
        goto Lfailed;
      }
      if (l > required_compression * e->len) {
        e->flag_bits.incompressible = true;
      }
      if (l > required_shrink * e->size) {
        goto Lfailed;
      }
      if (l < e->len) {
        e->flag_bits.compressed = cache_config_ram_cache_compress;
        bb                      = static_cast<char *>(ats_malloc(l));
        memcpy(bb, b, l);
        ats_free(b);
        e->compressed_len  = l;
        int64_t delta      = (static_cast<int64_t>(l)) - static_cast<int64_t>(e->size);
        this->_bytes      += delta;
        ts::Metrics::Gauge::increment(cache_rsb.ram_cache_bytes, delta);
        ts::Metrics::Gauge::increment(stripe->cache_vol->vol_rsb.ram_cache_bytes, delta);
        e->size = l;
      } else {
        ats_free(b);
        e->flag_bits.compressed = 0;
        bb                      = static_cast<char *>(ats_malloc(e->len));
        memcpy(bb, e->data->data(), e->len);
        int64_t delta  = (static_cast<int64_t>(e->len)) - static_cast<int64_t>(e->size);
        this->_bytes  += delta;
        ts::Metrics::Gauge::increment(cache_rsb.ram_cache_bytes, delta);
        ts::Metrics::Gauge::increment(stripe->cache_vol->vol_rsb.ram_cache_bytes, delta);
        e->size = e->len;
        l       = e->len;
      }
      e->data            = new_xmalloc_IOBufferData(bb, l);
      e->data->_mem_type = DEFAULT_ALLOC;
      check_accounting(this);
    }
    goto Lcontinue;
  Lfailed:
    ats_free(b);
    e->flag_bits.incompressible = 1;
  Lcontinue:;
    DDbg(dbg_ctl_ram_cache, "compress %X %" PRId64 " %d %d %d %d %d", e->key.slice32(3), e->auxkey, e->flag_bits.incompressible,
         e->flag_bits.compressed, e->len, e->compressed_len, this->_ncompressed);
    if (!e->lru_link.next) {
      break;
    }
    this->_compressed = e->lru_link.next;
    this->_ncompressed++;
  }
  MUTEX_UNTAKE_LOCK(stripe->mutex, thread);
  return;
}

void
RamCacheCLFUS::_requeue_victims(Que(RamCacheCLFUSEntry, lru_link) & victims)
{
  RamCacheCLFUSEntry *victim = nullptr;
  while ((victim = victims.dequeue())) {
    this->_bytes += victim->size + entry_overhead;
    ts::Metrics::Gauge::increment(cache_rsb.ram_cache_bytes, victim->size);
    ts::Metrics::Gauge::increment(stripe->cache_vol->vol_rsb.ram_cache_bytes, victim->size);
    victim->hits = requeue_hits(victim->hits);
    this->_lru[0].enqueue(victim);
  }
}

int
RamCacheCLFUS::put(CryptoHash *key, IOBufferData *data, uint32_t len, bool copy, uint64_t auxkey)
{
  if (!this->_max_bytes) {
    return 0;
  }
  uint32_t            i            = key->slice32(3) % this->_nbuckets;
  RamCacheCLFUSEntry *e            = this->_bucket[i].head;
  uint32_t            size         = copy ? len : data->block_size();
  double              victim_value = 0;
  while (e) {
    if (e->key == *key) {
      if (e->auxkey == auxkey) {
        break;
      } else {
        e = this->_destroy(e); // discard when aux keys conflict
        continue;
      }
    }
    e = e->hash_link.next;
  }
  if (e) {
    e->hits++;
    if (!e->flag_bits.lru) { // already in cache
      this->_move_compressed(e);
      this->_lru[e->flag_bits.lru].remove(e);
      this->_lru[e->flag_bits.lru].enqueue(e);
      if (copy && e->flag_bits.copy && !e->flag_bits.compressed) {
        // Already an uncompressed private copy of this object: get() never
        // exposes that buffer, so re-copying it (two requests that both missed
        // and both read the object from disk) changes nothing. Everything
        // below is a no-op for such an entry apart from the allocation and
        // memcpy -- size is already len, so delta is 0 -- except when the
        // entry is compressed, where the swap is what decompresses it and
        // skipping it would leave compressed bytes behind a cleared flag.
        DDbg(dbg_ctl_ram_cache, "put %X %" PRId64 " size %d HIT (private, unchanged)", key->slice32(3), auxkey, e->size);
        return 1;
      }
      int64_t delta  = (static_cast<int64_t>(size)) - static_cast<int64_t>(e->size);
      this->_bytes  += delta;
      ts::Metrics::Gauge::increment(cache_rsb.ram_cache_bytes, delta);
      ts::Metrics::Gauge::increment(stripe->cache_vol->vol_rsb.ram_cache_bytes, delta);
      if (!copy) {
        e->size = size;
        e->data = data;
      } else {
        e->data = copy_data_in(data, len);
        e->size = size;
      }
      e->len = len; // get() and the compressor read e->len bytes out of e->data
      check_accounting(this);
      e->flag_bits.copy       = copy;
      e->flag_bits.compressed = 0;
      DDbg(dbg_ctl_ram_cache, "put %X %" PRId64 " size %d HIT", key->slice32(3), auxkey, e->size);
      return 1;
    } else {
      this->_lru[1].remove(e);
      if (cache_value(e) < this->_average_value) {
        this->_lru[1].enqueue(e);
        return 0;
      }
    }
  }
  Que(RamCacheCLFUSEntry, lru_link) victims;
  RamCacheCLFUSEntry *victim        = nullptr;
  int                 requeue_count = requeue_limit;
  if (!this->_lru[1].head) { // initial fill
    if (this->_bytes + size <= this->_max_bytes) {
      goto Linsert;
    }
  }
  if (!e && cache_config_ram_cache_use_seen_filter) {
    uint32_t s     = key->slice32(3) % bucket_sizes[this->_ibuckets];
    uint16_t k     = key->slice32(3) >> 16;
    uint16_t kk    = this->_seen[s];
    this->_seen[s] = k;
    if (this->_history >= this->_objects && kk != k) {
      DDbg(dbg_ctl_ram_cache, "put %X %" PRId64 " size %d UNSEEN", key->slice32(3), auxkey, size);
      return 0;
    }
  }
  while (true) {
    victim = this->_lru[0].dequeue();
    if (!victim) {
      if (this->_bytes + size <= this->_max_bytes) {
        goto Linsert;
      }
      if (e) {
        this->_lru[1].enqueue(e);
      }
      this->_requeue_victims(victims);
      DDbg(dbg_ctl_ram_cache, "put %X %" PRId64 " NO VICTIM", key->slice32(3), auxkey);
      return 0;
    }
    this->_average_value = (cache_value(victim) + (this->_average_value * (average_value_over - 1))) / average_value_over;
    if (cache_value(victim) > this->_average_value && requeue_count-- > 0) {
      this->_lru[0].enqueue(victim);
      continue;
    }
    this->_bytes -= victim->size + entry_overhead;
    ts::Metrics::Gauge::decrement(cache_rsb.ram_cache_bytes, victim->size);
    ts::Metrics::Gauge::decrement(stripe->cache_vol->vol_rsb.ram_cache_bytes, victim->size);
    victims.enqueue(victim);
    if (victim == this->_compressed) {
      this->_compressed = nullptr;
    } else {
      this->_ncompressed--;
    }
    victim_value += cache_value(victim);
    this->_tick();
    if (!e) {
      goto Lhistory;
    } else { // e from history
      DDbg(dbg_ctl_ram_cache_compare, "put %f %f", victim_value, cache_value(e));
      if (this->_bytes + victim->size + size > this->_max_bytes && victim_value > cache_value(e)) {
        this->_requeue_victims(victims);
        this->_lru[1].enqueue(e);
        DDbg(dbg_ctl_ram_cache, "put %X %" PRId64 " size %d INC %" PRId64 " HISTORY", key->slice32(3), auxkey, e->size, e->hits);
        return 0;
      }
    }
    if (this->_bytes + size <= this->_max_bytes) {
      goto Linsert;
    }
  }
Linsert:
  while ((victim = victims.dequeue())) {
    if (this->_bytes + size + victim->size <= this->_max_bytes) {
      this->_bytes += victim->size + entry_overhead;
      ts::Metrics::Gauge::increment(cache_rsb.ram_cache_bytes, victim->size);
      ts::Metrics::Gauge::increment(stripe->cache_vol->vol_rsb.ram_cache_bytes, victim->size);
      victim->hits = requeue_hits(victim->hits);
      this->_lru[0].enqueue(victim);
    } else {
      this->_victimize(victim);
    }
  }
  if (e) {
    this->_history--; // move from history
  } else {
    e         = THREAD_ALLOC(ramCacheCLFUSEntryAllocator, this_ethread());
    e->key    = *key;
    e->auxkey = auxkey;
    e->hits   = 1;
    this->_bucket[i].push(e);
    if (this->_objects > this->_nbuckets) {
      ++this->_ibuckets;
      this->_resize_hashtable();
    }
  }
  check_accounting(this);
  e->flags = 0;
  if (!copy) {
    e->data = data;
  } else {
    e->data = copy_data_in(data, len);
  }
  e->flag_bits.copy  = copy;
  this->_bytes      += size + entry_overhead;
  ts::Metrics::Gauge::increment(cache_rsb.ram_cache_bytes, size);
  ts::Metrics::Gauge::increment(stripe->cache_vol->vol_rsb.ram_cache_bytes, size);
  e->size = size;
  this->_objects++;
  this->_lru[0].enqueue(e);
  e->len = len;
  check_accounting(this);
  DDbg(dbg_ctl_ram_cache, "put %X %" PRId64 " size %d INSERTED", key->slice32(3), auxkey, e->size);
  return 1;
Lhistory:
  this->_requeue_victims(victims);
  check_accounting(this);
  e         = THREAD_ALLOC(ramCacheCLFUSEntryAllocator, this_ethread());
  e->key    = *key;
  e->auxkey = auxkey;
  e->hits   = 1;
  e->size   = data->block_size();
  e->flags  = 0;
  this->_bucket[i].push(e);
  e->flag_bits.lru = 1;
  this->_lru[1].enqueue(e);
  this->_history++;
  DDbg(dbg_ctl_ram_cache, "put %X %" PRId64 " HISTORY", key->slice32(3), auxkey);
  return 0;
}

int
RamCacheCLFUS::fixup(const CryptoHash *key, uint64_t old_auxkey, uint64_t new_auxkey)
{
  if (!this->_max_bytes) {
    return 0;
  }
  uint32_t            i = key->slice32(3) % this->_nbuckets;
  RamCacheCLFUSEntry *e = this->_bucket[i].head;
  while (e) {
    if (e->key == *key && e->auxkey == old_auxkey) {
      e->auxkey = new_auxkey;
      return 1;
    }
    e = e->hash_link.next;
  }
  return 0;
}

RamCache *
new_RamCacheCLFUS()
{
  RamCacheCLFUS *r = new RamCacheCLFUS;
  return r;
}

// Guards against PR #11733-style regressions of the CLFUS value metric: the value density
// must be computed in floating point. Integer division truncates (hits + 1) / (size + overhead)
// to 0 for normal object sizes, zeroing the metric and silently collapsing CLFUS to FIFO (no
// promote-on-hit, no clock second chance, no value-based ghost re-admission).
REGRESSION_TEST(ram_cache_clfus_value)([[maybe_unused]] RegressionTest *t, [[maybe_unused]] int level, int *pstatus)
{
  *pstatus = REGRESSION_TEST_FAILED;

  constexpr float v_one   = cache_value_hits_size(1u, 16384u);   // a typical 16 KiB object, seen once
  constexpr float v_hot   = cache_value_hits_size(100u, 16384u); // same size, many more hits
  constexpr float v_small = cache_value_hits_size(10u, 1024u);   // smaller object, equal hits
  constexpr float v_large = cache_value_hits_size(10u, 16384u);

  // A non-zero fraction: the integer-division regression makes this exactly 0.0f.
  static_assert(v_one > 0.0f, "CLFUS value metric truncated to zero (integer division)");
  static_assert(v_hot > v_one, "CLFUS value metric does not increase with hits");
  static_assert(v_small > v_large, "CLFUS value metric does not decrease with size");

  *pstatus = REGRESSION_TEST_PASSED;
}

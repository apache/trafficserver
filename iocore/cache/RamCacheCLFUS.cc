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

#include "P_Cache.h"
#include "I_Tasks.h"
#include "tscore/fastlz.h"
#ifdef HAVE_ZLIB_H
#include <zlib.h>
#endif
#ifdef HAVE_LZMA_H
#include <lzma.h>
#endif

#define REQUIRED_COMPRESSION 0.9 // must get to this size or declared incompressible
#define REQUIRED_SHRINK 0.8      // must get to this size or keep original buffer (with padding)
#define HISTORY_HYSTERIA 10      // extra temporary history
#define ENTRY_OVERHEAD 256       // per-entry overhead to consider when computing cache value/size
#define LZMA_BASE_MEMLIMIT (64 * 1024 * 1024)
//#define CHECK_ACOUNTING 1 // very expensive double checking of all sizes

#define REQUEUE_HITS(_h) ((_h) ? ((_h)-1) : 0)
#define CACHE_VALUE_HITS_SIZE(_h, _s) ((float)((_h) + 1) / ((_s) + ENTRY_OVERHEAD))
#define CACHE_VALUE(_x) CACHE_VALUE_HITS_SIZE((_x)->hits, (_x)->size)

#define AVERAGE_VALUE_OVER 100
#define REQUEUE_LIMIT 100

struct RamCacheCLFUSEntry {
  CryptoHash key;
  uint32_t auxkey1;
  uint32_t auxkey2;
  uint64_t hits;
  uint32_t size; // memory used including padding in buffer
  uint32_t len;  // actual data length
  uint32_t compressed_len;
  union {
    struct {
      uint32_t compressed : 3; // compression type
      uint32_t incompressible : 1;
      uint32_t lru : 1;
      uint32_t copy : 1; // copy-in-copy-out
    } flag_bits;
    uint32_t flags;
  };
  LINK(RamCacheCLFUSEntry, lru_link);
  LINK(RamCacheCLFUSEntry, hash_link);
  Ptr<IOBufferData> data;
};

class RamCacheCLFUS : public RamCache
{
public:
  RamCacheCLFUS() {}

  // returns 1 on found/stored, 0 on not found/stored, if provided auxkey1 and auxkey2 must match
  int get(CryptoHash *key, Ptr<IOBufferData> *ret_data, uint32_t auxkey1 = 0, uint32_t auxkey2 = 0) override;
  int put(CryptoHash *key, IOBufferData *data, uint32_t len, bool copy = false, uint32_t auxkey1 = 0,
          uint32_t auxkey2 = 0) override;
  int fixup(const CryptoHash *key, uint32_t old_auxkey1, uint32_t old_auxkey2, uint32_t new_auxkey1, uint32_t new_auxkey2) override;
  int64_t size() const override;

  void init(int64_t max_bytes, Vol *vol) override;

  void compress_entries(EThread *thread, int do_at_most = INT_MAX);

  // TODO move it to private.
  Vol *vol = nullptr; // for stats
private:
  int64_t _max_bytes = 0;
  int64_t _bytes     = 0;
  int64_t _objects   = 0;

  double _average_value                         = 0;
  int64_t _history                              = 0;
  int _ibuckets                                 = 0;
  int _nbuckets                                 = 0;
  DList(RamCacheCLFUSEntry, hash_link) *_bucket = nullptr;
  Que(RamCacheCLFUSEntry, lru_link) _lru[2];
  uint16_t *_seen                 = nullptr;
  int _ncompressed                = 0;
  RamCacheCLFUSEntry *_compressed = nullptr; // first uncompressed lru[0] entry

  void _resize_hashtable();
  void _victimize(RamCacheCLFUSEntry *e);
  void _move_compressed(RamCacheCLFUSEntry *e);
  RamCacheCLFUSEntry *_destroy(RamCacheCLFUSEntry *e);
  void _requeue_victims(Que(RamCacheCLFUSEntry, lru_link) & victims);
  void _tick(); // move CLOCK on history
};

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
  int mainEvent(int event, Event *e);

  RamCacheCLFUSCompressor(RamCacheCLFUS *arc) : rc(arc) { SET_HANDLER(&RamCacheCLFUSCompressor::mainEvent); }
};

int
RamCacheCLFUSCompressor::mainEvent(int /* event ATS_UNUSED */, Event *e)
{
  switch (cache_config_ram_cache_compress) {
  default:
    Warning("unknown RAM cache compression type: %d", cache_config_ram_cache_compress);
  case CACHE_COMPRESSION_NONE:
  case CACHE_COMPRESSION_FASTLZ:
    break;
  case CACHE_COMPRESSION_LIBZ:
#ifndef HAVE_ZLIB_H
    Warning("libz not available for RAM cache compression");
#endif
    break;
  case CACHE_COMPRESSION_LIBLZMA:
#ifndef HAVE_LZMA_H
    Warning("lzma not available for RAM cache compression");
#endif
    break;
  }
  if (cache_config_ram_cache_compress_percent) {
    rc->compress_entries(e->ethread);
  }
  return EVENT_CONT;
}

ClassAllocator<RamCacheCLFUSEntry> ramCacheCLFUSEntryAllocator("RamCacheCLFUSEntry");

static const int bucket_sizes[] = {127,      251,      509,       1021,      2039,      4093,       8191,      16381,   32749,
                                   65521,    131071,   262139,    524287,    1048573,   2097143,    4194301,   8388593, 16777213,
                                   33554393, 67108859, 134217689, 268435399, 536870909, 1073741789, 2147483647};

void
RamCacheCLFUS::_resize_hashtable()
{
  int anbuckets = bucket_sizes[this->_ibuckets];
  DDebug("ram_cache", "resize hashtable %d", anbuckets);
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
RamCacheCLFUS::init(int64_t abytes, Vol *avol)
{
  ink_assert(avol != nullptr);
  vol              = avol;
  this->_max_bytes = abytes;
  DDebug("ram_cache", "initializing ram_cache %" PRId64 " bytes", abytes);
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
  int64_t x = 0, xsize = 0, h = 0;
  RamCacheCLFUSEntry *y = c->lru[0].head;
  while (y) {
    x++;
    xsize += y->size + ENTRY_OVERHEAD;
    y = y->lru_link.next;
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

int
RamCacheCLFUS::get(CryptoHash *key, Ptr<IOBufferData> *ret_data, uint32_t auxkey1, uint32_t auxkey2)
{
  if (!this->_max_bytes) {
    return 0;
  }
  int64_t i             = key->slice32(3) % this->_nbuckets;
  RamCacheCLFUSEntry *e = this->_bucket[i].head;
  char *b               = nullptr;
  while (e) {
    if (e->key == *key && e->auxkey1 == auxkey1 && e->auxkey2 == auxkey2) {
      this->_move_compressed(e);
      if (!e->flag_bits.lru) { // in memory
        if (CACHE_VALUE(e) > this->_average_value) {
          this->_lru[e->flag_bits.lru].remove(e);
          this->_lru[e->flag_bits.lru].enqueue(e);
        }
        e->hits++;
        uint32_t ram_hit_state = RAM_HIT_COMPRESS_NONE;
        if (e->flag_bits.compressed) {
          b = static_cast<char *>(ats_malloc(e->len));
          switch (e->flag_bits.compressed) {
          default:
            goto Lfailed;
          case CACHE_COMPRESSION_FASTLZ: {
            int l = static_cast<int>(e->len);
            if ((l != fastlz_decompress(e->data->data(), e->compressed_len, b, l))) {
              goto Lfailed;
            }
            ram_hit_state = RAM_HIT_COMPRESS_FASTLZ;
            break;
          }
#ifdef HAVE_ZLIB_H
          case CACHE_COMPRESSION_LIBZ: {
            uLongf l = e->len;
            if (Z_OK !=
                uncompress(reinterpret_cast<Bytef *>(b), &l, reinterpret_cast<Bytef *>(e->data->data()), e->compressed_len)) {
              goto Lfailed;
            }
            ram_hit_state = RAM_HIT_COMPRESS_LIBZ;
            break;
          }
#endif
#ifdef HAVE_LZMA_H
          case CACHE_COMPRESSION_LIBLZMA: {
            size_t l = static_cast<size_t>(e->len), ipos = 0, opos = 0;
            uint64_t memlimit = e->len * 2 + LZMA_BASE_MEMLIMIT;
            if (LZMA_OK != lzma_stream_buffer_decode(&memlimit, 0, nullptr, reinterpret_cast<uint8_t *>(e->data->data()), &ipos,
                                                     e->compressed_len, reinterpret_cast<uint8_t *>(b), &opos, l)) {
              goto Lfailed;
            }
            ram_hit_state = RAM_HIT_COMPRESS_LIBLZMA;
            break;
          }
#endif
          }
          IOBufferData *data = new_xmalloc_IOBufferData(b, e->len);
          data->_mem_type    = DEFAULT_ALLOC;
          if (!e->flag_bits.copy) { // don't bother if we have to copy anyway
            int64_t delta = (static_cast<int64_t>(e->compressed_len)) - static_cast<int64_t>(e->size);
            this->_bytes += delta;
            CACHE_SUM_DYN_STAT_THREAD(cache_ram_cache_bytes_stat, delta);
            e->size = e->compressed_len;
            check_accounting(this);
            e->flag_bits.compressed = 0;
            e->data                 = data;
          }
          (*ret_data) = data;
        } else {
          IOBufferData *data = e->data.get();
          if (e->flag_bits.copy) {
            data = new_IOBufferData(iobuffer_size_to_index(e->len, MAX_BUFFER_SIZE_INDEX), MEMALIGNED);
            ::memcpy(data->data(), e->data->data(), e->len);
          }
          (*ret_data) = data;
        }
        CACHE_SUM_DYN_STAT_THREAD(cache_ram_cache_hits_stat, 1);
        DDebug("ram_cache", "get %X %d %d size %d HIT", key->slice32(3), auxkey1, auxkey2, e->size);
        return ram_hit_state;
      } else {
        CACHE_SUM_DYN_STAT_THREAD(cache_ram_cache_misses_stat, 1);
        DDebug("ram_cache", "get %X %d %d HISTORY", key->slice32(3), auxkey1, auxkey2);
        return 0;
      }
    }
    assert(e != e->hash_link.next);
    e = e->hash_link.next;
  }
  DDebug("ram_cache", "get %X %d %d MISS", key->slice32(3), auxkey1, auxkey2);
Lerror:
  CACHE_SUM_DYN_STAT_THREAD(cache_ram_cache_misses_stat, 1);
  return 0;
Lfailed:
  ats_free(b);
  this->_destroy(e);
  DDebug("ram_cache", "get %X %d %d Z_ERR", key->slice32(3), auxkey1, auxkey2);
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
    e->hits = REQUEUE_HITS(e->hits);
    this->_lru[1].enqueue(e);
  } else {
    goto Lfree;
  }
  if (this->_history <= this->_objects + HISTORY_HYSTERIA) {
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
  DDebug("ram_cache", "put %X %d %d size %d FREED", e->key.slice32(3), e->auxkey1, e->auxkey2, e->size);
  THREAD_FREE(e, ramCacheCLFUSEntryAllocator, this_thread());
}

void
RamCacheCLFUS::_victimize(RamCacheCLFUSEntry *e)
{
  this->_objects--;
  DDebug("ram_cache", "put %X %d %d size %d VICTIMIZED", e->key.slice32(3), e->auxkey1, e->auxkey2, e->size);
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
    this->_bytes -= e->size + ENTRY_OVERHEAD;
    CACHE_SUM_DYN_STAT_THREAD(cache_ram_cache_bytes_stat, -(int64_t)e->size);
    e->data = nullptr;
  } else {
    this->_history--;
  }
  uint32_t b = e->key.slice32(3) % this->_nbuckets;
  this->_bucket[b].remove(e);
  DDebug("ram_cache", "put %X %d %d DESTROYED", e->key.slice32(3), e->auxkey1, e->auxkey2);
  THREAD_FREE(e, ramCacheCLFUSEntryAllocator, this_thread());
  return ret;
}

void
RamCacheCLFUS::compress_entries(EThread *thread, int do_at_most)
{
  if (!cache_config_ram_cache_compress) {
    return;
  }
  ink_assert(vol != nullptr);
  MUTEX_TAKE_LOCK(vol->mutex, thread);
  if (!this->_compressed) {
    this->_compressed  = this->_lru[0].head;
    this->_ncompressed = 0;
  }
  float target = (cache_config_ram_cache_compress_percent / 100.0) * this->_objects;
  int n        = 0;
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
      int ctype         = cache_config_ram_cache_compress;
      switch (ctype) {
      default:
        goto Lcontinue;
      case CACHE_COMPRESSION_FASTLZ:
        l = static_cast<uint32_t>(static_cast<double>(e->len) * 1.05 + 66);
        break;
#ifdef HAVE_ZLIB_H
      case CACHE_COMPRESSION_LIBZ:
        l = static_cast<uint32_t>(compressBound(e->len));
        break;
#endif
#ifdef HAVE_LZMA_H
      case CACHE_COMPRESSION_LIBLZMA:
        l = e->len;
        break;
#endif
      }
      // store transient data for lock release
      Ptr<IOBufferData> edata = e->data;
      uint32_t elen           = e->len;
      CryptoHash key          = e->key;
      MUTEX_UNTAKE_LOCK(vol->mutex, thread);
      b           = static_cast<char *>(ats_malloc(l));
      bool failed = false;
      switch (ctype) {
      default:
        goto Lfailed;
      case CACHE_COMPRESSION_FASTLZ:
        if (e->len < 16) {
          goto Lfailed;
        }
        if ((l = fastlz_compress(edata->data(), elen, b)) <= 0) {
          failed = true;
        }
        break;
#ifdef HAVE_ZLIB_H
      case CACHE_COMPRESSION_LIBZ: {
        uLongf ll = l;
        if ((Z_OK != compress(reinterpret_cast<Bytef *>(b), &ll, reinterpret_cast<Bytef *>(edata->data()), elen))) {
          failed = true;
        }
        l = static_cast<int>(ll);
        break;
      }
#endif
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
      }
      MUTEX_TAKE_LOCK(vol->mutex, thread);
      // see if the entry is till around
      {
        if (failed) {
          goto Lfailed;
        }
        uint32_t i             = key.slice32(3) % this->_nbuckets;
        RamCacheCLFUSEntry *ee = this->_bucket[i].head;
        while (ee) {
          if (ee->key == key && ee->data == edata) {
            break;
          }
          ee = ee->hash_link.next;
        }
        if (!ee || ee != e) {
          e = this->_compressed;
          ats_free(b);
          goto Lcontinue;
        }
      }
      if (l > REQUIRED_COMPRESSION * e->len) {
        e->flag_bits.incompressible = true;
      }
      if (l > REQUIRED_SHRINK * e->size) {
        goto Lfailed;
      }
      if (l < e->len) {
        e->flag_bits.compressed = cache_config_ram_cache_compress;
        bb                      = static_cast<char *>(ats_malloc(l));
        memcpy(bb, b, l);
        ats_free(b);
        e->compressed_len = l;
        int64_t delta     = (static_cast<int64_t>(l)) - static_cast<int64_t>(e->size);
        this->_bytes += delta;
        CACHE_SUM_DYN_STAT_THREAD(cache_ram_cache_bytes_stat, delta);
        e->size = l;
      } else {
        ats_free(b);
        e->flag_bits.compressed = 0;
        bb                      = static_cast<char *>(ats_malloc(e->len));
        memcpy(bb, e->data->data(), e->len);
        int64_t delta = (static_cast<int64_t>(e->len)) - static_cast<int64_t>(e->size);
        this->_bytes += delta;
        CACHE_SUM_DYN_STAT_THREAD(cache_ram_cache_bytes_stat, delta);
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
    DDebug("ram_cache", "compress %X %d %d %d %d %d %d %d", e->key.slice32(3), e->auxkey1, e->auxkey2, e->flag_bits.incompressible,
           e->flag_bits.compressed, e->len, e->compressed_len, this->_ncompressed);
    if (!e->lru_link.next) {
      break;
    }
    this->_compressed = e->lru_link.next;
    this->_ncompressed++;
  }
  MUTEX_UNTAKE_LOCK(vol->mutex, thread);
  return;
}

void RamCacheCLFUS::_requeue_victims(Que(RamCacheCLFUSEntry, lru_link) & victims)
{
  RamCacheCLFUSEntry *victim = nullptr;
  while ((victim = victims.dequeue())) {
    this->_bytes += victim->size + ENTRY_OVERHEAD;
    CACHE_SUM_DYN_STAT_THREAD(cache_ram_cache_bytes_stat, victim->size);
    victim->hits = REQUEUE_HITS(victim->hits);
    this->_lru[0].enqueue(victim);
  }
}

int
RamCacheCLFUS::put(CryptoHash *key, IOBufferData *data, uint32_t len, bool copy, uint32_t auxkey1, uint32_t auxkey2)
{
  if (!this->_max_bytes) {
    return 0;
  }
  uint32_t i            = key->slice32(3) % this->_nbuckets;
  RamCacheCLFUSEntry *e = this->_bucket[i].head;
  uint32_t size         = copy ? len : data->block_size();
  double victim_value   = 0;
  while (e) {
    if (e->key == *key) {
      if (e->auxkey1 == auxkey1 && e->auxkey2 == auxkey2) {
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
      int64_t delta = (static_cast<int64_t>(size)) - static_cast<int64_t>(e->size);
      this->_bytes += delta;
      CACHE_SUM_DYN_STAT_THREAD(cache_ram_cache_bytes_stat, delta);
      if (!copy) {
        e->size = size;
        e->data = data;
      } else {
        char *b = static_cast<char *>(ats_malloc(len));
        memcpy(b, data->data(), len);
        e->data            = new_xmalloc_IOBufferData(b, len);
        e->data->_mem_type = DEFAULT_ALLOC;
        e->size            = size;
      }
      check_accounting(this);
      e->flag_bits.copy       = copy;
      e->flag_bits.compressed = 0;
      DDebug("ram_cache", "put %X %d %d size %d HIT", key->slice32(3), auxkey1, auxkey2, e->size);
      return 1;
    } else {
      this->_lru[1].remove(e);
      if (CACHE_VALUE(e) < this->_average_value) {
        this->_lru[1].enqueue(e);
        return 0;
      }
    }
  }
  Que(RamCacheCLFUSEntry, lru_link) victims;
  RamCacheCLFUSEntry *victim = nullptr;
  int requeue_limit          = REQUEUE_LIMIT;
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
      DDebug("ram_cache", "put %X %d %d size %d UNSEEN", key->slice32(3), auxkey1, auxkey2, size);
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
      DDebug("ram_cache", "put %X %d %d NO VICTIM", key->slice32(3), auxkey1, auxkey2);
      return 0;
    }
    this->_average_value = (CACHE_VALUE(victim) + (this->_average_value * (AVERAGE_VALUE_OVER - 1))) / AVERAGE_VALUE_OVER;
    if (CACHE_VALUE(victim) > this->_average_value && requeue_limit-- > 0) {
      this->_lru[0].enqueue(victim);
      continue;
    }
    this->_bytes -= victim->size + ENTRY_OVERHEAD;
    CACHE_SUM_DYN_STAT_THREAD(cache_ram_cache_bytes_stat, -(int64_t)victim->size);
    victims.enqueue(victim);
    if (victim == this->_compressed) {
      this->_compressed = nullptr;
    } else {
      this->_ncompressed--;
    }
    victim_value += CACHE_VALUE(victim);
    this->_tick();
    if (!e) {
      goto Lhistory;
    } else { // e from history
      DDebug("ram_cache_compare", "put %f %f", victim_value, CACHE_VALUE(e));
      if (this->_bytes + victim->size + size > this->_max_bytes && victim_value > CACHE_VALUE(e)) {
        this->_requeue_victims(victims);
        this->_lru[1].enqueue(e);
        DDebug("ram_cache", "put %X %d %d size %d INC %" PRId64 " HISTORY", key->slice32(3), auxkey1, auxkey2, e->size, e->hits);
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
      this->_bytes += victim->size + ENTRY_OVERHEAD;
      CACHE_SUM_DYN_STAT_THREAD(cache_ram_cache_bytes_stat, victim->size);
      victim->hits = REQUEUE_HITS(victim->hits);
      this->_lru[0].enqueue(victim);
    } else {
      this->_victimize(victim);
    }
  }
  if (e) {
    this->_history--; // move from history
  } else {
    e          = THREAD_ALLOC(ramCacheCLFUSEntryAllocator, this_ethread());
    e->key     = *key;
    e->auxkey1 = auxkey1;
    e->auxkey2 = auxkey2;
    e->hits    = 1;
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
    char *b = static_cast<char *>(ats_malloc(len));
    memcpy(b, data->data(), len);
    e->data            = new_xmalloc_IOBufferData(b, len);
    e->data->_mem_type = DEFAULT_ALLOC;
  }
  e->flag_bits.copy = copy;
  this->_bytes += size + ENTRY_OVERHEAD;
  CACHE_SUM_DYN_STAT_THREAD(cache_ram_cache_bytes_stat, size);
  e->size = size;
  this->_objects++;
  this->_lru[0].enqueue(e);
  e->len = len;
  check_accounting(this);
  DDebug("ram_cache", "put %X %d %d size %d INSERTED", key->slice32(3), auxkey1, auxkey2, e->size);
  return 1;
Lhistory:
  this->_requeue_victims(victims);
  check_accounting(this);
  e          = THREAD_ALLOC(ramCacheCLFUSEntryAllocator, this_ethread());
  e->key     = *key;
  e->auxkey1 = auxkey1;
  e->auxkey2 = auxkey2;
  e->hits    = 1;
  e->size    = data->block_size();
  e->flags   = 0;
  this->_bucket[i].push(e);
  e->flag_bits.lru = 1;
  this->_lru[1].enqueue(e);
  this->_history++;
  DDebug("ram_cache", "put %X %d %d HISTORY", key->slice32(3), auxkey1, auxkey2);
  return 0;
}

int
RamCacheCLFUS::fixup(const CryptoHash *key, uint32_t old_auxkey1, uint32_t old_auxkey2, uint32_t new_auxkey1, uint32_t new_auxkey2)
{
  if (!this->_max_bytes) {
    return 0;
  }
  uint32_t i            = key->slice32(3) % this->_nbuckets;
  RamCacheCLFUSEntry *e = this->_bucket[i].head;
  while (e) {
    if (e->key == *key && e->auxkey1 == old_auxkey1 && e->auxkey2 == old_auxkey2) {
      e->auxkey1 = new_auxkey1;
      e->auxkey2 = new_auxkey2;
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

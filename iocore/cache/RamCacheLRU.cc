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

struct RamCacheLRUEntry {
  INK_MD5 key;
  uint32_t auxkey1;
  uint32_t auxkey2;
  LINK(RamCacheLRUEntry, lru_link);
  LINK(RamCacheLRUEntry, hash_link);
  Ptr<IOBufferData> data;
};

#define ENTRY_OVERHEAD 128 // per-entry overhead to consider when computing sizes

struct RamCacheLRU : public RamCache {
  int64_t max_bytes = 0;
  int64_t bytes     = 0;
  int64_t objects   = 0;

  // returns 1 on found/stored, 0 on not found/stored, if provided auxkey1 and auxkey2 must match
  int get(INK_MD5 *key, Ptr<IOBufferData> *ret_data, uint32_t auxkey1 = 0, uint32_t auxkey2 = 0) override;
  int put(INK_MD5 *key, IOBufferData *data, uint32_t len, bool copy = false, uint32_t auxkey1 = 0, uint32_t auxkey2 = 0) override;
  int fixup(const INK_MD5 *key, uint32_t old_auxkey1, uint32_t old_auxkey2, uint32_t new_auxkey1, uint32_t new_auxkey2) override;
  int64_t size() const override;

  void init(int64_t max_bytes, Vol *vol) override;

  // private
  uint16_t *seen = nullptr;
  Que(RamCacheLRUEntry, lru_link) lru;
  DList(RamCacheLRUEntry, hash_link) *bucket = nullptr;
  int nbuckets = 0;
  int ibuckets = 0;
  Vol *vol     = nullptr;

  void resize_hashtable();
  RamCacheLRUEntry *remove(RamCacheLRUEntry *e);
};

int64_t
RamCacheLRU::size() const
{
  int64_t s = 0;
  forl_LL(RamCacheLRUEntry, e, lru)
  {
    s += sizeof(*e);
    s += sizeof(*e->data);
    s += e->data->block_size();
  }
  return s;
}

ClassAllocator<RamCacheLRUEntry> ramCacheLRUEntryAllocator("RamCacheLRUEntry");

static const int bucket_sizes[] = {127,     251,      509,      1021,     2039,      4093,      8191,     16381,
                                   32749,   65521,    131071,   262139,   524287,    1048573,   2097143,  4194301,
                                   8388593, 16777213, 33554393, 67108859, 134217689, 268435399, 536870909};

void
RamCacheLRU::resize_hashtable()
{
  int anbuckets = bucket_sizes[ibuckets];
  DDebug("ram_cache", "resize hashtable %d", anbuckets);
  int64_t s = anbuckets * sizeof(DList(RamCacheLRUEntry, hash_link));
  DList(RamCacheLRUEntry, hash_link) *new_bucket = (DList(RamCacheLRUEntry, hash_link) *)ats_malloc(s);
  memset(new_bucket, 0, s);
  if (bucket) {
    for (int64_t i = 0; i < nbuckets; i++) {
      RamCacheLRUEntry *e = nullptr;
      while ((e = bucket[i].pop())) {
        new_bucket[e->key.slice32(3) % anbuckets].push(e);
      }
    }
    ats_free(bucket);
  }
  bucket   = new_bucket;
  nbuckets = anbuckets;
  ats_free(seen);
  int size = bucket_sizes[ibuckets] * sizeof(uint16_t);
  if (cache_config_ram_cache_use_seen_filter) {
    seen = (uint16_t *)ats_malloc(size);
    memset(seen, 0, size);
  }
}

void
RamCacheLRU::init(int64_t abytes, Vol *avol)
{
  vol       = avol;
  max_bytes = abytes;
  DDebug("ram_cache", "initializing ram_cache %" PRId64 " bytes", abytes);
  if (!max_bytes) {
    return;
  }
  resize_hashtable();
}

int
RamCacheLRU::get(INK_MD5 *key, Ptr<IOBufferData> *ret_data, uint32_t auxkey1, uint32_t auxkey2)
{
  if (!max_bytes) {
    return 0;
  }
  uint32_t i          = key->slice32(3) % nbuckets;
  RamCacheLRUEntry *e = bucket[i].head;
  while (e) {
    if (e->key == *key && e->auxkey1 == auxkey1 && e->auxkey2 == auxkey2) {
      lru.remove(e);
      lru.enqueue(e);
      (*ret_data) = e->data;
      DDebug("ram_cache", "get %X %d %d HIT", key->slice32(3), auxkey1, auxkey2);
      CACHE_SUM_DYN_STAT_THREAD(cache_ram_cache_hits_stat, 1);
      return 1;
    }
    e = e->hash_link.next;
  }
  DDebug("ram_cache", "get %X %d %d MISS", key->slice32(3), auxkey1, auxkey2);
  CACHE_SUM_DYN_STAT_THREAD(cache_ram_cache_misses_stat, 1);
  return 0;
}

RamCacheLRUEntry *
RamCacheLRU::remove(RamCacheLRUEntry *e)
{
  RamCacheLRUEntry *ret = e->hash_link.next;
  uint32_t b            = e->key.slice32(3) % nbuckets;
  bucket[b].remove(e);
  lru.remove(e);
  bytes -= ENTRY_OVERHEAD + e->data->block_size();
  CACHE_SUM_DYN_STAT_THREAD(cache_ram_cache_bytes_stat, -(ENTRY_OVERHEAD + e->data->block_size()));
  DDebug("ram_cache", "put %X %d %d FREED", e->key.slice32(3), e->auxkey1, e->auxkey2);
  e->data = nullptr;
  THREAD_FREE(e, ramCacheLRUEntryAllocator, this_thread());
  objects--;
  return ret;
}

// ignore 'copy' since we don't touch the data
int
RamCacheLRU::put(INK_MD5 *key, IOBufferData *data, uint32_t len, bool, uint32_t auxkey1, uint32_t auxkey2)
{
  if (!max_bytes) {
    return 0;
  }
  uint32_t i = key->slice32(3) % nbuckets;
  if (cache_config_ram_cache_use_seen_filter) {
    uint16_t k  = key->slice32(3) >> 16;
    uint16_t kk = seen[i];
    seen[i]     = k;
    if ((kk != (uint16_t)k)) {
      DDebug("ram_cache", "put %X %d %d len %d UNSEEN", key->slice32(3), auxkey1, auxkey2, len);
      return 0;
    }
  }
  RamCacheLRUEntry *e = bucket[i].head;
  while (e) {
    if (e->key == *key) {
      if (e->auxkey1 == auxkey1 && e->auxkey2 == auxkey2) {
        lru.remove(e);
        lru.enqueue(e);
        return 1;
      } else { // discard when aux keys conflict
        e = remove(e);
        continue;
      }
    }
    e = e->hash_link.next;
  }
  e          = THREAD_ALLOC(ramCacheLRUEntryAllocator, this_ethread());
  e->key     = *key;
  e->auxkey1 = auxkey1;
  e->auxkey2 = auxkey2;
  e->data    = data;
  bucket[i].push(e);
  lru.enqueue(e);
  bytes += ENTRY_OVERHEAD + data->block_size();
  objects++;
  CACHE_SUM_DYN_STAT_THREAD(cache_ram_cache_bytes_stat, ENTRY_OVERHEAD + data->block_size());
  while (bytes > max_bytes) {
    RamCacheLRUEntry *ee = lru.dequeue();
    if (ee) {
      remove(ee);
    } else {
      break;
    }
  }
  DDebug("ram_cache", "put %X %d %d INSERTED", key->slice32(3), auxkey1, auxkey2);
  if (objects > nbuckets) {
    ++ibuckets;
    resize_hashtable();
  }
  return 1;
}

int
RamCacheLRU::fixup(const INK_MD5 *key, uint32_t old_auxkey1, uint32_t old_auxkey2, uint32_t new_auxkey1, uint32_t new_auxkey2)
{
  if (!max_bytes) {
    return 0;
  }
  uint32_t i          = key->slice32(3) % nbuckets;
  RamCacheLRUEntry *e = bucket[i].head;
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
new_RamCacheLRU()
{
  return new RamCacheLRU;
}

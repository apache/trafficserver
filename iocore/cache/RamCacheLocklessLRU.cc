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

#include <atomic>
#include <bit>

#include "P_Cache.h"

#define ENTRY_OVERHEAD 64             // per-entry overhead to consider when computing sizes
#define BYTES_PER_ENTRY (1 << 14)     // 1 entry for every 16K bytes
#define LOCK static_cast<uint64_t>(7) // lowest 3 bits
#define ASSOCIATIVITY 8
#define BUCKET_SIZE ((ASSOCIATIVITY * sizeof(RamCacheLocklessLRUEntry)) + sizeof(RamCacheLocklessLRUTags))

// Lockless behavior.
// 'data' is a Ptr<IOBufferData> with the lower 3 bits used for marking.
// When accessing the entry the lowest 3 bits are incremented.
// When attempting to replace an entry, the writer will skip marked entries.

struct RamCacheLocklessLRUEntry {
  std::atomic<uint64_t> data;
  uint64_t auxkey;
  CryptoHash key;
};

struct RamCacheLocklessLRUTags {
  std::atomic<uint64_t> lru;
  std::atomic<uint64_t> tags;
};

struct RamCacheLocklessLRU : public RamCache {
  int64_t max_bytes            = 0;
  std::atomic<int64_t> bytes   = 0;
  std::atomic<int64_t> objects = 0;

  // returns 1 on found/stored, 0 on not found/stored, if provided auxkey must match
  int get(CryptoHash *key, Ptr<IOBufferData> *ret_data, uint64_t auxkey = 0) override;
  int put(CryptoHash *key, IOBufferData *data, uint32_t len, bool copy = false, uint64_t auxkey = 0) override;
  int fixup(const CryptoHash *key, uint64_t old_auxkey, uint64_t new_auxkey) override;
  int64_t size() const override;

  void init(int64_t max_bytes, Vol *) override;

  // private
  void *data                          = nullptr;
  int nbuckets                        = 0;
  int ibuckets                        = 0;
  Vol *vol                            = nullptr;
  std::atomic<uint64_t> reclaim_sweep = 0;

  int64_t remove(int i, RamCacheLocklessLRUEntry *e);
  int64_t remove_one();
  void get_bucket(int buckekt, RamCacheLocklessLRUTags **t, RamCacheLocklessLRUEntry **e);
};

int64_t
RamCacheLocklessLRU::size() const
{
  return bytes.load(std::memory_order_relaxed);
}

static const int bucket_sizes[] = {1,        3,        7,         13,        31,        61,           127,         251,
                                   509,      1021,     2039,      4093,      8191,      16381,        32749,       65521,
                                   131071,   262139,   524287,    1048573,   2097143,   4194301,      8388593,     16777213,
                                   33554393, 67108859, 134217689, 268435399, 536870909, 1073741789LL, 2147483647LL};

void
RamCacheLocklessLRU::init(int64_t abytes, Vol *avol)
{
  vol       = avol;
  max_bytes = abytes;
  DDebug("ram_cache", "initializing ram_cache %" PRId64 " bytes", abytes);
  if (!max_bytes) {
    return;
  }
  ibuckets = 0;
  while (true) {
    if (bucket_sizes[ibuckets] * ASSOCIATIVITY * BYTES_PER_ENTRY > abytes) {
      break;
    }
    ibuckets++;
  }
  nbuckets = bucket_sizes[ibuckets];
  size_t s = nbuckets * BUCKET_SIZE;
  data     = ats_malloc(s);
  memset(data, 0, s);
}

static uint64_t
increment_mark(std::atomic<uint64_t> *p)
{
  uint64_t d;
  while (true) {
    uint64_t data = p->load(std::memory_order_relaxed);
    if ((data & LOCK) == LOCK) { // Max count, just spin.
      continue;
    }
    d = data + 1;
    if (p->compare_exchange_weak(data, d, std::memory_order_acquire, std::memory_order_relaxed)) {
      return d;
    }
  }
}

static uint64_t
decrement_mark(std::atomic<uint64_t> *p)
{
  uint64_t d;
  while (true) {
    uint64_t data = p->load(std::memory_order_relaxed);
    d             = data - 1;
    if (p->compare_exchange_weak(data, d, std::memory_order_release, std::memory_order_relaxed)) {
      return d;
    }
  }
}

static void
update_lru(int i, RamCacheLocklessLRUTags *t)
{
  while (true) {
    uint64_t lru     = t->lru.load(std::memory_order_relaxed); // Handled at a higher level.
    uint64_t new_lru = lru;
    int shift        = i * 8;
    // Update the row so that there is a zero for i and 1 for all others.
    // Set row to all ones.
    uint64_t m = 0xFFull << shift;
    new_lru |= m;
    // Clear the column.
    m = 0x11111111ull << i;
    new_lru &= m;
    if (lru == new_lru || t->lru.compare_exchange_weak(lru, new_lru, std::memory_order_relaxed)) {
      return;
    }
  }
}

static void
update_tag(int i, RamCacheLocklessLRUTags *t, CryptoHash *key)
{
  while (true) {
    uint64_t tags     = t->tags.load(std::memory_order_relaxed); // Handled at a higher level.
    uint64_t new_tags = tags;
    int shift         = i * 8;
    uint64_t m        = 0xFFull << shift;
    new_tags &= ~m;
    uint8_t *entry_tag_bits = reinterpret_cast<uint8_t *>(key);
    m                       = static_cast<uint64_t>(*entry_tag_bits) << shift;
    new_tags |= m;
    if (tags == new_tags || t->tags.compare_exchange_weak(tags, new_tags, std::memory_order_relaxed)) {
      return;
    }
  }
}

void
RamCacheLocklessLRU::get_bucket(int bucket, RamCacheLocklessLRUTags **t, RamCacheLocklessLRUEntry **e)
{
  char *p = static_cast<char *>(data);
  p += bucket * BUCKET_SIZE;
  *t = reinterpret_cast<RamCacheLocklessLRUTags *>(p);
  *e = reinterpret_cast<RamCacheLocklessLRUEntry *>(p + sizeof(RamCacheLocklessLRUTags));
}

int
RamCacheLocklessLRU::get(CryptoHash *key, Ptr<IOBufferData> *ret_data, uint64_t auxkey)
{
  if (!max_bytes) {
    return 0;
  }
  RamCacheLocklessLRUEntry *b;
  RamCacheLocklessLRUTags *t;
  get_bucket(key->slice32(3) % nbuckets, &t, &b);
  uint64_t tags = t->tags.load(std::memory_order_relaxed);
  for (int i = 0; i < ASSOCIATIVITY; i++) {
    uint8_t *tag_bits           = reinterpret_cast<uint8_t *>(&tags);
    RamCacheLocklessLRUEntry *e = &b[i];
    uint8_t *entry_tag_bits     = reinterpret_cast<uint8_t *>(&e->key);
    if (*entry_tag_bits != *tag_bits << (8 * i)) {
      continue;
    }
    uint64_t d    = increment_mark(&e->data);
    uint64_t dptr = d & ~LOCK;
    if (!dptr) { // Empty
      decrement_mark(&e->data);
      continue;
    }
    if (e->key == *key && e->auxkey == auxkey) {
      (*ret_data) = reinterpret_cast<IOBufferData *>(dptr);
      DDebug("ram_cache", "get %X %" PRIu64 " HIT", key->slice32(3), auxkey);
      CACHE_SUM_DYN_STAT_THREAD(cache_ram_cache_hits_stat, 1);
      update_lru(i, t);
      decrement_mark(&e->data);
      return 1;
    }
    decrement_mark(&e->data);
  }
  DDebug("ram_cache", "get %X %" PRIu64 " MISS", key->slice32(3), auxkey);
  CACHE_SUM_DYN_STAT_THREAD(cache_ram_cache_misses_stat, 1);
  return 0;
}

// Succeeds if the entry is already empty or if it is removed.
// Returns 'bytes' on success or zero on failure.
int64_t
RamCacheLocklessLRU::remove(int i, RamCacheLocklessLRUEntry *bucket)
{
  RamCacheLocklessLRUEntry *e = &bucket[i];
  int64_t result              = 0;
  uint64_t ptr                = 0;
  uint64_t marked_ptr         = 0;
  while (true) {
    ptr = e->data.load(std::memory_order_acquire);
    if ((ptr & LOCK)) {
      return result;
    }
    if ((ptr & ~LOCK) == 0) {
      return result;
    }
    marked_ptr = ptr + 1;
    if (e->data.compare_exchange_weak(ptr, marked_ptr)) {
      break;
    }
  }
  IOBufferData *block = reinterpret_cast<IOBufferData *>(ptr);
  auto size           = ENTRY_OVERHEAD + block->block_size();
  uint64_t new_ptr    = 1;
  if (!e->data.compare_exchange_strong(marked_ptr, new_ptr, std::memory_order_relaxed, std::memory_order_relaxed)) {
    decrement_mark(&e->data);
    return false;
  }
  decrement_mark(&e->data);

  block->refcount_dec();

  result = bytes.fetch_sub(size) - size;
  CACHE_SUM_DYN_STAT_THREAD(cache_ram_cache_bytes_stat, -size);
  objects.fetch_sub(1, std::memory_order_relaxed);
  return result;
}

static int
find_lru_victim(RamCacheLocklessLRUTags *t, RamCacheLocklessLRUEntry *b)
{
  // Find the row with the most bits set.
  uint64_t lru = t->lru.load(std::memory_order_acquire);
  int i        = -1;
  int max_p    = -1;
  for (int j = 0; j < 8; j++) {
    if (!(b[j].data.load(std::memory_order_relaxed) & ~LOCK)) { // Skip empty entries.
      continue;
    }
    uint64_t c = lru >> (8 * j);
    c &= 0xFF;
    int p = std::__popcount(c);
    if (p > max_p) {
      i     = j;
      max_p = p;
    }
  }
  return i;
}

static int
find_lru(RamCacheLocklessLRUTags *t)
{
  // Find the row with the most bits set.
  uint64_t lru = t->lru.load(std::memory_order_acquire);
  int i        = -1;
  int max_p    = -1;
  for (int j = 0; j < 8; j++) {
    uint64_t c = lru >> (8 * j);
    c &= 0xFF;
    int p = std::__popcount(c);
    if (p > max_p) {
      i     = j;
      max_p = p;
    }
  }
  return i;
}

int64_t
RamCacheLocklessLRU::remove_one()
{
  int64_t result = 0;
  RamCacheLocklessLRUEntry *b;
  RamCacheLocklessLRUTags *t;
  int i;
  while (true) {
    auto bucket = reclaim_sweep.fetch_add(1, std::memory_order_relaxed) + 1;
    get_bucket(bucket % nbuckets, &t, &b);
    i = find_lru_victim(t, b);
    if (i < 0) {
      continue;
    }
    if (!(result = remove(i, b))) {
      continue;
    }
    break;
  }
  return result;
}

int
RamCacheLocklessLRU::put(CryptoHash *key, IOBufferData *data, uint32_t len, bool, uint64_t auxkey)
{
  if (!max_bytes) {
    return 0;
  }

  RamCacheLocklessLRUEntry *b;
  RamCacheLocklessLRUTags *t;
  get_bucket(key->slice32(3) % nbuckets, &t, &b);
  uint64_t tags = t->tags.load(std::memory_order_relaxed);
  int empty     = -1;
  for (int i = 0; i < ASSOCIATIVITY; i++) {
    RamCacheLocklessLRUEntry *e = &b[i];
    uint8_t *entry_tag_bits     = reinterpret_cast<uint8_t *>(&e->key);
    if (*entry_tag_bits != (tags << (0xFFull << (8 * i)))) {
      continue;
    }
    uint64_t d    = increment_mark(&e->data);
    uint64_t dptr = d & ~LOCK;
    if (!dptr) { // Empty
      decrement_mark(&e->data);
      empty = i;
      continue;
    }
    if (e->key == *key && e->auxkey == auxkey) {
      decrement_mark(&e->data);
      return 0;
    }
    decrement_mark(&e->data);
  }
  // Not found.

  // Free enough space.
  int64_t size = ENTRY_OVERHEAD + data->block_size();
  int64_t bb   = bytes.fetch_add(size, std::memory_order_relaxed) + size;
  while (bb > size && bb > max_bytes) {
    bb = remove_one();
  }

  int free = -1;
  // Find a cache line.
  if (empty < 0) {
    free = find_lru(t);
    if (free < 0) {
      bytes.fetch_sub(size, std::memory_order_relaxed);
      return 0;
    }
    if (!remove(free, b)) {
      bytes.fetch_sub(size, std::memory_order_relaxed);
      return 0;
    }
    empty = free;
  }

  // Swap in new pointer.
  RamCacheLocklessLRUEntry *e = &b[empty];
  uint64_t d                  = 0;
  uint64_t new_data           = reinterpret_cast<uint64_t>(data) + 1;
  if (!e->data.compare_exchange_strong(d, new_data, std::memory_order_relaxed)) {
    bytes.fetch_sub(size, std::memory_order_relaxed);
    return 0;
  }
  data->refcount_inc();

  // Update the key and auxkey.
  e->key    = *key;
  e->auxkey = auxkey;

  decrement_mark(&e->data);

  update_lru(empty, t);
  update_tag(empty, t, key);

  // bytes updated above
  objects.fetch_add(1, std::memory_order_relaxed);
  CACHE_SUM_DYN_STAT_THREAD(cache_ram_cache_bytes_stat, size);
  DDebug("ram_cache", "put %X %" PRIu64 " INSERTED", key->slice32(3), auxkey);
  return 1;
}

int
RamCacheLocklessLRU::fixup(const CryptoHash *key, uint64_t old_auxkey, uint64_t new_auxkey)
{
  if (!max_bytes) {
    return 0;
  }
  RamCacheLocklessLRUEntry *b;
  RamCacheLocklessLRUTags *t;
  get_bucket(key->slice32(3) % nbuckets, &t, &b);
  uint64_t tags = t->tags.load(std::memory_order_acquire);
  for (int i = 0; i < ASSOCIATIVITY; i++) {
    uint8_t *tag_bits           = reinterpret_cast<uint8_t *>(&tags);
    RamCacheLocklessLRUEntry *e = &b[i];
    uint8_t *entry_tag_bits     = reinterpret_cast<uint8_t *>(&e->key);
    if (*entry_tag_bits != *tag_bits) {
      continue;
    }
    uint64_t d    = increment_mark(&e->data);
    uint64_t dptr = d & ~LOCK;
    if (!dptr) { // Empty
      decrement_mark(&e->data);
      continue;
    }
    if (e->key == *key && e->auxkey == old_auxkey) {
      e->auxkey = new_auxkey;
      decrement_mark(&e->data);
      return 1;
    }
    decrement_mark(&e->data);
  }
  return 0;
}

RamCache *
new_RamCacheLocklessLRU()
{
  return new RamCacheLocklessLRU;
}

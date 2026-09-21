/** @file

  Catch-based unit tests for the RamCache seen filter and resident keys.

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

// The seen filter (proxy.config.cache.ram_cache.use_seen_filter, on by
// default) keeps one-hit wonders out of the RAM cache by declining the first
// put of a key it has not seen. It is an admission filter, so it must apply
// only to keys the cache has never held. Two kinds of put fall outside that:
//
//  - A put for an entry that is already resident. In production this is the
//    concurrent-miss race: another request inserted the object between this
//    request's RAM miss and its disk read completing. The put is a reference,
//    and for LRU it carries the recency bump.
//
//  - A put that replaces a stale copy of the same key under a new auxkey: a
//    rewrite. The object was admitted once already, so it is not a first
//    sighting, and filtering it costs a second disk read before the rewritten
//    object is RAM-resident again.
//
// LRU evaluated its filter before looking for either, and its per-slot bit is
// cleared on admission, so both were declined. These tests pin the corrected
// behaviour for LRU and the resident-put contract for every policy.

#include "main.h"
#include "test_doubles.h"

#include "../P_CacheInternal.h"
#include "../P_RamCache.h"

#include <cstdint>
#include <cstring>
#include <vector>

// Required by main.h
int  cache_vols           = 1;
bool reuse_existing_cache = false;

namespace
{

constexpr std::size_t PAYLOAD_LEN = 5000;

struct PolicyCase {
  RamCache *(*factory)();
  const char *name;
};

const PolicyCase policy_cases[] = {
  {new_RamCacheLRU,    "LRU"   },
  {new_RamCacheCLFUS,  "CLFUS" },
  {new_RamCacheS3FIFO, "S3FIFO"},
};

// The RamCache get/put paths touch only these metrics.
void
wire_stripe(StripeSM &stripe, CacheVol &cache_vol)
{
  stripe.cache_vol = &cache_vol;

  cache_rsb.ram_cache_bytes          = ts::Metrics::Gauge::createPtr("unit_test.seen.ram_cache.bytes");
  cache_rsb.ram_cache_hits           = ts::Metrics::Counter::createPtr("unit_test.seen.ram_cache.hits");
  cache_rsb.ram_cache_misses         = ts::Metrics::Counter::createPtr("unit_test.seen.ram_cache.misses");
  cache_vol.vol_rsb.ram_cache_bytes  = ts::Metrics::Gauge::createPtr("unit_test.seen.vol.ram_cache.bytes");
  cache_vol.vol_rsb.ram_cache_hits   = ts::Metrics::Counter::createPtr("unit_test.seen.vol.ram_cache.hits");
  cache_vol.vol_rsb.ram_cache_misses = ts::Metrics::Counter::createPtr("unit_test.seen.vol.ram_cache.misses");
}

Ptr<IOBufferData>
make_buffer()
{
  int64_t           idx = iobuffer_size_to_index(PAYLOAD_LEN, MAX_BUFFER_SIZE_INDEX);
  Ptr<IOBufferData> data{make_ptr(new_IOBufferData(idx, MEMALIGNED))};

  std::memset(data->data(), 'A', PAYLOAD_LEN);
  return data;
}

CryptoHash
fresh_key()
{
  static uint64_t salt = 0;

  ++salt;

  // The policies bucket on slice32(3), i.e. u32[3]; vary it so consecutive
  // keys land in different hash buckets and seen-filter slots.
  CryptoHash key;

  key.u64[0] = 0xc0ffee00 + salt;
  key.u64[1] = 0x5eed;
  key.u32[3] = static_cast<uint32_t>(0xdeadbeef + salt);
  return key;
}

RamCache *
make_cache(RamCache *(*factory)(), StripeSM &stripe, int64_t max_bytes)
{
  // No compression, so CLFUS does not schedule its background compressor
  // (which would retain a pointer to this cache). The filter is on: both LRU
  // and CLFUS size their filter state in init(), so it must be set first.
  cache_config_ram_cache_compress        = CACHE_COMPRESSION_NONE;
  cache_config_ram_cache_use_seen_filter = 1;

  // The policies have no destructors (entries are pool-allocated and only
  // released on eviction), so destroying a cache object strands its entries
  // for leak checkers. Keep every cache reachable for the life of the
  // process instead.
  static std::vector<RamCache *> &all_caches = *new std::vector<RamCache *>;
  RamCache                       *rc         = factory();

  all_caches.push_back(rc);
  rc->init(max_bytes, &stripe);
  return rc;
}

// Put a new key until some policy takes it: a never-seen key may be declined
// once. Only for the cross-policy contract test; the LRU tests assert the
// exact decline instead.
void
admit(RamCache *rc, CryptoHash &key, IOBufferData *data, uint64_t auxkey = 0)
{
  int r = rc->put(&key, data, PAYLOAD_LEN, false, auxkey);

  if (r == 0) {
    r = rc->put(&key, data, PAYLOAD_LEN, false, auxkey);
  }
  REQUIRE(r == 1);
}

// LRU with the filter on declines a never-seen key exactly once.
void
admit_lru(RamCache *rc, CryptoHash &key, IOBufferData *data, uint64_t auxkey = 0)
{
  REQUIRE(rc->put(&key, data, PAYLOAD_LEN, false, auxkey) == 0);
  REQUIRE(rc->put(&key, data, PAYLOAD_LEN, false, auxkey) == 1);
}

} // namespace

TEST_CASE("RamCache accepts a put for a resident entry under every policy", "[cache][ramcache][seen_filter]")
{
  CacheDisk disk;
  init_disk(disk);
  StripeSM stripe{&disk, 10, 0};
  CacheVol cache_vol;
  wire_stripe(stripe, cache_vol);

  const PolicyCase pc = GENERATE(from_range(policy_cases));
  INFO("policy: " << pc.name);

  // The contract every policy shares; the filter itself is LRU's to get wrong
  // (CLFUS gates its filter on new entries and S3-FIFO has none), so it is
  // pinned in the LRU-specific cases below.
  auto rc  = make_cache(pc.factory, stripe, 1 << 20);
  auto buf = make_buffer();
  auto key = fresh_key();

  admit(rc, key, buf.get());
  CHECK(rc->put(&key, buf.get(), PAYLOAD_LEN) == 1);

  Ptr<IOBufferData> got;

  CHECK(rc->get(&key, &got) >= 1);
}

TEST_CASE("RamCacheLRU seen filter declines a new key once and a resident key never", "[cache][ramcache][seen_filter]")
{
  CacheDisk disk;
  init_disk(disk);
  StripeSM stripe{&disk, 10, 0};
  CacheVol cache_vol;
  wire_stripe(stripe, cache_vol);

  auto rc  = make_cache(new_RamCacheLRU, stripe, 1 << 20);
  auto buf = make_buffer();
  auto key = fresh_key();

  // The filter is engaged: first sighting declined, second admitted.
  admit_lru(rc, key, buf.get());

  // Resident. Before the fix the filter ran first and its slot, cleared on
  // admission, declined every other put for this key.
  CHECK(rc->put(&key, buf.get(), PAYLOAD_LEN) == 1);
  CHECK(rc->put(&key, buf.get(), PAYLOAD_LEN) == 1);

  Ptr<IOBufferData> got;

  CHECK(rc->get(&key, &got) >= 1);
}

TEST_CASE("RamCacheLRU admits a rewrite of a resident key without filtering it", "[cache][ramcache][seen_filter]")
{
  CacheDisk disk;
  init_disk(disk);
  StripeSM stripe{&disk, 10, 0};
  CacheVol cache_vol;
  wire_stripe(stripe, cache_vol);

  // The auxkey is the object's directory offset. A rewrite gives the same key
  // a new offset; the next read misses on the auxkey, goes to disk, and puts
  // the new copy. That put discards the stale copy and must not be declined:
  // the key was admitted already, and declining it means a second disk read.
  auto rc  = make_cache(new_RamCacheLRU, stripe, 1 << 20);
  auto buf = make_buffer();
  auto key = fresh_key();

  admit_lru(rc, key, buf.get(), 1);
  CHECK(rc->put(&key, buf.get(), PAYLOAD_LEN, false, 2) == 1);

  Ptr<IOBufferData> got;

  CHECK(rc->get(&key, &got, 1) == 0);
  CHECK(rc->get(&key, &got, 2) >= 1);
}

TEST_CASE("RamCacheLRU re-put of a resident entry refreshes its recency", "[cache][ramcache][seen_filter]")
{
  CacheDisk disk;
  init_disk(disk);
  StripeSM cache_stripe{&disk, 10, 0};
  CacheVol cache_vol;
  wire_stripe(cache_stripe, cache_vol);

  // Fill the cache, with the object under test inserted first so it is the
  // eviction candidate. Re-put it, then admit one more object to force one
  // eviction: the re-put must have moved it to the recent end, so the
  // second-oldest object goes instead.
  constexpr int64_t cache_bytes = 128 * 1024;

  auto rc    = make_cache(new_RamCacheLRU, cache_stripe, cache_bytes);
  auto first = fresh_key();
  auto buf   = make_buffer();

  // Read what one entry costs off the gauge rather than assuming the policy's
  // per-entry overhead, so the "exactly one eviction" arithmetic tracks it.
  const int64_t before = ts::Metrics::Gauge::load(cache_rsb.ram_cache_bytes);

  admit_lru(rc, first, buf.get());

  const int64_t per_entry = ts::Metrics::Gauge::load(cache_rsb.ram_cache_bytes) - before;

  REQUIRE(per_entry > 0);

  const int64_t capacity = cache_bytes / per_entry;

  REQUIRE(capacity >= 3);

  std::vector<CryptoHash> others;

  for (int64_t i = 1; i < capacity; i++) {
    others.push_back(fresh_key());
    admit_lru(rc, others.back(), buf.get());
  }

  // `first` is the eviction candidate. No get() probes before the re-put:
  // get() bumps recency too, which would change that.
  REQUIRE(rc->put(&first, buf.get(), PAYLOAD_LEN) == 1);

  auto extra = fresh_key();

  admit_lru(rc, extra, buf.get());

  Ptr<IOBufferData> probe;

  CHECK(rc->get(&first, &probe) >= 1);
  CHECK(rc->get(&extra, &probe) >= 1);
  CHECK(rc->get(&others.front(), &probe) == 0);
}

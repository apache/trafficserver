/** @file

  Catch-based unit tests for the RamCache `copy` contract across all policies.

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

// put(..., copy = true) is the caller's promise that it may mutate its buffer
// after the put (CacheVC unmarshals HTTP headers in place after inserting into
// the RAM cache when compression is configured, see CacheVC.cc). The cache
// must therefore never share buffers with the caller in either direction for
// copy entries: it copies on put and copies on get. These tests pin that
// contract for every RamCache implementation.

#include "main.h"

#include "../P_CacheInternal.h"
#include "../P_RamCache.h"

#include "tscore/ink_config.h"

#include <cstdint>
#include <cstring>
#include <vector>

// Required by main.h
int  cache_vols           = 1;
bool reuse_existing_cache = false;

namespace
{

struct PolicyCase {
  RamCache *(*factory)();
  const char *name;
};

const PolicyCase policy_cases[] = {
  {new_RamCacheLRU,    "LRU"   },
  {new_RamCacheCLFUS,  "CLFUS" },
  {new_RamCacheS3FIFO, "S3FIFO"},
};

// Minimal CacheDisk wiring needed to construct a StripeSM. Mirrors the helper
// in test_Stripe.cc.
void
init_disk(CacheDisk &disk)
{
  disk.path                = static_cast<char *>(ats_malloc(1));
  disk.path[0]             = '\0';
  disk.disk_stripes        = static_cast<DiskStripe **>(ats_malloc(sizeof(DiskStripe *)));
  disk.disk_stripes[0]     = nullptr;
  disk.header              = static_cast<DiskHeader *>(ats_malloc(sizeof(DiskHeader)));
  disk.header->num_volumes = 0;
}

// The RamCache get/put paths touch only these metrics and the stripe mutex.
void
wire_stripe(StripeSM &stripe, CacheVol &cache_vol)
{
  stripe.cache_vol = &cache_vol;

  cache_rsb.ram_cache_bytes          = ts::Metrics::Gauge::createPtr("unit_test.copy.ram_cache.bytes");
  cache_rsb.ram_cache_hits           = ts::Metrics::Counter::createPtr("unit_test.copy.ram_cache.hits");
  cache_rsb.ram_cache_misses         = ts::Metrics::Counter::createPtr("unit_test.copy.ram_cache.misses");
  cache_vol.vol_rsb.ram_cache_bytes  = ts::Metrics::Gauge::createPtr("unit_test.copy.vol.ram_cache.bytes");
  cache_vol.vol_rsb.ram_cache_hits   = ts::Metrics::Counter::createPtr("unit_test.copy.vol.ram_cache.hits");
  cache_vol.vol_rsb.ram_cache_misses = ts::Metrics::Counter::createPtr("unit_test.copy.vol.ram_cache.misses");
}

std::vector<char>
pattern_bytes(std::size_t len, char base)
{
  std::vector<char> bytes(len);
  for (std::size_t i = 0; i < len; i++) {
    bytes[i] = static_cast<char>(base + (i % 26));
  }
  return bytes;
}

Ptr<IOBufferData>
make_buffer(const std::vector<char> &bytes)
{
  int64_t           idx = iobuffer_size_to_index(bytes.size(), MAX_BUFFER_SIZE_INDEX);
  Ptr<IOBufferData> data{make_ptr(new_IOBufferData(idx, MEMALIGNED))};

  std::memcpy(data->data(), bytes.data(), bytes.size());
  return data;
}

CryptoHash
fresh_key()
{
  static uint64_t salt = 0;

  ++salt;
  CryptoHash key;
  key.u64[0] = 0xc0ffee00 + salt;
  key.u64[1] = 0xdeadbeef + salt;
  return key;
}

RamCache *
make_cache(const PolicyCase &pc, StripeSM &stripe)
{
  // No compression: CLFUS must not schedule its background compressor (which
  // would retain a pointer to this cache), and the seen filter would
  // otherwise reject first-time puts.
  cache_config_ram_cache_compress        = 0;
  cache_config_ram_cache_use_seen_filter = 0;

  // The policies have no destructors (entries are pool-allocated and only
  // released on eviction), so destroying a cache object strands its entries
  // for leak checkers. Keep every cache reachable for the life of the
  // process instead.
  static std::vector<RamCache *> &all_caches = *new std::vector<RamCache *>;
  RamCache                       *rc         = pc.factory();

  all_caches.push_back(rc);
  rc->init(1 << 20, &stripe);
  return rc;
}

} // namespace

TEST_CASE("RamCache copy=true entries are immune to caller-side mutation after put", "[cache][ramcache][copy]")
{
  CacheDisk disk;
  init_disk(disk);
  StripeSM stripe{&disk, 10, 0};
  CacheVol cache_vol;
  wire_stripe(stripe, cache_vol);

  const PolicyCase pc = GENERATE(from_range(std::begin(policy_cases), std::end(policy_cases)));
  INFO("policy: " << pc.name);

  auto rc      = make_cache(pc, stripe);
  auto payload = pattern_bytes(8192, 'A');
  auto buf     = make_buffer(payload);
  auto key     = fresh_key();

  REQUIRE(rc->put(&key, buf.get(), payload.size(), true) == 1);

  // The caller mutates its buffer after the put, exactly as CacheVC does when
  // it unmarshals HTTP headers in place.
  std::memset(buf->data(), 0x5a, payload.size());

  Ptr<IOBufferData> got;

  REQUIRE(rc->get(&key, &got) >= 1);
  REQUIRE(got.get() != nullptr);
  CHECK(std::memcmp(got->data(), payload.data(), payload.size()) == 0);
}

TEST_CASE("RamCache copy=true entries are immune to caller-side mutation after get", "[cache][ramcache][copy]")
{
  CacheDisk disk;
  init_disk(disk);
  StripeSM stripe{&disk, 10, 0};
  CacheVol cache_vol;
  wire_stripe(stripe, cache_vol);

  const PolicyCase pc = GENERATE(from_range(std::begin(policy_cases), std::end(policy_cases)));
  INFO("policy: " << pc.name);

  auto rc      = make_cache(pc, stripe);
  auto payload = pattern_bytes(8192, 'A');
  auto buf     = make_buffer(payload);
  auto key     = fresh_key();

  REQUIRE(rc->put(&key, buf.get(), payload.size(), true) == 1);

  Ptr<IOBufferData> first;

  REQUIRE(rc->get(&key, &first) >= 1);
  REQUIRE(first.get() != nullptr);
  // The caller mutates the buffer it was handed, as it does when unmarshalling
  // a RAM-cache hit in place.
  std::memset(first->data(), 0x5a, payload.size());

  Ptr<IOBufferData> second;

  REQUIRE(rc->get(&key, &second) >= 1);
  REQUIRE(second.get() != nullptr);
  CHECK(std::memcmp(second->data(), payload.data(), payload.size()) == 0);
}

TEST_CASE("RamCache resident entries are refreshed by a copy=true put", "[cache][ramcache][copy]")
{
  CacheDisk disk;
  init_disk(disk);
  StripeSM stripe{&disk, 10, 0};
  CacheVol cache_vol;
  wire_stripe(stripe, cache_vol);

  const PolicyCase pc = GENERATE(from_range(std::begin(policy_cases), std::end(policy_cases)));
  INFO("policy: " << pc.name);

  auto rc      = make_cache(pc, stripe);
  auto payload = pattern_bytes(8192, 'A');
  auto buf     = make_buffer(payload);
  auto key     = fresh_key();

  // Entry first stored with copy=false (e.g. compression was disabled), then
  // the same object is re-put with copy=true after a config change. The cache
  // must own a private copy from that point on.
  REQUIRE(rc->put(&key, buf.get(), payload.size(), false) == 1);
  REQUIRE(rc->put(&key, buf.get(), payload.size(), true) == 1);

  std::memset(buf->data(), 0x5a, payload.size());

  Ptr<IOBufferData> got;

  REQUIRE(rc->get(&key, &got) >= 1);
  REQUIRE(got.get() != nullptr);
  CHECK(std::memcmp(got->data(), payload.data(), payload.size()) == 0);
}

TEST_CASE("RamCache copy=false entries still share the caller's buffer", "[cache][ramcache][copy]")
{
  CacheDisk disk;
  init_disk(disk);
  StripeSM stripe{&disk, 10, 0};
  CacheVol cache_vol;
  wire_stripe(stripe, cache_vol);

  const PolicyCase pc = GENERATE(from_range(std::begin(policy_cases), std::end(policy_cases)));
  INFO("policy: " << pc.name);

  auto rc      = make_cache(pc, stripe);
  auto payload = pattern_bytes(8192, 'A');
  auto buf     = make_buffer(payload);
  auto key     = fresh_key();

  REQUIRE(rc->put(&key, buf.get(), payload.size(), false) == 1);

  Ptr<IOBufferData> got;

  REQUIRE(rc->get(&key, &got) >= 1);
  REQUIRE(got.get() != nullptr);
  // Zero-copy is the point of copy=false: the cache hands back the same
  // buffer it was given.
  CHECK(got->data() == buf->data());
}

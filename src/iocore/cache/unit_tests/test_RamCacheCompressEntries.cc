/** @file

  Catch-based regression tests for RamCacheCLFUS::compress_entries().

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

#include "main.h"

#include "../RamCacheCLFUS.h"
#include "../P_CacheInternal.h"

#include <climits>
#include <cstdint>
#include <cstring>
#include <vector>

// Required by main.h
int  cache_vols           = 1;
bool reuse_existing_cache = false;

namespace
{

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

// The CLFUS get/put/compress paths touch only these metrics and the stripe
// mutex, so that is all the stripe needs for these tests.
void
wire_stripe(StripeSM &stripe, CacheVol &cache_vol)
{
  stripe.cache_vol = &cache_vol;

  cache_rsb.ram_cache_bytes          = ts::Metrics::Gauge::createPtr("unit_test.clfus.ram_cache.bytes");
  cache_rsb.ram_cache_hits           = ts::Metrics::Counter::createPtr("unit_test.clfus.ram_cache.hits");
  cache_rsb.ram_cache_misses         = ts::Metrics::Counter::createPtr("unit_test.clfus.ram_cache.misses");
  cache_vol.vol_rsb.ram_cache_bytes  = ts::Metrics::Gauge::createPtr("unit_test.clfus.vol.ram_cache.bytes");
  cache_vol.vol_rsb.ram_cache_hits   = ts::Metrics::Counter::createPtr("unit_test.clfus.vol.ram_cache.hits");
  cache_vol.vol_rsb.ram_cache_misses = ts::Metrics::Counter::createPtr("unit_test.clfus.vol.ram_cache.misses");
}

RamCacheCLFUS *
make_cache(StripeSM &stripe)
{
  // Initialize with compression disabled so init() does not schedule the
  // background compressor continuation, which would retain a pointer to the
  // cache; compression is driven synchronously by the tests instead. The
  // caches are kept reachable for the life of the process because the policy
  // has no destructor (entries are pool-allocated).
  cache_config_ram_cache_compress         = CACHE_COMPRESSION_NONE;
  cache_config_ram_cache_compress_percent = 100;
  cache_config_ram_cache_use_seen_filter  = 0;

  static std::vector<RamCacheCLFUS *> &all_caches = *new std::vector<RamCacheCLFUS *>;
  auto                                *rc         = new RamCacheCLFUS;

  all_caches.push_back(rc);
  rc->init(1 << 20, &stripe);
  return rc;
}

std::vector<char>
pattern_bytes(std::size_t len)
{
  std::vector<char> bytes(len);
  for (std::size_t i = 0; i < len; i++) {
    bytes[i] = static_cast<char>('A' + (i % 26));
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

} // namespace

// fastlz cannot compress inputs under 16 bytes. Such entries must be decided
// under the stripe lock and left intact. The races fixed alongside this test
// are only observable under concurrency, so this pins the single-threaded
// behavior of the restructured code: undersized entries are marked
// incompressible while locked, skipped on later passes, and stay readable.
TEST_CASE("CLFUS compress_entries leaves undersized payloads intact", "[cache][ramcache][compress]")
{
  CacheDisk disk;
  init_disk(disk);
  StripeSM stripe{&disk, 10, 0};
  CacheVol cache_vol;
  wire_stripe(stripe, cache_vol);

  auto *rc      = make_cache(stripe);
  auto  payload = pattern_bytes(8);
  auto  buf     = make_buffer(payload);
  auto  key     = fresh_key();

  REQUIRE(rc->put(&key, buf.get(), payload.size(), true) == 1);

  cache_config_ram_cache_compress = CACHE_COMPRESSION_FASTLZ;
  // Two passes: the first marks the entry incompressible; the second must
  // skip it (on the former code, the first pass's loop tail ran unlocked).
  rc->compress_entries(this_ethread(), INT_MAX);
  rc->compress_entries(this_ethread(), INT_MAX);

  Ptr<IOBufferData> got;

  REQUIRE(rc->get(&key, &got) == RAM_HIT_COMPRESS_NONE);
  REQUIRE(got.get() != nullptr);
  CHECK(std::memcmp(got->data(), payload.data(), payload.size()) == 0);
}

TEST_CASE("CLFUS compress_entries still compresses eligible payloads", "[cache][ramcache][compress]")
{
  CacheDisk disk;
  init_disk(disk);
  StripeSM stripe{&disk, 10, 0};
  CacheVol cache_vol;
  wire_stripe(stripe, cache_vol);

  auto *rc      = make_cache(stripe);
  auto  payload = pattern_bytes(8192);
  auto  buf     = make_buffer(payload);
  auto  key     = fresh_key();

  REQUIRE(rc->put(&key, buf.get(), payload.size(), true) == 1);

  cache_config_ram_cache_compress = CACHE_COMPRESSION_FASTLZ;
  int64_t size_before             = rc->size();

  rc->compress_entries(this_ethread(), INT_MAX);
  CHECK(rc->size() < size_before);

  Ptr<IOBufferData> got;

  REQUIRE(rc->get(&key, &got) == RAM_HIT_COMPRESS_FASTLZ);
  REQUIRE(got.get() != nullptr);
  CHECK(std::memcmp(got->data(), payload.data(), payload.size()) == 0);
}

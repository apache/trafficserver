/** @file

  Catch-based unit tests for RamCache hit rates under a Zipf workload.

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
#include "test_doubles.h"

#include "../P_CacheInternal.h"
#include "../P_RamCache.h"

#include "tscore/Random.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

int  cache_vols           = 1;
bool reuse_existing_cache = false;

namespace
{

// Inherited from the regression test. 1 MB is the tight size: LRU variable-size scored 0.5565 and CLFUS 0.5665 at the port.
constexpr double MIN_HIT_RATE = 0.55;
constexpr int    ZIPF_SIZE    = 1 << 20;
constexpr double ZIPF_ALPHA   = 1.2;

struct PolicyCase {
  RamCache *(*factory)();
  const char *name;
};

const PolicyCase policy_cases[] = {
  {new_RamCacheLRU,    "LRU"    },
  {new_RamCacheCLFUS,  "CLFUS"  },
  {new_RamCacheS3FIFO, "S3-FIFO"},
};

struct HitRates {
  bool    hot_keys_resident = true;
  double  fixed_size        = 0;
  double  variable_size     = 0;
  int64_t size              = 0;
};

void
wire_stripe(StripeSM &stripe, CacheVol &cache_vol)
{
  stripe.cache_vol = &cache_vol;

  cache_rsb.ram_cache_bytes          = ts::Metrics::Gauge::createPtr("unit_test.hitrate.ram_cache.bytes");
  cache_rsb.ram_cache_hits           = ts::Metrics::Counter::createPtr("unit_test.hitrate.ram_cache.hits");
  cache_rsb.ram_cache_misses         = ts::Metrics::Counter::createPtr("unit_test.hitrate.ram_cache.misses");
  cache_vol.vol_rsb.ram_cache_bytes  = ts::Metrics::Gauge::createPtr("unit_test.hitrate.vol.ram_cache.bytes");
  cache_vol.vol_rsb.ram_cache_hits   = ts::Metrics::Counter::createPtr("unit_test.hitrate.vol.ram_cache.hits");
  cache_vol.vol_rsb.ram_cache_misses = ts::Metrics::Counter::createPtr("unit_test.hitrate.vol.ram_cache.misses");
}

RamCache *
make_cache(RamCache *(*factory)(), StripeSM &stripe, int64_t max_bytes)
{
  // No compression, so CLFUS schedules no background compressor holding a pointer to this cache.
  cache_config_ram_cache_compress        = CACHE_COMPRESSION_NONE;
  cache_config_ram_cache_use_seen_filter = 1;

  // The policies have no destructors; keep every cache reachable so leak checkers stay quiet.
  static std::vector<RamCache *> &all_caches = *new std::vector<RamCache *>;
  RamCache                       *rc         = factory();

  all_caches.push_back(rc);
  rc->init(max_bytes, &stripe);
  return rc;
}

std::vector<double> const &
zipf_cdf()
{
  static std::vector<double> const table = [] {
    std::vector<double> t(ZIPF_SIZE);

    for (int i = 0; i < ZIPF_SIZE; i++) {
      t[i] = 1.0 / std::pow(i + 2, ZIPF_ALPHA);
    }
    for (int i = 1; i < ZIPF_SIZE; i++) {
      t[i] += t[i - 1];
    }
    double const total = t.back();
    for (auto &x : t) {
      x /= total;
    }
    return t;
  }();

  return table;
}

// Kept bit-for-bit with the former regression test's search: the hit-rate floor was calibrated against it.
int
zipf_sample(double v)
{
  auto const &cdf = zipf_cdf();
  int         l   = 0;
  int         r   = ZIPF_SIZE - 1;
  int         m   = 0;

  do {
    m = (r + l) / 2;
    if (v < cdf[m]) {
      r = m - 1;
    } else {
      l = m + 1;
    }
  } while (l < r);
  return m;
}

CryptoHash
numbered_key(uint64_t n)
{
  CryptoHash key;

  key.u64[0] = (n << 32) + n;
  key.u64[1] = (n << 32) + n;
  return key;
}

Ptr<IOBufferData>
zeroed_buffer(int64_t size_index)
{
  Ptr<IOBufferData> data{make_ptr(new_IOBufferData(size_index))};

  std::memset(data->data(), 0, data->block_size());
  return data;
}

// Only the second half of the samples is scored, so the cache is warm. Fixed-size fills put 16K buffers with a
// 32K length, as the regression did; variable-size fills use 8K/16K/32K buffers at their real length.
double
hit_rate(RamCache *cache, std::vector<int> const &samples, bool variable_size)
{
  std::vector<Ptr<IOBufferData>> fills;
  int const                      half   = static_cast<int>(samples.size()) / 2;
  int                            misses = 0;

  for (int i = 0; i < static_cast<int>(samples.size()); i++) {
    CryptoHash        key = numbered_key(samples[i]);
    Ptr<IOBufferData> got;

    if (!cache->get(&key, &got)) {
      fills.push_back(zeroed_buffer(variable_size ? BUFFER_SIZE_INDEX_8K + (samples[i] % 3) : BUFFER_SIZE_INDEX_16K));
      cache->put(&key, fills.back().get(), variable_size ? static_cast<uint32_t>(fills.back()->block_size()) : 1u << 15);
      if (i >= half) {
        misses++;
      }
    }
  }
  return 1.0 - static_cast<double>(misses) / half;
}

HitRates
measure(RamCache *cache, int64_t cache_size)
{
  HitRates                       rates;
  std::vector<Ptr<IOBufferData>> warm;

  warm.reserve(200);
  for (int i = 0; i < 200; i++) {
    warm.push_back(zeroed_buffer(BUFFER_SIZE_INDEX_16K));
  }
  for (int round = 0; round < 10; round++) {
    for (uint64_t i = 0; i < 200; i++) {
      CryptoHash key = numbered_key(i);

      cache->put(&key, warm[i].get(), 1 << 15);
      for (uint64_t j = 0; j <= i && j < 10; j++) {
        CryptoHash        hot = numbered_key(j);
        Ptr<IOBufferData> got;

        cache->get(&hot, &got);
      }
    }
  }
  for (uint64_t i = 0; i < 10; i++) {
    CryptoHash        key = numbered_key(i);
    Ptr<IOBufferData> got;

    if (!cache->get(&key, &got)) {
      rates.hot_keys_resident = false;
    }
  }
  warm.clear();

  std::vector<int> samples(cache_size >> 6);

  ts::Random::seed(13);
  for (auto &s : samples) {
    s = zipf_sample(ts::Random::drandom());
  }

  rates.fixed_size    = hit_rate(cache, samples, false);
  rates.variable_size = hit_rate(cache, samples, true);
  rates.size          = cache->size();
  return rates;
}

void
check_rates(HitRates const &rates, int64_t cache_size)
{
  INFO("fixed " << rates.fixed_size << ", variable " << rates.variable_size << ", size " << rates.size << " of " << cache_size);
  CHECK(rates.hot_keys_resident);
  CHECK(rates.fixed_size >= MIN_HIT_RATE);
  CHECK(rates.variable_size >= MIN_HIT_RATE);
  CHECK(std::abs(cache_size - rates.size) <= 0.02 * cache_size);
}

} // namespace

TEST_CASE("RamCache policies keep a Zipf working set resident", "[cache][ramcache][hitrate]")
{
  CacheDisk disk;
  init_disk(disk);
  StripeSM stripe{&disk, 10, 0};
  CacheVol cache_vol;
  wire_stripe(stripe, cache_vol);

  PolicyCase const pc         = GENERATE(from_range(policy_cases));
  int64_t const    cache_size = GENERATE(int64_t{1} << 20, int64_t{1} << 24);

  INFO("policy " << pc.name << ", cache size " << cache_size);
  check_rates(measure(make_cache(pc.factory, stripe, cache_size), cache_size), cache_size);
}

// Valid non-default S3-FIFO tunables must still meet the floor, proving the config globals reach init().
TEST_CASE("RamCacheS3FIFO meets the hit-rate floor with non-default tunables", "[cache][ramcache][hitrate]")
{
  CacheDisk disk;
  init_disk(disk);
  StripeSM stripe{&disk, 10, 0};
  CacheVol cache_vol;
  wire_stripe(stripe, cache_vol);

  int const     saved_main    = cache_config_ram_cache_s3fifo_main_percent;
  int const     saved_gsize   = cache_config_ram_cache_s3fifo_ghost_size_percent;
  int const     saved_gmem    = cache_config_ram_cache_s3fifo_ghost_mem_percent;
  int const     saved_promote = cache_config_ram_cache_s3fifo_promote_threshold;
  int64_t const cache_size    = 1LL << 24;

  cache_config_ram_cache_s3fifo_main_percent       = 80;
  cache_config_ram_cache_s3fifo_ghost_size_percent = 50;
  cache_config_ram_cache_s3fifo_ghost_mem_percent  = 15;
  cache_config_ram_cache_s3fifo_promote_threshold  = 1;

  check_rates(measure(make_cache(new_RamCacheS3FIFO, stripe, cache_size), cache_size), cache_size);

  cache_config_ram_cache_s3fifo_main_percent       = saved_main;
  cache_config_ram_cache_s3fifo_ghost_size_percent = saved_gsize;
  cache_config_ram_cache_s3fifo_ghost_mem_percent  = saved_gmem;
  cache_config_ram_cache_s3fifo_promote_threshold  = saved_promote;
}

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

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

int  cache_vols           = 1;
bool reuse_existing_cache = false;

namespace
{

// Floors sit a few points below the weakest policy: LRU variable-size scored 0.389 at 1 MB and 0.699 at 16 MB.
constexpr double MIN_HIT_RATE_SMALL = 0.35;
constexpr double MIN_HIT_RATE_LARGE = 0.65;
constexpr int    ZIPF_SIZE          = 1 << 20;
constexpr double ZIPF_ALPHA         = 1.2;

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

// Inverse-CDF sample: the smallest rank whose cumulative probability exceeds v.
int
zipf_sample(double v)
{
  auto const &cdf = zipf_cdf();
  auto const  it  = std::upper_bound(cdf.begin(), cdf.end(), v);

  return std::min(static_cast<int>(it - cdf.begin()), ZIPF_SIZE - 1);
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
  double const floor = cache_size <= (int64_t{1} << 20) ? MIN_HIT_RATE_SMALL : MIN_HIT_RATE_LARGE;

  CHECK(rates.fixed_size >= floor);
  CHECK(rates.variable_size >= floor);
  // At 1 MB, 2% is less than one 32K object, so allow at least that much slack.
  CHECK(std::abs(cache_size - rates.size) <= std::max(0.02 * cache_size, double{1 << 15}));
}

struct MixedRates {
  double object = 0;
  double bytes  = 0;
};

// Even Zipf ranks are 4K objects and odd ranks 64K, so both sizes share the hot set equally. As in hit_rate(), only
// the second half of the samples is scored.
MixedRates
mixed_size_hit_rates(RamCache *cache, std::vector<int> const &samples)
{
  Ptr<IOBufferData> const small = zeroed_buffer(BUFFER_SIZE_INDEX_4K);
  Ptr<IOBufferData> const large = zeroed_buffer(BUFFER_SIZE_INDEX_64K);
  int const               half  = static_cast<int>(samples.size()) / 2;
  int64_t                 hits = 0, bytes = 0, hit_bytes = 0;

  for (int i = 0; i < static_cast<int>(samples.size()); i++) {
    CryptoHash        key  = numbered_key(samples[i]);
    IOBufferData     *data = (samples[i] & 1) ? large.get() : small.get();
    Ptr<IOBufferData> got;
    bool const        hit = cache->get(&key, &got);

    if (!hit) {
      cache->put(&key, data, static_cast<uint32_t>(data->block_size()));
    }
    if (i >= half) {
      bytes += data->block_size();
      if (hit) {
        hits++;
        hit_bytes += data->block_size();
      }
    }
  }
  return {static_cast<double>(hits) / (samples.size() - half), static_cast<double>(hit_bytes) / bytes};
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

// CLFUS weighs hits by size, so on a 50/50 mix of 4K and 64K objects it should keep far more of the hot set than LRU
// without giving up byte hit rate. Measured at 1/4/16 MB: +14.8/+11.4/+8.8 object points, +4.3/+3.6/+2.9 byte points.
TEST_CASE("RamCacheCLFUS beats LRU on a mix of 4K and 64K objects", "[cache][ramcache][hitrate]")
{
  CacheDisk disk;
  init_disk(disk);
  StripeSM stripe{&disk, 10, 0};
  CacheVol cache_vol;
  wire_stripe(stripe, cache_vol);

  static std::vector<int> const samples = [] {
    std::vector<int> v(1 << 21);

    ts::Random::seed(13);
    for (auto &s : v) {
      s = zipf_sample(ts::Random::drandom());
    }
    return v;
  }();

  int64_t const    cache_size = GENERATE(int64_t{1} << 20, int64_t{4} << 20, int64_t{16} << 20);
  MixedRates const lru        = mixed_size_hit_rates(make_cache(new_RamCacheLRU, stripe, cache_size), samples);
  MixedRates const clfus      = mixed_size_hit_rates(make_cache(new_RamCacheCLFUS, stripe, cache_size), samples);

  INFO("cache size " << cache_size << ": LRU object " << lru.object << " bytes " << lru.bytes << ", CLFUS object " << clfus.object
                     << " bytes " << clfus.bytes);
  CHECK(clfus.object >= lru.object + 0.05);
  CHECK(clfus.bytes >= lru.bytes);
}

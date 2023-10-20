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

#include "main.h"

#include "P_Cache.h"

#include "tscore/Random.h"

// Required by main.h
int cache_vols            = 1;
bool reuse_existing_cache = false;

#define ZIPF_SIZE (1 << 20)

namespace
{

DbgCtl dbg_ctl_cache_test{"cache_test"};

double zipf_alpha        = 1.2;
int64_t zipf_bucket_size = 1;

double *zipf_table = nullptr;

void
build_zipf()
{
  if (zipf_table) {
    return;
  }
  zipf_table = static_cast<double *>(ats_malloc(ZIPF_SIZE * sizeof(double)));
  for (int i = 0; i < ZIPF_SIZE; i++) {
    zipf_table[i] = 1.0 / pow(i + 2, zipf_alpha);
  }
  for (int i = 1; i < ZIPF_SIZE; i++) {
    zipf_table[i] = zipf_table[i - 1] + zipf_table[i];
  }
  double x = zipf_table[ZIPF_SIZE - 1];
  for (int i = 0; i < ZIPF_SIZE; i++) {
    zipf_table[i] = zipf_table[i] / x;
  }
}

int
get_zipf(double v)
{
  int l = 0, r = ZIPF_SIZE - 1, m;
  do {
    m = (r + l) / 2;
    if (v < zipf_table[m]) {
      r = m - 1;
    } else {
      l = m + 1;
    }
  } while (l < r);
  if (zipf_bucket_size == 1) {
    return m;
  }
  double x = zipf_table[m], y = zipf_table[m + 1];
  m += static_cast<int>((v - x) / (y - x));
  return m;
}

void
test_RamCache(RamCache *cache, const char *name, int64_t cache_size)
{
  CacheKey key;
  Vol *vol = theCache->key_to_vol(&key, "example.com", sizeof("example.com") - 1);
  std::vector<Ptr<IOBufferData>> data;

  cache->init(cache_size, vol);

  for (int l = 0; l < 10; l++) {
    for (int i = 0; i < 200; i++) {
      IOBufferData *d = THREAD_ALLOC(ioDataAllocator, this_thread());
      CryptoHash hash;

      d->alloc(BUFFER_SIZE_INDEX_16K);
      data.push_back(make_ptr(d));
      hash.u64[0] = (static_cast<uint64_t>(i) << 32) + i;
      hash.u64[1] = (static_cast<uint64_t>(i) << 32) + i;
      cache->put(&hash, data[i].get(), 1 << 15);
      // More hits for the first 10.
      for (int j = 0; j <= i && j < 10; j++) {
        Ptr<IOBufferData> data;
        CryptoHash hash;

        hash.u64[0] = (static_cast<uint64_t>(j) << 32) + j;
        hash.u64[1] = (static_cast<uint64_t>(j) << 32) + j;
        cache->get(&hash, &data);
      }
    }
  }

  for (int i = 0; i < 10; i++) {
    CryptoHash hash;
    Ptr<IOBufferData> data;

    hash.u64[0] = (static_cast<uint64_t>(i) << 32) + i;
    hash.u64[1] = (static_cast<uint64_t>(i) << 32) + i;

    CHECK(cache->get(&hash, &data));
  }

  int sample_size = cache_size >> 6;
  build_zipf();
  ts::Random::seed(13);
  int *r = static_cast<int *>(ats_malloc(sample_size * sizeof(int)));
  for (int i = 0; i < sample_size; i++) {
    r[i] = get_zipf(ts::Random::drandom());
  }
  data.clear();
  int misses = 0;
  for (int i = 0; i < sample_size; i++) {
    CryptoHash hash;
    hash.u64[0] = (static_cast<uint64_t>(r[i]) << 32) + r[i];
    hash.u64[1] = (static_cast<uint64_t>(r[i]) << 32) + r[i];
    Ptr<IOBufferData> get_data;
    if (!cache->get(&hash, &get_data)) {
      IOBufferData *d = THREAD_ALLOC(ioDataAllocator, this_thread());
      d->alloc(BUFFER_SIZE_INDEX_16K);
      data.push_back(make_ptr(d));
      cache->put(&hash, data.back().get(), 1 << 15);
      if (i >= sample_size / 2) {
        misses++; // Sample last half of the gets.
      }
    }
  }
  double fixed_hit_rate = 1.0 - ((static_cast<double>(misses)) / (sample_size / 2));
  Dbg(dbg_ctl_cache_test, "RamCache %s Fixed Size Hit Rate %f", name, fixed_hit_rate);

  data.clear();
  misses = 0;
  for (int i = 0; i < sample_size; i++) {
    CryptoHash hash;
    hash.u64[0] = (static_cast<uint64_t>(r[i]) << 32) + r[i];
    hash.u64[1] = (static_cast<uint64_t>(r[i]) << 32) + r[i];
    Ptr<IOBufferData> get_data;
    if (!cache->get(&hash, &get_data)) {
      IOBufferData *d = THREAD_ALLOC(ioDataAllocator, this_thread());
      d->alloc(BUFFER_SIZE_INDEX_8K + (r[i] % 3));
      data.push_back(make_ptr(d));
      cache->put(&hash, data.back().get(), d->block_size());
      if (i >= sample_size / 2) {
        misses++; // Sample last half of the gets.
      }
    }
  }
  double variable_hit_rate = 1.0 - ((static_cast<double>(misses)) / (sample_size / 2));
  Dbg(dbg_ctl_cache_test, "RamCache %s Variable Size Hit Rate %f", name, variable_hit_rate);

  Dbg(dbg_ctl_cache_test, "RamCache %s Nominal Size %" PRId64 " Size %" PRId64, name, cache_size, cache->size());

  CHECK(fixed_hit_rate >= 0.55);
  CHECK(variable_hit_rate >= 0.55);

  CHECK(abs(cache_size - cache->size()) <= 0.02 * cache_size);

  ats_free(r);

  Dbg(dbg_ctl_cache_test, "RamCache %s Test Done", name);
}
} // namespace

class RamCacheTest : public CacheInit
{
public:
  int
  cache_init_success_callback(int event, void *e) override
  {
    // Test
    _run();

    // Teardown
    TerminalTest *tt = new TerminalTest;
    this_ethread()->schedule_imm(tt);
    delete this;

    return 0;
  }

private:
  void
  _run()
  {
    REQUIRE(cacheProcessor.IsCacheEnabled() == CACHE_INITIALIZED);

    for (int s = 20; s <= 28; s += 4) {
      int64_t cache_size = 1LL << s;
      test_RamCache(new_RamCacheLRU(), "LRU", cache_size);
      test_RamCache(new_RamCacheCLFUS(), "CLFUS", cache_size);
    }
  }
};

TEST_CASE("RamCache")
{
  init_cache(0);

  RamCacheTest *test = new RamCacheTest;

  this_ethread()->schedule_imm(test);
  this_thread()->execute();

  return;
}

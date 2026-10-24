/** @file

  Catch-based unit tests for the RAM cache (CLFUS).

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

#include "iocore/cache/Cache.h"
#include "tscore/ink_config.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// Required by main.h
int  cache_vols           = 1;
bool reuse_existing_cache = false;

// Reaches into RamCacheCLFUS to find a stored entry; declared a friend there.
struct RamCacheCLFUSTestAccess {
  static RamCacheCLFUSEntry *
  find_entry(RamCacheCLFUS &rc, const CryptoHash &key)
  {
    for (RamCacheCLFUSEntry *e = rc._bucket[key.slice32(3) % rc._nbuckets].head; e != nullptr; e = e->hash_link.next) {
      if (e->key == key) {
        return e;
      }
    }
    return nullptr;
  }

  static constexpr double
  cache_value_hits_size(const uint64_t hits, const uint32_t size)
  {
    return RamCacheCLFUS::cache_value_hits_size(hits, size);
  }

  static RamCacheCLFUSEntry *
  lru_head(RamCacheCLFUS &rc, int queue = 0)
  {
    return rc._lru[queue].head;
  }

  static RamCacheCLFUSEntry *
  lru_tail(RamCacheCLFUS &rc, int queue = 0)
  {
    return rc._lru[queue].tail;
  }

  static double
  average_value(RamCacheCLFUS &rc)
  {
    return rc._average_value;
  }

  static void
  set_average_value(RamCacheCLFUS &rc, double v)
  {
    rc._average_value = v;
  }

  static void
  set_last_decay_time(RamCacheCLFUS &rc, ink_hrtime t)
  {
    rc._last_decay_time = t;
  }

  static ink_hrtime
  last_decay_time(RamCacheCLFUS &rc)
  {
    return rc._last_decay_time;
  }

  static int64_t
  history(RamCacheCLFUS &rc)
  {
    return rc._history;
  }

  static int64_t
  objects(RamCacheCLFUS &rc)
  {
    return rc._objects;
  }

  static int64_t
  bytes(RamCacheCLFUS &rc)
  {
    return rc._bytes;
  }

  static int64_t
  max_bytes(RamCacheCLFUS &rc)
  {
    return rc._max_bytes;
  }

  static void
  set_resident_hits(RamCacheCLFUS &rc, uint32_t hits)
  {
    forl_LL(RamCacheCLFUSEntry, e, rc._lru[0])
    {
      e->hits = hits;
    }
  }
};

namespace
{

// A compression backend to exercise, along with the RAM_HIT_* state get()
// should report once a compressible object has been stored compressed.
struct CompressionCase {
  int         config;       // CACHE_COMPRESSION_*
  int         expected_hit; // RAM_HIT_COMPRESS_* reported by get() for compressible data
  const char *name;
};

std::vector<CompressionCase>
compression_cases()
{
  std::vector<CompressionCase> cases{
    {CACHE_COMPRESSION_NONE,   RAM_HIT_COMPRESS_NONE,   "none"  },
    {CACHE_COMPRESSION_FASTLZ, RAM_HIT_COMPRESS_FASTLZ, "fastlz"},
    {CACHE_COMPRESSION_LIBZ,   RAM_HIT_COMPRESS_LIBZ,   "libz"  },
  };
#ifdef HAVE_LZMA_H
  cases.push_back({CACHE_COMPRESSION_LIBLZMA, RAM_HIT_COMPRESS_LIBLZMA, "liblzma"});
#endif
#ifdef HAVE_LZ4_H
  cases.push_back({CACHE_COMPRESSION_LZ4, RAM_HIT_COMPRESS_LZ4, "lz4"});
#endif
#ifdef HAVE_ZSTD_H
  cases.push_back({CACHE_COMPRESSION_ZSTD, RAM_HIT_COMPRESS_ZSTD, "zstd"});
#endif
  return cases;
}

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

  cache_rsb.ram_cache_bytes                     = ts::Metrics::Gauge::createPtr("unit_test.clfus.ram_cache.bytes");
  cache_rsb.ram_cache_hits                      = ts::Metrics::Counter::createPtr("unit_test.clfus.ram_cache.hits");
  cache_rsb.ram_cache_misses                    = ts::Metrics::Counter::createPtr("unit_test.clfus.ram_cache.misses");
  cache_rsb.ram_cache_compress_failures         = ts::Metrics::Counter::createPtr("unit_test.clfus.ram_cache.compress.failure");
  cache_rsb.ram_cache_decompress_failures       = ts::Metrics::Counter::createPtr("unit_test.clfus.ram_cache.decompress.failure");
  cache_vol.vol_rsb.ram_cache_bytes             = ts::Metrics::Gauge::createPtr("unit_test.clfus.vol.ram_cache.bytes");
  cache_vol.vol_rsb.ram_cache_hits              = ts::Metrics::Counter::createPtr("unit_test.clfus.vol.ram_cache.hits");
  cache_vol.vol_rsb.ram_cache_misses            = ts::Metrics::Counter::createPtr("unit_test.clfus.vol.ram_cache.misses");
  cache_vol.vol_rsb.ram_cache_compress_failures = ts::Metrics::Counter::createPtr("unit_test.clfus.vol.ram_cache.compress.failure");
  cache_vol.vol_rsb.ram_cache_decompress_failures =
    ts::Metrics::Counter::createPtr("unit_test.clfus.vol.ram_cache.decompress.failure");
}

Ptr<IOBufferData>
make_buffer(const std::vector<char> &bytes)
{
  int64_t           idx = iobuffer_size_to_index(bytes.size(), MAX_BUFFER_SIZE_INDEX);
  Ptr<IOBufferData> data{make_ptr(new_IOBufferData(idx, MEMALIGNED))};
  std::memcpy(data->data(), bytes.data(), bytes.size());
  return data;
}

// Highly compressible: a short repeating pattern.
std::vector<char>
compressible_bytes(std::size_t len)
{
  std::vector<char> bytes(len);
  for (std::size_t i = 0; i < len; i++) {
    bytes[i] = static_cast<char>('A' + (i % 26));
  }
  return bytes;
}

// Effectively incompressible: a deterministic xorshift byte stream.
std::vector<char>
incompressible_bytes(std::size_t len)
{
  std::vector<char> bytes(len);
  uint32_t          state = 0x9e3779b9;
  for (std::size_t i = 0; i < len; i++) {
    state    ^= state << 13;
    state    ^= state >> 17;
    state    ^= state << 5;
    bytes[i]  = static_cast<char>(state & 0xff);
  }
  return bytes;
}

// Comparing two 256 KB vectors with CHECK() makes Catch2 stringify both
// operands, which throws before it can report anything useful. Report the
// offset of the first difference instead, so a genuine round-trip failure
// names the byte.
std::size_t
first_difference(const std::vector<char> &lhs, const std::vector<char> &rhs)
{
  std::size_t common = std::min(lhs.size(), rhs.size());
  for (std::size_t i = 0; i < common; i++) {
    if (lhs[i] != rhs[i]) {
      return i;
    }
  }
  return common;
}

struct RoundtripResult {
  int               hit         = 0;
  int64_t           size_before = 0; // rc.size() after put, before the compression pass
  int64_t           size_after  = 0; // rc.size() after the compression pass
  std::vector<char> out;
};

// Store payload under a fresh key, force a synchronous compression pass with
// `config`, then read it back.
RoundtripResult
store_compress_get(StripeSM &stripe, int config, const std::vector<char> &payload)
{
  // Initialize with compression disabled so init() does not schedule the
  // background compressor (which would retain a pointer to this stack object).
  cache_config_ram_cache_compress         = CACHE_COMPRESSION_NONE;
  cache_config_ram_cache_compress_percent = 100;
  cache_config_ram_cache_use_seen_filter  = 0;

  RamCacheCLFUS rc;
  rc.init(1 << 20, &stripe);

  Ptr<IOBufferData> in  = make_buffer(payload);
  uint32_t          len = static_cast<uint32_t>(payload.size());

  // A fresh RamCacheCLFUS per call, so one fixed key cannot collide with
  // anything.
  CryptoHash key;
  key.u64[0] = 0xc0ffee00;
  key.u64[1] = 0xdeadbeef;

  REQUIRE(rc.put(&key, in.get(), len) == 1);

  RoundtripResult r;

  r.size_before                   = rc.size();
  cache_config_ram_cache_compress = config;
  rc.compress_entries(this_ethread());
  r.size_after = rc.size();

  Ptr<IOBufferData> ret;

  r.hit = rc.get(&key, &ret);
  REQUIRE(ret.get() != nullptr);
  r.out.assign(ret->data(), ret->data() + len);
  return r;
}

} // namespace

TEST_CASE("CLFUS compressible objects roundtrip cleanly", "[cache][ramcache][compress]")
{
  CacheDisk disk;
  init_disk(disk);
  StripeSM stripe{&disk, 10, 0};
  CacheVol cache_vol;
  wire_stripe(stripe, cache_vol);

  // Large enough to exercise the *_compressBound() arithmetic and uint32_t
  // casts in compress_entries(), not just small-buffer paths.
  auto                  payload = compressible_bytes(256 * 1024);
  const CompressionCase c       = GENERATE(from_range(compression_cases()));
  INFO("compression backend: " << c.name);

  RoundtripResult r = store_compress_get(stripe, c.config, payload);

  CHECK(r.hit == c.expected_hit);
  CHECK(r.out.size() == payload.size());
  CHECK(first_difference(r.out, payload) == payload.size());
  if (c.config != CACHE_COMPRESSION_NONE) {
    // The feature's contract is that compression saves memory, not merely
    // that the entry is tagged compressed.
    CHECK(r.size_after < r.size_before);
  }
}

TEST_CASE("CLFUS incompressible objects fall back to uncompressed storage", "[cache][ramcache][compress]")
{
  CacheDisk disk;
  init_disk(disk);
  StripeSM stripe{&disk, 10, 0};
  CacheVol cache_vol;
  wire_stripe(stripe, cache_vol);

  auto payload = incompressible_bytes(256 * 1024);

  // Only the backends that actually attempt compression are interesting here.
  std::vector<CompressionCase> cases;
  for (auto const &candidate : compression_cases()) {
    if (candidate.config != CACHE_COMPRESSION_NONE) {
      cases.push_back(candidate);
    }
  }
  const CompressionCase c = GENERATE_REF(from_range(cases));
  INFO("compression backend: " << c.name);

  RoundtripResult r = store_compress_get(stripe, c.config, payload);

  // Incompressible data is kept verbatim, so a read reports no compression.
  CHECK(r.hit == RAM_HIT_COMPRESS_NONE);
  CHECK(r.out.size() == payload.size());
  CHECK(first_difference(r.out, payload) == payload.size());
  // And the pass never grows the entry: a regression that stored the expanded
  // "compressed" blob would still read back correctly but would cost memory.
  // Not an equality check, because re-storing an incompressible entry tightly
  // legitimately shrinks a payload that carried buffer padding.
  CHECK(r.size_after <= r.size_before);
}

TEST_CASE("CLFUS single-byte payload roundtrips", "[cache][ramcache][compress]")
{
  CacheDisk disk;
  init_disk(disk);
  StripeSM stripe{&disk, 10, 0};
  CacheVol cache_vol;
  wire_stripe(stripe, cache_vol);

  auto                  payload = compressible_bytes(1);
  const CompressionCase c       = GENERATE(from_range(compression_cases()));
  INFO("compression backend: " << c.name);

  RoundtripResult r = store_compress_get(stripe, c.config, payload);

  // Every codec emits a frame larger than a single byte, so a one-byte object
  // can never shrink; whichever way a backend declines it -- an explicit
  // too-small guard, the incompressible marking, or storing the bytes verbatim
  // -- the object must survive and read back uncompressed.
  CHECK(r.hit == RAM_HIT_COMPRESS_NONE);
  CHECK(r.out.size() == payload.size());
  CHECK(first_difference(r.out, payload) == payload.size());
}

TEST_CASE("CLFUS reports a corrupted compressed entry rather than serving it", "[cache][ramcache][compress]")
{
  CacheDisk disk;
  init_disk(disk);
  StripeSM stripe{&disk, 10, 0};
  CacheVol cache_vol;
  wire_stripe(stripe, cache_vol);

  // Only backends that actually store a compressed blob can have one corrupted.
  std::vector<CompressionCase> cases;
  for (auto const &candidate : compression_cases()) {
    if (candidate.config != CACHE_COMPRESSION_NONE) {
      cases.push_back(candidate);
    }
  }
  const CompressionCase c = GENERATE_REF(from_range(cases));
  INFO("compression backend: " << c.name);

  cache_config_ram_cache_compress         = CACHE_COMPRESSION_NONE;
  cache_config_ram_cache_compress_percent = 100;
  cache_config_ram_cache_use_seen_filter  = 0;

  RamCacheCLFUS rc;
  rc.init(1 << 20, &stripe);

  auto              payload = compressible_bytes(256 * 1024);
  uint32_t          len     = static_cast<uint32_t>(payload.size());
  Ptr<IOBufferData> in      = make_buffer(payload);

  CryptoHash key;
  key.u64[0] = 0xc0ffee00;
  key.u64[1] = 0xdeadbeef;

  REQUIRE(rc.put(&key, in.get(), len) == 1);

  cache_config_ram_cache_compress = c.config;
  rc.compress_entries(this_ethread());

  RamCacheCLFUSEntry *e = RamCacheCLFUSTestAccess::find_entry(rc, key);
  REQUIRE(e != nullptr);
  // The pass must actually have compressed it, or there is nothing to corrupt.
  REQUIRE(e->flag_bits.compressed != 0);
  REQUIRE(e->compressed_len > 0);

  // Overwrite the whole stored blob. Every codec here rejects this either
  // outright or by producing the wrong length, which is what get() checks.
  std::memset(e->data->data(), 0xff, e->compressed_len);

  int64_t before = ts::Metrics::Counter::load(cache_rsb.ram_cache_decompress_failures);

  Ptr<IOBufferData> ret;
  int               hit = rc.get(&key, &ret);

  // A corrupted entry is a miss, is counted, and is dropped rather than
  // handed to the caller.
  CHECK(hit == 0);
  CHECK(ts::Metrics::Counter::load(cache_rsb.ram_cache_decompress_failures) == before + 1);
  CHECK(RamCacheCLFUSTestAccess::find_entry(rc, key) == nullptr);
}

// A backend that is not compiled in silently disappears from the parametrized
// cases above; make that visible in the test output rather than shipping an
// untested backend behind a green run.
TEST_CASE("CLFUS compression backends compiled in", "[cache][ramcache][compress]")
{
#ifndef HAVE_LZMA_H
  WARN("liblzma is not compiled in; the liblzma RAM cache compression backend is NOT tested");
#endif
#ifndef HAVE_LZ4_H
  WARN("lz4 is not compiled in; the lz4 RAM cache compression backend is NOT tested");
#endif
#ifndef HAVE_ZSTD_H
  WARN("zstd is not compiled in; the zstd RAM cache compression backend is NOT tested");
#endif
  CHECK(compression_cases().size() >= 3);
}

// Guards against PR #11733-style regressions of the CLFUS value metric: the value density
// must be computed in floating point. Integer division truncates (hits + 1) / (size + overhead)
// to 0 for normal object sizes, zeroing the metric and silently collapsing CLFUS to FIFO (no
// promote-on-hit, no clock second chance, no value-based ghost re-admission).
TEST_CASE("CLFUS value metric is a floating-point density", "[cache][ramcache][clfus]")
{
  constexpr double v_one   = RamCacheCLFUSTestAccess::cache_value_hits_size(1u, 16384u);   // a typical 16 KiB object, seen once
  constexpr double v_hot   = RamCacheCLFUSTestAccess::cache_value_hits_size(100u, 16384u); // same size, many more hits
  constexpr double v_small = RamCacheCLFUSTestAccess::cache_value_hits_size(10u, 1024u);   // smaller object, equal hits
  constexpr double v_large = RamCacheCLFUSTestAccess::cache_value_hits_size(10u, 16384u);

  // A non-zero fraction: the integer-division regression makes this exactly 0.0.
  static_assert(v_one > 0.0, "CLFUS value metric truncated to zero (integer division)");
  static_assert(v_hot > v_one, "CLFUS value metric does not increase with hits");
  static_assert(v_small > v_large, "CLFUS value metric does not decrease with size");
}

namespace
{

CryptoHash
make_key(uint64_t id)
{
  CryptoHash key;
  key.u32[0] = static_cast<uint32_t>(0xc0ffee00 + id);
  key.u32[2] = static_cast<uint32_t>(0xdeadbeef + id);
  key.u32[3] = static_cast<uint32_t>(0x12345678 + id);
  return key;
}

} // namespace

TEST_CASE("CLFUS decays hit count when sparing victims during eviction", "[cache][ramcache][clfus]")
{
  CacheDisk disk;
  init_disk(disk);
  StripeSM stripe{&disk, 10, 0};
  CacheVol cache_vol;
  wire_stripe(stripe, cache_vol);

  cache_config_ram_cache_compress        = CACHE_COMPRESSION_NONE;
  cache_config_ram_cache_use_seen_filter = 0;

  RamCacheCLFUS rc;
  rc.init(32 * 1024, &stripe);

  auto buf  = make_buffer(compressible_bytes(8 * 1024));
  auto key1 = make_key(1);
  auto key2 = make_key(2);
  auto key3 = make_key(3);

  REQUIRE(rc.put(&key1, buf.get(), 8 * 1024) == 1);
  REQUIRE(rc.put(&key2, buf.get(), 8 * 1024) == 1);
  REQUIRE(rc.put(&key3, buf.get(), 8 * 1024) == 1);

  RamCacheCLFUSEntry *e1 = RamCacheCLFUSTestAccess::find_entry(rc, key1);
  REQUIRE(e1 != nullptr);
  e1->hits = 16; // Give key1 high hits so it exceeds average value

  // key4 triggers an eviction pass
  auto key4 = make_key(4);
  rc.put(&key4, buf.get(), 8 * 1024);

  // e1 was spared on its first pass as a victim (16 -> 8), then taken on the
  // second pass once the requeue budget ran out and put back by
  // _requeue_victims() (8 -> 7). Without the spared-victim decay it would be 15.
  CHECK(e1->hits == 7);
}

TEST_CASE("CLFUS unconditionally promotes resident entries to the tail on hits", "[cache][ramcache][clfus]")
{
  CacheDisk disk;
  init_disk(disk);
  StripeSM stripe{&disk, 10, 0};
  CacheVol cache_vol;
  wire_stripe(stripe, cache_vol);

  cache_config_ram_cache_compress        = CACHE_COMPRESSION_NONE;
  cache_config_ram_cache_use_seen_filter = 0;

  RamCacheCLFUS rc;
  rc.init(64 * 1024, &stripe);

  auto buf  = make_buffer(compressible_bytes(4 * 1024));
  auto key1 = make_key(1);
  auto key2 = make_key(2);
  auto key3 = make_key(3);

  REQUIRE(rc.put(&key1, buf.get(), 4 * 1024) == 1);
  REQUIRE(rc.put(&key2, buf.get(), 4 * 1024) == 1);
  REQUIRE(rc.put(&key3, buf.get(), 4 * 1024) == 1);

  RamCacheCLFUSEntry *e1 = RamCacheCLFUSTestAccess::find_entry(rc, key1);
  REQUIRE(e1 != nullptr);
  // Initial order in _lru[0] has e1 at the head
  CHECK(RamCacheCLFUSTestAccess::lru_head(rc) == e1);

  // Inflate _average_value so e1's value is lower than average
  RamCacheCLFUSTestAccess::set_average_value(rc, 1000.0);

  // get() on key1 must unconditionally promote e1 to the tail of _lru[0]
  Ptr<IOBufferData> got;
  CHECK(rc.get(&key1, &got) == 1);
  CHECK(RamCacheCLFUSTestAccess::lru_tail(rc) == e1);
  CHECK(RamCacheCLFUSTestAccess::lru_head(rc) != e1);
}

TEST_CASE("CLFUS history entry is refreshed on get() hit", "[cache][ramcache][clfus]")
{
  CacheDisk disk;
  init_disk(disk);
  StripeSM stripe{&disk, 10, 0};
  CacheVol cache_vol;
  wire_stripe(stripe, cache_vol);

  cache_config_ram_cache_compress        = CACHE_COMPRESSION_NONE;
  cache_config_ram_cache_use_seen_filter = 0;

  RamCacheCLFUS rc;
  rc.init(12 * 1024, &stripe);

  auto buf  = make_buffer(compressible_bytes(8 * 1024));
  auto key1 = make_key(1);
  auto key2 = make_key(2);
  auto key3 = make_key(3);

  REQUIRE(rc.put(&key1, buf.get(), 8 * 1024) == 1);
  // key2 and key3 cannot fit in 12KB (8KB + 8KB > 12KB), routed to history (_lru[1])
  CHECK(rc.put(&key2, buf.get(), 8 * 1024) == 0);
  CHECK(rc.put(&key3, buf.get(), 8 * 1024) == 0);

  RamCacheCLFUSEntry *e2 = RamCacheCLFUSTestAccess::find_entry(rc, key2);
  RamCacheCLFUSEntry *e3 = RamCacheCLFUSTestAccess::find_entry(rc, key3);
  REQUIRE(e2 != nullptr);
  REQUIRE(e3 != nullptr);
  REQUIRE(e2->flag_bits.lru == 1); // in history
  REQUIRE(e3->flag_bits.lru == 1);
  REQUIRE(RamCacheCLFUSTestAccess::lru_tail(rc, 1) == e3);

  uint64_t          hits_before = e2->hits;
  Ptr<IOBufferData> got;
  CHECK(rc.get(&key2, &got) == 0); // miss

  // Hits count must increment and position must refresh to the tail of _lru[1]
  CHECK(e2->hits == hits_before + 1);
  CHECK(RamCacheCLFUSTestAccess::lru_tail(rc, 1) == e2);
}

TEST_CASE("CLFUS size dampening curve bounds disparity between small and large objects", "[cache][ramcache][clfus]")
{
  constexpr double v_256 = RamCacheCLFUSTestAccess::cache_value_hits_size(10u, 256u);
  constexpr double v_1k  = RamCacheCLFUSTestAccess::cache_value_hits_size(10u, 1024u);
  constexpr double v_16k = RamCacheCLFUSTestAccess::cache_value_hits_size(10u, 16384u);
  constexpr double v_64k = RamCacheCLFUSTestAccess::cache_value_hits_size(10u, 65536u);

  // Monotonicity preserved
  CHECK(v_256 > v_1k);
  CHECK(v_1k > v_16k);
  CHECK(v_16k > v_64k);

  // Ratio between 1KB and 64KB is dampened below 15x (under pure linear division it was 64x)
  double ratio = v_1k / v_64k;
  CHECK(ratio < 15.0);
  CHECK(ratio > 1.0);
}

TEST_CASE("CLFUS periodic epoch decay halves hit counts and decays average value", "[cache][ramcache][clfus]")
{
  CacheDisk disk;
  init_disk(disk);
  StripeSM stripe{&disk, 10, 0};
  CacheVol cache_vol;
  wire_stripe(stripe, cache_vol);

  cache_config_ram_cache_compress        = CACHE_COMPRESSION_NONE;
  cache_config_ram_cache_use_seen_filter = 0;

  RamCacheCLFUS rc;
  rc.init(64 * 1024, &stripe);

  auto buf  = make_buffer(compressible_bytes(4 * 1024));
  auto key1 = make_key(1);
  REQUIRE(rc.put(&key1, buf.get(), 4 * 1024) == 1);

  RamCacheCLFUSEntry *e1 = RamCacheCLFUSTestAccess::find_entry(rc, key1);
  REQUIRE(e1 != nullptr);
  e1->hits = 20;

  RamCacheCLFUSTestAccess::set_average_value(rc, 4.0);

  // Simulate passage of 35 seconds
  RamCacheCLFUSTestAccess::set_last_decay_time(rc, ink_get_hrtime() - HRTIME_SECONDS(35));

  // Trigger put to execute epoch decay
  auto key2 = make_key(2);
  rc.put(&key2, buf.get(), 4 * 1024);

  // Decay is lazy: the epoch change does not walk the entries.
  CHECK(e1->hits == 20);
  // _average_value should have decayed by half
  CHECK(RamCacheCLFUSTestAccess::average_value(rc) <= 2.5);

  // Touching e1 applies the pending decay (20 -> 10) before counting the hit.
  Ptr<IOBufferData> out;
  REQUIRE(rc.get(&key1, &out) != 0);
  CHECK(e1->hits == 11);
}

TEST_CASE("CLFUS get-only traffic applies one decay per elapsed interval", "[cache][ramcache][clfus]")
{
  CacheDisk disk;
  init_disk(disk);
  StripeSM stripe{&disk, 10, 0};
  CacheVol cache_vol;
  wire_stripe(stripe, cache_vol);

  cache_config_ram_cache_compress        = CACHE_COMPRESSION_NONE;
  cache_config_ram_cache_use_seen_filter = 0;

  RamCacheCLFUS rc;
  rc.init(64 * 1024, &stripe);

  auto buf = make_buffer(compressible_bytes(4 * 1024));
  auto key = make_key(1);
  REQUIRE(rc.put(&key, buf.get(), 4 * 1024) == 1);

  RamCacheCLFUSEntry *e = RamCacheCLFUSTestAccess::find_entry(rc, key);
  REQUIRE(e != nullptr);
  e->hits = 64;
  RamCacheCLFUSTestAccess::set_average_value(rc, 8.0);

  // Three and a half intervals pass with no put(): the get() alone must age
  // the entry by all three whole intervals, not just one.
  RamCacheCLFUSTestAccess::set_last_decay_time(rc, ink_get_hrtime() - HRTIME_SECONDS(105));
  Ptr<IOBufferData> out;
  REQUIRE(rc.get(&key, &out) != 0);
  CHECK(e->hits == 64 / 8 + 1);
  CHECK(RamCacheCLFUSTestAccess::average_value(rc) == 1.0);

  // The partial interval carries over, so the next one is due in about 15s rather than 30s.
  CHECK(ink_get_hrtime() - RamCacheCLFUSTestAccess::last_decay_time(rc) >= HRTIME_SECONDS(15));

  // A long idle period must clear the count, not wrap the shift.
  e->hits = 0xffffffff;
  RamCacheCLFUSTestAccess::set_last_decay_time(rc, ink_get_hrtime() - HRTIME_SECONDS(40 * 30));
  REQUIRE(rc.get(&key, &out) != 0);
  CHECK(e->hits == 1);
  CHECK(RamCacheCLFUSTestAccess::average_value(rc) == std::ldexp(1.0, -40));
}

TEST_CASE("CLFUS seen filter admits a filtered key on its second sighting", "[cache][ramcache][clfus]")
{
  CacheDisk disk;
  init_disk(disk);
  StripeSM stripe{&disk, 10, 0};
  CacheVol cache_vol;
  wire_stripe(stripe, cache_vol);

  cache_config_ram_cache_compress        = CACHE_COMPRESSION_NONE;
  cache_config_ram_cache_use_seen_filter = 1;

  // Room for two 8 KiB objects, not three.
  RamCacheCLFUS rc;
  rc.init(20 * 1024, &stripe);

  auto buf = make_buffer(compressible_bytes(8 * 1024));
  auto k1  = make_key(1);
  auto k2  = make_key(2);
  REQUIRE(rc.put(&k1, buf.get(), 8 * 1024) == 1);
  REQUIRE(rc.put(&k2, buf.get(), 8 * 1024) == 1);

  // New keys go to history until it is as large as the resident set, after
  // which the seen filter rejects keys it has not seen before.
  for (uint64_t id = 10; RamCacheCLFUSTestAccess::history(rc) < RamCacheCLFUSTestAccess::objects(rc); ++id) {
    REQUIRE(id < 20);
    auto k = make_key(id);
    REQUIRE(rc.put(&k, buf.get(), 8 * 1024) == 0);
  }

  // Keep victims from being spared so the outcome depends only on the
  // victim-value comparison.
  RamCacheCLFUSTestAccess::set_average_value(rc, 1e9);

  SECTION("admitted when the victims are worth less than the new object")
  {
    RamCacheCLFUSTestAccess::set_resident_hits(rc, 0);

    auto k = make_key(50);
    CHECK(rc.put(&k, buf.get(), 8 * 1024) == 0);
    CHECK(RamCacheCLFUSTestAccess::find_entry(rc, k) == nullptr);

    CHECK(rc.put(&k, buf.get(), 8 * 1024) == 1);
    RamCacheCLFUSEntry *e = RamCacheCLFUSTestAccess::find_entry(rc, k);
    REQUIRE(e != nullptr);
    CHECK(e->flag_bits.lru == 0);
  }

  SECTION("sent to history when the victims are worth more")
  {
    RamCacheCLFUSTestAccess::set_resident_hits(rc, 1000);

    auto k = make_key(60);
    CHECK(rc.put(&k, buf.get(), 8 * 1024) == 0);
    CHECK(RamCacheCLFUSTestAccess::find_entry(rc, k) == nullptr);

    CHECK(rc.put(&k, buf.get(), 8 * 1024) == 0);
    RamCacheCLFUSEntry *e = RamCacheCLFUSTestAccess::find_entry(rc, k);
    REQUIRE(e != nullptr);
    CHECK(e->flag_bits.lru == 1);
    CHECK(RamCacheCLFUSTestAccess::find_entry(rc, k1) != nullptr);
    CHECK(RamCacheCLFUSTestAccess::find_entry(rc, k1)->flag_bits.lru == 0);
    CHECK(RamCacheCLFUSTestAccess::find_entry(rc, k2)->flag_bits.lru == 0);
  }
}

TEST_CASE("CLFUS admission accounts for per-entry overhead", "[cache][ramcache][clfus]")
{
  CacheDisk disk;
  init_disk(disk);
  StripeSM stripe{&disk, 10, 0};
  CacheVol cache_vol;
  wire_stripe(stripe, cache_vol);

  cache_config_ram_cache_compress        = CACHE_COMPRESSION_NONE;
  cache_config_ram_cache_use_seen_filter = 1;

  RamCacheCLFUS rc;
  rc.init(32 * 1024, &stripe);

  auto buf = make_buffer(compressible_bytes(8 * 1024));
  auto k1  = make_key(1);
  auto k2  = make_key(2);
  auto k3  = make_key(3);
  REQUIRE(rc.put(&k1, buf.get(), 8 * 1024) == 1);
  REQUIRE(rc.put(&k2, buf.get(), 8 * 1024) == 1);
  REQUIRE(rc.put(&k3, buf.get(), 8 * 1024) == 1);

  RamCacheCLFUSTestAccess::set_average_value(rc, 1e9);
  for (uint64_t id = 10; RamCacheCLFUSTestAccess::history(rc) < RamCacheCLFUSTestAccess::objects(rc); ++id) {
    REQUIRE(id < 20);
    auto k = make_key(id);
    REQUIRE(rc.put(&k, buf.get(), 8 * 1024) == 0);
  }
  REQUIRE(RamCacheCLFUSTestAccess::objects(rc) == 3);
  RamCacheCLFUSTestAccess::set_resident_hits(rc, 1000);

  // The three residents use 3 * (8 KiB + 256 B overhead), leaving exactly
  // 7424 bytes, so the candidate fits beside them only if its own overhead is
  // ignored. The first victim is worth more than the candidate, so the
  // candidate must not be admitted alongside it.
  constexpr uint32_t len = 7424;
  auto               k   = make_key(50);
  CHECK(rc.put(&k, buf.get(), len, true) == 0);
  CHECK(rc.put(&k, buf.get(), len, true) == 0);
  CHECK(RamCacheCLFUSTestAccess::bytes(rc) <= RamCacheCLFUSTestAccess::max_bytes(rc));
  CHECK(RamCacheCLFUSTestAccess::objects(rc) == 3);
}

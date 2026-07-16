/** @file

  Microbenchmark for the TLS Certificate Compression cache.

  Drives the production compression callbacks
  (compression_func_zlib/_brotli/_zstd) and the cache attach/invalidate
  helpers exported by inknet — no logic is duplicated here. The benchmark
  measures the three states the cache toggles between, per algorithm:

    - disabled : cache=false at registration; callback always compresses
    - cold     : cache attached but empty (just invalidated); callback
                 compresses and publishes a new Entry
    - warm     : cache attached and populated; callback takes the
                 acquire-load + memcpy fast path

  Run only the benchmarks: ./test_net "[!benchmark]"

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

#include "tscore/ink_config.h"

#if HAVE_SSL_CTX_ADD_CERT_COMPRESSION_ALG

#include "../TLSCertCompression.h"
#include "../TLSCertCompression_zlib.h"
#if HAVE_BROTLI_ENCODE_H
#include "../TLSCertCompression_brotli.h"
#endif
#if HAVE_ZSTD_H
#include "../TLSCertCompression_zstd.h"
#endif
#include "../SSLStats.h"

#include <openssl/ssl.h>
#include <openssl/bytestring.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <random>
#include <thread>
#include <vector>

namespace
{
// Realistic size for a server leaf + 2 intermediates with RSA keys.
constexpr size_t CERT_BLOB_SIZE = 3 * 1024;
// Generous upper bound on compressed output; CBB will grow if needed.
constexpr size_t CBB_INITIAL_CAPACITY = 8 * 1024;

std::vector<uint8_t>
make_cert_blob()
{
  // Mix of structured (DER-like repetition) and pseudo-random bytes so
  // compression ratios are non-trivial. Fixed seed for reproducibility.
  std::vector<uint8_t> blob(CERT_BLOB_SIZE);
  std::mt19937         rng(0xC0FFEE);
  for (size_t i = 0; i < blob.size(); ++i) {
    blob[i] = (i % 4 == 0) ? static_cast<uint8_t>(i & 0xFF) : static_cast<uint8_t>(rng() & 0xFF);
  }
  return blob;
}

struct CtxBundle {
  SSL_CTX *ctx{nullptr};
  SSL     *ssl{nullptr};

  CtxBundle(std::string const &alg, bool cache_enabled)
  {
    static bool stats_initialized = false;
    if (!stats_initialized) {
      SSLInitializeStatistics();
      stats_initialized = true;
    }
    cert_compress_cache_init();
    ctx = SSL_CTX_new(TLS_method());
    REQUIRE(ctx != nullptr);
    REQUIRE(register_certificate_compression_preference(ctx, {alg}, cache_enabled) == 1);
    ssl = SSL_new(ctx);
    REQUIRE(ssl != nullptr);
  }
  ~CtxBundle()
  {
    if (ssl) {
      SSL_free(ssl);
    }
    if (ctx) {
      SSL_CTX_free(ctx);
    }
  }
  CtxBundle(CtxBundle const &)            = delete;
  CtxBundle &operator=(CtxBundle const &) = delete;
};

// Single-shot compress through the production callback, into a fresh CBB.
// Returns the compressed length.
template <typename Fn>
size_t
run_callback(Fn fn, SSL *ssl, std::vector<uint8_t> const &input)
{
  CBB cbb;
  REQUIRE(CBB_init(&cbb, CBB_INITIAL_CAPACITY) == 1);
  int rv = fn(ssl, &cbb, input.data(), input.size());
  REQUIRE(rv == 1);
  size_t out_len = CBB_len(&cbb);
  CBB_cleanup(&cbb);
  return out_len;
}

// Saturate N threads on the warm path for a fixed duration and return total ops.
// Each thread gets its own SSL handle off the shared SSL_CTX so the SSL object
// itself is not a contention point — only the cache's shared atomics are.
template <typename Fn>
uint64_t
warm_throughput(Fn fn, SSL_CTX *ctx, std::vector<uint8_t> const &input, unsigned n_threads, std::chrono::milliseconds duration)
{
  std::atomic<bool>        go{false};
  std::atomic<bool>        stop{false};
  std::vector<uint64_t>    counts(n_threads, 0);
  std::vector<std::thread> workers;
  workers.reserve(n_threads);
  for (unsigned t = 0; t < n_threads; ++t) {
    workers.emplace_back([&, t] {
      SSL *ssl = SSL_new(ctx);
      while (!go.load(std::memory_order_acquire)) {}
      uint64_t local = 0;
      while (!stop.load(std::memory_order_relaxed)) {
        (void)run_callback(fn, ssl, input);
        ++local;
      }
      counts[t] = local;
      SSL_free(ssl);
    });
  }
  go.store(true, std::memory_order_release);
  std::this_thread::sleep_for(duration);
  stop.store(true, std::memory_order_relaxed);
  for (auto &w : workers) {
    w.join();
  }
  uint64_t total = 0;
  for (auto c : counts) {
    total += c;
  }
  return total;
}
} // namespace

TEST_CASE("Cert compression cache: zlib", "[!benchmark][cert_compress][zlib]")
{
  const auto input = make_cert_blob();

  SECTION("disabled")
  {
    CtxBundle b("zlib", /*cache=*/false);
    BENCHMARK("zlib disabled")
    {
      return run_callback(compression_func_zlib, b.ssl, input);
    };
  }
  SECTION("cold (invalidate per iteration)")
  {
    CtxBundle b("zlib", /*cache=*/true);
    BENCHMARK("zlib cold")
    {
      cert_compress_cache_invalidate(b.ctx);
      return run_callback(compression_func_zlib, b.ssl, input);
    };
  }
  SECTION("warm")
  {
    CtxBundle b("zlib", /*cache=*/true);
    // Prime the cache.
    (void)run_callback(compression_func_zlib, b.ssl, input);
    BENCHMARK("zlib warm")
    {
      return run_callback(compression_func_zlib, b.ssl, input);
    };
  }
}

#if HAVE_BROTLI_ENCODE_H
TEST_CASE("Cert compression cache: brotli", "[!benchmark][cert_compress][brotli]")
{
  const auto input = make_cert_blob();

  SECTION("disabled")
  {
    CtxBundle b("brotli", /*cache=*/false);
    BENCHMARK("brotli disabled")
    {
      return run_callback(compression_func_brotli, b.ssl, input);
    };
  }
  SECTION("cold (invalidate per iteration)")
  {
    CtxBundle b("brotli", /*cache=*/true);
    BENCHMARK("brotli cold")
    {
      cert_compress_cache_invalidate(b.ctx);
      return run_callback(compression_func_brotli, b.ssl, input);
    };
  }
  SECTION("warm")
  {
    CtxBundle b("brotli", /*cache=*/true);
    (void)run_callback(compression_func_brotli, b.ssl, input);
    BENCHMARK("brotli warm")
    {
      return run_callback(compression_func_brotli, b.ssl, input);
    };
  }
}
#endif

#if HAVE_ZSTD_H
TEST_CASE("Cert compression cache: zstd", "[!benchmark][cert_compress][zstd]")
{
  const auto input = make_cert_blob();

  SECTION("disabled")
  {
    CtxBundle b("zstd", /*cache=*/false);
    BENCHMARK("zstd disabled")
    {
      return run_callback(compression_func_zstd, b.ssl, input);
    };
  }
  SECTION("cold (invalidate per iteration)")
  {
    CtxBundle b("zstd", /*cache=*/true);
    BENCHMARK("zstd cold")
    {
      cert_compress_cache_invalidate(b.ctx);
      return run_callback(compression_func_zstd, b.ssl, input);
    };
  }
  SECTION("warm")
  {
    CtxBundle b("zstd", /*cache=*/true);
    (void)run_callback(compression_func_zstd, b.ssl, input);
    BENCHMARK("zstd warm")
    {
      return run_callback(compression_func_zstd, b.ssl, input);
    };
  }
}
#endif

// Scaling test: N threads hammer the warm fast path against the same
// SSL_CTX. Prints aggregate ops/sec and per-thread ops/sec so reader
// contention on the cache's shared atomics is directly visible.
TEST_CASE("Cert compression cache: warm scaling", "[!benchmark][cert_compress][scaling]")
{
  using namespace std::chrono_literals;
  const auto            input    = make_cert_blob();
  const auto            duration = 1000ms;
  const unsigned        hw       = std::max(1u, std::thread::hardware_concurrency());
  std::vector<unsigned> thread_counts;
  for (unsigned n : {1u, 2u, 4u, 8u, 16u, 32u}) {
    if (n <= hw * 2) {
      thread_counts.push_back(n);
    }
  }

  auto run = [&](char const *label, auto fn, char const *alg) {
    CtxBundle b(alg, /*cache=*/true);
    (void)run_callback(fn, b.ssl, input); // prime
    std::printf("\n[%s scaling] (hw_concurrency=%u, duration=%lldms)\n", label, hw, static_cast<long long>(duration.count()));
    std::printf("  threads | ops          | ops/sec        | ops/sec/thread | scaling\n");
    std::printf("  --------+--------------+----------------+----------------+--------\n");
    double baseline = 0.0;
    for (unsigned n : thread_counts) {
      uint64_t ops    = warm_throughput(fn, b.ctx, input, n, duration);
      double   rate   = static_cast<double>(ops) * 1000.0 / duration.count();
      double   per_th = rate / n;
      if (n == 1) {
        baseline = per_th;
      }
      double scaling = baseline > 0 ? rate / (baseline * n) : 0.0;
      std::printf("  %7u | %12llu | %14.0f | %14.0f | %6.2fx (vs ideal Nx)\n", n, static_cast<unsigned long long>(ops), rate,
                  per_th, scaling);
    }
  };

  run("zlib", compression_func_zlib, "zlib");
#if HAVE_BROTLI_ENCODE_H
  run("brotli", compression_func_brotli, "brotli");
#endif
#if HAVE_ZSTD_H
  run("zstd", compression_func_zstd, "zstd");
#endif
}

#endif // HAVE_SSL_CTX_ADD_CERT_COMPRESSION_ALG

/** @file

  Benchmark for UdiTable - measures throughput and contention performance.

  Run with:
    ./benchmark_UdiTable [--help]

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

#include "tsutil/UdiTable.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace
{

// Simple data structure for benchmarking
struct BenchData {
  std::atomic<uint64_t> count{0};
};

using BenchTable = ts::UdiTable<std::string, BenchData>;

/**
 * Zipf distribution generator.
 *
 * Generates values following a power-law distribution where:
 *   P(k) ∝ 1/k^s
 *
 * With s=1.0 (default), this is classic Zipf's law where the most frequent
 * item appears ~twice as often as the 2nd, ~3x as often as the 3rd, etc.
 *
 * This models real-world scenarios like:
 *   - IP address frequency in abuse detection (few attackers, many normal users)
 *   - Word frequency in natural language
 *   - Website popularity
 */
class ZipfDistribution
{
public:
  ZipfDistribution(size_t n, double exponent = 1.0)
  {
    // Pre-compute cumulative distribution function (CDF)
    cdf_.reserve(n);
    double sum = 0.0;
    for (size_t i = 1; i <= n; ++i) {
      sum += 1.0 / std::pow(static_cast<double>(i), exponent);
      cdf_.push_back(sum);
    }
    // Normalize to [0, 1]
    for (auto &v : cdf_) {
      v /= sum;
    }
  }

  // Generate a Zipf-distributed value in [0, n-1]
  template <typename Generator>
  size_t
  operator()(Generator &gen)
  {
    std::uniform_real_distribution<double> uniform(0.0, 1.0);
    double                                 u = uniform(gen);
    // Binary search for the value
    auto it = std::lower_bound(cdf_.begin(), cdf_.end(), u);
    return static_cast<size_t>(std::distance(cdf_.begin(), it));
  }

  // Return probability of rank k (0-indexed)
  double
  probability(size_t k) const
  {
    if (k == 0) {
      return cdf_[0];
    }
    return cdf_[k] - cdf_[k - 1];
  }

private:
  std::vector<double> cdf_;
};

// Pre-generate keys to avoid string allocation in hot path
std::vector<std::string>
generate_keys(int n)
{
  std::vector<std::string> keys;
  keys.reserve(n);
  for (int i = 0; i < n; ++i) {
    keys.push_back("key_" + std::to_string(i));
  }
  return keys;
}

// Run benchmark with specified number of threads
void
run_benchmark(int nthreads, int table_size, int ops_per_thread, const std::vector<std::string> &keys, bool use_zipf,
              double zipf_exponent)
{
  BenchTable               table(table_size);
  std::vector<std::thread> threads;
  std::atomic<uint64_t>    total_ops{0};
  std::atomic<uint64_t>    successful_ops{0};

  auto start = std::chrono::high_resolution_clock::now();

  for (int t = 0; t < nthreads; ++t) {
    threads.emplace_back([&, t]() {
      std::mt19937 gen(t + 1);

      uint64_t local_success = 0;

      if (use_zipf) {
        ZipfDistribution zipf(keys.size(), zipf_exponent);
        for (int i = 0; i < ops_per_thread; ++i) {
          auto data = table.process_event(keys[zipf(gen)]);
          if (data) {
            data->count.fetch_add(1, std::memory_order_relaxed);
            ++local_success;
          }
        }
      } else {
        std::uniform_int_distribution<> dist(0, static_cast<int>(keys.size()) - 1);
        for (int i = 0; i < ops_per_thread; ++i) {
          auto data = table.process_event(keys[dist(gen)]);
          if (data) {
            data->count.fetch_add(1, std::memory_order_relaxed);
            ++local_success;
          }
        }
      }

      total_ops.fetch_add(ops_per_thread, std::memory_order_relaxed);
      successful_ops.fetch_add(local_success, std::memory_order_relaxed);
    });
  }

  for (auto &t : threads) {
    t.join();
  }

  auto end          = std::chrono::high_resolution_clock::now();
  auto duration_ms  = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
  auto duration_sec = duration_ms / 1000.0;

  double ops_total   = static_cast<double>(total_ops.load());
  double ops_per_sec = ops_total / duration_sec;

  std::cout << std::setw(3) << nthreads << std::setw(12) << duration_ms << std::setw(15) << std::fixed << std::setprecision(0)
            << ops_per_sec << std::setw(12) << table.slots_used() << std::setw(12) << table.contests() << std::setw(12)
            << table.contests_won() << std::setw(12) << table.evictions() << std::endl;
}

} // namespace

int
main(int argc, char *argv[])
{
  int    table_size     = 10000;
  int    ops_per_thread = 100000;
  int    unique_keys    = 50000;
  int    max_threads    = 16;
  bool   use_zipf       = false;
  double zipf_exponent  = 1.0;

  // Parse args
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--table-size" && i + 1 < argc) {
      table_size = std::stoi(argv[++i]);
    } else if (arg == "--ops" && i + 1 < argc) {
      ops_per_thread = std::stoi(argv[++i]);
    } else if (arg == "--keys" && i + 1 < argc) {
      unique_keys = std::stoi(argv[++i]);
    } else if (arg == "--max-threads" && i + 1 < argc) {
      max_threads = std::stoi(argv[++i]);
    } else if (arg == "--zipf") {
      use_zipf = true;
      // Optional exponent follows
      if (i + 1 < argc && argv[i + 1][0] != '-') {
        zipf_exponent = std::stod(argv[++i]);
      }
    } else if (arg == "--help" || arg == "-h") {
      std::cout << "Usage: " << argv[0] << " [options]\n"
                << "  --table-size N    Table size (default: 10000)\n"
                << "  --ops N           Operations per thread (default: 100000)\n"
                << "  --keys N          Unique keys (default: 50000)\n"
                << "  --max-threads N   Max threads to test (default: 16)\n"
                << "  --zipf [S]        Use Zipf (power-law) distribution with exponent S (default: 1.0)\n"
                << "                    S=1.0: classic Zipf (top key ~2x frequency of 2nd)\n"
                << "                    S=0.5: flatter distribution\n"
                << "                    S=2.0: steeper distribution (more skewed)\n";
      return 0;
    }
  }

  auto keys = generate_keys(unique_keys);

  std::cout << "UdiTable Benchmark\n"
            << "  Table size:  " << table_size << "\n"
            << "  Ops/thread:  " << ops_per_thread << "\n"
            << "  Unique keys: " << unique_keys << "\n"
            << "  Distribution: " << (use_zipf ? "Zipf (s=" + std::to_string(zipf_exponent) + ")" : "Uniform") << "\n\n";

  // Show distribution preview for Zipf
  if (use_zipf) {
    ZipfDistribution zipf(unique_keys, zipf_exponent);
    std::cout << "  Top key frequencies:\n";
    std::cout << "    Rank 1:    " << std::fixed << std::setprecision(4) << (zipf.probability(0) * 100) << "%\n";
    std::cout << "    Rank 10:   " << std::fixed << std::setprecision(4) << (zipf.probability(9) * 100) << "%\n";
    std::cout << "    Rank 100:  " << std::fixed << std::setprecision(4) << (zipf.probability(99) * 100) << "%\n";
    std::cout << "    Rank 1000: " << std::fixed << std::setprecision(4) << (zipf.probability(999) * 100) << "%\n\n";
  }

  std::cout << std::setw(3) << "T" << std::setw(12) << "Time(ms)" << std::setw(15) << "Ops/sec" << std::setw(12) << "SlotsUsed"
            << std::setw(12) << "Contests" << std::setw(12) << "Won" << std::setw(12) << "Evictions" << std::endl;
  std::cout << std::string(76, '-') << std::endl;

  for (int nthreads = 1; nthreads <= max_threads; nthreads *= 2) {
    run_benchmark(nthreads, table_size, ops_per_thread, keys, use_zipf, zipf_exponent);
  }

  return 0;
}

/** @file

  Benchmark comparing different locking strategies for UdiTable.

  Implements and benchmarks 4 locking approaches:
    A. Per-partition locks (N partitions, each with own mutex)
    B. Global lookup + partitioned slots (hybrid approach)
    C. shared_mutex (reader-writer lock)
    D. Single std::mutex (current implementation)

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one or more contributor
  license agreements.
*/

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <random>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace
{

// ============================================================================
// Common types and utilities
// ============================================================================

struct BenchData {
  std::atomic<uint64_t> count{0};
};

// Zipf distribution for realistic workload
class ZipfDistribution
{
public:
  ZipfDistribution(size_t n, double exponent = 1.0)
  {
    cdf_.reserve(n);
    double sum = 0.0;
    for (size_t i = 1; i <= n; ++i) {
      sum += 1.0 / std::pow(static_cast<double>(i), exponent);
      cdf_.push_back(sum);
    }
    for (auto &v : cdf_) {
      v /= sum;
    }
  }

  template <typename Generator>
  size_t
  operator()(Generator &gen)
  {
    std::uniform_real_distribution<double> uniform(0.0, 1.0);
    double                                 u  = uniform(gen);
    auto                                   it = std::lower_bound(cdf_.begin(), cdf_.end(), u);
    return static_cast<size_t>(std::distance(cdf_.begin(), it));
  }

private:
  std::vector<double> cdf_;
};

// ============================================================================
// Option D: Single std::mutex (baseline - current implementation)
// ============================================================================

template <typename Key, typename Data, typename Hash = std::hash<Key>> class UdiTable_SingleMutex
{
public:
  using data_ptr = std::shared_ptr<Data>;

  explicit UdiTable_SingleMutex(size_t num_slots) : slots_(num_slots) { lookup_.reserve(num_slots); }

  data_ptr
  process_event(Key const &key, uint32_t score_delta = 1)
  {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = lookup_.find(key);
    if (it != lookup_.end()) {
      Slot &slot  = slots_[it->second];
      slot.score += score_delta;
      return slot.data;
    }
    return contest(key, score_delta);
  }

  size_t
  slots_used() const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return lookup_.size();
  }

  uint64_t
  contests() const
  {
    return metric_contests_.load(std::memory_order_relaxed);
  }
  uint64_t
  evictions() const
  {
    return metric_evictions_.load(std::memory_order_relaxed);
  }

private:
  struct Slot {
    Key                   key{};
    uint32_t              score{0};
    std::shared_ptr<Data> data;

    bool
    is_empty() const
    {
      return !data;
    }
  };

  data_ptr
  contest(Key const &key, uint32_t incoming_score)
  {
    metric_contests_.fetch_add(1, std::memory_order_relaxed);
    if (slots_.empty())
      return nullptr;

    size_t slot_idx        = contest_ptr_;
    contest_ptr_           = (contest_ptr_ + 1) % slots_.size();
    Slot    &slot          = slots_[slot_idx];
    uint32_t current_score = slot.score;

    if (slot.is_empty() || incoming_score > current_score) {
      if (!slot.is_empty()) {
        lookup_.erase(slot.key);
        metric_evictions_.fetch_add(1, std::memory_order_relaxed);
      }
      slot.key     = key;
      slot.score   = incoming_score;
      slot.data    = std::make_shared<Data>();
      lookup_[key] = slot_idx;
      return slot.data;
    } else {
      if (current_score > 0)
        --slot.score;
      return nullptr;
    }
  }

  mutable std::mutex                    mutex_;
  std::unordered_map<Key, size_t, Hash> lookup_;
  std::vector<Slot>                     slots_;
  size_t                                contest_ptr_{0};
  std::atomic<uint64_t>                 metric_contests_{0};
  std::atomic<uint64_t>                 metric_evictions_{0};
};

// ============================================================================
// Option C: shared_mutex (reader-writer lock)
// ============================================================================

template <typename Key, typename Data, typename Hash = std::hash<Key>> class UdiTable_SharedMutex
{
public:
  using data_ptr = std::shared_ptr<Data>;

  explicit UdiTable_SharedMutex(size_t num_slots) : slots_(num_slots) { lookup_.reserve(num_slots); }

  data_ptr
  process_event(Key const &key, uint32_t score_delta = 1)
  {
    // Try read lock first for existing keys
    {
      std::shared_lock<std::shared_mutex> read_lock(mutex_);
      auto                                it = lookup_.find(key);
      if (it != lookup_.end()) {
        Slot &slot = slots_[it->second];
        // Need atomic increment since we only have read lock
        slot.score.fetch_add(score_delta, std::memory_order_relaxed);
        return slot.data;
      }
    }

    // Upgrade to write lock for new keys
    std::unique_lock<std::shared_mutex> write_lock(mutex_);
    // Double-check after acquiring write lock
    auto it = lookup_.find(key);
    if (it != lookup_.end()) {
      Slot &slot = slots_[it->second];
      slot.score.fetch_add(score_delta, std::memory_order_relaxed);
      return slot.data;
    }
    return contest(key, score_delta);
  }

  size_t
  slots_used() const
  {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return lookup_.size();
  }

  uint64_t
  contests() const
  {
    return metric_contests_.load(std::memory_order_relaxed);
  }
  uint64_t
  evictions() const
  {
    return metric_evictions_.load(std::memory_order_relaxed);
  }

private:
  struct Slot {
    Key                   key{};
    std::atomic<uint32_t> score{0};
    std::shared_ptr<Data> data;
    mutable std::mutex    slot_mutex; // For data initialization

    bool
    is_empty() const
    {
      return !data;
    }
  };

  data_ptr
  contest(Key const &key, uint32_t incoming_score)
  {
    metric_contests_.fetch_add(1, std::memory_order_relaxed);
    if (slots_.empty())
      return nullptr;

    size_t slot_idx        = contest_ptr_;
    contest_ptr_           = (contest_ptr_ + 1) % slots_.size();
    Slot    &slot          = slots_[slot_idx];
    uint32_t current_score = slot.score.load(std::memory_order_relaxed);

    if (slot.is_empty() || incoming_score > current_score) {
      if (!slot.is_empty()) {
        lookup_.erase(slot.key);
        metric_evictions_.fetch_add(1, std::memory_order_relaxed);
      }
      slot.key = key;
      slot.score.store(incoming_score, std::memory_order_relaxed);
      slot.data    = std::make_shared<Data>();
      lookup_[key] = slot_idx;
      return slot.data;
    } else {
      uint32_t expected = current_score;
      while (expected > 0 && !slot.score.compare_exchange_weak(expected, expected - 1, std::memory_order_relaxed)) {}
      return nullptr;
    }
  }

  mutable std::shared_mutex             mutex_;
  std::unordered_map<Key, size_t, Hash> lookup_;
  std::vector<Slot>                     slots_;
  size_t                                contest_ptr_{0};
  std::atomic<uint64_t>                 metric_contests_{0};
  std::atomic<uint64_t>                 metric_evictions_{0};
};

// ============================================================================
// Option A: Per-partition locks
// ============================================================================

template <typename Key, typename Data, typename Hash = std::hash<Key>> class UdiTable_Partitioned
{
  static constexpr size_t NUM_PARTITIONS = 16;

public:
  using data_ptr = std::shared_ptr<Data>;

  explicit UdiTable_Partitioned(size_t num_slots) : total_slots_(num_slots)
  {
    size_t slots_per_partition = (num_slots + NUM_PARTITIONS - 1) / NUM_PARTITIONS;
    for (size_t i = 0; i < NUM_PARTITIONS; ++i) {
      partitions_[i].slots.resize(slots_per_partition);
      partitions_[i].lookup.reserve(slots_per_partition);
    }
  }

  data_ptr
  process_event(Key const &key, uint32_t score_delta = 1)
  {
    size_t     partition_idx = hasher_(key) % NUM_PARTITIONS;
    Partition &partition     = partitions_[partition_idx];

    std::lock_guard<std::mutex> lock(partition.mutex);

    auto it = partition.lookup.find(key);
    if (it != partition.lookup.end()) {
      Slot &slot  = partition.slots[it->second];
      slot.score += score_delta;
      return slot.data;
    }
    return contest(partition, key, score_delta);
  }

  size_t
  slots_used() const
  {
    size_t total = 0;
    for (auto const &p : partitions_) {
      std::lock_guard<std::mutex> lock(p.mutex);
      total += p.lookup.size();
    }
    return total;
  }

  uint64_t
  contests() const
  {
    return metric_contests_.load(std::memory_order_relaxed);
  }
  uint64_t
  evictions() const
  {
    return metric_evictions_.load(std::memory_order_relaxed);
  }

private:
  struct Slot {
    Key                   key{};
    uint32_t              score{0};
    std::shared_ptr<Data> data;

    bool
    is_empty() const
    {
      return !data;
    }
  };

  struct Partition {
    mutable std::mutex                    mutex;
    std::unordered_map<Key, size_t, Hash> lookup;
    std::vector<Slot>                     slots;
    size_t                                contest_ptr{0};
  };

  data_ptr
  contest(Partition &partition, Key const &key, uint32_t incoming_score)
  {
    metric_contests_.fetch_add(1, std::memory_order_relaxed);
    if (partition.slots.empty())
      return nullptr;

    size_t slot_idx        = partition.contest_ptr;
    partition.contest_ptr  = (partition.contest_ptr + 1) % partition.slots.size();
    Slot    &slot          = partition.slots[slot_idx];
    uint32_t current_score = slot.score;

    if (slot.is_empty() || incoming_score > current_score) {
      if (!slot.is_empty()) {
        partition.lookup.erase(slot.key);
        metric_evictions_.fetch_add(1, std::memory_order_relaxed);
      }
      slot.key              = key;
      slot.score            = incoming_score;
      slot.data             = std::make_shared<Data>();
      partition.lookup[key] = slot_idx;
      return slot.data;
    } else {
      if (current_score > 0)
        --slot.score;
      return nullptr;
    }
  }

  Hash                                  hasher_;
  size_t                                total_slots_;
  std::array<Partition, NUM_PARTITIONS> partitions_;
  std::atomic<uint64_t>                 metric_contests_{0};
  std::atomic<uint64_t>                 metric_evictions_{0};
};

// ============================================================================
// Option B: Global lookup + partitioned slots
// ============================================================================

template <typename Key, typename Data, typename Hash = std::hash<Key>> class UdiTable_HybridLock
{
  static constexpr size_t NUM_SLOT_PARTITIONS = 16;

public:
  using data_ptr = std::shared_ptr<Data>;

  explicit UdiTable_HybridLock(size_t num_slots) : slots_(num_slots)
  {
    lookup_.reserve(num_slots);
    slots_per_partition_ = (num_slots + NUM_SLOT_PARTITIONS - 1) / NUM_SLOT_PARTITIONS;
  }

  data_ptr
  process_event(Key const &key, uint32_t score_delta = 1)
  {
    // First check lookup with global lock
    {
      std::lock_guard<std::mutex> lookup_lock(lookup_mutex_);
      auto                        it = lookup_.find(key);
      if (it != lookup_.end()) {
        size_t slot_idx      = it->second;
        size_t partition_idx = slot_idx / slots_per_partition_;
        if (partition_idx >= NUM_SLOT_PARTITIONS)
          partition_idx = NUM_SLOT_PARTITIONS - 1;

        std::lock_guard<std::mutex> slot_lock(slot_mutexes_[partition_idx]);
        Slot                       &slot  = slots_[slot_idx];
        slot.score                       += score_delta;
        return slot.data;
      }
    }

    // Need to contest - requires both locks
    std::lock_guard<std::mutex> lookup_lock(lookup_mutex_);

    // Double-check
    auto it = lookup_.find(key);
    if (it != lookup_.end()) {
      size_t slot_idx      = it->second;
      size_t partition_idx = slot_idx / slots_per_partition_;
      if (partition_idx >= NUM_SLOT_PARTITIONS)
        partition_idx = NUM_SLOT_PARTITIONS - 1;

      std::lock_guard<std::mutex> slot_lock(slot_mutexes_[partition_idx]);
      Slot                       &slot  = slots_[slot_idx];
      slot.score                       += score_delta;
      return slot.data;
    }

    return contest(key, score_delta);
  }

  size_t
  slots_used() const
  {
    std::lock_guard<std::mutex> lock(lookup_mutex_);
    return lookup_.size();
  }

  uint64_t
  contests() const
  {
    return metric_contests_.load(std::memory_order_relaxed);
  }
  uint64_t
  evictions() const
  {
    return metric_evictions_.load(std::memory_order_relaxed);
  }

private:
  struct Slot {
    Key                   key{};
    uint32_t              score{0};
    std::shared_ptr<Data> data;

    bool
    is_empty() const
    {
      return !data;
    }
  };

  data_ptr
  contest(Key const &key, uint32_t incoming_score)
  {
    // Called with lookup_mutex_ held
    metric_contests_.fetch_add(1, std::memory_order_relaxed);
    if (slots_.empty())
      return nullptr;

    size_t slot_idx      = contest_ptr_;
    contest_ptr_         = (contest_ptr_ + 1) % slots_.size();
    size_t partition_idx = slot_idx / slots_per_partition_;
    if (partition_idx >= NUM_SLOT_PARTITIONS)
      partition_idx = NUM_SLOT_PARTITIONS - 1;

    std::lock_guard<std::mutex> slot_lock(slot_mutexes_[partition_idx]);
    Slot                       &slot          = slots_[slot_idx];
    uint32_t                    current_score = slot.score;

    if (slot.is_empty() || incoming_score > current_score) {
      if (!slot.is_empty()) {
        lookup_.erase(slot.key);
        metric_evictions_.fetch_add(1, std::memory_order_relaxed);
      }
      slot.key     = key;
      slot.score   = incoming_score;
      slot.data    = std::make_shared<Data>();
      lookup_[key] = slot_idx;
      return slot.data;
    } else {
      if (current_score > 0)
        --slot.score;
      return nullptr;
    }
  }

  mutable std::mutex                          lookup_mutex_;
  std::unordered_map<Key, size_t, Hash>       lookup_;
  std::vector<Slot>                           slots_;
  std::array<std::mutex, NUM_SLOT_PARTITIONS> slot_mutexes_;
  size_t                                      slots_per_partition_;
  size_t                                      contest_ptr_{0};
  std::atomic<uint64_t>                       metric_contests_{0};
  std::atomic<uint64_t>                       metric_evictions_{0};
};

// ============================================================================
// Benchmark runner
// ============================================================================

struct BenchResult {
  std::string name;
  int         threads;
  long        duration_ms;
  double      ops_per_sec;
  size_t      slots_used;
  uint64_t    contests;
  uint64_t    evictions;
};

template <typename Table>
BenchResult
run_benchmark(const std::string &name, int nthreads, int table_size, int ops_per_thread, const std::vector<std::string> &keys,
              bool use_zipf)
{
  Table                    table(table_size);
  std::vector<std::thread> threads;
  std::atomic<uint64_t>    total_ops{0};

  auto start = std::chrono::high_resolution_clock::now();

  for (int t = 0; t < nthreads; ++t) {
    threads.emplace_back([&, t]() {
      std::mt19937 gen(t + 1);

      if (use_zipf) {
        ZipfDistribution zipf(keys.size(), 1.0);
        for (int i = 0; i < ops_per_thread; ++i) {
          auto data = table.process_event(keys[zipf(gen)]);
          if (data) {
            data->count.fetch_add(1, std::memory_order_relaxed);
          }
        }
      } else {
        std::uniform_int_distribution<> dist(0, static_cast<int>(keys.size()) - 1);
        for (int i = 0; i < ops_per_thread; ++i) {
          auto data = table.process_event(keys[dist(gen)]);
          if (data) {
            data->count.fetch_add(1, std::memory_order_relaxed);
          }
        }
      }
      total_ops.fetch_add(ops_per_thread, std::memory_order_relaxed);
    });
  }

  for (auto &t : threads) {
    t.join();
  }

  auto end          = std::chrono::high_resolution_clock::now();
  auto duration_ms  = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
  auto duration_sec = duration_ms / 1000.0;

  BenchResult result;
  result.name        = name;
  result.threads     = nthreads;
  result.duration_ms = duration_ms;
  result.ops_per_sec = static_cast<double>(total_ops.load()) / duration_sec;
  result.slots_used  = table.slots_used();
  result.contests    = table.contests();
  result.evictions   = table.evictions();

  return result;
}

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

void
print_result(const BenchResult &r)
{
  std::cout << std::setw(25) << std::left << r.name << std::setw(8) << std::right << r.threads << std::setw(12) << r.duration_ms
            << std::setw(15) << std::fixed << std::setprecision(0) << r.ops_per_sec << std::setw(12) << r.slots_used
            << std::setw(12) << r.contests << std::setw(12) << r.evictions << std::endl;
}

} // namespace

int
main(int argc, char *argv[])
{
  int  table_size     = 10000;
  int  ops_per_thread = 100000;
  int  unique_keys    = 50000;
  int  num_threads    = 16;
  bool use_zipf       = true;

  // Parse args
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--table-size" && i + 1 < argc) {
      table_size = std::stoi(argv[++i]);
    } else if (arg == "--ops" && i + 1 < argc) {
      ops_per_thread = std::stoi(argv[++i]);
    } else if (arg == "--keys" && i + 1 < argc) {
      unique_keys = std::stoi(argv[++i]);
    } else if (arg == "--threads" && i + 1 < argc) {
      num_threads = std::stoi(argv[++i]);
    } else if (arg == "--uniform") {
      use_zipf = false;
    } else if (arg == "--help" || arg == "-h") {
      std::cout << "Usage: " << argv[0] << " [options]\n"
                << "  --table-size N    Table size (default: 10000)\n"
                << "  --ops N           Operations per thread (default: 100000)\n"
                << "  --keys N          Unique keys (default: 50000)\n"
                << "  --threads N       Number of threads (default: 16)\n"
                << "  --uniform         Use uniform distribution (default: Zipf)\n";
      return 0;
    }
  }

  auto keys = generate_keys(unique_keys);

  std::cout << "UdiTable Locking Strategy Benchmark\n"
            << "====================================\n\n"
            << "Configuration:\n"
            << "  Table size:   " << table_size << "\n"
            << "  Ops/thread:   " << ops_per_thread << "\n"
            << "  Unique keys:  " << unique_keys << "\n"
            << "  Threads:      " << num_threads << "\n"
            << "  Distribution: " << (use_zipf ? "Zipf (s=1.0)" : "Uniform") << "\n\n";

  std::cout << std::setw(25) << std::left << "Strategy" << std::setw(8) << std::right << "Threads" << std::setw(12) << "Time(ms)"
            << std::setw(15) << "Ops/sec" << std::setw(12) << "SlotsUsed" << std::setw(12) << "Contests" << std::setw(12)
            << "Evictions" << std::endl;
  std::cout << std::string(96, '-') << std::endl;

  // Run benchmarks for each strategy
  std::vector<BenchResult> results;

  // Option D: Single mutex (baseline)
  results.push_back(run_benchmark<UdiTable_SingleMutex<std::string, BenchData>>("D: Single mutex", num_threads, table_size,
                                                                                ops_per_thread, keys, use_zipf));
  print_result(results.back());

  // Option C: shared_mutex
  results.push_back(run_benchmark<UdiTable_SharedMutex<std::string, BenchData>>("C: shared_mutex", num_threads, table_size,
                                                                                ops_per_thread, keys, use_zipf));
  print_result(results.back());

  // Option A: Partitioned (16 partitions)
  results.push_back(run_benchmark<UdiTable_Partitioned<std::string, BenchData>>("A: Partitioned (16)", num_threads, table_size,
                                                                                ops_per_thread, keys, use_zipf));
  print_result(results.back());

  // Option B: Hybrid lock
  results.push_back(run_benchmark<UdiTable_HybridLock<std::string, BenchData>>("B: Hybrid lock", num_threads, table_size,
                                                                               ops_per_thread, keys, use_zipf));
  print_result(results.back());

  // Summary
  std::cout << "\n\nSummary (sorted by throughput):\n";
  std::cout << std::string(50, '-') << std::endl;

  std::sort(results.begin(), results.end(),
            [](const BenchResult &a, const BenchResult &b) { return a.ops_per_sec > b.ops_per_sec; });

  double baseline = results.back().ops_per_sec; // Slowest as baseline
  for (const auto &r : results) {
    double speedup = r.ops_per_sec / baseline;
    std::cout << std::setw(25) << std::left << r.name << std::setw(15) << std::right << std::fixed << std::setprecision(0)
              << r.ops_per_sec << " ops/sec" << std::setw(10) << std::setprecision(2) << speedup << "x" << std::endl;
  }

  return 0;
}

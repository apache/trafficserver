/** @file

  Micro-benchmarks for abuse_shield IP tracking: the per-request hook path,
  the 1 Hz maintenance pass, and eviction from a full table.

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one or more contributor license
  agreements.  See the NOTICE file distributed with this work for additional information regarding
  copyright ownership.  The ASF licenses this file to you under the Apache License, Version 2.0
  (the "License"); you may not use this file except in compliance with the License.  You may obtain
  a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software distributed under the License
  is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
  or implied. See the License for the specific language governing permissions and limitations under
  the License.
*/

// abuse_shield.cc calls the TS API throughout, so it cannot be linked here.
// The functions below that carry a "Mirrors" comment copy the loop of the
// named plugin function with the TS API calls removed. Keep them in step with
// abuse_shield.cc. Everything they call (UdiTable, RuleBuckets, TokenBucket) is
// the real code from ip_data.cc and tsutil/UdiTable.h.

#include "config.h"
#include "ip_data.h"

#define CATCH_CONFIG_ENABLE_BENCHMARKING
#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>

using namespace abuse_shield;

namespace
{
// Every tracker entry carries this many rule buckets, one per configured rule
// that sets a rate for that tracker's metric.
constexpr int RULES_PER_METRIC = 3;

// One address in this many is IPv6, the rest IPv4.
constexpr size_t IP6_EVERY = 10;

// Table sizes. Production runs with 100k slots.
#define BENCH_TABLE_SIZES GENERATE(as<size_t>{}, 10'000, 50'000, 100'000)

// Stands in for TSStatIntSet in sync_tracker_stats.
std::atomic<int64_t> g_stat_sink{0};

/** The parts of abuse_shield::Config that the hook and maintenance paths read.
 *
 * The real Config is built by parsing YAML, which pulls in yaml-cpp and
 * tsutil. The address spaces are empty, as in a configuration without trusted
 * or rate-limited IP lists, but they are still searched so the per-request
 * lookups are paid for.
 */
struct BenchConfig {
  BenchConfig() = default;
  explicit BenchConfig(std::vector<Rule> rules_in) : rules(std::move(rules_in)) {}

  std::vector<Rule>   rules;
  swoc::IPSpace<bool> trusted_ips;
  swoc::IPSpace<bool> rate_limited_req_ips;
  swoc::IPSpace<bool> rate_limited_conn_ips;
  swoc::IPSpace<bool> rate_limited_h2_ips;

  // Mirrors Config::is_trusted.
  bool
  is_trusted(const swoc::IPAddr &ip) const
  {
    return trusted_ips.find(ip) != trusted_ips.end();
  }

  // Mirrors Config::is_rate_limited_for_metric.
  bool
  is_rate_limited_for_metric(const swoc::IPAddr &ip, RateMetric metric) const
  {
    switch (metric) {
    case RateMetric::REQUEST:
      return rate_limited_req_ips.find(ip) != rate_limited_req_ips.end();
    case RateMetric::CONNECTION:
      return rate_limited_conn_ips.find(ip) != rate_limited_conn_ips.end();
    case RateMetric::H2_ERROR:
      return rate_limited_h2_ips.find(ip) != rate_limited_h2_ips.end();
    }
    return false;
  }

  // Mirrors Config::rule_applies_to_ip.
  bool
  rule_applies_to_ip(const Rule &rule, const swoc::IPAddr &ip) const
  {
    if (rule.filter.has_rate_limited_ips()) {
      return rule.filter.rate_limited_ips->find(ip) != rule.filter.rate_limited_ips->end();
    }
    if (rule.filter.max_req_rate > 0 && is_rate_limited_for_metric(ip, RateMetric::REQUEST)) {
      return false;
    }
    if (rule.filter.max_conn_rate > 0 && is_rate_limited_for_metric(ip, RateMetric::CONNECTION)) {
      return false;
    }
    if (rule.filter.max_h2_error_rate > 0 && is_rate_limited_for_metric(ip, RateMetric::H2_ERROR)) {
      return false;
    }
    return true;
  }
};

/** Build a rule set like the shipped abuse_shield.yaml: each rule limits one
 * metric. With @a rules_per_metric rules per metric, every tracker entry holds
 * that many buckets once it has seen traffic.
 */
std::vector<Rule>
make_rules(int rules_per_metric)
{
  std::vector<Rule> rules;
  for (int i = 0; i < rules_per_metric; ++i) {
    Rule req;
    req.name                        = "excessive_requests_" + std::to_string(i);
    req.filter.max_req_rate         = 100 * (i + 1);
    req.filter.req_burst_multiplier = 2.0;
    rules.push_back(req);

    Rule conn;
    conn.name                         = "excessive_connections_" + std::to_string(i);
    conn.filter.max_conn_rate         = 20 * (i + 1);
    conn.filter.conn_burst_multiplier = 1.5;
    rules.push_back(conn);

    Rule h2;
    h2.name                       = "excessive_h2_errors_" + std::to_string(i);
    h2.filter.max_h2_error_rate   = 10 * (i + 1);
    h2.filter.h2_burst_multiplier = 1.0;
    rules.push_back(h2);
  }
  return rules;
}

// Mirrors metric_rate in abuse_shield.cc.
int
metric_rate(const RuleFilter &filter, RateMetric metric)
{
  switch (metric) {
  case RateMetric::REQUEST:
    return filter.max_req_rate;
  case RateMetric::CONNECTION:
    return filter.max_conn_rate;
  case RateMetric::H2_ERROR:
    return filter.max_h2_error_rate;
  }
  return 0;
}

// Mirrors metric_burst_multiplier in abuse_shield.cc.
double
metric_burst_multiplier(const RuleFilter &filter, RateMetric metric)
{
  switch (metric) {
  case RateMetric::REQUEST:
    return filter.req_burst_multiplier;
  case RateMetric::CONNECTION:
    return filter.conn_burst_multiplier;
  case RateMetric::H2_ERROR:
    return filter.h2_burst_multiplier;
  }
  return 1.0;
}

// Mirrors the TrackerStats counters that consume_rule_buckets increments.
struct BenchStats {
  std::atomic<int64_t> events{0};
  std::atomic<int64_t> events_untracked{0};
  std::atomic<int64_t> scan_exhausted{0};
};

// Mirrors consume_rule_buckets in abuse_shield.cc. TSStatIntIncrement becomes
// a relaxed atomic increment.
template <typename Table>
void
consume_rule_buckets(Table *tracker, const swoc::IPAddr &ip, const BenchConfig &config, RateMetric metric, BenchStats &stats,
                     uint64_t error_code = 0)
{
  typename Table::data_ptr slot;
  bool                     consumed = false;

  for (const auto &rule : config.rules) {
    int rate = metric_rate(rule.filter, metric);
    if (rate <= 0 || !config.rule_applies_to_ip(rule, ip)) {
      continue;
    }

    if (!slot) {
      typename Table::ProcessStatus status;
      slot = tracker->process_event(ip, 1, &status);
      if (!slot) {
        stats.events_untracked.fetch_add(1, std::memory_order_relaxed);
        if (status == Table::ProcessStatus::NO_CANDIDATE) {
          stats.scan_exhausted.fetch_add(1, std::memory_order_relaxed);
        }
        return;
      }
    }

    int burst = static_cast<int>(static_cast<double>(rate) * metric_burst_multiplier(rule.filter, metric));
    if constexpr (std::is_same_v<typename Table::data_type, H2Data>) {
      slot->consume(rule.name, rate, burst, error_code);
    } else {
      slot->consume(rule.name, rate, burst);
    }
    consumed = true;
  }

  if (consumed) {
    stats.events.fetch_add(1, std::memory_order_relaxed);
  }
}

// Mirrors rate_exceeded in abuse_shield.cc.
template <typename Table>
bool
rate_exceeded(Table *tracker, const Rule &rule, const swoc::IPAddr &ip)
{
  auto slot = tracker ? tracker->find(ip) : nullptr;
  return slot && slot->buckets.exceeded(rule.name);
}

/** The three trackers, as the plugin's g_txn_tracker, g_conn_tracker and g_h2_tracker. */
struct Trackers {
  explicit Trackers(size_t slots)
    : txn(std::make_unique<TxnTable>(slots)), conn(std::make_unique<ConnTable>(slots)), h2(std::make_unique<H2Table>(slots))
  {
  }

  std::unique_ptr<TxnTable>  txn;
  std::unique_ptr<ConnTable> conn;
  std::unique_ptr<H2Table>   h2;
};

// Mirrors the rate-rule part of rule_matches and evaluate_rate_rules in
// abuse_shield.cc, for rules without fingerprints. Returns the matched rule or
// nullptr.
const Rule *
evaluate_rate_rules(const Trackers &trackers, const swoc::IPAddr &ip, const BenchConfig &config)
{
  for (const auto &rule : config.rules) {
    if (rule.filter.has_fingerprints()) {
      continue;
    }

    const auto &f = rule.filter;
    if (!config.rule_applies_to_ip(rule, ip)) {
      continue;
    }
    if (f.max_req_rate == 0 && f.max_conn_rate == 0 && f.max_h2_error_rate == 0) {
      continue;
    }
    if (f.max_req_rate > 0 && !rate_exceeded(trackers.txn.get(), rule, ip)) {
      continue;
    }
    if (f.max_conn_rate > 0 && !rate_exceeded(trackers.conn.get(), rule, ip)) {
      continue;
    }
    if (f.max_h2_error_rate > 0 && !rate_exceeded(trackers.h2.get(), rule, ip)) {
      continue;
    }
    return &rule;
  }
  return nullptr;
}

// Mirrors prune_rule_buckets in abuse_shield.cc as it runs on every
// handle_maintenance pass.
void
prune_rule_buckets(const Trackers &trackers, const BenchConfig &config)
{
  auto prune = [&config](auto &table, RateMetric metric) {
    std::unordered_set<std::string> names;
    for (const auto &rule : config.rules) {
      if (metric_rate(rule.filter, metric) > 0) {
        names.insert(rule.name);
      }
    }
    for (const auto &data : table->data_snapshot()) {
      data->buckets.prune(names);
    }
  };
  prune(trackers.txn, RateMetric::REQUEST);
  prune(trackers.conn, RateMetric::CONNECTION);
  prune(trackers.h2, RateMetric::H2_ERROR);
}

// Mirrors sync_tracker_stats in abuse_shield.cc. TSStatIntSet becomes a
// relaxed atomic store.
template <typename Table>
void
sync_tracker_stats(Table *tracker)
{
  if (tracker) {
    g_stat_sink.store(static_cast<int64_t>(tracker->slots_used()), std::memory_order_relaxed);
    g_stat_sink.store(static_cast<int64_t>(tracker->contests()), std::memory_order_relaxed);
    g_stat_sink.store(static_cast<int64_t>(tracker->contests_won()), std::memory_order_relaxed);
    g_stat_sink.store(static_cast<int64_t>(tracker->evictions()), std::memory_order_relaxed);
  }
}

// Mirrors sync_all_tracker_stats in abuse_shield.cc.
void
sync_all_tracker_stats(const Trackers &trackers)
{
  sync_tracker_stats(trackers.txn.get());
  sync_tracker_stats(trackers.conn.get());
  sync_tracker_stats(trackers.h2.get());
}

/** The @a n th distinct address of a deterministic, scattered sequence.
 *
 * Multiplying by an odd constant is a bijection on 32 bits, so distinct @a n
 * give distinct addresses, spread across the space rather than sequential.
 */
swoc::IPAddr
nth_ip(uint32_t n)
{
  uint32_t mixed = n * 2654435761U;
  if (n % IP6_EVERY == 0) {
    in6_addr addr{};
    addr.s6_addr[0] = 0x20;
    addr.s6_addr[1] = 0x01;
    addr.s6_addr[2] = 0x0d;
    addr.s6_addr[3] = 0xb8;
    std::memcpy(&addr.s6_addr[12], &mixed, sizeof(mixed));
    std::memcpy(&addr.s6_addr[8], &n, sizeof(n));
    return swoc::IPAddr{swoc::IP6Addr{addr}};
  }
  return swoc::IPAddr{swoc::IP4Addr{htonl(mixed)}};
}

std::vector<swoc::IPAddr>
make_ips(size_t count, uint32_t first = 0)
{
  std::vector<swoc::IPAddr> ips;
  ips.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    ips.push_back(nth_ip(first + static_cast<uint32_t>(i)));
  }
  return ips;
}

/** Fill @a table with @a ips, giving each entry one bucket per rule of
 * @a metric, with the rates those rules configure.
 */
template <typename Table>
void
populate(Table &table, const std::vector<swoc::IPAddr> &ips, const BenchConfig &config, RateMetric metric)
{
  BenchStats stats;
  for (const auto &ip : ips) {
    consume_rule_buckets(&table, ip, config, metric, stats);
  }
  REQUIRE(table.slots_used() == ips.size());
}

/** Put every entry of @a table whose index satisfies @a in_debt into rate debt
 * on rule @a rule_name. A zero rate never replenishes, so the debt holds for
 * the whole benchmark.
 */
template <typename Table, typename Pred>
void
put_in_debt(Table &table, const std::vector<swoc::IPAddr> &ips, const std::string &rule_name, Pred in_debt)
{
  for (size_t i = 0; i < ips.size(); ++i) {
    if (in_debt(i)) {
      auto data = table.find(ips[i]);
      REQUIRE(data);
      data->buckets.consume(rule_name, 0, 1);
      data->buckets.consume(rule_name, 0, 1);
      REQUIRE(data->buckets.has_debt());
    }
  }
}

std::vector<swoc::IPAddr>
shuffled(std::vector<swoc::IPAddr> ips)
{
  std::mt19937 rng(42);
  std::shuffle(ips.begin(), ips.end(), rng);
  return ips;
}

/** Run @a fn(thread_index, op_index) @a ops_per_thread times on each of
 * @a threads threads that start together.
 */
template <typename Fn>
void
run_threads(int threads, int ops_per_thread, Fn fn)
{
  std::atomic<int>         ready{0};
  std::atomic<bool>        go{false};
  std::vector<std::thread> workers;
  workers.reserve(threads);
  for (int t = 0; t < threads; ++t) {
    workers.emplace_back([&, t]() {
      ready.fetch_add(1, std::memory_order_relaxed);
      while (!go.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
      for (int op = 0; op < ops_per_thread; ++op) {
        fn(t, op);
      }
    });
  }
  while (ready.load(std::memory_order_relaxed) < threads) {
    std::this_thread::yield();
  }
  go.store(true, std::memory_order_release);
  for (auto &worker : workers) {
    worker.join();
  }
}

} // namespace

// ============================================================================
// Hook path: TS_EVENT_HTTP_TXN_START on an IP already in the table.
// ============================================================================

TEST_CASE("abuse_shield request hook path", "[abuse_shield][bench][hot_path]")
{
  size_t const      slots = BENCH_TABLE_SIZES;
  BenchConfig const config{make_rules(RULES_PER_METRIC)};
  Trackers          trackers(slots);
  auto const        ips = make_ips(slots);
  populate(*trackers.txn, ips, config, RateMetric::REQUEST);

  // Visit tracked IPs in random order so the table is not walked in cache order.
  auto const order = shuffled(ips);
  BenchStats stats;
  size_t     next = 0;

  // The per-request bucket update alone: one process_event and one consume per
  // request rule.
  BENCHMARK("consume_rule_buckets, " + std::to_string(slots) + " slots")
  {
    auto const &ip = order[next++ % order.size()];
    consume_rule_buckets(trackers.txn.get(), ip, config, RateMetric::REQUEST, stats);
    return stats.events.load(std::memory_order_relaxed);
  };

  // What handle_txn_event_impl does for TS_EVENT_HTTP_TXN_START once it has the
  // client address: the trusted check, the bucket update, then rule evaluation.
  BENCHMARK("TXN_START hook, " + std::to_string(slots) + " slots")
  {
    auto const &ip = order[next++ % order.size()];
    if (config.is_trusted(ip)) {
      return static_cast<Rule const *>(nullptr);
    }
    consume_rule_buckets(trackers.txn.get(), ip, config, RateMetric::REQUEST, stats);
    return evaluate_rate_rules(trackers, ip, config);
  };
}

TEST_CASE("abuse_shield request hook path under thread contention", "[abuse_shield][bench][hot_path][threaded]")
{
  // The table size matters little here; the shared table mutex does.
  size_t const slots           = 100'000;
  int const    threads         = GENERATE(4, 16);
  int constexpr OPS_PER_THREAD = 20'000;
  BenchConfig const config{make_rules(RULES_PER_METRIC)};
  Trackers          trackers(slots);
  auto const        ips = make_ips(slots);
  populate(*trackers.txn, ips, config, RateMetric::REQUEST);
  auto const order = shuffled(ips);
  BenchStats stats;

  // Each sample is one batch of threads * OPS_PER_THREAD calls; divide the
  // reported time by OPS_PER_THREAD for the wall time per call on each thread.
  BENCHMARK(std::to_string(threads) + " threads x " + std::to_string(OPS_PER_THREAD) + " consume_rule_buckets, distinct IPs")
  {
    run_threads(threads, OPS_PER_THREAD, [&](int t, int op) {
      // Disjoint stripes of the shuffled IPs, one per thread.
      auto const &ip = order[(static_cast<size_t>(op) * threads + t) % order.size()];
      consume_rule_buckets(trackers.txn.get(), ip, config, RateMetric::REQUEST, stats);
    });
    return stats.events.load(std::memory_order_relaxed);
  };

  BENCHMARK(std::to_string(threads) + " threads x " + std::to_string(OPS_PER_THREAD) + " consume_rule_buckets, same IP")
  {
    run_threads(threads, OPS_PER_THREAD,
                [&](int, int) { consume_rule_buckets(trackers.txn.get(), order[0], config, RateMetric::REQUEST, stats); });
    return stats.events.load(std::memory_order_relaxed);
  };
}

// ============================================================================
// Maintenance: prune_rule_buckets and sync_all_tracker_stats.
// ============================================================================

TEST_CASE("abuse_shield maintenance prune pass", "[abuse_shield][bench][maintenance]")
{
  size_t const      slots = BENCH_TABLE_SIZES;
  BenchConfig const config{make_rules(RULES_PER_METRIC)};
  auto const        ips = make_ips(slots);

  SECTION("steady state, three trackers populated")
  {
    Trackers trackers(slots);
    populate(*trackers.txn, ips, config, RateMetric::REQUEST);
    populate(*trackers.conn, ips, config, RateMetric::CONNECTION);
    populate(*trackers.h2, ips, config, RateMetric::H2_ERROR);

    // Nothing to remove: every bucket belongs to a configured rule.
    BENCHMARK("prune pass, 3 trackers x " + std::to_string(slots) + " entries, nothing removed")
    {
      prune_rule_buckets(trackers, config);
    };
  }

  SECTION("steady state, request and connection trackers populated")
  {
    // A deployment where no client has caused an HTTP/2 error leaves the H2
    // tracker empty.
    Trackers trackers(slots);
    populate(*trackers.txn, ips, config, RateMetric::REQUEST);
    populate(*trackers.conn, ips, config, RateMetric::CONNECTION);

    BENCHMARK("prune pass, 2 trackers x " + std::to_string(slots) + " entries, nothing removed")
    {
      prune_rule_buckets(trackers, config);
    };
  }

  SECTION("steady state, one rule per metric")
  {
    BenchConfig const one_rule{make_rules(1)};
    Trackers          trackers(slots);
    populate(*trackers.txn, ips, one_rule, RateMetric::REQUEST);
    populate(*trackers.conn, ips, one_rule, RateMetric::CONNECTION);

    BENCHMARK("prune pass, 2 trackers x " + std::to_string(slots) + " entries, 1 rule each, nothing removed")
    {
      prune_rule_buckets(trackers, one_rule);
    };
  }

  SECTION("one rule removed per metric")
  {
    // After a reload drops one rule of each metric, every entry of every
    // tracker erases one bucket. Removal is one-shot, so each timed run needs
    // a bucket of its own to remove: the setup gives every entry one
    // "removed_i" bucket per run on top of the kept rules, and run i removes
    // its own while keeping those of later runs. With one run per sample,
    // which is what a pass this long gets, every entry starts with three
    // buckets and erases one.
    BenchConfig const kept{make_rules(RULES_PER_METRIC - 1)};
    Trackers          trackers(slots);
    populate(*trackers.txn, ips, kept, RateMetric::REQUEST);
    populate(*trackers.conn, ips, kept, RateMetric::CONNECTION);
    populate(*trackers.h2, ips, kept, RateMetric::H2_ERROR);

    BENCHMARK_ADVANCED("prune pass, 3 trackers x " + std::to_string(slots) + " entries, one bucket removed per entry")
    (Catch::Benchmark::Chronometer meter)
    {
      int const                runs = meter.runs();
      std::vector<BenchConfig> run_configs(runs);
      for (int run = 0; run < runs; ++run) {
        run_configs[run].rules = kept.rules;
        for (int later = run + 1; later < runs; ++later) {
          for (auto rule : make_rules(1)) {
            rule.name += "_removed_" + std::to_string(later);
            run_configs[run].rules.push_back(rule);
          }
        }
      }
      for (int run = 0; run < runs; ++run) {
        std::string const suffix = "_removed_" + std::to_string(run);
        std::string const req    = "excessive_requests_0" + suffix;
        std::string const conn   = "excessive_connections_0" + suffix;
        std::string const h2     = "excessive_h2_errors_0" + suffix;
        for (const auto &ip : ips) {
          trackers.txn->find(ip)->buckets.consume(req, 100, 200);
          trackers.conn->find(ip)->buckets.consume(conn, 20, 30);
          trackers.h2->find(ip)->buckets.consume(h2, 10, 10);
        }
      }
      meter.measure([&](int run) { prune_rule_buckets(trackers, run_configs[run]); });
    };
  }
}

TEST_CASE("abuse_shield maintenance stats sync", "[abuse_shield][bench][maintenance]")
{
  size_t const      slots = BENCH_TABLE_SIZES;
  BenchConfig const config{make_rules(RULES_PER_METRIC)};
  auto const        ips = make_ips(slots);
  Trackers          trackers(slots);
  populate(*trackers.txn, ips, config, RateMetric::REQUEST);
  populate(*trackers.conn, ips, config, RateMetric::CONNECTION);
  populate(*trackers.h2, ips, config, RateMetric::H2_ERROR);

  BENCHMARK("sync_all_tracker_stats, 3 trackers x " + std::to_string(slots) + " entries")
  {
    sync_all_tracker_stats(trackers);
    return g_stat_sink.load(std::memory_order_relaxed);
  };
}

// ============================================================================
// Eviction: new IPs arriving at a full table.
// ============================================================================

TEST_CASE("abuse_shield eviction from a full table", "[abuse_shield][bench][eviction]")
{
  size_t const      slots = BENCH_TABLE_SIZES;
  BenchConfig const config{make_rules(RULES_PER_METRIC)};
  auto const        ips = make_ips(slots);

  // Fresh addresses come from beyond the populated range, so none is tracked.
  // Each call takes the next one; the sequence is long enough that no sample
  // wraps around to an address an earlier call inserted.
  uint32_t const first_fresh = 1U << 30;

  auto bench = [&](std::string const &label, auto in_debt) {
    TxnTable table(slots);
    populate(table, ips, config, RateMetric::REQUEST);
    put_in_debt(table, ips, config.rules.front().name, in_debt);

    uint32_t                next_fresh = first_fresh;
    TxnTable::ProcessStatus status;
    // The table call consume_rule_buckets makes for an IP that is not tracked.
    BENCHMARK("process_event on a new IP, " + std::to_string(slots) + " slots, " + label)
    {
      return table.process_event(nth_ip(next_fresh++), 1, &status);
    };
    INFO("contests " << table.contests() << ", won " << table.contests_won() << ", evictions " << table.evictions());
    CHECK(table.contests() > 0);
  };

  SECTION("no entry in debt")
  {
    bench("no entry in debt", [](size_t) { return false; });
  }
  SECTION("90% of entries in debt")
  {
    bench("90% of entries in debt", [](size_t i) { return i % 10 != 0; });
  }
  SECTION("every entry in debt")
  {
    // DebtAwareEviction keeps every slot, so each contest walks its full
    // MAX_CONTEST_PROBES window.
    bench("every entry in debt", [](size_t) { return true; });
  }
}

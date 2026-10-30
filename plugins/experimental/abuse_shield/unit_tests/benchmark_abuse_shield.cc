/** @file

  Micro-benchmarks for abuse_shield IP tracking: the table and bucket calls
  of the per-request hook path, the maintenance prune pass, and eviction from
  a full table.

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

// Everything timed here is the plugin's own code: UdiTable from tsutil, and
// TokenBucket, RuleBuckets and prune_tracker_buckets from ip_data. Nothing is
// copied out of abuse_shield.cc. The per-request functions there
// (consume_rule_buckets and the rule evaluation) call the TS API and cannot be
// linked here, so the hook path cases time the table and bucket calls those
// functions make, and their labels name those calls.

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

/** A rule bucket as an entry of one tracker holds it: the rule's name, and the
 * rate and burst the rule sets for that tracker's metric.
 */
struct BucketSpec {
  std::string name;
  int         rate;
  int         burst;
};

/** The bucket each rule in @a rules gives an entry of the @a metric tracker,
 * with the rate and burst the plugin derives from the rule.
 */
std::vector<BucketSpec>
bucket_specs(const std::vector<Rule> &rules, RateMetric metric)
{
  std::vector<BucketSpec> specs;
  for (const auto &rule : rules) {
    int rate = metric_rate(rule.filter, metric);
    if (rate <= 0) {
      continue;
    }
    int burst = static_cast<int>(static_cast<double>(rate) * metric_burst_multiplier(rule.filter, metric));
    specs.push_back({rule.name, rate, burst});
  }
  return specs;
}

/** Track @a ip in @a table and take a token from each bucket in @a buckets.
 *
 * These are the UdiTable and RuleBuckets calls consume_rule_buckets makes for
 * one event. The plugin function also checks each rule's IP lists and counts
 * the event in TS stats, which this leaves out.
 */
template <typename Table>
typename Table::data_ptr
track(Table &table, const swoc::IPAddr &ip, const std::vector<BucketSpec> &buckets, typename Table::ProcessStatus *status = nullptr)
{
  auto data = table.process_event(ip, 1, status);
  if (data) {
    for (const auto &bucket : buckets) {
      data->consume(bucket.name, bucket.rate, bucket.burst);
    }
  }
  return data;
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

/** One maintenance prune pass over @a trackers: the prune_tracker_buckets
 * calls prune_rule_buckets in abuse_shield.cc makes for the three trackers.
 */
void
prune_pass(const Trackers &trackers, const std::vector<Rule> &rules)
{
  prune_tracker_buckets(*trackers.txn, rules, RateMetric::REQUEST);
  prune_tracker_buckets(*trackers.conn, rules, RateMetric::CONNECTION);
  prune_tracker_buckets(*trackers.h2, rules, RateMetric::H2_ERROR);
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
    addr.s6_addr[0]         = 0x20;
    addr.s6_addr[1]         = 0x01;
    addr.s6_addr[2]         = 0x0d;
    addr.s6_addr[3]         = 0xb8;
    uint32_t const mixed_be = htonl(mixed);
    uint32_t const n_be     = htonl(n);
    std::memcpy(&addr.s6_addr[12], &mixed_be, sizeof(mixed_be));
    std::memcpy(&addr.s6_addr[8], &n_be, sizeof(n_be));
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
populate(Table &table, const std::vector<swoc::IPAddr> &ips, const std::vector<Rule> &rules, RateMetric metric)
{
  auto const buckets = bucket_specs(rules, metric);
  for (const auto &ip : ips) {
    REQUIRE(track(table, ip, buckets));
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

/** Give every entry of @a table a bucket for each rule in @a rules that sets a
 * rate for @a metric, at the rate and burst that rule configures.
 */
template <typename Table>
void
add_rule_buckets(Table &table, const std::vector<swoc::IPAddr> &ips, const std::vector<Rule> &rules, RateMetric metric)
{
  for (const auto &bucket : bucket_specs(rules, metric)) {
    for (const auto &ip : ips) {
      table.find(ip)->buckets.consume(bucket.name, bucket.rate, bucket.burst);
    }
  }
}

/** How many entries of @a table have no bucket for @a rule_name. Every bucket
 * in these benchmarks either holds most of its burst or is in debt, so a token
 * count of zero means the bucket is missing.
 */
template <typename Table>
size_t
entries_without_bucket(Table &table, const std::string &rule_name)
{
  auto const snapshot = table.data_snapshot();
  return std::count_if(snapshot.begin(), snapshot.end(), [&](const auto &data) { return data->buckets.tokens(rule_name) == 0; });
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
// Hook path: the bucket update for a request from an IP already in the table.
// ============================================================================

TEST_CASE("abuse_shield request bucket update", "[abuse_shield][bench][hot_path]")
{
  size_t const slots = BENCH_TABLE_SIZES;
  auto const   ips   = make_ips(slots);
  // Visit tracked IPs in random order so the table is not walked in cache order.
  auto const order = shuffled(ips);
  auto const rules = make_rules(RULES_PER_METRIC);
  TxnTable   table(slots);
  populate(table, ips, rules, RateMetric::REQUEST);
  auto const buckets = bucket_specs(rules, RateMetric::REQUEST);
  size_t     next    = 0;

  BENCHMARK("process_event + " + std::to_string(buckets.size()) + " bucket consumes, " + std::to_string(slots) + " slots")
  {
    return track(table, order[next++ % order.size()], buckets);
  };
}

TEST_CASE("abuse_shield request bucket update under thread contention", "[abuse_shield][bench][hot_path][threaded]")
{
  // The table size matters little here; the shared table mutex does.
  size_t const slots           = 100'000;
  int const    threads         = GENERATE(4, 16);
  int constexpr OPS_PER_THREAD = 20'000;
  auto const rules             = make_rules(RULES_PER_METRIC);
  TxnTable   table(slots);
  auto const ips = make_ips(slots);
  populate(table, ips, rules, RateMetric::REQUEST);
  auto const order   = shuffled(ips);
  auto const buckets = bucket_specs(rules, RateMetric::REQUEST);

  // Each sample is one batch of threads * OPS_PER_THREAD calls; divide the
  // reported time by OPS_PER_THREAD for the wall time per call on each thread.
  BENCHMARK(std::to_string(threads) + " threads x " + std::to_string(OPS_PER_THREAD) + " bucket updates, distinct IPs")
  {
    run_threads(threads, OPS_PER_THREAD, [&](int t, int op) {
      // Disjoint stripes of the shuffled IPs, one per thread.
      track(table, order[(static_cast<size_t>(op) * threads + t) % order.size()], buckets);
    });
  };

  BENCHMARK(std::to_string(threads) + " threads x " + std::to_string(OPS_PER_THREAD) + " bucket updates, same IP")
  {
    run_threads(threads, OPS_PER_THREAD, [&](int, int) { track(table, order[0], buckets); });
  };
}

// ============================================================================
// Maintenance: one prune pass. The maintenance task prunes on a reload and for
// the next few passes after it; between reloads it only syncs stats.
// ============================================================================

TEST_CASE("abuse_shield maintenance prune pass", "[abuse_shield][bench][maintenance]")
{
  size_t const slots = BENCH_TABLE_SIZES;
  auto const   rules = make_rules(RULES_PER_METRIC);
  auto const   ips   = make_ips(slots);

  SECTION("steady state, three trackers populated")
  {
    Trackers trackers(slots);
    populate(*trackers.txn, ips, rules, RateMetric::REQUEST);
    populate(*trackers.conn, ips, rules, RateMetric::CONNECTION);
    populate(*trackers.h2, ips, rules, RateMetric::H2_ERROR);

    // Nothing to remove: every bucket belongs to a configured rule.
    BENCHMARK("prune pass, 3 trackers x " + std::to_string(slots) + " entries, nothing removed")
    {
      prune_pass(trackers, rules);
    };
  }

  SECTION("steady state, request and connection trackers populated")
  {
    // A deployment where no client has caused an HTTP/2 error leaves the H2
    // tracker empty.
    Trackers trackers(slots);
    populate(*trackers.txn, ips, rules, RateMetric::REQUEST);
    populate(*trackers.conn, ips, rules, RateMetric::CONNECTION);

    BENCHMARK("prune pass, 2 trackers x " + std::to_string(slots) + " entries, nothing removed")
    {
      prune_pass(trackers, rules);
    };
  }

  SECTION("steady state, one rule per metric")
  {
    auto const one_rule = make_rules(1);
    Trackers   trackers(slots);
    populate(*trackers.txn, ips, one_rule, RateMetric::REQUEST);
    populate(*trackers.conn, ips, one_rule, RateMetric::CONNECTION);

    BENCHMARK("prune pass, 2 trackers x " + std::to_string(slots) + " entries, 1 rule each, nothing removed")
    {
      prune_pass(trackers, one_rule);
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
    auto const kept = make_rules(RULES_PER_METRIC - 1);
    Trackers   trackers(slots);
    populate(*trackers.txn, ips, kept, RateMetric::REQUEST);
    populate(*trackers.conn, ips, kept, RateMetric::CONNECTION);
    populate(*trackers.h2, ips, kept, RateMetric::H2_ERROR);
    std::string last_removed;

    BENCHMARK_ADVANCED("prune pass, 3 trackers x " + std::to_string(slots) + " entries, one bucket removed per entry")
    (Catch::Benchmark::Chronometer meter)
    {
      int const                      runs = meter.runs();
      std::vector<std::vector<Rule>> removed(runs);
      for (int run = 0; run < runs; ++run) {
        for (auto rule : make_rules(1)) {
          rule.name += "_removed_" + std::to_string(run);
          removed[run].push_back(rule);
        }
        add_rule_buckets(*trackers.txn, ips, removed[run], RateMetric::REQUEST);
        add_rule_buckets(*trackers.conn, ips, removed[run], RateMetric::CONNECTION);
        add_rule_buckets(*trackers.h2, ips, removed[run], RateMetric::H2_ERROR);
      }
      std::vector<std::vector<Rule>> run_rules(runs, kept);
      for (int run = 0; run < runs; ++run) {
        for (int later = run + 1; later < runs; ++later) {
          run_rules[run].insert(run_rules[run].end(), removed[later].begin(), removed[later].end());
        }
      }
      meter.measure([&](int run) { prune_pass(trackers, run_rules[run]); });
      last_removed = removed.back().front().name;
    };
    // The last run removed its own bucket from every entry.
    CHECK(entries_without_bucket(*trackers.txn, last_removed) == slots);
  }
}

// ============================================================================
// Eviction: new IPs arriving at a full table.
// ============================================================================

TEST_CASE("abuse_shield eviction from a full table", "[abuse_shield][bench][eviction]")
{
  size_t const slots   = BENCH_TABLE_SIZES;
  auto const   rules   = make_rules(RULES_PER_METRIC);
  auto const   buckets = bucket_specs(rules, RateMetric::REQUEST);
  auto const   ips     = make_ips(slots);

  // Fresh addresses come from beyond the populated range, so none is tracked.
  // Each call takes the next one; the sequence is long enough that no sample
  // wraps around to an address an earlier call inserted.
  uint32_t const first_fresh = 1U << 30;

  auto bench = [&](std::string const &label, auto in_debt) {
    TxnTable table(slots);
    populate(table, ips, rules, RateMetric::REQUEST);
    put_in_debt(table, ips, rules.front().name, in_debt);

    // Only entries out of debt are contest candidates, and each starts with a
    // score of one. One pass of the contest pointer over them loses every
    // contest and drops each score to zero; the next pass wins every contest
    // and leaves each winner with a score of one, back where it started. A
    // block of single calls falls in one pass or the other depending on how
    // many calls came before it, so each measured call runs both passes and
    // every sample sees the same mix. With no candidate, every call is the
    // same full scan and one call is the whole cycle.
    size_t candidates = 0;
    for (size_t i = 0; i < ips.size(); ++i) {
      candidates += in_debt(i) ? 0 : 1;
    }
    size_t const cycle = candidates > 0 ? 2 * candidates : 1;
    table.reset_metrics();

    uint32_t next_fresh   = first_fresh;
    uint64_t untracked    = 0;
    uint64_t no_candidate = 0;
    // The whole update for an IP that is not tracked: the contest, then a
    // bucket per request rule in the new entry. Timing process_event alone
    // would leave each winner with no buckets, and within a few warmup rounds
    // the samples would run against a table of empty entries that are cheaper
    // to scan and to evict than the ones they replaced.
    std::string const ips_label = cycle == 1 ? "a new IP" : std::to_string(cycle) + " new IPs";
    BENCHMARK("bucket update on " + ips_label + ", " + std::to_string(slots) + " slots, " + label)
    {
      for (size_t call = 0; call < cycle; ++call) {
        TxnTable::ProcessStatus status;
        if (!track(table, nth_ip(next_fresh++), buckets, &status)) {
          ++untracked;
          if (status == TxnTable::ProcessStatus::NO_CANDIDATE) {
            ++no_candidate;
          }
        }
      }
      return untracked;
    };
    uint64_t const contests = table.contests();
    uint64_t const lost     = untracked - no_candidate;
    INFO("contests " << contests << ", won " << table.contests_won() << ", lost " << lost << ", no candidate " << no_candidate
                     << ", evictions " << table.evictions());
    CHECK(contests > 0);
    // Whole cycles only, so each candidate lost and won the same number of times.
    CHECK(contests % cycle == 0);
    CHECK(table.contests_won() == contests / cycle * candidates);
    CHECK(lost == contests / cycle * candidates);
    CHECK(no_candidate == (candidates > 0 ? 0 : contests));
    // Every entry, original or new, still carries its buckets.
    CHECK(entries_without_bucket(table, rules.front().name) == 0);
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

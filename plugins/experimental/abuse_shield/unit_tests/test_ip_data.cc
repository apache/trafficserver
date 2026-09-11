/** @file

  Unit tests for abuse_shield per-rule token buckets.

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

#include "ip_data.h"

#include <catch2/catch_test_macros.hpp>

#include <thread>
#include <vector>

using namespace abuse_shield;

TEST_CASE("An unused TokenBucket has no debt", "[abuse_shield][token_bucket]")
{
  TokenBucket bucket;

  CHECK(bucket.tokens() == 0);
  CHECK(bucket.consume(0, 1) == 0);
  CHECK(bucket.consume(0, 1) == -1);
  CHECK(bucket.tokens() == -1);
}

TEST_CASE("TokenBucket consumes atomically", "[abuse_shield][token_bucket]")
{
  TokenBucket bucket;

  CHECK(bucket.consume(100, 100) == 99);
  CHECK(bucket.consume(100, 100) <= 98);
  CHECK(bucket.tokens() <= 98);
}

TEST_CASE("TokenBucket preserves every concurrent consume", "[abuse_shield][token_bucket][threaded]")
{
  TokenBucket              bucket;
  constexpr int            THREADS           = 8;
  constexpr int            EVENTS_PER_THREAD = 1000;
  std::vector<std::thread> threads;

  // A fixed timestamp isolates lost updates from replenishment.
  for (int i = 0; i < THREADS; ++i) {
    threads.emplace_back([&bucket]() {
      for (int event = 0; event < EVENTS_PER_THREAD; ++event) {
        bucket.consume(1, 1, 100);
      }
    });
  }
  for (auto &thread : threads) {
    thread.join();
  }

  CHECK(bucket.tokens() == 1 - THREADS * EVENTS_PER_THREAD);
}

TEST_CASE("RuleBuckets keep thresholds independent of rule order", "[abuse_shield][token_bucket][rules]")
{
  RuleBuckets buckets;

  for (int i = 0; i < 6; ++i) {
    buckets.consume("lenient", 100, 100);
    buckets.consume("strict", 5, 5);
  }

  CHECK_FALSE(buckets.exceeded("lenient"));
  CHECK(buckets.exceeded("strict"));
  CHECK(buckets.tokens("lenient") > 0);
  CHECK(buckets.tokens("strict") < 0);
  CHECK(buckets.has_debt());
}

TEST_CASE("Rate debt protects a table entry from eviction", "[abuse_shield][table]")
{
  TxnTable                table(1);
  swoc::IPAddr            debtor{"192.0.2.1"};
  swoc::IPAddr            challenger{"192.0.2.2"};
  TxnTable::ProcessStatus status;

  auto data = table.process_event(debtor);
  REQUIRE(data);
  data->consume("strict", 1, 1);
  data->consume("strict", 1, 1);
  REQUIRE(data->buckets.has_debt());

  CHECK_FALSE(table.process_event(challenger, 100, &status));
  CHECK(status == TxnTable::ProcessStatus::NO_CANDIDATE);
  CHECK(table.find(debtor));
  CHECK_FALSE(table.find(challenger));
}

TEST_CASE("An ordinary table contest loss is distinguished from scan exhaustion", "[abuse_shield][table]")
{
  TxnTable                table(1);
  swoc::IPAddr            incumbent{"192.0.2.1"};
  swoc::IPAddr            challenger{"192.0.2.2"};
  TxnTable::ProcessStatus status;

  REQUIRE(table.process_event(incumbent, 2));
  CHECK_FALSE(table.process_event(challenger, 1, &status));
  CHECK(status == TxnTable::ProcessStatus::CONTEST_LOST);
  CHECK(table.find(incumbent));
  CHECK_FALSE(table.find(challenger));
}

TEST_CASE("Tracker data records per-rule events", "[abuse_shield][tracker]")
{
  TxnData txn;
  CHECK(txn.consume("request_rule", 10, 10) == 9);
  CHECK(txn.count.load() == 1);
  CHECK(txn.buckets.tokens("request_rule") == 9);

  ConnData conn;
  CHECK(conn.consume("connection_rule", 10, 10) == 9);
  CHECK(conn.count.load() == 1);

  H2Data h2;
  h2.consume("h2_rule", 10, 10, 1);
  h2.consume("h2_rule", 10, 10, 256);
  CHECK(h2.count.load() == 2);
  CHECK(h2.error_codes[1].load() == 1);
  CHECK(h2.error_codes[0].load() == 0);
}

TEST_CASE("now_ms is monotonic", "[abuse_shield][time]")
{
  uint64_t first = now_ms();
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  CHECK(now_ms() > first);
}

TEST_CASE("Log interval permits an IP's first log", "[abuse_shield][logging]")
{
  std::atomic<uint64_t> last_logged{0};

  CHECK(claim_log_interval(last_logged, 1, 10'000));
  CHECK(last_logged.load(std::memory_order_relaxed) == 1);
  CHECK_FALSE(claim_log_interval(last_logged, 2, 10'000));
  CHECK(claim_log_interval(last_logged, 10'001, 10'000));
}

TEST_CASE("Log interval claim is atomic", "[abuse_shield][logging][threaded]")
{
  std::atomic<uint64_t> last_logged{0};
  std::atomic<int>      claims{0};

  constexpr int            THREADS = 8;
  std::vector<std::thread> threads;

  for (int i = 0; i < THREADS; ++i) {
    threads.emplace_back([&last_logged, &claims]() {
      if (claim_log_interval(last_logged, 1, 10'000)) {
        claims.fetch_add(1, std::memory_order_relaxed);
      }
    });
  }
  for (auto &thread : threads) {
    thread.join();
  }

  CHECK(claims.load(std::memory_order_relaxed) == 1);
}

TEST_CASE("TokenBucket preserves fractional replenishment", "[abuse_shield][token_bucket]")
{
  for (int rate : {30, 60, 75, 100}) {
    TokenBucket bucket;
    for (int i = 0; i < rate * 30; ++i) {
      REQUIRE(bucket.consume(rate, rate, static_cast<uint32_t>(i * 1000 / rate)) >= 0);
    }
  }

  TokenBucket bucket;
  CHECK(bucket.consume(30, 1, 0) == 0);
  CHECK(bucket.consume(30, 1, 33) == -1);
  CHECK(bucket.tokens() == -1);
  CHECK(bucket.consume(30, 1, 67) == 0);
  CHECK(bucket.consume(30, 1, 1000) == 0);
}

TEST_CASE("Saturated debt cannot collide with the initial sentinel", "[abuse_shield][token_bucket]")
{
  TokenBucket bucket;
  for (int i = 0; i < 2'200'000; ++i) {
    bucket.consume(0, 1, 0);
  }
  CHECK(bucket.tokens() == -2'147'484);
  CHECK(bucket.consume(0, 1, 0) == -2'147'484);
}

TEST_CASE("Removing a rule releases obsolete debt", "[abuse_shield][table]")
{
  TxnTable     table(1);
  swoc::IPAddr ip{"192.0.2.1"};
  auto         data = table.process_event(ip);
  REQUIRE(data);
  data->consume("removed", 0, 1);
  data->consume("removed", 0, 1);
  data->consume("retained", 0, 2);
  REQUIRE_FALSE(data->is_evictable());
  for (const auto &entry : table.data_snapshot()) {
    entry->buckets.prune({"retained"});
  }
  CHECK(data->is_evictable());
  CHECK(data->buckets.tokens("retained") == 1);
  CHECK(table.process_event(swoc::IPAddr{"192.0.2.2"}, 100));
}

TEST_CASE("BlockedIpTable preserves capacity expiry and maximum extension", "[abuse_shield][blocking]")
{
  BlockedIpTable table(1);
  swoc::IPAddr   first{"192.0.2.1"};
  swoc::IPAddr   second{"192.0.2.2"};
  REQUIRE(table.block(first, 100, 0));
  CHECK_FALSE(table.block(second, 200, 0));
  CHECK(table.block(first, 50, 0));
  CHECK(table.is_blocked(first, 75));
  CHECK(table.block(first, 150, 75));
  CHECK(table.is_blocked(first, 100));
  CHECK_FALSE(table.is_blocked(first, 150));
  CHECK(table.block(second, 200, 150));
  CHECK(table.block(first, 300, 200));
  CHECK_FALSE(table.is_blocked(second, 200));
  table.clear();
  CHECK_FALSE(table.is_blocked(first, 200));
}

TEST_CASE("Variable arrival times preserve earned credit", "[abuse_shield][token_bucket]")
{
  TokenBucket bucket;
  uint32_t    now = 0;
  // Alternate a short and a long interval at 100 requests per second.
  for (int i = 0; i < 10'000; ++i) {
    REQUIRE(bucket.consume(100, 100, now) >= 0);
    now += i % 2 == 0 ? 3 : 17;
  }
}

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

#include "../ip_data.h"

#include <catch2/catch_test_macros.hpp>

#include <thread>
#include <vector>

using namespace abuse_shield;

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
  uint64_t                 start_ms = now_ms();

  // A nonzero rate keeps the replenishment path active. At one token per
  // second this tight loop still completes before any token can replenish.
  for (int i = 0; i < THREADS; ++i) {
    threads.emplace_back([&bucket]() {
      for (int event = 0; event < EVENTS_PER_THREAD; ++event) {
        bucket.consume(1, 1);
      }
    });
  }
  for (auto &thread : threads) {
    thread.join();
  }

  int64_t elapsed_ms           = static_cast<int64_t>(now_ms() - start_ms);
  int64_t maximum_valid_tokens = 2 - (THREADS * EVENTS_PER_THREAD) + elapsed_ms / 1000;
  CHECK(bucket.tokens() <= maximum_valid_tokens);
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

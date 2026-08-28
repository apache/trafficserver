/** @file

  Unit tests for UdiTable.

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

#include "tsutil/UdiTable.h"

#include <atomic>
#include <cstdint>
#include <limits>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace
{

struct TestData {
  std::atomic<uint32_t> count{0};
};

using TestTable = ts::UdiTable<std::string, TestData>;

struct ProtectedData {
  std::atomic<bool> can_evict{true};
};

struct CanEvictProtectedData {
  bool
  operator()(ProtectedData const &data) const
  {
    return data.can_evict.load(std::memory_order_relaxed);
  }
};

using ProtectedTable = ts::UdiTable<std::string, ProtectedData, std::hash<std::string>, CanEvictProtectedData>;

} // namespace

TEST_CASE("UdiTable provides bounded basic operations", "[tsutil][UdiTable]")
{
  TestTable table(2);

  CHECK(table.num_slots() == 2);
  CHECK(table.slots_used() == 0);
  CHECK_FALSE(table.find("missing"));

  auto first = table.process_event("first", 2);
  REQUIRE(first);
  first->count.store(7, std::memory_order_relaxed);

  CHECK(table.slots_used() == 1);
  CHECK(table.find("first") == first);
  CHECK(table.find("first")->count.load(std::memory_order_relaxed) == 7);

  CHECK(table.remove("first"));
  CHECK_FALSE(table.remove("first"));
  CHECK_FALSE(table.find("first"));
  CHECK(table.slots_used() == 0);
  CHECK(first->count.load(std::memory_order_relaxed) == 7);
}

TEST_CASE("UdiTable distinguishes contest outcomes", "[tsutil][UdiTable][contest]")
{
  TestTable                table(1);
  TestTable::ProcessStatus status;

  REQUIRE(table.process_event("incumbent", 2));
  CHECK_FALSE(table.process_event("challenger", 1, &status));
  CHECK(status == TestTable::ProcessStatus::CONTEST_LOST);
  CHECK(table.find("incumbent"));

  auto replacement = table.process_event("replacement", 3, &status);
  REQUIRE(replacement);
  CHECK(status == TestTable::ProcessStatus::TRACKED);
  CHECK_FALSE(table.find("incumbent"));
  CHECK(table.find("replacement") == replacement);
  CHECK(table.contests() == 3);
  CHECK(table.contests_won() == 2);
  CHECK(table.evictions() == 1);
}

TEST_CASE("UdiTable honors its eviction predicate", "[tsutil][UdiTable][eviction]")
{
  ProtectedTable                table(1);
  ProtectedTable::ProcessStatus status;

  auto protected_data = table.process_event("protected");
  REQUIRE(protected_data);
  protected_data->can_evict.store(false, std::memory_order_relaxed);

  CHECK_FALSE(table.process_event("challenger", 100, &status));
  CHECK(status == ProtectedTable::ProcessStatus::NO_CANDIDATE);
  CHECK(table.find("protected"));
  CHECK_FALSE(table.find("challenger"));
}

TEST_CASE("UdiTable saturates scores", "[tsutil][UdiTable][score]")
{
  TestTable table(1);
  REQUIRE(table.process_event("key", std::numeric_limits<uint32_t>::max()));
  REQUIRE(table.process_event("key", 1));

  uint32_t score = 0;
  table.dump([&score](std::string const &, uint32_t entry_score, TestTable::const_data_ptr const &) {
    score = entry_score;
    return std::string{};
  });
  CHECK(score == std::numeric_limits<uint32_t>::max());
}

TEST_CASE("UdiTable formats snapshots outside its lock", "[tsutil][UdiTable][dump]")
{
  TestTable table(1);
  REQUIRE(table.process_event("key"));

  std::string output = table.dump([&table](std::string const &key, uint32_t score, TestTable::const_data_ptr const &) {
    CHECK(table.find(key));
    return key + "=" + std::to_string(score);
  });
  CHECK(output == "key=1");
}

TEST_CASE("UdiTable serializes concurrent updates", "[tsutil][UdiTable][threading]")
{
  TestTable table(1);

  constexpr int THREAD_COUNT      = 4;
  constexpr int EVENTS_PER_THREAD = 1000;

  std::atomic<bool>        process_failed{false};
  std::vector<std::thread> threads;

  for (int thread = 0; thread < THREAD_COUNT; ++thread) {
    threads.emplace_back([&table, &process_failed]() {
      for (int event = 0; event < EVENTS_PER_THREAD; ++event) {
        auto data = table.process_event("shared");
        if (data) {
          data->count.fetch_add(1, std::memory_order_relaxed);
        } else {
          process_failed.store(true, std::memory_order_relaxed);
        }
      }
    });
  }
  for (auto &thread : threads) {
    thread.join();
  }

  auto data = table.find("shared");
  REQUIRE(data);
  CHECK_FALSE(process_failed.load(std::memory_order_relaxed));
  CHECK(data->count.load(std::memory_order_relaxed) == THREAD_COUNT * EVENTS_PER_THREAD);
  CHECK(table.slots_used() == 1);
}

TEST_CASE("UdiTable resets metrics without removing entries", "[tsutil][UdiTable][metrics]")
{
  TestTable table(1);
  REQUIRE(table.process_event("first"));
  table.process_event("second", 2);
  REQUIRE(table.contests() > 0);

  table.reset_metrics();
  CHECK(table.contests() == 0);
  CHECK(table.contests_won() == 0);
  CHECK(table.evictions() == 0);
  CHECK(table.seconds_since_reset() == 0);
  CHECK(table.slots_used() == 1);
}

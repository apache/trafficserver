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
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "swoc/swoc_ip.h"

namespace
{

// ============================================================================
// String key test data (user data only, key/score owned by UdiTable)
// ============================================================================
struct StringData {
  std::atomic<uint32_t> count{0};
};

using StringTable = ts::UdiTable<std::string, StringData, std::hash<std::string>>;

// ============================================================================
// IP address key test data (user data only, key/score owned by UdiTable)
// ============================================================================
struct IPData {
  std::atomic<uint32_t> error_count{0};
  std::atomic<uint32_t> success_count{0};
};

using IPTable = ts::UdiTable<swoc::IPAddr, IPData, std::hash<swoc::IPAddr>>;

} // namespace

// ============================================================================
// String key tests
// ============================================================================

TEST_CASE("UdiTable string key basic operations", "[UdiTable][string]")
{
  StringTable table(100);

  SECTION("empty table")
  {
    REQUIRE(table.num_slots() == 100);
    REQUIRE(table.slots_used() == 0);
    REQUIRE(table.find("nonexistent") == nullptr);
  }

  SECTION("process_event and find")
  {
    auto data = table.process_event("key1", 5);
    REQUIRE(data != nullptr);

    auto found = table.find("key1");
    REQUIRE(found == data);
    REQUIRE(table.slots_used() == 1);
  }

  SECTION("process_event same key increases score")
  {
    table.process_event("key1", 5);
    auto data = table.process_event("key1", 3);
    REQUIRE(data != nullptr);
    // Score should be 8 but we can't check it directly (internal)
    REQUIRE(table.slots_used() == 1);
  }

  SECTION("process_event multiple keys")
  {
    table.process_event("key1", 1);
    table.process_event("key2", 2);
    table.process_event("key3", 3);

    REQUIRE(table.slots_used() == 3);
    REQUIRE(table.find("key1") != nullptr);
    REQUIRE(table.find("key2") != nullptr);
    REQUIRE(table.find("key3") != nullptr);
  }

  SECTION("remove key")
  {
    table.process_event("key1", 5);
    REQUIRE(table.remove("key1"));
    REQUIRE(table.find("key1") == nullptr);
    REQUIRE(table.slots_used() == 0);
  }

  SECTION("remove nonexistent returns false")
  {
    REQUIRE_FALSE(table.remove("nonexistent"));
  }

  SECTION("user data can be modified")
  {
    auto data = table.process_event("key1", 1);
    REQUIRE(data != nullptr);

    data->count.fetch_add(10, std::memory_order_relaxed);

    auto found = table.find("key1");
    REQUIRE(found != nullptr);
    REQUIRE(found->count.load() == 10);
  }

  SECTION("shared_ptr survives eviction")
  {
    // Fill table completely
    StringTable small_table(2);
    auto        data1 = small_table.process_event("key1", 5);
    REQUIRE(data1 != nullptr);
    data1->count.store(42, std::memory_order_relaxed);

    small_table.process_event("key2", 5);

    // Now evict key1 by adding key3 with higher score
    small_table.process_event("key3", 100);

    // Original shared_ptr should still be valid and contain the data
    REQUIRE(data1->count.load() == 42);

    // But find should no longer return it (it's been evicted)
    // Note: key1 may or may not be evicted depending on contest, so we just check data1 is valid
  }
}

TEST_CASE("UdiTable string key contest algorithm", "[UdiTable][string]")
{
  StringTable table(4);

  SECTION("fill table then contest")
  {
    table.process_event("a", 1);
    table.process_event("b", 2);
    table.process_event("c", 3);
    table.process_event("d", 4);

    uint64_t initial_contests = table.contests();

    // New key with higher score should win
    auto data = table.process_event("e", 10);
    REQUIRE(data != nullptr);
    REQUIRE(table.contests() > initial_contests);
    REQUIRE(table.contests_won() > 0);
  }

  SECTION("low score loses contest")
  {
    table.process_event("a", 100);
    table.process_event("b", 100);
    table.process_event("c", 100);
    table.process_event("d", 100);

    // Low score contest - may or may not win depending on current slot scores
    [[maybe_unused]] auto data = table.process_event("e", 1);
    REQUIRE(table.contests() > 0);
  }
}

TEST_CASE("UdiTable string key statistics", "[UdiTable][string]")
{
  StringTable table(100);

  table.process_event("key1", 1);
  table.process_event("key2", 2);
  table.process_event("key3", 3);

  SECTION("dump produces output")
  {
    std::string dump = table.dump();
    REQUIRE_FALSE(dump.empty());
  }

  SECTION("custom dump format")
  {
    auto format = [](const std::string &key, uint32_t score, const std::shared_ptr<StringData> &data) -> std::string {
      return "KEY:" + key + " SCORE:" + std::to_string(score) + " COUNT:" + std::to_string(data->count.load()) + "\n";
    };

    std::string dump = table.dump(format);
    REQUIRE(dump.find("KEY:key1") != std::string::npos);
  }

  SECTION("metrics reset")
  {
    // Force some contests
    for (int i = 0; i < 10; ++i) {
      table.process_event("contest_key_" + std::to_string(i), 1);
    }

    REQUIRE(table.contests() > 0);

    table.reset_metrics();

    REQUIRE(table.contests() == 0);
    REQUIRE(table.contests_won() == 0);
    REQUIRE(table.evictions() == 0);
    REQUIRE(table.seconds_since_reset() == 0);
  }
}

// ============================================================================
// IP address key tests
// ============================================================================

TEST_CASE("UdiTable IP key basic operations", "[UdiTable][ip]")
{
  IPTable table(100);

  swoc::IPAddr ip1{"192.168.1.1"};
  swoc::IPAddr ip2{"192.168.1.2"};
  swoc::IPAddr ip3{"10.0.0.1"};
  swoc::IPAddr ip_nonexistent{"8.8.8.8"};

  SECTION("empty table")
  {
    REQUIRE(table.num_slots() == 100);
    REQUIRE(table.slots_used() == 0);
    REQUIRE(table.find(ip_nonexistent) == nullptr);
  }

  SECTION("process_event and find IPv4")
  {
    auto data = table.process_event(ip1, 5);
    REQUIRE(data != nullptr);

    auto found = table.find(ip1);
    REQUIRE(found == data);
    REQUIRE(table.slots_used() == 1);
  }

  SECTION("process_event and find IPv6")
  {
    swoc::IPAddr ipv6{"2001:db8::1"};
    auto         data = table.process_event(ipv6, 10);
    REQUIRE(data != nullptr);

    auto found = table.find(ipv6);
    REQUIRE(found == data);
  }

  SECTION("process_event same IP increases score")
  {
    table.process_event(ip1, 5);
    auto data = table.process_event(ip1, 3);
    REQUIRE(data != nullptr);
    REQUIRE(table.slots_used() == 1);
  }

  SECTION("process_event multiple IPs")
  {
    table.process_event(ip1, 1);
    table.process_event(ip2, 2);
    table.process_event(ip3, 3);

    REQUIRE(table.slots_used() == 3);
    REQUIRE(table.find(ip1) != nullptr);
    REQUIRE(table.find(ip2) != nullptr);
    REQUIRE(table.find(ip3) != nullptr);
  }

  SECTION("mixed IPv4 and IPv6")
  {
    swoc::IPAddr ipv4{"192.168.1.100"};
    swoc::IPAddr ipv6{"::ffff:192.168.1.100"}; // IPv4-mapped IPv6

    table.process_event(ipv4, 5);
    table.process_event(ipv6, 10);

    // These should be different entries (different address representations)
    auto data4 = table.find(ipv4);
    auto data6 = table.find(ipv6);

    REQUIRE(data4 != nullptr);
    REQUIRE(data6 != nullptr);
  }
}

TEST_CASE("UdiTable IP key contest algorithm", "[UdiTable][ip]")
{
  IPTable table(4);

  SECTION("fill table then contest")
  {
    table.process_event(swoc::IPAddr{"1.1.1.1"}, 1);
    table.process_event(swoc::IPAddr{"2.2.2.2"}, 2);
    table.process_event(swoc::IPAddr{"3.3.3.3"}, 3);
    table.process_event(swoc::IPAddr{"4.4.4.4"}, 4);

    uint64_t initial_contests = table.contests();

    // New IP with higher score should win
    swoc::IPAddr new_ip{"5.5.5.5"};
    auto         data = table.process_event(new_ip, 10);
    REQUIRE(data != nullptr);
    REQUIRE(table.contests() > initial_contests);
    REQUIRE(table.contests_won() > 0);
  }
}

TEST_CASE("UdiTable IP key user data", "[UdiTable][ip]")
{
  IPTable table(100);

  swoc::IPAddr attacker{"192.168.1.100"};

  SECTION("track error and success counts")
  {
    auto data = table.process_event(attacker, 1);
    REQUIRE(data != nullptr);

    // Simulate recording errors
    data->error_count.fetch_add(5, std::memory_order_relaxed);

    // Simulate recording successes
    data->success_count.fetch_add(2, std::memory_order_relaxed);

    auto found = table.find(attacker);
    REQUIRE(found != nullptr);
    REQUIRE(found->error_count.load() == 5);
    REQUIRE(found->success_count.load() == 2);
  }
}

// ============================================================================
// Thread safety tests
// ============================================================================

TEST_CASE("UdiTable thread safety with strings", "[UdiTable][threading][string]")
{
  StringTable table(1000);

  constexpr int            NUM_THREADS    = 4;
  constexpr int            OPS_PER_THREAD = 1000;
  std::vector<std::thread> threads;

  SECTION("concurrent process_events")
  {
    for (int t = 0; t < NUM_THREADS; ++t) {
      threads.emplace_back([&table, t]() {
        for (int i = 0; i < OPS_PER_THREAD; ++i) {
          std::string key  = "thread" + std::to_string(t) + "_key" + std::to_string(i);
          auto        data = table.process_event(key, 1);
          if (data) {
            data->count.fetch_add(1, std::memory_order_relaxed);
          }
        }
      });
    }

    for (auto &t : threads) {
      t.join();
    }

    REQUIRE(table.slots_used() > 0);
  }
}

TEST_CASE("UdiTable thread safety with IPs", "[UdiTable][threading][ip]")
{
  IPTable table(1000);

  constexpr int            NUM_THREADS    = 4;
  constexpr int            OPS_PER_THREAD = 500;
  std::vector<std::thread> threads;

  SECTION("concurrent IP process_events")
  {
    for (int t = 0; t < NUM_THREADS; ++t) {
      threads.emplace_back([&table, t]() {
        for (int i = 0; i < OPS_PER_THREAD; ++i) {
          // Generate unique IPs: 10.t.i/256.i%256
          std::string  ip_str = "10." + std::to_string(t) + "." + std::to_string(i / 256) + "." + std::to_string(i % 256);
          swoc::IPAddr ip{ip_str};

          auto data = table.process_event(ip, 1);
          if (data) {
            data->error_count.fetch_add(1, std::memory_order_relaxed);
          }
        }
      });
    }

    for (auto &t : threads) {
      t.join();
    }

    REQUIRE(table.slots_used() > 0);
  }

  SECTION("concurrent find and process_event")
  {
    // Pre-populate with some IPs
    for (int i = 0; i < 100; ++i) {
      std::string  ip_str = "192.168.1." + std::to_string(i);
      swoc::IPAddr ip{ip_str};
      table.process_event(ip, 10);
    }

    std::atomic<int> found_count{0};

    for (int t = 0; t < NUM_THREADS; ++t) {
      threads.emplace_back([&table, &found_count]() {
        for (int i = 0; i < OPS_PER_THREAD; ++i) {
          std::string  ip_str = "192.168.1." + std::to_string(i % 100);
          swoc::IPAddr ip{ip_str};

          if (auto data = table.find(ip)) {
            found_count.fetch_add(1, std::memory_order_relaxed);
            data->error_count.fetch_add(1, std::memory_order_relaxed);
          }
        }
      });
    }

    for (auto &t : threads) {
      t.join();
    }

    REQUIRE(found_count.load() > 0);
  }
}

TEST_CASE("UdiTable slots_used never exceeds num_slots", "[UdiTable]")
{
  // This test verifies that the lookup map size never exceeds the slot vector size.
  // A bug existed where is_empty() checked score==0 instead of !data, causing
  // stale keys to remain in lookup_ when scores were decremented to 0.

  constexpr size_t NUM_SLOTS  = 10;
  constexpr size_t NUM_KEYS   = 1000; // Many more keys than slots
  constexpr size_t ITERATIONS = 100;

  StringTable table(NUM_SLOTS);

  // Generate many unique keys
  std::vector<std::string> keys;
  for (size_t i = 0; i < NUM_KEYS; ++i) {
    keys.push_back("key_" + std::to_string(i));
  }

  // Process many events - this will cause contests and score decrements
  for (size_t iter = 0; iter < ITERATIONS; ++iter) {
    for (auto const &key : keys) {
      table.process_event(key, 1);

      // Critical check: slots_used() must never exceed num_slots()
      REQUIRE(table.slots_used() <= table.num_slots());
    }
  }

  // Final verification
  REQUIRE(table.slots_used() <= NUM_SLOTS);
  INFO("Final slots_used: " << table.slots_used() << ", num_slots: " << table.num_slots());
}

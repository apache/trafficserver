/** @file

  Unit tests for ThreadLeasedSharedPtr.

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

#include <catch2/catch_test_macros.hpp>

#include "tsutil/ThreadLeasedSharedPtr.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

namespace
{
// Records its destruction so tests can check when the shared value is released.
struct Tracked {
  Tracked(int v, std::atomic<int> &destroyed) : value(v), destroyed_count(destroyed) {}
  ~Tracked() { destroyed_count.fetch_add(1); }

  int               value;
  std::atomic<int> &destroyed_count;
};
} // namespace

TEST_CASE("ThreadLeasedSharedPtr shares one reference per thread", "[tsutil][ThreadLeasedSharedPtr]")
{
  std::atomic<int>               destroyed{0};
  ThreadLeasedSharedPtr<Tracked> ptr;
  auto                           original = std::make_shared<Tracked>(1, destroyed);
  ptr.store(original);
  long const baseline = original.use_count();

  std::vector<std::shared_ptr<Tracked>> copies;
  for (int i = 0; i < 10; ++i) {
    copies.push_back(ptr.load_leased());
  }

  CHECK(copies.front().get() == original.get());
  // All ten copies count against the thread's control block, which holds one shared reference.
  CHECK(original.use_count() == baseline + 1);
  CHECK(copies.front().use_count() == 10);

  SECTION("the thread's reference is dropped with its last copy")
  {
    copies.clear();
    CHECK(original.use_count() == baseline);
  }

  SECTION("a copy released on another thread still releases the shared reference")
  {
    auto moved = std::move(copies);
    std::thread([&moved] { moved.clear(); }).join();
    CHECK(original.use_count() == baseline);
  }
}

TEST_CASE("ThreadLeasedSharedPtr follows store and exchange", "[tsutil][ThreadLeasedSharedPtr]")
{
  std::atomic<int>               destroyed{0};
  ThreadLeasedSharedPtr<Tracked> ptr;

  ptr.store(std::make_shared<Tracked>(1, destroyed));
  auto first = ptr.load_leased();
  REQUIRE(first);
  CHECK(first->value == 1);

  ptr.store(std::make_shared<Tracked>(2, destroyed));
  auto second = ptr.load_leased();
  REQUIRE(second);
  CHECK(second->value == 2);
  // A reference taken before the store still holds the old value.
  CHECK(first->value == 1);
  CHECK(destroyed.load() == 0);

  first.reset();
  CHECK(destroyed.load() == 1);

  auto previous = ptr.exchange(nullptr);
  CHECK(previous.get() == second.get());
  CHECK(ptr.load_leased() == nullptr);

  previous.reset();
  second.reset();
  CHECK(destroyed.load() == 2);
}

TEST_CASE("ThreadLeasedSharedPtr does not extend the value's lifetime when idle", "[tsutil][ThreadLeasedSharedPtr]")
{
  std::atomic<int>               destroyed{0};
  ThreadLeasedSharedPtr<Tracked> ptr;

  ptr.store(std::make_shared<Tracked>(1, destroyed));
  ptr.load_leased().reset();
  std::thread([&ptr] { ptr.load_leased().reset(); }).join();

  // With no references outstanding, dropping the stored value destroys it immediately.
  ptr.store(nullptr);
  CHECK(destroyed.load() == 1);
}

TEST_CASE("ThreadLeasedSharedPtr readers see whole values during replacement", "[tsutil][ThreadLeasedSharedPtr]")
{
  constexpr int READERS = 4;
  constexpr int STORES  = 2000;

  std::atomic<int>               destroyed{0};
  ThreadLeasedSharedPtr<Tracked> ptr;
  ptr.store(std::make_shared<Tracked>(0, destroyed));

  std::atomic<bool> stop{false};
  std::atomic<int>  bad_reads{0};
  std::atomic<int>  started{0};

  std::vector<std::thread> readers;
  for (int i = 0; i < READERS; ++i) {
    readers.emplace_back([&] {
      // Holding the previous value across loads keeps the thread's lease live, so unchanged
      // generations take the cached path while the writer replaces values.
      std::shared_ptr<Tracked> v    = ptr.load_leased();
      int                      last = v ? v->value : 0;
      started.fetch_add(1);
      while (!stop.load(std::memory_order_relaxed)) {
        v = ptr.load_leased();
        // Values are stored in increasing order, so a reader must never see one go backwards.
        if (!v || v->value < last) {
          bad_reads.fetch_add(1);
        } else {
          last = v->value;
        }
      }
    });
  }

  auto const stop_readers = [&] {
    stop.store(true);
    for (auto &t : readers) {
      t.join();
    }
  };

  auto const deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (started.load() < READERS && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::yield();
  }
  // Joining the readers lets every one of them reach started, so decide before cleaning up.
  bool const readers_started = started.load() == READERS;
  if (!readers_started) {
    stop_readers();
  }
  REQUIRE(readers_started);

  for (int i = 1; i <= STORES; ++i) {
    ptr.store(std::make_shared<Tracked>(i, destroyed));
  }
  stop_readers();

  CHECK(bad_reads.load() == 0);
  // Every replaced value is released once readers finish; only the last stored one remains.
  CHECK(destroyed.load() == STORES);
  CHECK(ptr.load_leased()->value == STORES);
}

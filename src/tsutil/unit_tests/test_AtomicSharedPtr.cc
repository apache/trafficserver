/** @file

  Unit tests for AtomicSharedPtr

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

#include "tsutil/AtomicSharedPtr.h"

#include <algorithm>
#include <atomic>
#include <memory>
#include <thread>
#include <vector>

namespace
{
struct Payload {
  static constexpr size_t VALUE_COUNT = 8;

  explicit Payload(int generation) : generation_(generation) { std::fill(std::begin(values_), std::end(values_), generation); }

  bool
  is_valid() const
  {
    return std::all_of(std::begin(values_), std::end(values_), [this](int value) { return value == generation_; });
  }

  int generation_ = 0;
  int values_[VALUE_COUNT];
};

struct ReaderState {
  std::atomic_flag should_start;
  std::atomic_flag should_stop;
  std::atomic<int> invalid_reads{0};
  std::atomic<int> read_count{0};
};

void
run_reader(AtomicSharedPtr<Payload> &ptr, ReaderState &state)
{
  state.should_start.wait(false, std::memory_order_acquire);

  while (!state.should_stop.test(std::memory_order_acquire)) {
    auto current = ptr.load(std::memory_order_acquire);
    if (current == nullptr || !current->is_valid()) {
      state.invalid_reads.fetch_add(1, std::memory_order_relaxed);
    }
    auto const reads = state.read_count.fetch_add(1, std::memory_order_release) + 1;
    if (reads == 1) {
      // Release start_readers() now that this reader has completed a read.
      state.read_count.notify_all();
    }
    if (reads % 64 == 0) {
      std::this_thread::yield();
    }
  }
}

std::vector<std::thread>
make_readers(int reader_count, AtomicSharedPtr<Payload> &ptr, ReaderState &state)
{
  std::vector<std::thread> readers;

  readers.reserve(reader_count);
  for (int i = 0; i < reader_count; ++i) {
    readers.emplace_back([&ptr, &state] { run_reader(ptr, state); });
  }
  return readers;
}

// Release the readers and block until one of them has read, so that the
// writer's swaps overlap with readers that are already loading the pointer.
void
start_readers(ReaderState &state)
{
  state.should_start.test_and_set(std::memory_order_release);
  state.should_start.notify_all();
  state.read_count.wait(0, std::memory_order_acquire);
}

void
stop_readers(ReaderState &state, std::vector<std::thread> &readers)
{
  state.should_stop.test_and_set(std::memory_order_release);
  for (auto &reader : readers) {
    if (reader.joinable()) {
      reader.join();
    }
  }
}
} // end anonymous namespace

TEST_CASE("AtomicSharedPtr load store exchange", "[libts][AtomicSharedPtr]")
{
  AtomicSharedPtr<int> ptr;

  CHECK(ptr.load() == nullptr);

  auto first = std::make_shared<int>(1);
  ptr.store(first);
  CHECK(ptr.load() == first);
  CHECK(*ptr.load() == 1);

  auto second   = std::make_shared<int>(2);
  auto previous = ptr.exchange(second);
  CHECK(previous == first);
  CHECK(ptr.load() == second);
  CHECK(*ptr.load() == 2);
}

TEST_CASE("AtomicSharedPtr supports concurrent readers during writer swaps", "[libts][AtomicSharedPtr]")
{
  static constexpr int READER_COUNT = 8;
  static constexpr int WRITE_COUNT  = 5000;

  AtomicSharedPtr<Payload> ptr{std::make_shared<Payload>(0)};
  ReaderState              state;
  auto                     readers = make_readers(READER_COUNT, ptr, state);

  start_readers(state);

  for (int generation = 1; generation <= WRITE_COUNT; ++generation) {
    ptr.store(std::make_shared<Payload>(generation), std::memory_order_release);
    if (generation % 64 == 0) {
      std::this_thread::yield();
    }
  }
  stop_readers(state, readers);

  CHECK(state.invalid_reads.load() == 0);
  CHECK(state.read_count.load() > 0);
  REQUIRE(ptr.load() != nullptr);
  CHECK(ptr.load()->generation_ == WRITE_COUNT);
}

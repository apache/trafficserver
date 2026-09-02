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
#include <chrono>
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
  std::atomic<bool> should_start{false};
  std::atomic<bool> should_stop{false};
  std::atomic<int>  invalid_reads{0};
  std::atomic<int>  read_count{0};
};

void
run_reader(AtomicSharedPtr<Payload> &ptr, ReaderState &state)
{
  while (!state.should_start.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }

  while (!state.should_stop.load(std::memory_order_acquire)) {
    auto current = ptr.load(std::memory_order_acquire);
    if (current == nullptr || !current->is_valid()) {
      state.invalid_reads.fetch_add(1, std::memory_order_relaxed);
    }
    auto const reads = state.read_count.fetch_add(1, std::memory_order_release) + 1;
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

bool
wait_for_reader(const ReaderState &state, std::chrono::steady_clock::duration timeout)
{
  auto const deadline = std::chrono::steady_clock::now() + timeout;

  while (std::chrono::steady_clock::now() < deadline) {
    if (state.read_count.load(std::memory_order_acquire) > 0) {
      return true;
    }
    std::this_thread::yield();
  }
  return false;
}

void
stop_readers(ReaderState &state, std::vector<std::thread> &readers)
{
  state.should_stop.store(true, std::memory_order_release);
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

  state.should_start.store(true, std::memory_order_release);
  auto const reader_started = wait_for_reader(state, std::chrono::seconds(5));
  if (!reader_started) {
    stop_readers(state, readers);
  }
  REQUIRE(reader_started);

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

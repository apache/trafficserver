/** @file

  End-to-end test of the Clang thread-safety annotation chain.

  This translation unit is compiled with -Wthread-safety -Werror=thread-safety
  under Clang (see CMakeLists.txt), so it doubles as a compile-time assertion:
  the annotated example below must analyze cleanly, and any access to the
  protected data outside a held guard would fail the build.

  The chain demonstrated here is:
    * ts::mutex / ts::shared_mutex are capabilities         (TsMutex.h,
                                                            TsSharedMutex.h),
    * ts::scoped_lock / ts::scoped_writer_lock /
      ts::scoped_reader_lock supply the held capability for
      the duration of a scope                              (TsMutex.h,
                                                            TsSharedMutex.h),
    * the data members below are annotated as protected by that capability.

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

#include "tsutil/TsMutex.h"
#include "tsutil/TsSharedMutex.h"

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <string>
#include <thread>
#include <vector>

namespace
{
class ProtectedTable
{
public:
  void
  put(const std::string &key, int value)
  {
    ts::scoped_writer_lock lock(_mutex);
    _map[key] = value; // write under exclusive lock
    bump_locked();     // calls a TS_REQUIRES helper while the lock is held
  }

  int
  get(const std::string &key)
  {
    ts::scoped_reader_lock lock(_mutex);
    auto                   it = _map.find(key); // read under shared lock
    return it == _map.end() ? -1 : it->second;
  }

  int
  writes() const
  {
    ts::scoped_reader_lock lock(_mutex);
    return _writes;
  }

private:
  // The compiler proves the caller holds _mutex; no runtime witness needed.
  void
  bump_locked() TS_REQUIRES(_mutex)
  {
    ++_writes;
  }

  mutable ts::shared_mutex        _mutex;
  std::map<std::string, int> _map TS_GUARDED_BY(_mutex);
  int _writes                     TS_GUARDED_BY(_mutex) = 0;
};

// The plain-mutex counterpart: ts::mutex guarded data taken through
// ts::scoped_lock.
class ProtectedCounter
{
public:
  void
  add(int n)
  {
    ts::scoped_lock lock(_mutex);
    _sum += n;     // write under exclusive lock
    bump_locked(); // calls a TS_REQUIRES helper while the lock is held
  }

  int
  sum() const
  {
    ts::scoped_lock lock(_mutex);
    return _sum;
  }

  int
  adds() const
  {
    ts::scoped_lock lock(_mutex);
    return _adds;
  }

private:
  void
  bump_locked() TS_REQUIRES(_mutex)
  {
    ++_adds;
  }

  mutable ts::mutex _mutex;
  int _sum          TS_GUARDED_BY(_mutex) = 0;
  int _adds         TS_GUARDED_BY(_mutex) = 0;
};

// Counterexample (kept as documentation): adding an unguarded accessor such as
//   int peek() const { return _writes; }
// to ProtectedTable makes this file fail to compile under -Werror=thread-safety,
// with: "reading variable '_writes' requires holding shared_mutex '_mutex'".
// That compile-time failure is the whole point.
} // namespace

TEST_CASE("ts::shared_mutex annotated guards provide mutual exclusion", "[thread_safety]")
{
  ProtectedTable table;

  constexpr int n_threads  = 8;
  constexpr int per_thread = 1000;

  std::vector<std::thread> threads;
  threads.reserve(n_threads);
  for (int t = 0; t < n_threads; ++t) {
    threads.emplace_back([&table, t]() {
      const std::string key = "k" + std::to_string(t);
      for (int i = 0; i < per_thread; ++i) {
        table.put(key, i);
        (void)table.get(key);
      }
    });
  }
  for (auto &th : threads) {
    th.join();
  }

  REQUIRE(table.writes() == n_threads * per_thread);
}

TEST_CASE("ts::mutex annotated guard provides mutual exclusion", "[thread_safety]")
{
  ProtectedCounter counter;

  constexpr int n_threads  = 8;
  constexpr int per_thread = 1000;

  std::vector<std::thread> threads;
  threads.reserve(n_threads);
  for (int t = 0; t < n_threads; ++t) {
    threads.emplace_back([&counter]() {
      for (int i = 0; i < per_thread; ++i) {
        counter.add(1);
      }
    });
  }
  for (auto &th : threads) {
    th.join();
  }

  REQUIRE(counter.sum() == n_threads * per_thread);
  REQUIRE(counter.adds() == n_threads * per_thread);
}

/** @file

  Micro-benchmarks for THREAD_ALLOC / THREAD_FREE round-trips.

  Establishes the SC-007 baseline for the inkevent design cleanup
  (feature 007). The Thread::cache_for<T>() introduction (US4) must
  show ±2% against this benchmark since the constant-index table
  read replaces the named-field access on the hot allocation path.

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

#define CATCH_CONFIG_ENABLE_BENCHMARKING

#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

#include <memory>
#include <vector>

#include "iocore/eventsystem/Thread.h"
#include "iocore/eventsystem/ProxyAllocator.h"
#include "tscore/Allocator.h"

namespace
{
class BThread : public Thread
{
public:
  void
  set_specific() override
  {
    Thread::set_specific();
  }

  void
  execute() override
  {
  }
};

struct BItem {
  char buffer[128];
};
} // namespace

// THREAD_ALLOC/FREE expects a global Allocator named to match a
// ProxyAllocator field on Thread (today: ioAllocator).
ClassAllocator<BItem, false> ioAllocator("io_bench");

TEST_CASE("THREAD_ALLOC round-trip", "[iocore][bench][thread_alloc]")
{
  auto bench_thread = std::make_unique<BThread>();
  bench_thread->set_specific();

  // Three working set sizes: cache-hot (fits in the freelist
  // watermark), watermark-edge (forces freeup), and large (exercises
  // global-allocator fallback).
  for (int count : {64, 1024, 8192}) {
    thread_freelist_high_watermark = count + 1;

    char name[64];
    std::snprintf(name, sizeof(name), "alloc/free count=%d", count);

    BENCHMARK(name)
    {
      std::vector<BItem *> items;
      items.reserve(count);
      for (int i = 0; i < count; ++i) {
        items.push_back(THREAD_ALLOC(ioAllocator, this_thread()));
      }
      for (auto *item : items) {
        THREAD_FREE(item, ioAllocator, this_thread());
      }
      return bench_thread->ioAllocator.allocated;
    };
  }
}

/** @file

Simple benchmark for ProxyAllocator

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
#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "I_EventSystem.h"
#include "I_Thread.h"
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

// THREAD_ALLOC/FREE requires allocators be global variables and are named after one of the defined ProxyAllocator members
ClassAllocator<BItem> ioAllocator("io");

#define OLD_THREAD_FREE(_p, _a, _t)                                                                \
  do {                                                                                             \
    ::_a.destroy_if_enabled(_p);                                                                   \
    if (!cmd_disable_pfreelist) {                                                                  \
      *(char **)_p    = (char *)_t->_a.freelist;                                                   \
      _t->_a.freelist = _p;                                                                        \
      _t->_a.allocated++;                                                                          \
      if (thread_freelist_high_watermark > 0 && _t->_a.allocated > thread_freelist_high_watermark) \
        thread_freeup(::_a.raw(), _t->_a);                                                         \
    } else {                                                                                       \
      ::_a.raw().free_void(_p);                                                                    \
    }                                                                                              \
  } while (0)

TEST_CASE("ProxyAllocator", "[iocore]")
{
  Thread *bench_thread = new BThread();
  bench_thread->set_specific();
  int count = 10000;

  // set higher than iteration count so the freeup doesn't run during benchmark
  thread_freelist_high_watermark = count + 1;

  BENCHMARK("thread_free old")
  {
    auto items = std::vector<BItem *>();
    items.reserve(count);
    for (int i = 0; i < count; i++) {
      auto *item = THREAD_ALLOC(ioAllocator, this_thread());
      items.push_back(item);
    }

    for (auto item : items) {
      OLD_THREAD_FREE(item, ioAllocator, this_thread());
    }
    return bench_thread->ioAllocator.allocated;
  };

  BENCHMARK("thread_free new")
  {
    auto items = std::vector<BItem *>();
    items.reserve(count);
    for (int i = 0; i < count; i++) {
      auto *item = THREAD_ALLOC(ioAllocator, this_thread());
      items.push_back(item);
    }

    for (auto item : items) {
      THREAD_FREE(item, ioAllocator, this_thread());
    }
    return bench_thread->ioAllocator.allocated;
  };

  delete bench_thread;
}

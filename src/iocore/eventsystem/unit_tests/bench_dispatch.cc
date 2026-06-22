/** @file

  Micro-benchmarks for Continuation::handleEvent dispatch.

  Establishes the SC-007 baseline reference for the inkevent design
  cleanup (feature 007). Subsequent PRs that touch the dispatch path
  must show ±2% against this benchmark.

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

#include <cstdio>
#include <memory>
#include <vector>

#include "iocore/eventsystem/Continuation.h"
#include "iocore/eventsystem/EThread.h"
#include "iocore/eventsystem/Lock.h"

namespace
{
struct CounterCont : public Continuation {
  int count = 0;

  CounterCont() : Continuation(static_cast<ProxyMutex *>(nullptr)) { SET_HANDLER(&CounterCont::handle); }

  int
  handle(int /* event ATS_UNUSED */, void * /* data ATS_UNUSED */)
  {
    ++count;
    return 0;
  }
};
} // namespace

TEST_CASE("Continuation::handleEvent dispatch", "[iocore][bench][dispatch]")
{
  // Realistic queue depths: we want both the cache-hot (small) and
  // cache-cold (large) cases. The cleanup PRs that touch dispatch
  // (Scheduler split, Continuation re-parenting, Event encapsulation)
  // can shift either curve; we keep both as the SC-007 reference.
  for (int depth : {1, 64, 1024}) {
    std::vector<std::unique_ptr<CounterCont>> conts;
    conts.reserve(depth);
    for (int i = 0; i < depth; ++i) {
      conts.emplace_back(std::make_unique<CounterCont>());
    }

    char name[64];
    std::snprintf(name, sizeof(name), "handleEvent depth=%d", depth);

    BENCHMARK(name)
    {
      int sum = 0;
      for (auto &c : conts) {
        sum += c->handleEvent(0, nullptr);
      }
      return sum;
    };
  }
}

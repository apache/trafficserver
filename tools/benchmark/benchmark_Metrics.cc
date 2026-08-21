/** @file

  Micro benchmark tool for ts::Metrics

  The metric values are lock free atomics, but reaching one from an id is not free, and the read
  paths that get there have very different costs. Four cases, all scaled by thread count because
  contention is the interesting axis:

    increment(id)     what TSStatIntIncrement does: valid() then lookup(id) then fetch_add
    increment(ptr)    what core, cripts and a few plugins do: a bare fetch_add on a cached pointer
    lookup(id)        the lock free id resolution on its own
    lookup(name)      the same resolution by name, which still takes Storage's mutex

  increment(id) against increment(ptr) is the cost a plugin pays for having only an id. lookup(id)
  against lookup(name) isolates the mutex: the two do comparable work, so a gap that widens with
  thread count is contention rather than instructions.

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
#include <catch2/catch_session.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

#include "tsutil/Metrics.h"

#include <atomic>
#include <string>
#include <thread>
#include <vector>

using ts::Metrics;

namespace
{
// Args
struct Conf {
  int nthreads = 1;
  int nops     = 1000;
  int nmetrics = 64;
};

Conf conf;

/// The metrics every case operates on, created once so no case pays for creation.
struct Fixture {
  std::vector<Metrics::IdType>                ids;
  std::vector<Metrics::Counter::AtomicType *> ptrs;
  std::vector<std::string>                    names;

  Fixture()
  {
    auto &m = Metrics::instance();

    ids.reserve(conf.nmetrics);
    ptrs.reserve(conf.nmetrics);
    names.reserve(conf.nmetrics);

    for (int i = 0; i < conf.nmetrics; ++i) {
      names.push_back("benchmark.metrics." + std::to_string(i));

      // createPtr is what core and cripts do: create once at init and keep the pointer. It also
      // registers the name, so the id and name lookups below resolve to the same metric.
      ptrs.push_back(Metrics::Counter::createPtr(names.back()));
      ids.push_back(m.lookup(names.back()));
    }
  }
};

Fixture *fixture = nullptr;

/** Run @a op on every thread, @c nops times each, and return a value derived from the results.
 *
 * The return value exists so nothing can be optimized away; it is not meaningful.
 */
template <typename F>
int64_t
run(F &&op)
{
  std::vector<std::thread> threads;
  std::atomic<int64_t>     sink{0};

  threads.reserve(conf.nthreads);

  for (int t = 0; t < conf.nthreads; ++t) {
    threads.emplace_back([t, &sink, &op]() {
      int64_t local = 0;

      for (int i = 0; i < conf.nops; ++i) {
        // Stride the starting point per thread so they are not all hammering one metric, which
        // would measure cacheline ping-pong on that one atomic rather than the lookup path.
        local += op((t + i) % conf.nmetrics);
      }
      sink.fetch_add(local, std::memory_order_relaxed);
    });
  }

  for (auto &th : threads) {
    th.join();
  }

  return sink.load();
}

} // namespace

TEST_CASE("Micro benchmark of ts::Metrics", "")
{
  auto &m = Metrics::instance();

  SECTION("increment by id")
  {
    // The TSStatIntIncrement path: validation, then id resolution, then the add.
    BENCHMARK("increment(id)")
    {
      return run([&m](int i) -> int64_t {
        auto id = fixture->ids[i];

        return m.valid(id) ? m.increment(id, 1) : 0;
      });
    };
  }

  SECTION("increment by cached pointer")
  {
    // What core does. This is the floor: no resolution at all.
    BENCHMARK("increment(ptr)")
    {
      return run([](int i) -> int64_t {
        Metrics::Counter::increment(fixture->ptrs[i], 1);

        return 1;
      });
    };
  }

  SECTION("lookup by id")
  {
    // Lock free resolution.
    BENCHMARK("lookup(id)")
    {
      return run([&m](int i) -> int64_t { return m.lookup(fixture->ids[i]) != nullptr; });
    };
  }

  SECTION("lookup by name")
  {
    // The same resolution, but through the mutex guarded name map.
    BENCHMARK("lookup(name)")
    {
      return run([&m](int i) -> int64_t { return m.lookup(fixture->names[i]) != Metrics::NOT_FOUND; });
    };
  }
}

int
main(int argc, char *argv[])
{
  Catch::Session session;

  using namespace Catch::Clara;

  // clang-format off
  auto cli = session.cli() |
    Opt(conf.nthreads, "")["--ts-nthreads"]("number of threads (default: 1)") |
    Opt(conf.nops, "")["--ts-nops"]("operations per thread per run (default: 1000)") |
    Opt(conf.nmetrics, "")["--ts-nmetrics"]("distinct metrics to spread across (default: 64)");
  // clang-format on

  session.cli(cli);

  int returnCode = session.applyCommandLine(argc, argv);
  if (returnCode != 0) {
    return returnCode;
  }

  Fixture f;
  fixture = &f;

  return session.run();
}

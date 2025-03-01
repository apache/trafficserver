/** @file

  Micro Benchmark tool for shared_mutex - requires Catch2 v2.9.0+

  - e.g. example of running 64 threads with read/write rate is 100:1
  ```
  $ taskset -c 0-63 ./benchmark_shared_mutex --ts-nthreads 64 --ts-nloop 1000 --ts-nread 100 --ts-nwrite 1
  ```

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
#define CATCH_CONFIG_RUNNER

#include "catch.hpp"

#include "tsutil/Bravo.h"

#include <mutex>
#include <shared_mutex>
#include <thread>

namespace
{
// Args
struct Conf {
  int nloop    = 1;
  int nthreads = 1;
  int nread    = 1;
  int nwrite   = 1;
};

Conf conf;
template <typename T, typename S>
int
run(T &mutex)
{
  std::thread list[conf.nthreads];
  int         counter = 0;

  for (int i = 0; i < conf.nthreads; i++) {
    new (&list[i]) std::thread{[&counter](T &mutex) {
                                 int c = 0;
                                 for (int j = 0; j < conf.nloop; ++j) {
                                   // reader
                                   for (int i = 0; i < conf.nread; ++i) {
                                     S lock(mutex);
                                     // Do not optimize
                                     c = counter;
                                   }

                                   // writer
                                   for (int i = 0; i < conf.nwrite; ++i) {
                                     std::lock_guard lock(mutex);
                                     // Do not optimize
                                     ++c;
                                     counter = c;
                                   }
                                 }
                               },
                               std::ref(mutex)};
  }

  for (int i = 0; i < conf.nthreads; i++) {
    list[i].join();
  }

  return counter;
}

} // namespace

TEST_CASE("Micro benchmark of shared_mutex", "")
{
  SECTION("std::shared_mutex")
  {
    BENCHMARK("std::shared_mutex")
    {
      std::shared_mutex mutex;

      return run<std::shared_mutex, std::shared_lock<std::shared_mutex>>(mutex);
    };
  }

  SECTION("ts::bravo::shared_mutex")
  {
    BENCHMARK("ts::bravo::shared_mutex")
    {
      ts::bravo::shared_mutex mutex;

      return run<ts::bravo::shared_mutex, ts::bravo::shared_lock<ts::bravo::shared_mutex>>(mutex);
    };
  }
}

int
main(int argc, char *argv[])
{
  Catch::Session session;

  using namespace Catch::clara;

  // clang-format off
  auto cli = session.cli() |
    Opt(conf.nthreads, "")["--ts-nthreads"]("number of threads (default: 1)") |
    Opt(conf.nread, "")["--ts-nread"]("number of read op (default: 1)") |
    Opt(conf.nwrite, "")["--ts-nwrite"]("number of write op (default: 1)") |
    Opt(conf.nloop, "")["--ts-nloop"]("number of read-write loop (default: 1)");
  // clang-format on

  session.cli(cli);

  int returnCode = session.applyCommandLine(argc, argv);
  if (returnCode != 0) {
    return returnCode;
  }

  return session.run();
}

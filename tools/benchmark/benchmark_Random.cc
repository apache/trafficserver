
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

#include "tscore/ink_rand.h"
#include <cmath>
#include <limits>
#define CATCH_CONFIG_ENABLE_BENCHMARKING
#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/generators/catch_generators_random.hpp>

#include "tscore/Random.h"

TEST_CASE("BenchRandom", "[bench][random]")
{
  InkRand gen(42);
  ts::Random::seed(13);

  BENCHMARK("IncRand")
  {
    return gen.random();
  };

  BENCHMARK("ts::Random")
  {
    return ts::Random::random();
  };

  std::mt19937_64 mt;
  BENCHMARK("std::mt19937_64")
  {
    return mt();
  };

  std::ranlux48_base rb;
  BENCHMARK("std::ranlux48_base")
  {
    return rb();
  };

  std::ranlux24_base rb24;
  BENCHMARK("std::ranlux24_base")
  {
    return rb24();
  };

  std::uniform_int_distribution<uint64_t> mtdist{0, UINT64_MAX};

  BENCHMARK("std::uniform_int_distribution")
  {
    return mtdist(mt);
  };
}

TEST_CASE("RandomDistribution", "[random][distribution]")
{
  auto          g = Catch::Generators::random(std::numeric_limits<uint64_t>::min(), std::numeric_limits<uint64_t>::max());
  InkRand       gen(g.get());
  int           iterations      = 1000000;
  constexpr int buckets         = 100;
  int           counts[buckets] = {0};

  for (int i = 0; i < iterations; i++) {
    counts[gen.random() % buckets]++;
  }

  double expected = static_cast<double>(iterations) / static_cast<double>(buckets);

  double min = std::numeric_limits<double>::max();
  double max = std::numeric_limits<double>::min();

  for (int count : counts) {
    double ratio = static_cast<double>(count) / expected;

    min = std::min(ratio, min);
    max = std::max(ratio, max);
  }
  REQUIRE(0.95 < min);
  REQUIRE(max < 1.05);
}

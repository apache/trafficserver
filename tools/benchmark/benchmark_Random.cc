
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
#define CATCH_CONFIG_ENABLE_BENCHMARKING
#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

#include "tscore/Random.h"

TEST_CASE("BenchRandom", "[bench][random]")
{
  InkRand gen(42);
  ts::Random::seed(13);
  int iterations = 1000000;

  BENCHMARK("IncRand")
  {
    uint64_t sum = 0;
    for (int i = 0; i < iterations; i++) {
      sum += gen.random();
    }
    return sum;
  };

  BENCHMARK("ts::Random")
  {
    uint64_t sum = 0;
    for (int i = 0; i < iterations; i++) {
      sum += ts::Random::random();
    }
    return sum;
  };

  std::mt19937_64 mt;
  BENCHMARK("std::mt19937_64")
  {
    uint64_t sum = 0;
    for (int i = 0; i < iterations; i++) {
      sum += mt();
    }
    return sum;
  };

  std::ranlux48_base rb;
  BENCHMARK("std::ranlux48_base")
  {
    uint64_t sum = 0;
    for (int i = 0; i < iterations; i++) {
      sum += rb();
    }
    return sum;
  };

  std::ranlux24_base rb24;
  BENCHMARK("std::ranlux24_base")
  {
    uint64_t sum = 0;
    for (int i = 0; i < iterations; i++) {
      sum += rb24();
    }
    return sum;
  };

  std::uniform_int_distribution<uint64_t> mtdist{0, UINT64_MAX};

  BENCHMARK("std::uniform_int_distribution")
  {
    uint64_t sum = 0;
    for (int i = 0; i < iterations; i++) {
      sum += mtdist(mt);
    }
    return sum;
  };
}

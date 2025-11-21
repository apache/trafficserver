/** @file

  Pseudorandom Number Generator test

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
#include "tscore/Random.h"
#include "tscore/ink_rand.h"
#include <iostream>

TEST_CASE("test random", "[libts][random]")
{
  ts::Random::seed(13);
  InkRand x(13);

  for (auto i = 0; i < 1000000; ++i) {
    REQUIRE(ts::Random::random() == x.random());
    REQUIRE(uint64_t(ts::Random::drandom() * 100000000) == uint64_t(x.drandom() * 100000000)); // they are pretty close to the same
  }

  auto start = std::chrono::high_resolution_clock::now();
  for (auto i = 0; i < 1000000; ++i) {
    ts::Random::random();
  }
  auto diff = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now() - start);
  std::cout << std::endl << static_cast<double>(diff.count()) / 1000000 << " ns per Random::random()" << std::endl;

  start = std::chrono::high_resolution_clock::now();
  for (auto i = 0; i < 1000000; ++i) {
    x.random();
  }
  diff = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now() - start);
  std::cout << static_cast<double>(diff.count()) / 1000000 << " ns per InkRand::random()" << std::endl;
}

TEST_CASE("test random reseeding", "[libts][random]")
{
  // Test that reseeding produces deterministic sequences
  // This verifies that distribution reset() is called properly in seed()

  // Generate first sequence
  ts::Random::seed(42);
  uint64_t first_int    = ts::Random::random();
  double   first_double = ts::Random::drandom();

  // Generate some values to potentially populate distribution cache
  for (int i = 0; i < 100; ++i) {
    ts::Random::random();
    ts::Random::drandom();
  }

  // Reseed with same value - should reset distributions and produce identical sequence
  ts::Random::seed(42);
  uint64_t second_int    = ts::Random::random();
  double   second_double = ts::Random::drandom();

  REQUIRE(first_int == second_int);
  REQUIRE(first_double == second_double);

  // Verify this works with InkRand too for consistency
  InkRand ink1(42);
  InkRand ink2(42);

  REQUIRE(ink1.random() == ink2.random());
  REQUIRE(ink1.drandom() == ink2.drandom());

  // And that ts::Random matches InkRand after reseeding
  ts::Random::seed(42);
  InkRand ink3(42);

  REQUIRE(ts::Random::random() == ink3.random());
}

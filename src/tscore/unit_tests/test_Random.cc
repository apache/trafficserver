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

#include "catch.hpp"
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
  std::cout << std::endl << (double)diff.count() / 1000000 << " ns per Random::random()" << std::endl;

  start = std::chrono::high_resolution_clock::now();
  for (auto i = 0; i < 1000000; ++i) {
    x.random();
  }
  diff = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now() - start);
  std::cout << (double)diff.count() / 1000000 << " ns per InkRand::random()" << std::endl;
}

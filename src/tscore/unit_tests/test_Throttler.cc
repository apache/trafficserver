/**
  @file Test for Regex.cc

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

#include "tscore/Throttler.h"
#include "catch.hpp"

#include <chrono>
#include <thread>

using namespace std::literals;

TEST_CASE("Throttler", "[libts][Throttler]")
{
  auto const periodicity = 100ms;
  Throttler throttler(periodicity);
  uint64_t skipped_count = 0;

  // The first check should be allowed.
  CHECK_FALSE(throttler.is_throttled(skipped_count));

  // The first time this is called, none were skipped.
  CHECK(skipped_count == 0);

  // In rapid succession, do a few more that should be skipped.
  auto const expected_skip_count = 5u;
  for (auto i = 0u; i < expected_skip_count; ++i) {
    CHECK(throttler.is_throttled(skipped_count));
  }

  // Sleep more than enough time for the throttler to allow the following
  // check.
  std::this_thread::sleep_for(2 * periodicity);

  CHECK_FALSE(throttler.is_throttled(skipped_count));
  CHECK(skipped_count == expected_skip_count);
}

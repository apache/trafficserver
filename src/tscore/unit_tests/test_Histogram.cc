/** @file

    Histogram unit tests.

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

#include <sstream>
#include <catch.hpp>
#include "tscpp/util/Histogram.h"

// -------------
// --- TESTS ---
// -------------
TEST_CASE("Histogram Basic", "[libts][histogram]")
{
  ts::Histogram<7, 2> h;

  h(12);
  REQUIRE(h[10] == 1);

  REQUIRE(h.lower_bound(0) == 0);
  REQUIRE(h.lower_bound(3) == 3);
  REQUIRE(h.lower_bound(4) == 4);
  REQUIRE(h.lower_bound(8) == 8);
  REQUIRE(h.lower_bound(9) == 10);
  REQUIRE(h.lower_bound(12) == 16);
  REQUIRE(h.lower_bound(13) == 20);
  REQUIRE(h.lower_bound(16) == 32);
  REQUIRE(h.lower_bound(17) == 40);

  for (auto x : {0, 1, 4, 6, 19, 27, 36, 409, 16000, 1097}) {
    h(x);
  }
  REQUIRE(h[0] == 1);
  REQUIRE(h[1] == 1);
  REQUIRE(h[2] == 0);
  REQUIRE(h[12] == 1); // sample 19 shoud be here.
  REQUIRE(h[14] == 1); // sample 27 should be here.
};

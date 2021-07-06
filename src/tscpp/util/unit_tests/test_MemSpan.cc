/** @file

    MemSpan unit tests.

    @section license License

    Licensed to the Apache Software Foundation (ASF) under one or more contributor license
    agreements.  See the NOTICE file distributed with this work for additional information regarding
    copyright ownership.  The ASF licenses this file to you under the Apache License, Version 2.0
    (the "License"); you may not use this file except in compliance with the License.  You may
    obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software distributed under the
    License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
    express or implied. See the License for the specific language governing permissions and
    limitations under the License.
*/

#include <iostream>
#include "tscpp/util/MemSpan.h"
#include "catch.hpp"

using ts::MemSpan;

TEST_CASE("MemSpan", "[libswoc][MemSpan]")
{
  int32_t idx[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  char buff[1024];

  MemSpan<char> span(buff, sizeof(buff));
  MemSpan<char> left = span.prefix(512);
  REQUIRE(left.size() == 512);
  REQUIRE(span.size() == 1024);
  span.remove_prefix(512);
  REQUIRE(span.size() == 512);
  REQUIRE(left.end() == span.begin());

  left.assign(buff, sizeof(buff));
  span = left.suffix(768);
  left.remove_suffix(768);
  REQUIRE(left.end() == span.begin());
  REQUIRE(left.size() + span.size() == 1024);

  MemSpan<int32_t> idx_span(idx);
  REQUIRE(idx_span.count() == 11);
  REQUIRE(idx_span.size() == sizeof(idx));
  REQUIRE(idx_span.data() == idx);

  auto sp2 = idx_span.rebind<int16_t>();
  REQUIRE(sp2.size() == idx_span.size());
  REQUIRE(sp2.count() == 2 * idx_span.count());
  REQUIRE(sp2[0] == 0);
  REQUIRE(sp2[1] == 0);
  // exactly one of { le, be } must be true.
  bool le = sp2[2] == 1 && sp2[3] == 0;
  bool be = sp2[2] == 0 && sp2[3] == 1;
  REQUIRE(le != be);
  auto idx2 = sp2.rebind<int32_t>(); // still the same if converted back to original?
  REQUIRE(idx_span.is_same(idx2));

  // Verify attempts to rebind on non-integral sized arrays fails.
  span.assign(buff, 1022);
  REQUIRE(span.size() == 1022);
  REQUIRE(span.count() == 1022);
  auto vs = span.rebind<void>();
  REQUIRE_THROWS_AS(span.rebind<uint32_t>(), std::invalid_argument);
  REQUIRE_THROWS_AS(vs.rebind<uint32_t>(), std::invalid_argument);

  // Check for defaulting to a void rebind.
  vs = span.rebind();
  REQUIRE(vs.size() == 1022);

  // Check for assignment to void.
  vs = span;
  REQUIRE(vs.size() == 1022);

  // Test array constructors.
  MemSpan<char> a{buff};
  REQUIRE(a.size() == sizeof(buff));
  REQUIRE(a.data() == buff);
  float floats[] = {1.1, 2.2, 3.3, 4.4, 5.5};
  MemSpan<float> fspan{floats};
  REQUIRE(fspan.count() == 5);
  REQUIRE(fspan[3] == 4.4f);
  MemSpan<float> f2span{floats, floats + 5};
  REQUIRE(fspan.data() == f2span.data());
  REQUIRE(fspan.count() == f2span.count());
  REQUIRE(fspan.is_same(f2span));
};

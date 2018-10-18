/** @file

    TextView unit tests.

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

#include <iostream>
#include "tscpp/util/MemSpan.h"
#include "catch.hpp"

using ts::MemSpan;

TEST_CASE("MemSpan", "[libts][MemSpan]")
{
  int idx[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  char buff[1024];
  MemSpan span(buff, sizeof(buff));
  MemSpan left = span.prefix(512);
  REQUIRE(left.size() == 512);
  REQUIRE(span.size() == 1024);
  span.remove_prefix(512);
  REQUIRE(span.size() == 512);
  REQUIRE(left.data_end() == span.data());

  MemSpan idx_span(idx);
  REQUIRE(idx_span.size() == sizeof(idx));
  REQUIRE(idx_span.data() == idx);
  REQUIRE(idx_span.find<int>(4) == idx + 4);
  REQUIRE(idx_span.find<int>(8) == idx + 8);
  MemSpan a = idx_span.suffix(idx_span.find<int>(7));
  REQUIRE(a.at<int>(0) == 7);
  MemSpan b = idx_span.suffix(-(4 * sizeof(int)));
  REQUIRE(b.size() == 4 * sizeof(int));
  REQUIRE(b.at<int>(0) == 7);
  REQUIRE(a == b);
  MemSpan c = idx_span.prefix(3 * sizeof(int));
  REQUIRE(c.size() == 3 * sizeof(int));
  REQUIRE(c.ptr<int>(2) == idx + 2);
}

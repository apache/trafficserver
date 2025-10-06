/*
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

// This file adds multiple test cases for Pattern: compile, match, capture, replace.
#include <catch2/catch_test_macros.hpp>
#include "pattern.h"

TEST_CASE("Pattern compile and match behavior", "[cachekey][pattern]")
{
  SECTION("Simple literal match")
  {
    Pattern p;
    REQUIRE(p.init("hello"));
    CHECK(p.match("hello") == true);
    CHECK(p.match("hell") == false);
  }

  SECTION("Simple capture groups")
  {
    Pattern p;
    REQUIRE(p.init("^(\\w+)-(\\d+)$"));
    StringVector caps;
    CHECK(p.capture("item-123", caps));
    // capture returns all groups including group 0, so expect 3 entries (full + 2 groups)
    CHECK(caps.size() == 3);
    CHECK(caps[1] == "item");
    CHECK(caps[2] == "123");
  }

  SECTION("Replacement using tokens")
  {
    Pattern p;
    REQUIRE(p.init("^(\\w+)-(\\d+)$", "$2:$1", /*replace*/ true));
    String res;
    CHECK(p.replace("item-123", res));
    CHECK(res == "123:item");
  }

  SECTION("Invalid pattern fails to compile")
  {
    Pattern p;
    // malformed pattern (unclosed parentheses)
    CHECK(p.init("(unclosed") == false);
  }
}

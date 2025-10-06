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

  SECTION("Greedy vs non-greedy capture")
  {
    Pattern pg;
    Pattern png;
    REQUIRE(pg.init("a(.*)b"));   // greedy
    REQUIRE(png.init("a(.*?)b")); // non-greedy

    StringVector caps_g;
    StringVector caps_ng;
    REQUIRE(pg.capture("a123b456b", caps_g));
    REQUIRE(png.capture("a123b456b", caps_ng));

    // greedy should capture up to the last 'b'
    CHECK(caps_g.size() >= 2);
    CHECK(caps_g[1] == "123b456");

    // non-greedy should capture up to the first 'b'
    CHECK(caps_ng.size() >= 2);
    CHECK(caps_ng[1] == "123");
  }

  SECTION("Empty-string anchors")
  {
    Pattern p;
    REQUIRE(p.init("^$"));
    // Pattern::match uses PCRE_NOTEMPTY which prevents empty-string matches.
    // Therefore '^$' will NOT match an empty subject with the current implementation.
    CHECK(p.match("") == false);
    CHECK(p.match("not-empty") == false);
  }

  SECTION("Case-insensitive inline flag")
  {
    Pattern p;
    // PCRE inline flag for case-insensitive
    REQUIRE(p.init("(?i)AbC"));
    CHECK(p.match("aBc") == true);
    CHECK(p.match("ABC") == true);
  }

  SECTION("Repeated captures and empty captures")
  {
    Pattern p;
    REQUIRE(p.init("(\\w*)-(\\w*)"));
    StringVector caps;
    REQUIRE(p.capture("-foo", caps));
    CHECK(caps.size() == 3);
    // first group before '-' is empty
    CHECK(caps[1] == "");
    CHECK(caps[2] == "foo");
  }

  SECTION("Long subject match")
  {
    Pattern p;
    REQUIRE(p.init("^a+$"));
    // create a long string of 'a' characters
    std::string long_s(10000, 'a');
    CHECK(p.match(long_s.c_str()) == true);
  }
}

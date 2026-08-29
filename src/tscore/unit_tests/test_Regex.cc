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

#include <array>
#include <string_view>

#include "tscore/ink_assert.h"
#include "tscore/ink_defs.h"
#include "tscore/Regex.h"
#include "catch.hpp"

struct subject_match_t {
  std::string_view subject;
  bool match;
};

struct test_t {
  std::string_view regex;
  std::array<subject_match_t, 4> tests;
};

std::array<test_t, 2> test_data{{{{"^foo"}, {{{{"foo"}, true}, {{"bar"}, false}, {{"foobar"}, true}, {{"foobarbaz"}, true}}}},
                                 {{"foo$"}, {{{{"foo"}, true}, {{"bar"}, false}, {{"foobar"}, false}, {{"foobarbaz"}, false}}}}}};

TEST_CASE("Regex", "[libts][Regex]")
{
  for (auto &item : test_data) {
    Regex r;
    r.compile(item.regex.data());

    for (auto &test : item.tests) {
      REQUIRE(r.exec(test.subject.data()) == test.match);
    }
  }
}

// PCRE1 has no end-anchor compile option, so RE_ENDANCHORED is implemented by
// rewriting the pattern. These cases pin the parts of that rewrite that are easy
// to get wrong.
TEST_CASE("Regex end anchored", "[libts][Regex]")
{
  SECTION("both anchors require a whole subject match")
  {
    Regex r;

    REQUIRE(r.compile("cdn\\.example\\.com", RE_ANCHORED | RE_ENDANCHORED) == true);
    CHECK(r.exec(std::string_view{"cdn.example.com"}) == true);
    CHECK(r.exec(std::string_view{"cdn.example.com.test"}) == false);
    CHECK(r.exec(std::string_view{"prefix.cdn.example.com"}) == false);
  }

  SECTION("the end anchor applies to every branch of a top level alternation")
  {
    Regex r;

    REQUIRE(r.compile("foo|bar", RE_ANCHORED | RE_ENDANCHORED) == true);
    CHECK(r.exec(std::string_view{"foo"}) == true);
    CHECK(r.exec(std::string_view{"bar"}) == true);
    CHECK(r.exec(std::string_view{"foobaz"}) == false);
    CHECK(r.exec(std::string_view{"barbaz"}) == false);
  }

  SECTION("capture numbering is unchanged")
  {
    Regex r;
    std::array<int, Regex::DEFAULT_GROUP_COUNT * 3> ovector = {{0}};
    const std::string_view subject{"host.test"};

    REQUIRE(r.compile("([a-z]+)\\.([a-z]+)", RE_ANCHORED | RE_ENDANCHORED) == true);
    REQUIRE(r.get_capture_count() == 2);
    REQUIRE(r.exec(subject, ovector.data(), int(ovector.size())) == true);
    CHECK(subject.substr(ovector[2], ovector[3] - ovector[2]) == "host");
    CHECK(subject.substr(ovector[4], ovector[5] - ovector[4]) == "test");
  }

  SECTION("a lazy quantifier still matches the full subject")
  {
    Regex r;

    // The engine has to backtrack to satisfy the end anchor. Checking the match
    // span after the fact instead would reject this.
    REQUIRE(r.compile("a+?", RE_ANCHORED | RE_ENDANCHORED) == true);
    CHECK(r.exec(std::string_view{"aaa"}) == true);
  }

  SECTION("RE_ANCHORED alone still permits a trailing substring")
  {
    Regex r;

    REQUIRE(r.compile("cdn\\.example\\.com", RE_ANCHORED) == true);
    CHECK(r.exec(std::string_view{"cdn.example.com.test"}) == true);
  }
}

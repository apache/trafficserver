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

#include <string_view>
#include <vector>

#include "tscore/ink_assert.h"
#include "tscore/ink_defs.h"
#include "tsutil/Regex.h"
#include "catch.hpp"

struct subject_match_t {
  std::string_view subject;
  bool             match;
};

struct test_t {
  std::string_view             regex;
  std::vector<subject_match_t> tests;
};

std::vector<test_t> test_data{
  {
   {{R"(^foo)"}, {{{{"foo"}, true}, {{"bar"}, false}, {{"foobar"}, true}, {{"foobarbaz"}, true}}}},
   {{R"(foo$)"}, {{{{"foo"}, true}, {{"bar"}, false}, {{"foobar"}, false}, {{"foobarbaz"}, false}}}},
   // url regular expression
    {{R"(^(https?:\/\/)?([\da-z\.-]+)\.([a-z\.]{2,6})([\/\w \.-]*)*\/?$)"},
     {{{{"http://www.example.com"}, true},
       {{"https://www.example.com"}, true},
       {{"http://~example.com"}, false},
       {{"http://www.example.com/foo/bar"}, true}}}},
   // ip address regular expression
    {R"(^(?:(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.){3}(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$)",
     {{{{"1.2.3.4"}, true}, {{"127.0.0.1"}, true}, {{"256.256.256.256"}, false}, {{".1.1.1.1"}, false}}}},
   }
};

// test case insensitive test data
std::vector<test_t> test_data_case_insensitive{
  {
   {{R"(^foo)"}, {{{{"FoO"}, true}, {{"bar"}, false}, {{"foObar"}, true}, {{"foobaRbaz"}, true}}}},
   {{R"(foo$)"}, {{{{"foO"}, true}, {{"bar"}, false}, {{"foobar"}, false}, {{"foobarbaz"}, false}}}},
   }
};

// test case for anchored flag
std::vector<test_t> test_data_anchored{
  {
   {{R"(foo)"}, {{{{"foo"}, true}, {{"bar"}, false}, {{"foobar"}, true}, {{"foobarbaz"}, true}}}},
   {{R"(bar)"}, {{{{"foo"}, false}, {{"bar"}, true}, {{"foobar"}, false}, {{"foobarbaz"}, false}}}},
   }
};

struct submatch_t {
  std::string_view              subject;
  int32_t                       count;
  std::vector<std::string_view> submatches;
};

struct submatch_test_t {
  std::string_view        regex;
  int                     capture_count;
  std::vector<submatch_t> tests;
};

std::vector<submatch_test_t> submatch_test_data{
  {
   {{R"(^foo)"}, 0, {{{{"foo"}, 1, {{"foo"}}}, {{"bar"}, -1, {}}, {{"foobar"}, 1, {{"foo"}}}, {{"foobarbaz"}, 1, {{"foo"}}}}}},
   {{R"(foo$)"}, 0, {{{{"foo"}, 1, {{"foo"}}}, {{"bar"}, -1, {}}, {{"foobar"}, -1, {}}, {{"foobarbaz"}, -1, {}}}}},
   {{R"(^(foo)(bar))"}, 2, {{{{"foobar"}, 3, {{"foobar", "foo", "bar"}}}, {{"barfoo"}, -1, {}}, {{"foo"}, -1, {}}}}},
   }
};

TEST_CASE("Regex", "[libts][Regex]")
{
  // case sensitive test
  for (auto &item : test_data) {
    Regex r;
    REQUIRE(r.compile(item.regex.data()) == true);

    for (auto &test : item.tests) {
      REQUIRE(r.exec(test.subject.data()) == test.match);
    }
  }

  // case insensitive test
  for (auto &item : test_data_case_insensitive) {
    Regex r;
    REQUIRE(r.compile(item.regex.data(), RE_CASE_INSENSITIVE) == true);

    for (auto &test : item.tests) {
      REQUIRE(r.exec(test.subject.data()) == test.match);
    }
  }

  // case anchored test
  for (auto &item : test_data_anchored) {
    Regex r;
    REQUIRE(r.compile(item.regex.data(), RE_ANCHORED) == true);

    for (auto &test : item.tests) {
      REQUIRE(r.exec(test.subject.data()) == test.match);
    }
  }

  // test getting submatches with operator[]
  for (auto &item : submatch_test_data) {
    Regex r;
    REQUIRE(r.compile(item.regex.data()) == true);
    REQUIRE(r.get_capture_count() == item.capture_count);

    for (auto &test : item.tests) {
      RegexMatches matches;
      REQUIRE(r.exec(test.subject.data(), matches) == test.count);
      REQUIRE(matches.size() == test.count);

      for (int32_t i = 0; i < test.count; i++) {
        REQUIRE(matches[i] == test.submatches[i]);
      }
    }
  }

  // test getting submatches with ovector pointer
  for (auto &item : submatch_test_data) {
    Regex r;
    REQUIRE(r.compile(item.regex.data()) == true);
    REQUIRE(r.get_capture_count() == item.capture_count);

    for (auto &test : item.tests) {
      RegexMatches matches;
      REQUIRE(r.exec(test.subject.data(), matches) == test.count);
      REQUIRE(matches.size() == test.count);

      size_t *ovector = matches.get_ovector_pointer();
      for (int32_t i = 0; i < test.count; i++) {
        REQUIRE(test.submatches[i] == std::string_view{test.subject.data() + ovector[i * 2], ovector[i * 2 + 1] - ovector[i * 2]});
      }
    }
  }

  // test for invalid regular expression
  {
    Regex r;
    REQUIRE(r.compile(R"((\d+)", RE_CASE_INSENSITIVE) == false);
  }

  // test for not compiling regular expression
  {
    Regex        r;
    RegexMatches matches;
    REQUIRE(r.exec("foo") == false);
    REQUIRE(r.exec("foo", matches) == 0);
  }

  // test for recompiling the regular expression
  {
    Regex r;
    REQUIRE(r.compile(R"(foo)") == true);
    REQUIRE(r.exec("foo") == true);
    REQUIRE(r.compile(R"(bar)") == true);
    REQUIRE(r.exec("bar") == true);
  }

// test with matches set to 100, don't run the test in debug mode or the test will abort with a message to increase the buffer size
#ifndef DEBUG
  {
    Regex        r;
    RegexMatches matches(100);
    REQUIRE(r.compile(R"(foo)") == true);
    REQUIRE(r.exec("foo", matches) == 1);
  }
#endif
}

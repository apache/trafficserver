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

#include "ts/ink_assert.h"
#include "ts/ink_defs.h"
#include "ts/Regex.h"
#include "catch.hpp"

typedef struct {
  std::string_view subject;
  bool match;
} subject_match_t;

typedef struct {
  std::string_view regex;
  std::array<subject_match_t, 4> tests;
} test_t;

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

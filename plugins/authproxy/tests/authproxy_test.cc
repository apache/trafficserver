/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <string_view>
#define CATCH_CONFIG_MAIN /* include main function */
#include <catch.hpp>      /* catch unit-test framework */
#include "../utils.h"

using std::string_view;
TEST_CASE("Util methods", "[authproxy][utility]")
{
  SECTION("ContainsPrefix()")
  {
    CHECK(ContainsPrefix(string_view{"abcdef"}, "abc") == true);
    CHECK(ContainsPrefix(string_view{"abc"}, "abcdef") == false);
    CHECK(ContainsPrefix(string_view{"abcdef"}, "abd") == false);
    CHECK(ContainsPrefix(string_view{"abc"}, "abc") == true);
    CHECK(ContainsPrefix(string_view{""}, "") == true);
    CHECK(ContainsPrefix(string_view{"abc"}, "") == true);
    CHECK(ContainsPrefix(string_view{""}, "abc") == false);
    CHECK(ContainsPrefix(string_view{"abcdef"}, "abc\0") == true);
    CHECK(ContainsPrefix(string_view{"abcdef\0"}, "abc\0") == true);
  }
}

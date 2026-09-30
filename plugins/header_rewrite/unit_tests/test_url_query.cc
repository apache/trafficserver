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
#include <catch2/catch_test_macros.hpp>

#include "url_query.h"

TEST_CASE("sort_query orders params by name", "[header_rewrite][url_query]")
{
  SECTION("out-of-order params are sorted")
  {
    CHECK(sort_query("b=2&a=1") == "a=1&b=2");
  }

  SECTION("valueless params sort by their own name")
  {
    CHECK(sort_query("b&a=1") == "a=1&b");
  }

  SECTION("params with duplicate names keep their relative order")
  {
    CHECK(sort_query("x=1&a=2&x=3") == "a=2&x=1&x=3");
  }

  SECTION("empty query stays empty")
  {
    CHECK(sort_query("") == "");
  }

  SECTION("single param is unaffected")
  {
    CHECK(sort_query("a=1") == "a=1");
  }

  SECTION("trailing '&' produces a clean drop, not an empty trailing token")
  {
    CHECK(sort_query("a=1&") == "a=1");
  }

  SECTION("param value containing '=' is preserved and sorts by its name only")
  {
    CHECK(sort_query("a=1=2") == "a=1=2");
  }

  SECTION("leading '&' produces a clean drop, not an empty leading token")
  {
    CHECK(sort_query("&a=1") == "a=1");
  }

  SECTION("consecutive '&'s produce a clean drop, not empty middle tokens")
  {
    CHECK(sort_query("b=2&&a=1&&c=3") == "a=1&b=2&c=3");
  }

  SECTION("empty params are dropped even between params with an empty name")
  {
    CHECK(sort_query("=x&&=y") == "=x&=y");
  }
}

TEST_CASE("is_query_sorted detects queries sort_query would leave unchanged", "[header_rewrite][url_query]")
{
  SECTION("params in name order are sorted")
  {
    CHECK(is_query_sorted("a=1&b=2&c=3"));
  }

  SECTION("out-of-order params are not sorted")
  {
    CHECK_FALSE(is_query_sorted("b=2&a=1"));
  }

  SECTION("duplicate names in any value order are sorted, since the sort is stable")
  {
    CHECK(is_query_sorted("a=2&x=3&x=1"));
  }

  SECTION("empty query and single param are sorted")
  {
    CHECK(is_query_sorted(""));
    CHECK(is_query_sorted("a=1"));
  }

  SECTION("empty params are not sorted, since sort_query drops them")
  {
    CHECK_FALSE(is_query_sorted("&a=1"));
    CHECK_FALSE(is_query_sorted("a=1&"));
    CHECK_FALSE(is_query_sorted("a=1&&b=2"));
    CHECK_FALSE(is_query_sorted("&"));
  }

  SECTION("agrees with sort_query on every edge case")
  {
    for (std::string_view q :
         {"",      "&",           "&&",          "a",     "a=1",    "a=1&",   "&a=1",     "a=1&&b=2", "b=2&a=1", "a=1&b=2",
          "b&a=1", "x=1&a=2&x=3", "a=2&x=3&x=1", "a=1=2", "=x&a=1", "a=1&=x", "a1=x&a=x", "a=x&a1=x", "=x&=y",   "=x&&=y"}) {
      INFO("query: \"" << q << "\"");
      CHECK(is_query_sorted(q) == (sort_query(q) == q));
    }
  }
}

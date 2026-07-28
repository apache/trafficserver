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
}

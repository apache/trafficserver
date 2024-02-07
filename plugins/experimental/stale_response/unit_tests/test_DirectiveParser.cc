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

#include "DirectiveParser.h"

#include <catch.hpp>

TEST_CASE("DirectiveParser Constructor")
{
  SECTION("Default constructor")
  {
    DirectiveParser parser;
    REQUIRE(parser.get_max_age() == -1);
    REQUIRE(parser.get_stale_while_revalidate() == -1);
    REQUIRE(parser.get_stale_if_error() == -1);
  }
  SECTION("max-age")
  {
    DirectiveParser parser{"max-age=123"};
    REQUIRE(parser.get_max_age() == 123);
    REQUIRE(parser.get_stale_while_revalidate() == -1);
    REQUIRE(parser.get_stale_if_error() == -1);
  }
  SECTION("stale-while-revalidate")
  {
    DirectiveParser parser{"stale-while-revalidate=123"};
    REQUIRE(parser.get_max_age() == -1);
    REQUIRE(parser.get_stale_while_revalidate() == 123);
    REQUIRE(parser.get_stale_if_error() == -1);
  }
  SECTION("stale-if-error")
  {
    DirectiveParser parser{"stale-if-error=123"};
    REQUIRE(parser.get_max_age() == -1);
    REQUIRE(parser.get_stale_while_revalidate() == -1);
    REQUIRE(parser.get_stale_if_error() == 123);
  }
  SECTION("other")
  {
    DirectiveParser parser{"s-maxage=123"};
    REQUIRE(parser.get_max_age() == -1);
    REQUIRE(parser.get_stale_while_revalidate() == -1);
    REQUIRE(parser.get_stale_if_error() == -1);
  }
  SECTION("multiple")
  {
    DirectiveParser parser{"max-age=123, stale-while-revalidate=456, stale-if-error=789"};
    REQUIRE(parser.get_max_age() == 123);
    REQUIRE(parser.get_stale_while_revalidate() == 456);
    REQUIRE(parser.get_stale_if_error() == 789);
  }
  SECTION("multiple with noise")
  {
    DirectiveParser parser{"max-age=123, s-maxage=456, stale-while-revalidate=789, must-understand, stale-if-error=012, public"};
    REQUIRE(parser.get_max_age() == 123);
    REQUIRE(parser.get_stale_while_revalidate() == 789);
    REQUIRE(parser.get_stale_if_error() == 012);
  }
  SECTION("without commas")
  {
    DirectiveParser parser{"max-age=123 s-maxage=456 stale-while-revalidate=789 must-understand stale-if-error=012 public"};
    REQUIRE(parser.get_max_age() == 123);
    REQUIRE(parser.get_stale_while_revalidate() == 789);
    REQUIRE(parser.get_stale_if_error() == 012);
  }
}

TEST_CASE("DirectiveParser::merge")
{
  SECTION("Other replaces this")
  {
    DirectiveParser self{"max-age=123, stale-while-revalidate=456, stale-if-error=789"};
    DirectiveParser other{"max-age=321, stale-while-revalidate=654, stale-if-error=987"};
    self.merge(other);
    REQUIRE(self.get_max_age() == 321);
    REQUIRE(self.get_stale_while_revalidate() == 654);
    REQUIRE(self.get_stale_if_error() == 987);
  }
  SECTION("Other unset does not replace this")
  {
    DirectiveParser self{"max-age=123, stale-while-revalidate=456, stale-if-error=789"};
    DirectiveParser other{"max-age=321"};
    self.merge(other);
    REQUIRE(self.get_max_age() == 321);
    REQUIRE(self.get_stale_while_revalidate() == 456);
    REQUIRE(self.get_stale_if_error() == 789);
  }
}

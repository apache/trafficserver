/** @file

    ParseRules unit test

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

#include "../../../tests/include/catch.hpp"
#include <tscore/ParseRules.h>
#include <iostream>

TEST_CASE("parse_rules", "[libts][parse_rules]")
{
  // test "100"
  {
    const char *end = nullptr;
    int64_t value   = ink_atoi64("100", &end);
    REQUIRE(value == 100);
    REQUIRE(*end == '\0');
  }

  // testf "1M"
  {
    const char *end = nullptr;
    int64_t value   = ink_atoi64("1M", &end);
    REQUIRE(value == 1 << 20);
    REQUIRE(*end == '\0');
  }

  // test -100
  {
    const char *end = nullptr;
    int64_t value   = ink_atoi64("-100", &end);
    REQUIRE(value == -100);
    REQUIRE(*end == '\0');
  }

  // testf "-1M"
  {
    const char *end = nullptr;
    int64_t value   = ink_atoi64("-1M", &end);
    REQUIRE(value == (1 << 20) * -1);
    REQUIRE(*end == '\0');
  }

  // test "9223372036854775807"
  {
    const char *end = nullptr;
    int64_t value   = ink_atoi64("9223372036854775807", &end);
    REQUIRE(value == 9223372036854775807ull);
    REQUIRE(*end == '\0');
  }

  // test "-9223372036854775807"
  {
    const char *end = nullptr;
    int64_t value   = ink_atoi64("-9223372036854775807", &end);
    REQUIRE(value == -9223372036854775807ll);
    REQUIRE(*end == '\0');
  }

  // testf "1.5T" - error case
  {
    const char *end = nullptr;
    int64_t value   = ink_atoi64("1.5T", &end);
    REQUIRE(value != 1649267441664);
    REQUIRE(*end != '\0');
  }

  // testf "asdf" - error case
  {
    const char *end = nullptr;
    int64_t value   = ink_atoi64("asdf", &end);
    REQUIRE(value == 0);
    REQUIRE(*end != '\0');
  }
}
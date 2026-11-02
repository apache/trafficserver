/** @file

    Tests for the exception description helpers.

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

#include "tscore/Demangle.h"

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <string>

namespace
{
struct NotAnException {
};

std::string
describe_thrown(auto const &thrown)
{
  try {
    throw thrown;
  } catch (...) {
    return ts::describe_current_exception();
  }
}
} // namespace

TEST_CASE("demangle", "[libts][Demangle]")
{
  CHECK(ts::demangle(typeid(int)) == "int");
  CHECK(ts::demangle(typeid(std::runtime_error)).find("runtime_error") != std::string::npos);
  CHECK(ts::demangle(typeid(NotAnException)).find("NotAnException") != std::string::npos);
}

TEST_CASE("describe_current_exception", "[libts][Demangle]")
{
  SECTION("a std::exception reports its type and message")
  {
    auto const text = describe_thrown(std::runtime_error{"boom"});

    CHECK(text.find("runtime_error") != std::string::npos);
    CHECK(text.ends_with(": boom"));
  }

  SECTION("the dynamic type is reported, not the type that was caught")
  {
    CHECK(describe_thrown(std::invalid_argument{"bad"}).find("invalid_argument") != std::string::npos);
  }

  SECTION("a thrown type outside std::exception is named")
  {
    CHECK(describe_thrown(42) == "exception of type int");
    CHECK(describe_thrown(NotAnException{}).find("exception of type ") == 0);
    CHECK(describe_thrown(NotAnException{}).find("NotAnException") != std::string::npos);
  }

  SECTION("no exception in flight")
  {
    CHECK(ts::describe_current_exception() == "no active exception");
  }
}

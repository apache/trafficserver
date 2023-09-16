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

#include "json_utils.h"

#include <cstring>
#include <catch.hpp>

using namespace traffic_dump;

TEST_CASE("JsonUtilsMap", "[json_entry]")
{
  SECTION("Test the string_view value overload")
  {
    CHECK(std::string(R"("name":"value")") == json_entry("name", "value"));
    CHECK(std::string(R"("":"value")") == json_entry("", "value"));
    CHECK(std::string(R"("name":"")") == json_entry("name", ""));
  }

  SECTION("Test the char buffer value overload")
  {
    CHECK(std::string(R"("name":"value")") == json_entry("name", "value", strlen("value")));
    CHECK(std::string(R"("name":"val")") == json_entry("name", "value", 3));
    CHECK(std::string(R"("":"value")") == json_entry("", "value", strlen("value")));
    CHECK(std::string(R"("name":"")") == json_entry("name", "", 0));
  }

  SECTION("Test that escaped characters are encoded as expected")
  {
    // Note that the raw strings on the left, i.e., R"(...)", leaves "\b" as
    // two characters, not a single escaped one. The escaped characters on
    // the right, by contrast, such as '\b', is a single escaped character.
    CHECK(std::string(R"("name":"val\bue")") == json_entry("name", "val\bue"));
    CHECK(std::string(R"("name":"\\value")") == json_entry("name", "\\value"));
    CHECK(std::string(R"("name":"value\f")") == json_entry("name", "value\f"));
    CHECK(std::string(R"("na\rme":"\tva\nlue\f")") == json_entry("na\rme", "\tva\nlue\f"));
    CHECK(std::string(R"("\r":"\t\n\f")") == json_entry("\r", "\t\n\f"));
  }
}

TEST_CASE("JsonUtilsArray", "[json_entry_array]")
{
  CHECK(std::string(R"(["name","value"])") == json_entry_array("name", "value"));
}

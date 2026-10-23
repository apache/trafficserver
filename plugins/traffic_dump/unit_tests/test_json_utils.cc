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
#include <catch2/catch_test_macros.hpp>

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

TEST_CASE("JsonUtilsUtf8", "[escape_json]")
{
  // UTF-8 bytes must remain intact, rather than being treated as signed control characters.
  std::string_view const samples[] = {
    "caf\xc3\xa9",      // Two-byte character.
    "\xe4\xb8\xad",     // Three-byte character.
    "\xf0\x9f\x98\x80", // Four-byte character.
    "\xc2\x80",         // U+0080, the first non-ASCII code point.
    "\xdf\xbf",         // U+07FF, the last two-byte code point.
    "\xe0\xa0\x80",     // U+0800, the first three-byte code point.
    "\xef\xbf\xbf",     // U+FFFF, the last three-byte code point.
    "\xf0\x90\x80\x80", // U+10000, the first four-byte code point.
    "\xf4\x8f\xbf\xbf", // U+10FFFF, the last Unicode code point.
  };

  for (auto const text : samples) {
    CHECK(escape_json(text) == text);
    CHECK(escape_json(text.data(), text.size()) == text);
  }
}

TEST_CASE("JsonUtilsUtf8Escapes", "[escape_json]")
{
  SECTION("UTF-8 next to characters requiring escapes")
  {
    std::string_view const text     = "\xc3\xa9\"\xe4\xb8\xad\\\xf0\x9f\x98\x80\n\x01";
    std::string_view const expected = "\xc3\xa9\\\"\xe4\xb8\xad\\\\\xf0\x9f\x98\x80\\n\\u0001";

    CHECK(escape_json(text) == expected);
    CHECK(escape_json(text.data(), text.size()) == expected);
  }

  SECTION("Explicit length preserves embedded NUL and excludes trailing bytes")
  {
    char const text[] = "\xc3\xa9\0\xe4\xb8\xad";

    CHECK(escape_json(std::string_view{text, sizeof(text) - 1}) == "\xc3\xa9\\u0000\xe4\xb8\xad");
    CHECK(escape_json(text, sizeof(text) - 1) == "\xc3\xa9\\u0000\xe4\xb8\xad");
    CHECK(escape_json(text, 2) == "\xc3\xa9");
    CHECK(escape_json(text, 0).empty());
  }

  SECTION("ASCII controls and the adjacent printable boundary")
  {
    std::string_view const text{"\0\x01\b\f\n\r\t\x1f \x7f", 10};

    CHECK(escape_json(text) == "\\u0000\\u0001\\b\\f\\n\\r\\t\\u001f \x7f");
    CHECK(escape_json("").empty());
  }
}

TEST_CASE("JsonUtilsUtf8Entries", "[json_entry][json_entry_array]")
{
  std::string_view const name  = "caf\xc3\xa9";
  std::string_view const value = "\xe4\xb8\xad\xf0\x9f\x98\x80";

  CHECK(json_entry(name, value) == "\"caf\xc3\xa9\":\"\xe4\xb8\xad\xf0\x9f\x98\x80\"");
  CHECK(json_entry(name, value.data(), value.size()) == "\"caf\xc3\xa9\":\"\xe4\xb8\xad\xf0\x9f\x98\x80\"");
  CHECK(json_entry_array(name, value) == "[\"caf\xc3\xa9\",\"\xe4\xb8\xad\xf0\x9f\x98\x80\"]");
}

TEST_CASE("JsonUtilsInvalidUtf8", "[escape_json]")
{
  std::string_view const samples[] = {
    "caf\xe9", "\xc0\xaf", "\xed\xa0\x80", "\xf0\x9f", "\x80\xbf", "\xf5\xff",
  };
  for (auto const text : samples) {
    CHECK(escape_json(text) == text);
    CHECK(escape_json(text.data(), text.size()) == text);
  }

  std::string_view const text     = "\xff\"\x80\\\xe9\n";
  std::string_view const expected = "\xff\\\"\x80\\\\\xe9\\n";

  CHECK(escape_json(text) == expected);
  CHECK(escape_json(text.data(), text.size()) == expected);
}

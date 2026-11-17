/** @file

    SimpleTokenizer tests.

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

#include "tscore/SimpleTokenizer.h"

#include <string>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

TEST_CASE("SimpleTokenizer trims tokens by default", "[libts][SimpleTokenizer]")
{
  SimpleTokenizer tok("one    two\\ and\\ three four:   five : six");

  CHECK(std::string(tok.getNext()) == "one");
  CHECK(std::string(tok.getNext()) == "two and three");
  CHECK(std::string(tok.getNext(':')) == "four");
  CHECK(std::string(tok.peekAtRestOfString()) == "   five : six");
  CHECK(std::string(tok.getNext(':')) == "five");
  CHECK(std::string(tok.getNext(':')) == "six");
  CHECK(tok.getNext() == nullptr);
}

TEST_CASE("SimpleTokenizer drops trailing delimiters from a multi-token request", "[libts][SimpleTokenizer]")
{
  SimpleTokenizer tok("a,b,,", ',');

  CHECK(std::string(tok.getNext(3)) == "a,b");
  CHECK(tok.getNext() == nullptr);
}

TEST_CASE("SimpleTokenizer keeps whitespace when asked", "[libts][SimpleTokenizer]")
{
  SimpleTokenizer tok(",  with null fields ,,,", ',', SimpleTokenizer::CONSIDER_NULL_FIELDS | SimpleTokenizer::KEEP_WHITESPACE);

  CHECK(std::string(tok.getNext()) == "");
  CHECK(std::string(tok.getNext()) == "  with null fields ");
  CHECK(tok.getNumTokensRemaining() == 3);
}

TEST_CASE("SimpleTokenizer returns a leading null field", "[libts][SimpleTokenizer]")
{
  unsigned const mode = GENERATE(as<unsigned>{}, SimpleTokenizer::CONSIDER_NULL_FIELDS,
                                 SimpleTokenizer::CONSIDER_NULL_FIELDS | SimpleTokenizer::KEEP_WHITESPACE_LEFT);

  SECTION("at the start of the string")
  {
    SimpleTokenizer tok(",abc", ',', mode);

    CHECK(std::string(tok.getNext()) == "");
    CHECK(std::string(tok.getNext()) == "abc");
    CHECK(tok.getNext() == nullptr);
  }

  SECTION("after leading whitespace")
  {
    SimpleTokenizer tok("  ,abc", ',', mode);

    CHECK(std::string(tok.getNext()) == "");
    CHECK(std::string(tok.getNext()) == "abc");
  }
}

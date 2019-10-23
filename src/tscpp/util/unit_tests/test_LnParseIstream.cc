/** @file

    Unit tests for PostScript.h.

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

#include "catch.hpp"
#include <tscpp/util/LnParseIstream.h>

#include <fstream>
#include <sstream>
#include <string>

using ts::LnParseIstream;

TEST_CASE("LnParseIstream", "[LPI]")
{
  {
    std::ifstream is("unit_tests/LnParseIstream.txt");

    int line_num = 0;

    int catch_sucks = LnParseIstream::EXTRA_FIELDS;
    REQUIRE(LnParseIstream::skipEmpty(is, line_num) == catch_sucks);
    REQUIRE(5 == line_num);

    int a;
    float b;
    int c;
    std::string d;
    REQUIRE(LnParseIstream(is, a, b, c, d) == 4);
    ++line_num;
    REQUIRE(a == 1);
    REQUIRE(b == 2.0);
    REQUIRE(c == 3);
    REQUIRE(d == "text");

    catch_sucks = LnParseIstream::EXTRA_FIELDS;
    REQUIRE(LnParseIstream::skipEmpty(is, line_num) == catch_sucks);
    REQUIRE(9 == line_num);

    catch_sucks = LnParseIstream::EXTRA_FIELDS;
    REQUIRE(LnParseIstream(is, a, b) == catch_sucks);
    REQUIRE(a == 4);
    REQUIRE(b == 5.0);

    REQUIRE(LnParseIstream(is, c, d) == 2);
    ++line_num;
    REQUIRE(c == 6);
    REQUIRE(d == "TEXT");

    REQUIRE(LnParseIstream(is, a, b, c, d) == 2);
    ++line_num;
    REQUIRE(a == 7);
    REQUIRE(b == 8.0);
    REQUIRE(c == 6);
    REQUIRE(d == "TEXT");

    LnParseIstream::Quoted qs1, qs2;

    REQUIRE(LnParseIstream(is, a, qs1, qs2, d) == 4);
    ++line_num;
    REQUIRE(a == 4);
    REQUIRE(qs1.value == "A\t \tquoted string");
    REQUIRE(qs2.value == "A quoted string with an embedded quote here \" and at the end \"");
    REQUIRE(d == "TEXT");

    catch_sucks = LnParseIstream::END_OF_FILE;
    REQUIRE(LnParseIstream::skipEmpty(is, line_num) == catch_sucks);
    REQUIRE(14 == line_num);
  }
  {
    std::ifstream is("unit_tests/LnParseIstream2.txt");

    int catch_sucks = LnParseIstream::EXTRA_FIELDS;

    int a;
    REQUIRE(LnParseIstream(is, a) == catch_sucks);
    REQUIRE(a == 666);

    is >> std::hex;

    REQUIRE(LnParseIstream(is, a) == catch_sucks);
    REQUIRE(a == 0xabc);

    is >> std::dec;

    REQUIRE(LnParseIstream(is, a) == 1);
    REQUIRE(a == 667);

    catch_sucks = LnParseIstream::STREAM_ERROR;
    REQUIRE(LnParseIstream(is, a) == catch_sucks);
  }
  {
    std::ifstream is("not_there/not_there");

    int line_num = 0;

    int catch_sucks = LnParseIstream::STREAM_ERROR;
    REQUIRE(LnParseIstream::skipEmpty(is, line_num) == catch_sucks);
  }
  {
    std::stringstream is("");

    int catch_sucks = LnParseIstream::END_OF_FILE;
    REQUIRE(LnParseIstream(is) == catch_sucks);
  }
  {
    std::stringstream is("\n\n\n\n\n");

    int line_num = 0;

    int catch_sucks = LnParseIstream::END_OF_FILE;
    REQUIRE(LnParseIstream::skipEmpty(is, line_num) == catch_sucks);
    REQUIRE(5 == line_num);
  }
  {
    std::stringstream is(" ");

    int catch_sucks = LnParseIstream::STREAM_ERROR;
    REQUIRE(LnParseIstream(is) == catch_sucks);
  }
  {
    std::stringstream is("5 \"unterminated string\n");

    int i;
    LnParseIstream::Quoted qs;
    int catch_sucks = LnParseIstream::STREAM_ERROR;
    REQUIRE(LnParseIstream(is, i, qs) == catch_sucks);
  }
  {
    std::string s1{"has quotes"};
    std::string s2{"no-quotes"};
    std::string s3{"| quoted with quotes |"};
    std::string sall = '|' + s1 + "| " + s2 + " ||" + s3 + "||\n";
    std::stringstream is{sall};

    LnParseIstream::OptQuoted f1{'|'}, f2{'|'}, f3{'|'};
    REQUIRE(LnParseIstream(is, f1, f2, f3) == 3);
    REQUIRE(f1.value == s1);
    REQUIRE(f2.value == s2);
    REQUIRE(f3.value == s3);

    int line_num = 1;

    int catch_sucks = LnParseIstream::END_OF_FILE;
    REQUIRE(LnParseIstream::skipEmpty(is, line_num) == catch_sucks);
    REQUIRE(1 == line_num);
  }
  {
    std::stringstream is(" ");

    int catch_sucks = LnParseIstream::STREAM_ERROR;
    REQUIRE(LnParseIstream(is) == catch_sucks);
  }
  {
    std::stringstream is("5 \"unterminated string\n");
  }
}

/** @file

   Catch-based unit tests for various header logic.
   This replaces the old regression tests in HdrTest.cc.

   @section license License

   Licensed to the Apache Software Foundation (ASF) under one or more contributor license agreements.
   See the NOTICE file distributed with this work for additional information regarding copyright
   ownership.  The ASF licenses this file to you under the Apache License, Version 2.0 (the
   "License"); you may not use this file except in compliance with the License.  You may obtain a
   copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software distributed under the License
   is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
   or implied. See the License for the specific language governing permissions and limitations under
   the License.
 */

#include <cstdio>
#include <string>
#include <cstring>

#include "catch.hpp"

#include "MIME.h"

TEST_CASE("HdrTest", "[proxy][hdrtest]")
{
  mime_init();

  SECTION("Field Char Check")
  {
    static struct {
      std::string_view line;
      ParseResult expected;
    } test_cases[] = {
      ////
      // Field Name
      {"Content-Length: 10\r\n", PARSE_RESULT_CONT},
      {"Content-Length\x0b: 10\r\n", PARSE_RESULT_ERROR},
      ////
      // Field Value
      // SP
      {"Content-Length: 10\r\n", PARSE_RESULT_CONT},
      {"Foo: ab cd\r\n", PARSE_RESULT_CONT},
      // HTAB
      {"Foo: ab\td/cd\r\n", PARSE_RESULT_CONT},
      // VCHAR
      {"Foo: ab\x21/cd\r\n", PARSE_RESULT_CONT},
      {"Foo: ab\x7e/cd\r\n", PARSE_RESULT_CONT},
      // DEL
      {"Foo: ab\x7f/cd\r\n", PARSE_RESULT_ERROR},
      // obs-text
      {"Foo: ab\x80/cd\r\n", PARSE_RESULT_CONT},
      {"Foo: ab\xff/cd\r\n", PARSE_RESULT_CONT},
      // control char
      {"Content-Length: 10\x0b\r\n", PARSE_RESULT_ERROR},
      {"Content-Length:\x0b 10\r\n", PARSE_RESULT_ERROR},
      {"Foo: ab\x1d/cd\r\n", PARSE_RESULT_ERROR},
    };

    MIMEHdr hdr;
    MIMEParser parser;
    mime_parser_init(&parser);

    for (const auto &t : test_cases) {
      mime_parser_clear(&parser);

      const char *start = t.line.data();
      const char *end   = start + t.line.size();

      int r = hdr.parse(&parser, &start, end, false, false);
      if (r != t.expected) {
        std::printf("Expected %s is %s, but not", t.line.data(), t.expected == PARSE_RESULT_ERROR ? "invalid" : "valid");
        CHECK(false);
      }
    }
  }
}

/** @file

   Catch-based tests for HdrsUtils.cc

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

#include <string>
#include <cstring>
#include <cctype>
#include <bitset>
#include <initializer_list>
#include <new>

#include "catch.hpp"

#include "HdrHeap.h"
#include "MIME.h"
#include "HdrUtils.h"

TEST_CASE("HdrUtils", "[proxy][hdrutils]")
{
  static constexpr ts::TextView text{"One: alpha\r\n"
                                     "Two: alpha, bravo\r\n"
                                     "Three: zwoop, \"A,B\" , , phil  , \"unterminated\r\n"
                                     "Five: alpha, bravo, charlie\r\n"
                                     "Four: itchi, \"ni, \\\"san\" , \"\" , \"\r\n"
                                     "Five: delta, echo\r\n"
                                     "\r\n"};

  static constexpr std::string_view ONE_TAG{"One"};
  static constexpr std::string_view TWO_TAG{"Two"};
  static constexpr std::string_view THREE_TAG{"Three"};
  static constexpr std::string_view FOUR_TAG{"Four"};
  static constexpr std::string_view FIVE_TAG{"Five"};

  HdrHeap *heap = new_HdrHeap(HDR_HEAP_DEFAULT_SIZE + 64);
  MIMEParser parser;
  char const *real_s = text.data();
  char const *real_e = text.data_end();
  MIMEHdr mime;

  mime.create(heap);
  mime_parser_init(&parser);

  auto result = mime_parser_parse(&parser, heap, mime.m_mime, &real_s, real_e, false, true);
  REQUIRE(PARSE_RESULT_DONE == result);

  HdrCsvIter iter;

  MIMEField *field{mime.field_find(ONE_TAG.data(), int(ONE_TAG.size()))};
  REQUIRE(field != nullptr);

  auto value = iter.get_first(field);
  REQUIRE(value == "alpha");

  field = mime.field_find(TWO_TAG.data(), int(TWO_TAG.size()));
  value = iter.get_first(field);
  REQUIRE(value == "alpha");
  value = iter.get_next();
  REQUIRE(value == "bravo");
  value = iter.get_next();
  REQUIRE(value.empty());

  field = mime.field_find(THREE_TAG.data(), int(THREE_TAG.size()));
  value = iter.get_first(field);
  REQUIRE(value == "zwoop");
  value = iter.get_next();
  REQUIRE(value == "A,B"); // quotes escape separator, and are stripped.
  value = iter.get_next();
  REQUIRE(value == "phil");
  value = iter.get_next();
  REQUIRE(value == "unterminated");
  value = iter.get_next();
  REQUIRE(value.empty());

  field = mime.field_find(FOUR_TAG.data(), int(FOUR_TAG.size()));
  value = iter.get_first(field);
  REQUIRE(value == "itchi");
  value = iter.get_next();
  REQUIRE(value == "ni, \\\"san"); // verify escaped quotes are passed through.
  value = iter.get_next();
  REQUIRE(value.empty());

  // Check that duplicates are handled correctly.
  field = mime.field_find(FIVE_TAG.data(), int(FIVE_TAG.size()));
  value = iter.get_first(field);
  REQUIRE(value == "alpha");
  value = iter.get_next();
  REQUIRE(value == "bravo");
  value = iter.get_next();
  REQUIRE(value == "charlie");
  value = iter.get_next();
  REQUIRE(value == "delta");
  value = iter.get_next();
  REQUIRE(value == "echo");
  value = iter.get_next();
  REQUIRE(value.empty());

  field = mime.field_find(FIVE_TAG.data(), int(FIVE_TAG.size()));
  value = iter.get_first(field, false);
  REQUIRE(value == "alpha");
  value = iter.get_next();
  REQUIRE(value == "bravo");
  value = iter.get_next();
  REQUIRE(value == "charlie");
  value = iter.get_next();
  REQUIRE(value.empty());
}

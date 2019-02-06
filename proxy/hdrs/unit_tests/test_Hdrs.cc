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

#include <string>
#include <cstring>
#include <cctype>
#include <bitset>
#include <initializer_list>
#include <array>
#include <new>

#include "catch.hpp"

#include "HTTP.h"

// replaces test_http_parser_eos_boundary_cases
TEST_CASE("HdrTest", "[proxy][hdrtest]")
{
  struct Test {
    ts::TextView msg;
    int expected_result;
    int expected_bytes_consumed;
  };
  static const std::array<Test, 20> tests = {{
    {"GET /index.html HTTP/1.0\r\n", PARSE_RESULT_DONE, 26},
    {"GET /index.html HTTP/1.0\r\n\r\n***BODY****", PARSE_RESULT_DONE, 28},
    {"GET /index.html HTTP/1.0\r\nUser-Agent: foobar\r\n\r\n***BODY****", PARSE_RESULT_DONE, 48},
    {"GET", PARSE_RESULT_ERROR, 3},
    {"GET /index.html", PARSE_RESULT_ERROR, 15},
    {"GET /index.html\r\n", PARSE_RESULT_ERROR, 17},
    {"GET /index.html HTTP/1.0", PARSE_RESULT_ERROR, 24},
    {"GET /index.html HTTP/1.0\r", PARSE_RESULT_ERROR, 25},
    {"GET /index.html HTTP/1.0\n", PARSE_RESULT_DONE, 25},
    {"GET /index.html HTTP/1.0\n\n", PARSE_RESULT_DONE, 26},
    {"GET /index.html HTTP/1.0\r\n\r\n", PARSE_RESULT_DONE, 28},
    {"GET /index.html HTTP/1.0\r\nUser-Agent: foobar", PARSE_RESULT_ERROR, 44},
    {"GET /index.html HTTP/1.0\r\nUser-Agent: foobar\n", PARSE_RESULT_DONE, 45},
    {"GET /index.html HTTP/1.0\r\nUser-Agent: foobar\r\n", PARSE_RESULT_DONE, 46},
    {"GET /index.html HTTP/1.0\r\nUser-Agent: foobar\r\n\r\n", PARSE_RESULT_DONE, 48},
    {"GET /index.html HTTP/1.0\nUser-Agent: foobar\n", PARSE_RESULT_DONE, 44},
    {"GET /index.html HTTP/1.0\nUser-Agent: foobar\nBoo: foo\n", PARSE_RESULT_DONE, 53},
    {"GET /index.html HTTP/1.0\r\nUser-Agent: foobar\r\n", PARSE_RESULT_DONE, 46},
    {"GET /index.html HTTP/1.0\r\n", PARSE_RESULT_DONE, 26},
    {"", PARSE_RESULT_ERROR, 0},
  }};

  HTTPParser parser;

  http_parser_init(&parser);

  for (auto const &test : tests) {
    HTTPHdr req_hdr;
    HdrHeap *heap = new_HdrHeap(HDR_HEAP_DEFAULT_SIZE + 64); // extra to prevent proxy allocation.

    req_hdr.create(HTTP_TYPE_REQUEST, heap);

    http_parser_clear(&parser);

    auto start          = test.msg.data();
    auto ret            = req_hdr.parse_req(&parser, &start, test.msg.data_end(), true);
    auto bytes_consumed = start - test.msg.data();

    REQUIRE(bytes_consumed == test.expected_bytes_consumed);
    REQUIRE(ret == test.expected_result);

    req_hdr.destroy();
  }
}

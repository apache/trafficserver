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
#include <cstdio>
#include <memory>

#include "tscore/Regex.h"
#include "tscore/ink_time.h"

#include "catch.hpp"

#include "HTTP.h"
#include "HttpCompat.h"

// replaces test_http_parser_eos_boundary_cases
TEST_CASE("HdrTestHttpParse", "[proxy][hdrtest]")
{
  struct Test {
    ts::TextView msg;
    int expected_result;
    int expected_bytes_consumed;
  };
  static const std::array<Test, 21> tests = {{
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
    {"GET /index.html hTTP/1.0\r\n", PARSE_RESULT_ERROR, 26},
    {"", PARSE_RESULT_ERROR, 0},
  }};

  HTTPParser parser;

  http_parser_init(&parser);

  for (auto const &test : tests) {
    HTTPHdr req_hdr;
    HdrHeap *heap = new_HdrHeap(HdrHeap::DEFAULT_SIZE + 64); // extra to prevent proxy allocation.

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

TEST_CASE("MIMEScanner_fragments", "[proxy][mimescanner_fragments]")
{
  constexpr ts::TextView const message = "GET /index.html HTTP/1.0\r\n";

  struct Fragment {
    ts::TextView msg;
    bool shares_input;
    int expected_result;
  };
  constexpr std::array<Fragment, 3> const fragments = {{
    {message.substr(0, 11), true, PARSE_RESULT_CONT},
    {message.substr(11, 11), true, PARSE_RESULT_CONT},
    {message.substr(22), false, PARSE_RESULT_OK},
  }};

  MIMEScanner scanner;
  ts::TextView output; // only set on last call

  for (auto const &frag : fragments) {
    ts::TextView input          = frag.msg;
    bool got_shares_input       = !frag.shares_input;
    constexpr bool const is_eof = false;
    ParseResult const got_res   = scanner.get(input, output, got_shares_input, is_eof, MIMEScanner::LINE);

    REQUIRE(frag.expected_result == got_res);
    REQUIRE(frag.shares_input == got_shares_input);
  }

  REQUIRE(message == output);
}

namespace
{
static const char *
comp_http_hdr(HTTPHdr *h1, HTTPHdr *h2)
{
  int h1_len = h1->length_get();
  int h2_len = h2->length_get();

  if (h1_len != h2_len) {
    return "length mismatch";
  }

  std::unique_ptr<char[]> h1_pbuf(new char[h1_len + 1]);
  std::unique_ptr<char[]> h2_pbuf(new char[h2_len + 1]);

  int p_index = 0, p_dumpoffset = 0;

  int rval = h1->print(h1_pbuf.get(), h1_len, &p_index, &p_dumpoffset);
  if (rval != 1) {
    return "hdr print failed";
  }

  p_index = p_dumpoffset = 0;
  rval                   = h2->print(h2_pbuf.get(), h2_len, &p_index, &p_dumpoffset);
  if (rval != 1) {
    return "hdr print failed";
  }

  rval = memcmp(h1_pbuf.get(), h2_pbuf.get(), h1_len);

  if (rval != 0) {
    return "compare failed";
  } else {
    return nullptr;
  }
}

int
test_http_hdr_copy_over_aux(int testnum, const char *request, const char *response)
{
  int err;
  HTTPHdr req_hdr;
  HTTPHdr resp_hdr;
  HTTPHdr copy1;
  HTTPHdr copy2;

  HTTPParser parser;
  const char *start;
  const char *end;
  const char *comp_str = nullptr;

  /*** (1) parse the request string into hdr ***/

  req_hdr.create(HTTP_TYPE_REQUEST);

  start = request;
  end   = start + strlen(start); // 1 character past end of string

  http_parser_init(&parser);

  while (true) {
    err = req_hdr.parse_req(&parser, &start, end, true);
    if (err != PARSE_RESULT_CONT) {
      break;
    }
  }

  if (err == PARSE_RESULT_ERROR) {
    std::printf("FAILED: (test #%d) parse error parsing request hdr\n", testnum);
    return (0);
  }
  http_parser_clear(&parser);

  /*** (2) parse the response string into hdr ***/

  resp_hdr.create(HTTP_TYPE_RESPONSE);

  start = response;
  end   = start + strlen(start); // 1 character past end of string

  http_parser_init(&parser);

  while (true) {
    err = resp_hdr.parse_resp(&parser, &start, end, true);
    if (err != PARSE_RESULT_CONT) {
      break;
    }
  }

  if (err == PARSE_RESULT_ERROR) {
    printf("FAILED: (test #%d) parse error parsing response hdr\n", testnum);
    return (0);
  }

  /*** (3) Basic copy testing ***/
  copy1.create(HTTP_TYPE_REQUEST);
  copy1.copy(&req_hdr);
  comp_str = comp_http_hdr(&req_hdr, &copy1);
  if (comp_str) {
    goto done;
  }

  copy2.create(HTTP_TYPE_RESPONSE);
  copy2.copy(&resp_hdr);
  comp_str = comp_http_hdr(&resp_hdr, &copy2);
  if (comp_str) {
    goto done;
  }

  // The APIs for copying headers uses memcpy() which can be unsafe for
  // overlapping memory areas. It's unclear to me why these tests were
  // created in the first place honestly, since nothing else does this.

  /*** (4) Gender bending copying ***/
  copy1.copy(&resp_hdr);
  comp_str = comp_http_hdr(&resp_hdr, &copy1);
  if (comp_str) {
    goto done;
  }

  copy2.copy(&req_hdr);
  comp_str = comp_http_hdr(&req_hdr, &copy2);
  if (comp_str) {
    goto done;
  }

done:
  req_hdr.destroy();
  resp_hdr.destroy();
  copy1.destroy();
  copy2.destroy();

  if (comp_str) {
    printf("FAILED: (test #%d) copy & compare: %s\n", testnum, comp_str);
    printf("REQ:\n[%.*s]\n", static_cast<int>(strlen(request)), request);
    printf("RESP  :\n[%.*s]\n", static_cast<int>(strlen(response)), response);
    return (0);
  } else {
    return (1);
  }
}

int
test_http_hdr_null_char(int testnum, const char *request, const char * /*request_tgt*/)
{
  int err;
  HTTPHdr hdr;
  HTTPParser parser;
  const char *start;
  char cpy_buf[2048];
  const char *cpy_buf_ptr = cpy_buf;

  /*** (1) parse the request string into hdr ***/

  hdr.create(HTTP_TYPE_REQUEST);

  start = request;

  if (strlen(start) > sizeof(cpy_buf)) {
    std::printf("FAILED: (test #%d) Internal buffer too small for null char test\n", testnum);
    return (0);
  }
  strcpy(cpy_buf, start);

  // Put a null character somewhere in the header
  int length          = strlen(start);
  cpy_buf[length / 2] = '\0';
  http_parser_init(&parser);

  while (true) {
    err = hdr.parse_req(&parser, &cpy_buf_ptr, cpy_buf_ptr + length, true);
    if (err != PARSE_RESULT_CONT) {
      break;
    }
  }
  if (err != PARSE_RESULT_ERROR) {
    std::printf("FAILED: (test #%d) no parse error parsing request with null char\n", testnum);
    return (0);
  }
  return 1;
}

int
test_http_hdr_ctl_char(int testnum, const char *request, const char * /*request_tgt */)
{
  int err;
  HTTPHdr hdr;
  HTTPParser parser;
  const char *start;
  char cpy_buf[2048];
  const char *cpy_buf_ptr = cpy_buf;

  /*** (1) parse the request string into hdr ***/

  hdr.create(HTTP_TYPE_REQUEST);

  start = request;

  if (strlen(start) > sizeof(cpy_buf)) {
    std::printf("FAILED: (test #%d) Internal buffer too small for ctl char test\n", testnum);
    return (0);
  }
  strcpy(cpy_buf, start);

  // Replace a character in the method
  cpy_buf[1] = 16;

  http_parser_init(&parser);

  while (true) {
    err = hdr.parse_req(&parser, &cpy_buf_ptr, cpy_buf_ptr + strlen(start), true);
    if (err != PARSE_RESULT_CONT) {
      break;
    }
  }

  if (err != PARSE_RESULT_ERROR) {
    std::printf("FAILED: (test #%d) no parse error parsing method with ctl char\n", testnum);
    return (0);
  }
  return 1;
}

int
test_http_hdr_print_and_copy_aux(int testnum, const char *request, const char *request_tgt, const char *response,
                                 const char *response_tgt)
{
  int err;
  HTTPHdr hdr;
  HTTPParser parser;
  const char *start;
  const char *end;

  char prt_buf[2048];
  int prt_bufsize = sizeof(prt_buf);
  int prt_bufindex, prt_dumpoffset, prt_ret;

  char cpy_buf[2048];
  int cpy_bufsize = sizeof(cpy_buf);
  int cpy_bufindex, cpy_dumpoffset, cpy_ret;

  std::unique_ptr<char[]> marshal_buf(new char[2048]);
  int marshal_bufsize = sizeof(cpy_buf);

  /*** (1) parse the request string into hdr ***/

  hdr.create(HTTP_TYPE_REQUEST);

  start = request;
  end   = start + strlen(start); // 1 character past end of string

  http_parser_init(&parser);

  while (true) {
    err = hdr.parse_req(&parser, &start, end, true);
    if (err != PARSE_RESULT_CONT) {
      break;
    }
  }

  if (err == PARSE_RESULT_ERROR) {
    std::printf("FAILED: (test #%d) parse error parsing request hdr\n", testnum);
    return (0);
  }

  /*** (2) copy the request header ***/
  HTTPHdr new_hdr, marshal_hdr;
  RefCountObj ref;

  // Pretend to pin this object with a refcount.
  ref.refcount_inc();

  int marshal_len = hdr.m_heap->marshal(marshal_buf.get(), marshal_bufsize);
  marshal_hdr.create(HTTP_TYPE_REQUEST);
  marshal_hdr.unmarshal(marshal_buf.get(), marshal_len, &ref);
  new_hdr.create(HTTP_TYPE_REQUEST);
  new_hdr.copy(&marshal_hdr);

  /*** (3) print the request header and copy to buffers ***/

  prt_bufindex = prt_dumpoffset = 0;
  prt_ret                       = hdr.print(prt_buf, prt_bufsize, &prt_bufindex, &prt_dumpoffset);

  cpy_bufindex = cpy_dumpoffset = 0;
  cpy_ret                       = new_hdr.print(cpy_buf, cpy_bufsize, &cpy_bufindex, &cpy_dumpoffset);

  if ((prt_ret != 1) || (cpy_ret != 1)) {
    std::printf("FAILED: (test #%d) couldn't print req hdr or copy --- prt_ret=%d, cpy_ret=%d\n", testnum, prt_ret, cpy_ret);
    return (0);
  }

  if ((static_cast<size_t>(prt_bufindex) != strlen(request_tgt)) || (static_cast<size_t>(cpy_bufindex) != strlen(request_tgt))) {
    std::printf("FAILED: (test #%d) print req output size mismatch --- tgt=%d, prt_bufsize=%d, cpy_bufsize=%d\n", testnum,
                static_cast<int>(strlen(request_tgt)), prt_bufindex, cpy_bufindex);

    std::printf("ORIGINAL:\n[%.*s]\n", static_cast<int>(strlen(request)), request);
    std::printf("TARGET  :\n[%.*s]\n", static_cast<int>(strlen(request_tgt)), request_tgt);
    std::printf("PRT_BUFF:\n[%.*s]\n", prt_bufindex, prt_buf);
    std::printf("CPY_BUFF:\n[%.*s]\n", cpy_bufindex, cpy_buf);
    return (0);
  }

  if ((strncasecmp(request_tgt, prt_buf, strlen(request_tgt)) != 0) ||
      (strncasecmp(request_tgt, cpy_buf, strlen(request_tgt)) != 0)) {
    std::printf("FAILED: (test #%d) print req output mismatch\n", testnum);
    std::printf("ORIGINAL:\n[%.*s]\n", static_cast<int>(strlen(request)), request);
    std::printf("TARGET  :\n[%.*s]\n", static_cast<int>(strlen(request_tgt)), request_tgt);
    std::printf("PRT_BUFF:\n[%.*s]\n", prt_bufindex, prt_buf);
    std::printf("CPY_BUFF:\n[%.*s]\n", cpy_bufindex, cpy_buf);
    return (0);
  }

  hdr.destroy();
  new_hdr.destroy();

  /*** (4) parse the response string into hdr ***/

  hdr.create(HTTP_TYPE_RESPONSE);

  start = response;
  end   = start + strlen(start); // 1 character past end of string

  http_parser_init(&parser);

  while (true) {
    err = hdr.parse_resp(&parser, &start, end, true);
    if (err != PARSE_RESULT_CONT) {
      break;
    }
  }

  if (err == PARSE_RESULT_ERROR) {
    std::printf("FAILED: (test #%d) parse error parsing response hdr\n", testnum);
    return (0);
  }

  /*** (2) copy the response header ***/

  new_hdr.create(HTTP_TYPE_RESPONSE);
  new_hdr.copy(&hdr);

  /*** (3) print the response header and copy to buffers ***/

  prt_bufindex = prt_dumpoffset = 0;
  prt_ret                       = hdr.print(prt_buf, prt_bufsize, &prt_bufindex, &prt_dumpoffset);

  cpy_bufindex = cpy_dumpoffset = 0;
  cpy_ret                       = new_hdr.print(cpy_buf, cpy_bufsize, &cpy_bufindex, &cpy_dumpoffset);

  if ((prt_ret != 1) || (cpy_ret != 1)) {
    std::printf("FAILED: (test #%d) couldn't print rsp hdr or copy --- prt_ret=%d, cpy_ret=%d\n", testnum, prt_ret, cpy_ret);
    return (0);
  }

  if ((static_cast<size_t>(prt_bufindex) != strlen(response_tgt)) || (static_cast<size_t>(cpy_bufindex) != strlen(response_tgt))) {
    std::printf("FAILED: (test #%d) print rsp output size mismatch --- tgt=%d, prt_bufsize=%d, cpy_bufsize=%d\n", testnum,
                static_cast<int>(strlen(response_tgt)), prt_bufindex, cpy_bufindex);
    std::printf("ORIGINAL:\n[%.*s]\n", static_cast<int>(strlen(response)), response);
    std::printf("TARGET  :\n[%.*s]\n", static_cast<int>(strlen(response_tgt)), response_tgt);
    std::printf("PRT_BUFF:\n[%.*s]\n", prt_bufindex, prt_buf);
    std::printf("CPY_BUFF:\n[%.*s]\n", cpy_bufindex, cpy_buf);
    return (0);
  }

  if ((strncasecmp(response_tgt, prt_buf, strlen(response_tgt)) != 0) ||
      (strncasecmp(response_tgt, cpy_buf, strlen(response_tgt)) != 0)) {
    std::printf("FAILED: (test #%d) print rsp output mismatch\n", testnum);
    std::printf("ORIGINAL:\n[%.*s]\n", static_cast<int>(strlen(response)), response);
    std::printf("TARGET  :\n[%.*s]\n", static_cast<int>(strlen(response_tgt)), response_tgt);
    std::printf("PRT_BUFF:\n[%.*s]\n", prt_bufindex, prt_buf);
    std::printf("CPY_BUFF:\n[%.*s]\n", cpy_bufindex, cpy_buf);
    return (0);
  }

  hdr.destroy();
  new_hdr.destroy();

  if (test_http_hdr_copy_over_aux(testnum, request, response) == 0) {
    return 0;
  }

  return (1);
}

int
test_arena_aux(Arena *arena, int len)
{
  char *str      = arena->str_alloc(len);
  int verify_len = static_cast<int>(arena->str_length(str));

  if (len != verify_len) {
    std::printf("FAILED: requested %d, got %d bytes\n", len, verify_len);
    return (1); // 1 error (different return convention)
  } else {
    return (0); // no errors (different return convention)
  }
}

} // end anonymous namespace

TEST_CASE("HdrTest", "[proxy][hdrtest]")
{
  hdrtoken_init();
  url_init();
  mime_init();
  http_init();

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

      int r = hdr.parse(&parser, &start, end, false, false, false);
      if (r != t.expected) {
        std::printf("Expected %s is %s, but not", t.line.data(), t.expected == PARSE_RESULT_ERROR ? "invalid" : "valid");
        CHECK(false);
      }
    }
  }

  SECTION("Test parse date")
  {
    static struct {
      const char *fast;
      const char *slow;
    } dates[] = {
      {"Sun, 06 Nov 1994 08:49:37 GMT", "Sunday, 06-Nov-1994 08:49:37 GMT"},
      {"Mon, 07 Nov 1994 08:49:37 GMT", "Monday, 07-Nov-1994 08:49:37 GMT"},
      {"Tue, 08 Nov 1994 08:49:37 GMT", "Tuesday, 08-Nov-1994 08:49:37 GMT"},
      {"Wed, 09 Nov 1994 08:49:37 GMT", "Wednesday, 09-Nov-1994 08:49:37 GMT"},
      {"Thu, 10 Nov 1994 08:49:37 GMT", "Thursday, 10-Nov-1994 08:49:37 GMT"},
      {"Fri, 11 Nov 1994 08:49:37 GMT", "Friday, 11-Nov-1994 08:49:37 GMT"},
      {"Sat, 11 Nov 1994 08:49:37 GMT", "Saturday, 11-Nov-1994 08:49:37 GMT"},
      {"Sun, 03 Jan 1999 08:49:37 GMT", "Sunday, 03-Jan-1999 08:49:37 GMT"},
      {"Sun, 07 Feb 1999 08:49:37 GMT", "Sunday, 07-Feb-1999 08:49:37 GMT"},
      {"Sun, 07 Mar 1999 08:49:37 GMT", "Sunday, 07-Mar-1999 08:49:37 GMT"},
      {"Sun, 04 Apr 1999 08:49:37 GMT", "Sunday, 04-Apr-1999 08:49:37 GMT"},
      {"Sun, 02 May 1999 08:49:37 GMT", "Sunday, 02-May-1999 08:49:37 GMT"},
      {"Sun, 06 Jun 1999 08:49:37 GMT", "Sunday, 06-Jun-1999 08:49:37 GMT"},
      {"Sun, 04 Jul 1999 08:49:37 GMT", "Sunday, 04-Jul-1999 08:49:37 GMT"},
      {"Sun, 01 Aug 1999 08:49:37 GMT", "Sunday, 01-Aug-1999 08:49:37 GMT"},
      {"Sun, 05 Sep 1999 08:49:37 GMT", "Sunday, 05-Sep-1999 08:49:37 GMT"},
      {"Sun, 03 Oct 1999 08:49:37 GMT", "Sunday, 03-Oct-1999 08:49:37 GMT"},
      {"Sun, 07 Nov 1999 08:49:37 GMT", "Sunday, 07-Nov-1999 08:49:37 GMT"},
      {"Sun, 05 Dec 1999 08:49:37 GMT", "Sunday, 05-Dec-1999 08:49:37 GMT"},
      {nullptr, nullptr},
    };

    int i;
    time_t fast_t, slow_t;

    for (i = 0; dates[i].fast; i++) {
      fast_t = mime_parse_date(dates[i].fast, dates[i].fast + static_cast<int>(strlen(dates[i].fast)));
      slow_t = mime_parse_date(dates[i].slow, dates[i].slow + static_cast<int>(strlen(dates[i].slow)));
      // compare with strptime here!
      if (fast_t != slow_t) {
        std::printf("FAILED: date %lu (%s) != %lu (%s)\n", static_cast<unsigned long>(fast_t), dates[i].fast,
                    static_cast<unsigned long>(slow_t), dates[i].slow);
        CHECK(false);
      }
    }
  }

  SECTION("Test format date")
  {
    static const char *dates[] = {
      "Sun, 06 Nov 1994 08:49:37 GMT",
      "Sun, 03 Jan 1999 08:49:37 GMT",
      "Sun, 05 Dec 1999 08:49:37 GMT",
      "Tue, 25 Apr 2000 20:29:53 GMT",
      nullptr,
    };

    // (1) Test a few hand-created dates

    int i;
    time_t t, t2, t3;
    char buffer[128], buffer2[128];
    static const char *envstr = "TZ=GMT0";

    // shift into GMT timezone for cftime conversions
    putenv(const_cast<char *>(envstr));
    tzset();

    for (i = 0; dates[i]; i++) {
      t = mime_parse_date(dates[i], dates[i] + static_cast<int>(strlen(dates[i])));

      cftime_replacement(buffer, sizeof(buffer), "%a, %d %b %Y %T %Z", &t);
      if (memcmp(dates[i], buffer, 29) != 0) {
        std::printf("FAILED: original date doesn't match cftime date\n");
        std::printf("  input date:  %s\n", dates[i]);
        std::printf("  cftime date: %s\n", buffer);
        CHECK(false);
      }

      mime_format_date(buffer, t);
      if (memcmp(dates[i], buffer, 29) != 0) {
        std::printf("FAILED: original date doesn't match mime_format_date date\n");
        std::printf("  input date:  %s\n", dates[i]);
        std::printf("  cftime date: %s\n", buffer);
        CHECK(false);
      }
    }

    // (2) test a few times per day from 1/1/1970 to past 2010

    // coverity[dont_call]
    for (t = 0; t < 40 * 366 * (24 * 60 * 60); t += static_cast<int>(drand48() * (24 * 60 * 60))) {
      cftime_replacement(buffer, sizeof(buffer), "%a, %d %b %Y %T %Z", &t);
      t2 = mime_parse_date(buffer, buffer + static_cast<int>(strlen(buffer)));
      if (t2 != t) {
        std::printf("FAILED: parsed time_t doesn't match original time_t\n");
        std::printf("  input time_t:  %d (%s)\n", static_cast<int>(t), buffer);
        std::printf("  parsed time_t: %d\n", static_cast<int>(t2));
        CHECK(false);
      }
      mime_format_date(buffer2, t);
      if (memcmp(buffer, buffer2, 29) != 0) {
        std::printf("FAILED: formatted date doesn't match original date\n");
        std::printf("  original date:  %s\n", buffer);
        std::printf("  formatted date: %s\n", buffer2);
        CHECK(false);
      }
      t3 = mime_parse_date(buffer2, buffer2 + static_cast<int>(strlen(buffer2)));
      if (t != t3) {
        std::printf("FAILED: parsed time_t doesn't match original time_t\n");
        std::printf("  input time_t:  %d (%s)\n", static_cast<int>(t), buffer2);
        std::printf("  parsed time_t: %d\n", static_cast<int>(t3));
        CHECK(false);
      }
    }
  }

  SECTION("Test url")
  {
    static const char *strs[] = {
      "http://some.place/path;params?query#fragment",

      // Start with an easy one...
      "http://trafficserver.apache.org/index.html",

      "cheese://bogosity",

      "some.place",
      "some.place/",
      "http://some.place",
      "http://some.place/",
      "http://some.place/path",
      "http://some.place/path;params",
      "http://some.place/path;params?query",
      "http://some.place/path;params?query#fragment",
      "http://some.place/path?query#fragment",
      "http://some.place/path#fragment",

      "some.place:80",
      "some.place:80/",
      "http://some.place:80",
      "http://some.place:80/",

      "foo@some.place:80",
      "foo@some.place:80/",
      "http://foo@some.place:80",
      "http://foo@some.place:80/",

      "foo:bar@some.place:80",
      "foo:bar@some.place:80/",
      "http://foo:bar@some.place:80",
      "http://foo:bar@some.place:80/",

      // Some address stuff
      "http://172.16.28.101",
      "http://172.16.28.101:8080",
      "http://[::]",
      "http://[::1]",
      "http://[fc01:172:16:28::101]",
      "http://[fc01:172:16:28::101]:80",
      "http://[fc01:172:16:28:BAAD:BEEF:DEAD:101]",
      "http://[fc01:172:16:28:BAAD:BEEF:DEAD:101]:8080",
      "http://172.16.28.101/some/path",
      "http://172.16.28.101:8080/some/path",
      "http://[::1]/some/path",
      "http://[fc01:172:16:28::101]/some/path",
      "http://[fc01:172:16:28::101]:80/some/path",
      "http://[fc01:172:16:28:BAAD:BEEF:DEAD:101]/some/path",
      "http://[fc01:172:16:28:BAAD:BEEF:DEAD:101]:8080/some/path",
      "http://172.16.28.101/",
      "http://[fc01:172:16:28:BAAD:BEEF:DEAD:101]:8080/",

      // "foo:@some.place", TODO - foo:@some.place is change to foo@some.place in the test
      "foo:bar@some.place",
      "foo:bar@some.place/",
      "http://foo:bar@some.place",
      "http://foo:bar@some.place/",
      "http://foo:bar@[::1]:8080/",
      "http://foo@[::1]",

      "mms://sm02.tsqa.example.com/0102rally.asf",
      "pnm://foo:bar@some.place:80/path;params?query#fragment",
      "rtsp://foo:bar@some.place:80/path;params?query#fragment",
      "rtspu://foo:bar@some.place:80/path;params?query#fragment",
      "/finance/external/cbsm/*http://cbs.marketwatch.com/archive/19990713/news/current/net.htx?source=blq/yhoo&dist=yhoo",
      "http://a.b.com/xx.jpg?newpath=http://bob.dave.com",

      "ht-tp://a.b.com",
      "ht+tp://a.b.com",
      "ht.tp://a.b.com",

      "h1ttp://a.b.com",
      "http1://a.b.com",
    };

    static const char *bad[] = {
      "http://[1:2:3:4:5:6:7:8:9]",
      "http://1:2:3:4:5:6:7:8:A:B",
      "http://bob.com[::1]",
      "http://[::1].com",

      "http://foo:bar:baz@bob.com/",
      "http://foo:bar:baz@[::1]:8080/",

      "http://]",
      "http://:",

      "http:/",
      "http:/foo.bar.com/",
      "~http://invalid.char.in.scheme/foo",
      "http~://invalid.char.in.scheme/foo",
      "ht~tp://invalid.char.in.scheme/foo",
      "1http://first.char.not.alpha",
      "some.domain.com/http://invalid.domain/foo",
      ":",
      "://",

      // maybe this should be a valid URL
      "a.b.com/xx.jpg?newpath=http://bob.dave.com",
    };

    int err, failed = 0;
    URL url;
    const char *start;
    const char *end;
    int old_length, new_length;

    for (unsigned i = 0; i < countof(strs); i++) {
      old_length = static_cast<int>(strlen(strs[i]));
      start      = strs[i];
      end        = start + old_length;

      url.create(nullptr);
      err = url.parse(&start, end);
      if (err < 0) {
        std::printf("Failed to parse url '%s'\n", start);
        failed = 1;
        break;
      }

      char print_buf[1024];
      new_length = 0;
      int offset = 0;
      url.print(print_buf, 1024, &new_length, &offset);
      print_buf[new_length] = '\0';

      const char *fail_text = nullptr;

      if (old_length == new_length) {
        if (memcmp(print_buf, strs[i], new_length) != 0) {
          fail_text = "URLS DIFFER";
        }
      } else if (old_length == new_length - 1) {
        // Check to see if the difference is the trailing
        //   slash we add
        if (memcmp(print_buf, strs[i], old_length) != 0 || print_buf[new_length - 1] != '/' || (strs[i])[old_length - 1] == '/') {
          fail_text = "TRAILING SLASH";
        }
      } else {
        fail_text = "LENGTHS DIFFER";
      }

      if (fail_text) {
        failed = 1;
        std::printf("%16s: OLD: (%4d) %s\n", fail_text, old_length, strs[i]);
        std::printf("%16s: NEW: (%4d) %s\n", "", new_length, print_buf);
        obj_describe(url.m_url_impl, true);
      } else {
        std::printf("%16s: '%s'\n", "PARSE SUCCESS", strs[i]);
      }

      url.destroy();
    }

    for (unsigned i = 0; i < countof(bad); ++i) {
      const char *x = bad[i];

      url.create(nullptr);
      err = url.parse(x, strlen(x));
      url.destroy();
      if (err == PARSE_RESULT_DONE) {
        failed = 1;
        std::printf("Successfully parsed invalid url '%s'", x);
        break;
      } else {
        std::printf("   bad URL - PARSE FAILED: '%s'\n", bad[i]);
      }
    }

    CHECK(failed == 0);
  }

  SECTION("Test mime")
  {
    // This can not be a static string (any more) since we unfold the headers
    // in place.
    char mime[] = {
      //        "Date: Tuesday, 08-Dec-98 20:32:17 GMT\r\n"
      "Date: 6 Nov 1994 08:49:37 GMT\r\n"
      "Max-Forwards: 65535\r\n"
      "Cache-Control: private\r\n"
      "accept: foo\r\n"
      "accept: bar\n"
      ": (null) field name\r\n"
      "aCCept: \n"
      "ACCEPT\r\n"
      "foo: bar\r\n"
      "foo: argh\r\n"
      "foo: three, four\r\n"
      "word word: word \r\n"
      "accept: \"fazzle, dazzle\"\r\n"
      "accept: 1, 2, 3, 4, 5, 6, 7, 8\r\n"
      "continuation: part1\r\n"
      " part2\r\n"
      "scooby: doo\r\n"
      " scooby: doo\r\n"
      "bar: foo\r\n"
      "\r\n",
    };

    int err;
    MIMEHdr hdr;
    MIMEParser parser;
    const char *start;
    const char *end;

    std::printf("   <<< MUST BE HAND-VERIFIED FOR FULL-BENEFIT>>>\n\n");

    start = mime;
    end   = start + strlen(start);

    mime_parser_init(&parser);

    bool must_copy_strs = false;

    hdr.create(nullptr);
    err = hdr.parse(&parser, &start, end, must_copy_strs, false, false);

    REQUIRE(err >= 0);

    // Test the (new) continuation line folding to be correct. This should replace the
    // \r\n with two spaces (so a total of three between "part1" and "part2").
    int length               = 0;
    const char *continuation = hdr.value_get("continuation", 12, &length);

    if ((13 != length)) {
      std::printf("FAILED: continue header folded line was too short\n");
      REQUIRE(false);
    }

    if (strncmp(continuation + 5, "   ", 3)) {
      std::printf("FAILED: continue header unfolding did not produce correct WS's\n");
      REQUIRE(false);
    }

    if (strncmp(continuation, "part1   part2", 13)) {
      std::printf("FAILED: continue header unfolding was not correct\n");
      REQUIRE(false);
    }

    hdr.field_delete("not_there", 9);
    hdr.field_delete("accept", 6);
    hdr.field_delete("scooby", 6);
    hdr.field_delete("scooby", 6);
    hdr.field_delete("bar", 3);
    hdr.field_delete("continuation", 12);

    int count = hdr.fields_count();
    std::printf("hdr.fields_count() = %d\n", count);

    int i_max_forwards = hdr.value_get_int("Max-Forwards", 12);
    int u_max_forwards = hdr.value_get_uint("Max-Forwards", 12);
    std::printf("i_max_forwards = %d   u_max_forwards = %d\n", i_max_forwards, u_max_forwards);

    hdr.set_age(9999);

    length = hdr.length_get();
    std::printf("hdr.length_get() = %d\n", length);

    time_t t0, t1, t2;

    t0 = hdr.get_date();
    if (t0 == 0) {
      std::printf("FAILED: Initial date is zero but shouldn't be\n");
      REQUIRE(false);
    }

    t1 = time(nullptr);
    hdr.set_date(t1);
    t2 = hdr.get_date();
    if (t1 != t2) {
      std::printf("FAILED: set_date(%" PRId64 ") ... get_date = %" PRId64 "\n\n", static_cast<int64_t>(t1),
                  static_cast<int64_t>(t2));
      REQUIRE(false);
    }

    hdr.value_append("Cache-Control", 13, "no-cache", 8, true);

    MIMEField *cc_field;
    StrList slist;

    cc_field = hdr.field_find("Cache-Control", 13);

    if (cc_field == nullptr) {
      std::printf("FAILED: missing Cache-Control header\n\n");
      REQUIRE(false);
    }

    // TODO: Do we need to check the "count" returned?
    cc_field->value_get_comma_list(&slist); // FIX: correct usage?

    if (cc_field->value_get_index("Private", 7) < 0) {
      std::printf("Failed: value_get_index of Cache-Control did not find private");
      REQUIRE(false);
    }
    if (cc_field->value_get_index("Bogus", 5) >= 0) {
      std::printf("Failed: value_get_index of Cache-Control incorrectly found bogus");
      REQUIRE(false);
    }
    if (hdr.value_get_index("foo", 3, "three", 5) < 0) {
      std::printf("Failed: value_get_index of foo did not find three");
      REQUIRE(false);
    }
    if (hdr.value_get_index("foo", 3, "bar", 3) < 0) {
      std::printf("Failed: value_get_index of foo did not find bar");
      REQUIRE(false);
    }
    if (hdr.value_get_index("foo", 3, "Bogus", 5) >= 0) {
      std::printf("Failed: value_get_index of foo incorrectly found bogus");
      REQUIRE(false);
    }

    mime_parser_clear(&parser);

    hdr.print(nullptr, 0, nullptr, nullptr);
    std::printf("\n");

    obj_describe((HdrHeapObjImpl *)(hdr.m_mime), true);

    const char *field_name = "Test_heap_reuse";

    MIMEField *f = hdr.field_create(field_name, static_cast<int>(strlen(field_name)));
    REQUIRE(f->m_ptr_value == nullptr);

    hdr.field_attach(f);
    REQUIRE(f->m_ptr_value == nullptr);

    const char *test_value = "mytest";

    std::printf("Testing Heap Reuse..\n");
    hdr.field_value_set(f, "orig_value", strlen("orig_value"));
    const char *m_ptr_value_orig = f->m_ptr_value;
    hdr.field_value_set(f, test_value, strlen(test_value), true);
    REQUIRE(f->m_ptr_value != test_value);       // should be copied
    REQUIRE(f->m_ptr_value == m_ptr_value_orig); // heap doesn't change
    REQUIRE(f->m_len_value == strlen(test_value));
    REQUIRE(memcmp(f->m_ptr_value, test_value, f->m_len_value) == 0);

    m_ptr_value_orig           = f->m_ptr_value;
    const char *new_test_value = "myTest";
    hdr.field_value_set(f, new_test_value, strlen(new_test_value), false);
    REQUIRE(f->m_ptr_value != new_test_value);   // should be copied
    REQUIRE(f->m_ptr_value != m_ptr_value_orig); // new heap
    REQUIRE(f->m_len_value == strlen(new_test_value));
    REQUIRE(memcmp(f->m_ptr_value, new_test_value, f->m_len_value) == 0);

    hdr.fields_clear();

    hdr.destroy();
  }

  SECTION("Test http hdr print and copy")
  {
    static struct {
      const char *req;
      const char *req_tgt;
      const char *rsp;
      const char *rsp_tgt;
    } tests[] = {
      {"GET http://foo.com/bar.txt HTTP/1.0\r\n"
       "Accept-Language: fjdfjdslkf dsjkfdj flkdsfjlk sjfdlk ajfdlksa\r\n"
       "\r\n",
       "GET http://foo.com/bar.txt HTTP/1.0\r\n"
       "Accept-Language: fjdfjdslkf dsjkfdj flkdsfjlk sjfdlk ajfdlksa\r\n"
       "\r\n",
       "HTTP/1.0 200 OK\r\n"
       "\r\n",
       "HTTP/1.0 200 OK\r\n"
       "\r\n"},
      {"GET http://foo.com/bar.txt HTTP/1.0\r\n"
       "Accept-Language: fjdfjdslkf dsjkfdj flkdsfjlk sjfdlk ajfdlksa fjfj dslkfjdslk fjsdafkl dsajfkldsa jfkldsafj "
       "klsafjs lkafjdsalk fsdjakfl sdjaflkdsaj flksdjflsd ;ffd salfdjs lf;sdaf ;dsaf jdsal;fdjsaflkjsda \r\n"
       "\r\n",
       "GET http://foo.com/bar.txt HTTP/1.0\r\n"
       "Accept-Language: fjdfjdslkf dsjkfdj flkdsfjlk sjfdlk ajfdlksa fjfj dslkfjdslk fjsdafkl dsajfkldsa jfkldsafj "
       "klsafjs lkafjdsalk fsdjakfl sdjaflkdsaj flksdjflsd ;ffd salfdjs lf;sdaf ;dsaf jdsal;fdjsaflkjsda \r\n"
       "\r\n",
       "HTTP/1.0 200 OK\r\n"
       "\r\n",
       "HTTP/1.0 200 OK\r\n"
       "\r\n"},
      {"GET http://foo.com/bar.txt HTTP/1.0\r\n"
       "Accept-Language: fjdfjdslkf dsjkfdj flkdsfjlk sjfdlk ajfdlksa fjfj dslkfjdslk fjsdafkl dsajfkldsa jfkldsafj "
       "klsafjs lkafjdsalk fsdjakfl sdjaflkdsaj flksdjflsd ;ffd salfdjs lf;sdaf ;dsaf jdsal;fdjsaflkjsda kfl; fsdajfl; "
       "sdjafl;dsajlsjfl;sdafjsdal;fjds al;fdjslaf ;slajdk;f\r\n"
       "\r\n",
       "GET http://foo.com/bar.txt HTTP/1.0\r\n"
       "Accept-Language: fjdfjdslkf dsjkfdj flkdsfjlk sjfdlk ajfdlksa fjfj dslkfjdslk fjsdafkl dsajfkldsa jfkldsafj "
       "klsafjs lkafjdsalk fsdjakfl sdjaflkdsaj flksdjflsd ;ffd salfdjs lf;sdaf ;dsaf jdsal;fdjsaflkjsda kfl; fsdajfl; "
       "sdjafl;dsajlsjfl;sdafjsdal;fjds al;fdjslaf ;slajdk;f\r\n"
       "\r\n",
       "HTTP/1.0 200 OK\r\n"
       "\r\n",
       "HTTP/1.0 200 OK\r\n"
       "\r\n"},
      {"GET http://people.netscape.com/jwz/hacks-1.gif HTTP/1.0\r\n"
       "If-Modified-Since: Wednesday, 26-Feb-97 06:58:17 GMT; length=842\r\n"
       "Referer: chocolate fribble\r\n", // missing final CRLF
       "GET http://people.netscape.com/jwz/hacks-1.gif HTTP/1.0\r\n"
       "If-Modified-Since: Wednesday, 26-Feb-97 06:58:17 GMT; length=842\r\n"
       "Referer: chocolate fribble\r\n"
       "\r\n",
       "HTTP/1.0 200 OK\r\n"
       "MIME-Version: 1.0\r\n"
       "Server: WebSTAR/2.1 ID/30013\r\n"
       "Content-Type: text/html\r\n"
       "Content-Length: 939\r\n"
       "Last-Modified: Thursday, 01-Jan-04 05:00:00 GMT\r\n", // missing final CRLF
       "HTTP/1.0 200 OK\r\n"
       "MIME-Version: 1.0\r\n"
       "Server: WebSTAR/2.1 ID/30013\r\n"
       "Content-Type: text/html\r\n"
       "Content-Length: 939\r\n"
       "Last-Modified: Thursday, 01-Jan-04 05:00:00 GMT\r\n"
       "\r\n"},
      {"GET http://people.netscape.com/jwz/hacks-1.gif HTTP/1.0\r\n"
       "If-Modified-Since: Wednesday, 26-Feb-97 06:58:17 GMT; length=842\r\n"
       "Referer: \r\n", // missing final CRLF
       "GET http://people.netscape.com/jwz/hacks-1.gif HTTP/1.0\r\n"
       "If-Modified-Since: Wednesday, 26-Feb-97 06:58:17 GMT; length=842\r\n"
       "Referer: \r\n"
       "\r\n",
       "HTTP/1.0 200 OK\r\n"
       "MIME-Version: 1.0\r\n"
       "Server: WebSTAR/2.1 ID/30013\r\n"
       "Content-Type: text/html\r\n"
       "Content-Length: 939\r\n"
       "Last-Modified: Thursday, 01-Jan-04 05:00:00 GMT\r\n"
       "\r\n",
       "HTTP/1.0 200 OK\r\n"
       "MIME-Version: 1.0\r\n"
       "Server: WebSTAR/2.1 ID/30013\r\n"
       "Content-Type: text/html\r\n"
       "Content-Length: 939\r\n"
       "Last-Modified: Thursday, 01-Jan-04 05:00:00 GMT\r\n"
       "\r\n"},
      {"GET http://www.news.com:80/ HTTP/1.0\r\n"
       "Proxy-Connection: Keep-Alive\r\n"
       "User-Agent: Mozilla/4.04 [en] (X11; I; Linux 2.0.33 i586)\r\n"
       "Pragma: no-cache\r\n"
       "Host: www.news.com\r\n"
       "Accept: image/gif, image/x-xbitmap, image/jpeg, image/pjpeg, image/png, */*\r\n"
       "Accept-Language: en\r\n"
       "Accept-Charset: iso-8859-1, *, utf-8\r\n"
       "Client-ip: D1012148\r\n"
       "Foo: abcdefghijklmnopqrtu\r\n"
       "\r\n",
       "GET http://www.news.com:80/ HTTP/1.0\r\n"
       "Proxy-Connection: Keep-Alive\r\n"
       "User-Agent: Mozilla/4.04 [en] (X11; I; Linux 2.0.33 i586)\r\n"
       "Pragma: no-cache\r\n"
       "Host: www.news.com\r\n"
       "Accept: image/gif, image/x-xbitmap, image/jpeg, image/pjpeg, image/png, */*\r\n"
       "Accept-Language: en\r\n"
       "Accept-Charset: iso-8859-1, *, utf-8\r\n"
       "Client-ip: D1012148\r\n"
       "Foo: abcdefghijklmnopqrtu\r\n"
       "\r\n",
       "HTTP/1.0 200 OK\r\n"
       "Content-Length: 16428\r\n"
       "Content-Type: text/html\r\n"
       "\r\n",
       "HTTP/1.0 200 OK\r\n"
       "Content-Length: 16428\r\n"
       "Content-Type: text/html\r\n"
       "\r\n"},
      {"GET http://people.netscape.com/jwz/hacks-1.gif HTTP/1.0\r\n"
       "If-Modified-Since: Wednesday, 26-Feb-97 06:58:17 GMT; length=842\r\n"
       "Referer: http://people.netscape.com/jwz/index.html\r\n"
       "Proxy-Connection: Keep-Alive\r\n"
       "User-Agent:  Mozilla/3.01 (X11; I; Linux 2.0.28 i586)\r\n"
       "Pragma: no-cache\r\n"
       "Host: people.netscape.com\r\n"
       "Accept: image/gif, image/x-xbitmap, image/jpeg, image/pjpeg, */*\r\n"
       "\r\n",
       "GET http://people.netscape.com/jwz/hacks-1.gif HTTP/1.0\r\n"
       "If-Modified-Since: Wednesday, 26-Feb-97 06:58:17 GMT; length=842\r\n"
       "Referer: http://people.netscape.com/jwz/index.html\r\n"
       "Proxy-Connection: Keep-Alive\r\n"
       "User-Agent:  Mozilla/3.01 (X11; I; Linux 2.0.28 i586)\r\n"
       "Pragma: no-cache\r\n"
       "Host: people.netscape.com\r\n"
       "Accept: image/gif, image/x-xbitmap, image/jpeg, image/pjpeg, */*\r\n"
       "\r\n",
       "HTTP/1.0 200 OK\r\n"
       "Content-Length: 16428\r\n"
       "Content-Type: text/html\r\n"
       "\r\n",
       "HTTP/1.0 200 OK\r\n"
       "Content-Length: 16428\r\n"
       "Content-Type: text/html\r\n"
       "\r\n"},
    };

    int ntests = sizeof(tests) / sizeof(tests[0]);
    int i;

    for (i = 0; i < ntests; i++) {
      int status = test_http_hdr_print_and_copy_aux(i + 1, tests[i].req, tests[i].req_tgt, tests[i].rsp, tests[i].rsp_tgt);
      CHECK(status != 0);

      // Test for expected failures
      // parse with a '\0' in the header.  Should fail
      status = test_http_hdr_null_char(i + 1, tests[i].req, tests[i].req_tgt);
      CHECK(status != 0);

      // Parse with a CTL character in the method name.  Should fail
      status = test_http_hdr_ctl_char(i + 1, tests[i].req, tests[i].req_tgt);
      CHECK(status != 0);
    }
  }

  SECTION("Test http")
  {
    static const char request0[] = {
      "GET http://www.news.com:80/ HTTP/1.0\r\n"
      "Proxy-Connection: Keep-Alive\r\n"
      "User-Agent: Mozilla/4.04 [en] (X11; I; Linux 2.0.33 i586)\r\n"
      "Pragma: no-cache\r\n"
      "Host: www.news.com\r\n"
      "Accept: image/gif, image/x-xbitmap, image/jpeg, image/pjpeg, image/png, */*\r\n"
      "Accept-Language: en\r\n"
      "Accept-Charset: iso-8859-1, *, utf-8\r\n"
      "Cookie: u_vid_0_0=00031ba3; "
      "s_cur_0_0=0101sisi091314775496e7d3Jx4+POyJakrMybmNOsq6XOn5bVn5Z6a4Ln5crU5M7Rxq2lm5aWpqupo20=; "
      "SC_Cnet001=Sampled; SC_Cnet002=Sampled\r\n"
      "Client-ip: D1012148\r\n"
      "Foo: abcdefghijklmnopqrtu\r\n"
      "\r\n",
    };

    static const char request09[] = {
      "GET /index.html\r\n"
      "\r\n",
    };

    static const char request1[] = {
      "GET http://people.netscape.com/jwz/hacks-1.gif HTTP/1.0\r\n"
      "If-Modified-Since: Wednesday, 26-Feb-97 06:58:17 GMT; length=842\r\n"
      "Referer: http://people.netscape.com/jwz/index.html\r\n"
      "Proxy-Connection: Keep-Alive\r\n"
      "User-Agent:  Mozilla/3.01 (X11; I; Linux 2.0.28 i586)\r\n"
      "Pragma: no-cache\r\n"
      "Host: people.netscape.com\r\n"
      "Accept: image/gif, image/x-xbitmap, image/jpeg, image/pjpeg, */*\r\n"
      "\r\n",
    };

    static const char request_no_colon[] = {
      "GET http://people.netscape.com/jwz/hacks-1.gif HTTP/1.0\r\n"
      "If-Modified-Since Wednesday, 26-Feb-97 06:58:17 GMT; length=842\r\n"
      "Referer http://people.netscape.com/jwz/index.html\r\n"
      "Proxy-Connection Keep-Alive\r\n"
      "User-Agent  Mozilla/3.01 (X11; I; Linux 2.0.28 i586)\r\n"
      "Pragma no-cache\r\n"
      "Host people.netscape.com\r\n"
      "Accept image/gif, image/x-xbitmap, image/jpeg, image/pjpeg, */*\r\n"
      "\r\n",
    };

    static const char request_no_val[] = {
      "GET http://people.netscape.com/jwz/hacks-1.gif HTTP/1.0\r\n"
      "If-Modified-Since:\r\n"
      "Referer:     "
      "Proxy-Connection:\r\n"
      "User-Agent:     \r\n"
      "Host:::\r\n"
      "\r\n",
    };

    static const char request_multi_fblock[] = {
      "GET http://people.netscape.com/jwz/hacks-1.gif HTTP/1.0\r\n"
      "If-Modified-Since: Wednesday, 26-Feb-97 06:58:17 GMT; length=842\r\n"
      "Referer: http://people.netscape.com/jwz/index.html\r\n"
      "Proxy-Connection: Keep-Alive\r\n"
      "User-Agent:  Mozilla/3.01 (X11; I; Linux 2.0.28 i586)\r\n"
      "Pragma: no-cache\r\n"
      "Host: people.netscape.com\r\n"
      "Accept: image/gif, image/x-xbitmap, image/jpeg, image/pjpeg, */*\r\n"
      "X-1: blah\r\n"
      "X-2: blah\r\n"
      "X-3: blah\r\n"
      "X-4: blah\r\n"
      "X-5: blah\r\n"
      "X-6: blah\r\n"
      "X-7: blah\r\n"
      "X-8: blah\r\n"
      "X-9: blah\r\n"
      "Pragma: no-cache\r\n"
      "X-X-1: blah\r\n"
      "X-X-2: blah\r\n"
      "X-X-3: blah\r\n"
      "X-X-4: blah\r\n"
      "X-X-5: blah\r\n"
      "X-X-6: blah\r\n"
      "X-X-7: blah\r\n"
      "X-X-8: blah\r\n"
      "X-X-9: blah\r\n"
      "\r\n",
    };

    static const char request_leading_space[] = {
      " GET http://www.news.com:80/ HTTP/1.0\r\n"
      "Proxy-Connection: Keep-Alive\r\n"
      "User-Agent: Mozilla/4.04 [en] (X11; I; Linux 2.0.33 i586)\r\n"
      "\r\n",
    };

    static const char request_padding[] = {
      "GET http://www.padding.com:80/ HTTP/1.0\r\n"
      "X-1: blah1\r\n"
      //       "X-2:  blah2\r\n"
      "X-3:   blah3\r\n"
      //       "X-4:    blah4\r\n"
      "X-5:     blah5\r\n"
      //       "X-6:      blah6\r\n"
      "X-7:       blah7\r\n"
      //       "X-8:        blah8\r\n"
      "X-9:         blah9\r\n"
      "\r\n",
    };

    static const char request_09p[] = {
      "GET http://www.news09.com/\r\n"
      "\r\n",
    };

    static const char request_09ht[] = {
      "GET http://www.news09.com/ HT\r\n"
      "\r\n",
    };

    static const char request_11[] = {
      "GET http://www.news.com/ HTTP/1.1\r\n"
      "Connection: close\r\n"
      "\r\n",
    };

    static const char request_too_long[] = {
      "GET http://www.news.com/i/am/too/long HTTP/1.1\r\n"
      "Connection: close\r\n"
      "\r\n",
    };

    static const char request_unterminated[] = {
      "GET http://www.unterminated.com/ HTTP/1.1",
    };

    static const char request_blank[] = {
      "\r\n",
    };

    static const char request_blank2[] = {
      "\r\n"
      "\r\n",
    };

    static const char request_blank3[] = {
      "     "
      "\r\n",
    };

    ////////////////////////////////////////////////////

    static const char response0[] = {
      "HTTP/1.0 200 OK\r\n"
      "MIME-Version: 1.0\r\n"
      "Server: WebSTAR/2.1 ID/30013\r\n"
      "Content-Type: text/html\r\n"
      "Content-Length: 939\r\n"
      "Last-Modified: Thursday, 01-Jan-04 05:00:00 GMT\r\n"
      "\r\n",
    };

    static const char response1[] = {
      "HTTP/1.0 200 OK\r\n"
      "Server: Netscape-Communications/1.12\r\n"
      "Date: Tuesday, 08-Dec-98 20:32:17 GMT\r\n"
      "Content-Type: text/html\r\n"
      "\r\n",
    };

    static const char response_no_colon[] = {
      "HTTP/1.0 200 OK\r\n"
      "Server Netscape-Communications/1.12\r\n"
      "Date: Tuesday, 08-Dec-98 20:32:17 GMT\r\n"
      "Content-Type: text/html\r\n"
      "\r\n",
    };

    static const char response_unterminated[] = {
      "HTTP/1.0 200 OK",
    };

    static const char response09[] = {
      "",
    };

    static const char response_blank[] = {
      "\r\n",
    };

    static const char response_blank2[] = {
      "\r\n"
      "\r\n",
    };

    static const char response_blank3[] = {
      "     "
      "\r\n",
    };

    static const char response_too_long_req[] = {
      "HTTP/1.0 414 URI Too Long\r\n"
      "\r\n",
    };

    struct RequestResponse {
      char const *request;
      char const *response;
    };

    RequestResponse rr[] = {{request0, response0},
                            {request09, response09},
                            {request1, response1},
                            {request_no_colon, response_no_colon},
                            {request_no_val, response_no_colon},
                            {request_leading_space, response0},
                            {request_multi_fblock, response0},
                            {request_padding, response0},
                            {request_09p, response0},
                            {request_09ht, response0},
                            {request_11, response0},
                            {request_unterminated, response_unterminated},
                            {request_blank, response_blank},
                            {request_blank2, response_blank2},
                            {request_blank3, response_blank3}};

    int err;
    HTTPHdr req_hdr, rsp_hdr;
    HTTPParser parser;
    const char *start;
    const char *end;
    const char *request;
    const char *response;

    for (unsigned idx = 0; idx < (sizeof(rr) / sizeof(*rr)); ++idx) {
      request  = rr[idx].request;
      response = rr[idx].response;

      std::printf("   <<< MUST BE HAND-VERIFIED FOR FULL BENEFIT >>>\n\n");

      /*** (1) parse the request string into req_hdr ***/

      start = request;
      end   = start + strlen(start); // 1 character past end of string

      http_parser_init(&parser);

      req_hdr.create(HTTP_TYPE_REQUEST);
      rsp_hdr.create(HTTP_TYPE_RESPONSE);

      std::printf("======== parsing\n\n");
      while (true) {
        err = req_hdr.parse_req(&parser, &start, end, true);
        if (err != PARSE_RESULT_CONT) {
          break;
        }
      }
      if (err == PARSE_RESULT_ERROR) {
        req_hdr.destroy();
        rsp_hdr.destroy();
        break;
      }

      /*** useless copy to exercise copy function ***/

      HTTPHdr new_hdr;
      new_hdr.create(HTTP_TYPE_REQUEST);
      new_hdr.copy(&req_hdr);
      new_hdr.destroy();

      /*** (2) print out the request ***/

      std::printf("======== real request (length=%d)\n\n", static_cast<int>(strlen(request)));
      std::printf("%s\n", request);

      std::printf("\n[");
      req_hdr.print(nullptr, 0, nullptr, nullptr);
      std::printf("]\n\n");

      obj_describe(req_hdr.m_http, true);

      // req_hdr.destroy ();
      // REQUIRE(!"req_hdr.destroy() not defined");

      /*** (3) parse the response string into rsp_hdr ***/

      start = response;
      end   = start + strlen(start);

      http_parser_clear(&parser);
      http_parser_init(&parser);

      while (true) {
        err = rsp_hdr.parse_resp(&parser, &start, end, true);
        if (err != PARSE_RESULT_CONT) {
          break;
        }
      }
      if (err == PARSE_RESULT_ERROR) {
        req_hdr.destroy();
        rsp_hdr.destroy();
        break;
      }

      http_parser_clear(&parser);

      /*** (4) print out the response ***/

      std::printf("\n======== real response (length=%d)\n\n", static_cast<int>(strlen(response)));
      std::printf("%s\n", response);

      std::printf("\n[");
      rsp_hdr.print(nullptr, 0, nullptr, nullptr);
      std::printf("]\n\n");

      obj_describe(rsp_hdr.m_http, true);

      const int NNN = 1000;
      {
        char buf[NNN];
        int bufindex, last_bufindex;
        int tmp;
        int i;

        bufindex = 0;

        do {
          last_bufindex = bufindex;
          tmp           = bufindex;
          buf[0]        = '#'; // make it obvious if hdr.print doesn't print anything
          err           = rsp_hdr.print(buf, NNN, &bufindex, &tmp);

          // std::printf("test_header: tmp = %d  err = %d  bufindex = %d\n", tmp, err, bufindex);
          putchar('{');
          for (i = 0; i < bufindex - last_bufindex; i++) {
            if (!iscntrl(buf[i])) {
              putchar(buf[i]);
            } else {
              std::printf("\\%o", buf[i]);
            }
          }
          putchar('}');
        } while (!err);
      }

      // rsp_hdr.print (NULL, 0, NULL, NULL);

      req_hdr.destroy();
      rsp_hdr.destroy();
    }

    {
      request  = request_too_long;
      response = response_too_long_req;

      int status = 1;

      /*** (1) parse the request string into req_hdr ***/

      start = request;
      end   = start + strlen(start); // 1 character past end of string

      http_parser_init(&parser);

      req_hdr.create(HTTP_TYPE_REQUEST);
      rsp_hdr.create(HTTP_TYPE_RESPONSE);

      std::printf("======== test_http_req_parse_error parsing\n\n");
      err = req_hdr.parse_req(&parser, &start, end, true, true, 1);
      if (err != PARSE_RESULT_ERROR) {
        status = 0;
      }

      http_parser_clear(&parser);

      /*** (4) print out the response ***/

      std::printf("\n======== real response (length=%d)\n\n", static_cast<int>(strlen(response)));
      std::printf("%s\n", response);

      obj_describe(rsp_hdr.m_http, true);

      req_hdr.destroy();
      rsp_hdr.destroy();

      CHECK(status != 0);
    }
  }

  SECTION("Test http mutation")
  {
    std::printf("   <<< MUST BE HAND-VERIFIED FOR FULL BENEFIT>>>\n\n");

    HTTPHdr resp_hdr;
    int err, i;
    HTTPParser parser;
    const char base_resp[] = "HTTP/1.0 200 OK\r\n\r\n";
    const char *start, *end;

    /*** (1) parse the response string into req_hdr ***/

    start = base_resp;
    end   = start + strlen(start);

    http_parser_init(&parser);

    resp_hdr.create(HTTP_TYPE_RESPONSE);

    while (true) {
      err = resp_hdr.parse_resp(&parser, &start, end, true);
      if (err != PARSE_RESULT_CONT) {
        break;
      }
    }

    std::printf("\n======== before mutation ==========\n\n");
    std::printf("\n[");
    resp_hdr.print(nullptr, 0, nullptr, nullptr);
    std::printf("]\n\n");

    /*** (2) add in a bunch of header fields ****/
    char field_name[1024];
    char field_value[1024];
    for (i = 1; i <= 100; i++) {
      snprintf(field_name, sizeof(field_name), "Test%d", i);
      snprintf(field_value, sizeof(field_value), "%d %d %d %d %d", i, i, i, i, i);
      resp_hdr.value_set(field_name, static_cast<int>(strlen(field_name)), field_value, static_cast<int>(strlen(field_value)));
    }

    /**** (3) delete all the even numbered fields *****/
    for (i = 2; i <= 100; i += 2) {
      snprintf(field_name, sizeof(field_name), "Test%d", i);
      resp_hdr.field_delete(field_name, static_cast<int>(strlen(field_name)));
    }

    /***** (4) add in secondary fields for all multiples of 3 ***/
    for (i = 3; i <= 100; i += 3) {
      snprintf(field_name, sizeof(field_name), "Test%d", i);
      MIMEField *f = resp_hdr.field_create(field_name, static_cast<int>(strlen(field_name)));
      resp_hdr.field_attach(f);
      snprintf(field_value, sizeof(field_value), "d %d %d %d %d %d", i, i, i, i, i);
      f->value_set(resp_hdr.m_heap, resp_hdr.m_mime, field_value, static_cast<int>(strlen(field_value)));
    }

    /***** (5) append all fields with multiples of 5 ***/
    for (i = 5; i <= 100; i += 5) {
      snprintf(field_name, sizeof(field_name), "Test%d", i);
      snprintf(field_value, sizeof(field_value), "a %d", i);

      resp_hdr.value_append(field_name, static_cast<int>(strlen(field_name)), field_value, static_cast<int>(strlen(field_value)),
                            true);
    }

    /**** (6) delete all multiples of nine *****/
    for (i = 9; i <= 100; i += 9) {
      snprintf(field_name, sizeof(field_name), "Test%d", i);
      resp_hdr.field_delete(field_name, static_cast<int>(strlen(field_name)));
    }

    std::printf("\n======== mutated response ==========\n\n");
    std::printf("\n[");
    resp_hdr.print(nullptr, 0, nullptr, nullptr);
    std::printf("]\n\n");

    resp_hdr.destroy();
  }

  SECTION("Test arena")
  {
    Arena *arena;

    arena = new Arena;

    CHECK(test_arena_aux(arena, 1) != 1);
    CHECK(test_arena_aux(arena, 127) != 1);
    CHECK(test_arena_aux(arena, 128) != 1);
    CHECK(test_arena_aux(arena, 129) != 1);
    CHECK(test_arena_aux(arena, 255) != 1);
    CHECK(test_arena_aux(arena, 256) != 1);
    CHECK(test_arena_aux(arena, 16384) != 1);
    CHECK(test_arena_aux(arena, 16385) != 1);
    CHECK(test_arena_aux(arena, 16511) != 1);
    CHECK(test_arena_aux(arena, 16512) != 1);
    CHECK(test_arena_aux(arena, 2097152) != 1);
    CHECK(test_arena_aux(arena, 2097153) != 1);
    CHECK(test_arena_aux(arena, 2097279) != 1);
    CHECK(test_arena_aux(arena, 2097280) != 1);

    delete arena;
  }

  SECTION("Test regex")
  {
    DFA dfa;

    const char *test_harness[] = {"foo", "(.*\\.apache\\.org)", "(.*\\.example\\.com)"};

    dfa.compile(test_harness, SIZEOF(test_harness));
    CHECK(dfa.match("trafficserver.apache.org") == 1);
    CHECK(dfa.match("www.example.com") == 2);
    CHECK(dfa.match("aaaaaafooooooooinktomi....com.org") == -1);
    CHECK(dfa.match("foo") == 0);
  }

  SECTION("Test accept language match")
  {
    struct {
      const char *content_language;
      const char *accept_language;
      float Q;
      int L;
      int I;
    } test_cases[] = {
      {"en", "*", 1.0, 1, 1},
      {"en", "fr", 0.0, 0, 0},
      {"en", "de, fr, en;q=0.7", 0.7, 2, 3},
      {"en-cockney", "de, fr, en;q=0.7", 0.7, 2, 3},
      {"en-cockney", "de, fr, en-foobar;q=0.8, en;q=0.7", 0.7, 2, 4},
      {"en-cockney", "de, fr, en-cockney;q=0.8, en;q=0.7", 0.8, 10, 3},
      {"en-cockney", "de, fr, en;q=0.8, en;q=0.7", 0.8, 2, 3},
      {"en-cockney", "de, fr, en;q=0.7, en;q=0.8", 0.8, 2, 4},
      {"en-cockney", "de, fr, en;q=0.8, en;q=0.8", 0.8, 2, 3},
      {"en-cockney", "de, fr, en-cockney;q=0.7, en;q=0.8", 0.7, 10, 3},
      {"en-cockney", "de, fr, en;q=0.8, en-cockney;q=0.7", 0.7, 10, 4},
      {"en-cockney", "de, fr, en-cockney;q=0.8, en;q=0.8", 0.8, 10, 3},
      {"en-cockney", "de, fr, en-cockney;q=0.8, en;q=0.7", 0.8, 10, 3},
      {"en-cockney", "de, fr, en-american", 0.0, 0, 0},
      {"en-cockney", "de, fr, en;q=0.8, en;q=0.8, *", 0.8, 2, 3},
      {"en-cockney", "de, fr, en;q=0.8, en;q=0.8, *;q=0.9", 0.8, 2, 3},
      {"en-foobar", "de, fr, en;q=0.8, en;q=0.8, *;q=0.9", 0.8, 2, 3},
      {"oo-foobar", "de, fr, en;q=0.8, en;q=0.8, *;q=0.9", 0.9, 1, 5},
      {"oo-foobar", "de, fr, en;q=0.8, en;q=0.8, *;q=0.9, *", 1.0, 1, 6},
      {"oo-foobar", "de, fr, en;q=0.8, en;q=0.8, *, *;q=0.9", 1.0, 1, 5},
      {"fr-belgian", "de, fr;hi-there;q=0.9, fr;q=0.8, en", 0.9, 2, 2},
      {"fr-belgian", "de, fr;q=0.8, fr;hi-there;q=0.9, en", 0.9, 2, 3},
      {nullptr, nullptr, 0.0, 0, 0},
    };

    int i, I, L;
    float Q;

    for (i = 0; test_cases[i].accept_language; i++) {
      StrList acpt_lang_list(false);
      HttpCompat::parse_comma_list(&acpt_lang_list, test_cases[i].accept_language,
                                   static_cast<int>(strlen(test_cases[i].accept_language)));

      Q = HttpCompat::match_accept_language(test_cases[i].content_language,
                                            static_cast<int>(strlen(test_cases[i].content_language)), &acpt_lang_list, &L, &I);

      if ((Q != test_cases[i].Q) || (L != test_cases[i].L) || (I != test_cases[i].I)) {
        std::printf(
          "FAILED: (#%d) got { Q = %.3f; L = %d; I = %d; }, expected { Q = %.3f; L = %d; I = %d; }, from matching\n  '%s' "
          "against '%s'\n",
          i, Q, L, I, test_cases[i].Q, test_cases[i].L, test_cases[i].I, test_cases[i].content_language,
          test_cases[i].accept_language);
        CHECK(false);
      }
    }
  }

  SECTION("Test accept charset match")
  {
    struct {
      const char *content_charset;
      const char *accept_charset;
      float Q;
      int I;
    } test_cases[] = {
      {"iso-8859-1", "*", 1.0, 1},
      {"iso-8859-1", "iso-8859-2", 0.0, 0},
      {"iso-8859-1", "iso-8859", 0.0, 0},
      {"iso-8859-1", "iso-8859-12", 0.0, 0},
      {"iso-8859-1", "koi-8-r", 0.0, 0},
      {"euc-jp", "shift_jis, iso-2022-jp, euc-jp;q=0.7", 0.7, 3},
      {"euc-jp", "shift_jis, iso-2022-jp, euc-jp;q=0.7", 0.7, 3},
      {"euc-jp", "shift_jis, iso-2022-jp, euc-jp;q=0.8, euc-jp;q=0.7", 0.8, 3},
      {"euc-jp", "shift_jis, iso-2022-jp, euc-jp;q=0.7, euc-jp;q=0.8", 0.8, 4},
      {"euc-jp", "euc-jp;q=0.9, shift_jis, iso-2022-jp, euc-jp;q=0.7, euc-jp;q=0.8", 0.9, 1},
      {"EUC-JP", "euc-jp;q=0.9, shift_jis, iso-2022-jp, euc-jp, euc-jp;q=0.8", 1.0, 4},
      {"euc-jp", "euc-jp;q=0.9, shift_jis, iso-2022-jp, EUC-JP, euc-jp;q=0.8", 1.0, 4},
      {"euc-jp", "shift_jis, iso-2022-jp, euc-jp-foobar", 0.0, 0},
      {"euc-jp", "shift_jis, iso-2022-jp, euc-jp-foobar, *", 1.0, 4},
      {"euc-jp", "shift_jis, iso-2022-jp, euc-jp-foobar, *;q=0.543", 0.543, 4},
      {"euc-jp", "shift_jis, iso-2022-jp, euc-jp-foobar, *;q=0.0", 0.0, 4},
      {"euc-jp", "shift_jis, iso-2022-jp, *;q=0.0, euc-jp-foobar, *;q=0.0", 0.0, 3},
      {"euc-jp", "shift_jis, iso-2022-jp, *;q=0.0, euc-jp-foobar, *;q=0.5", 0.5, 5},
      {"euc-jp", "shift_jis, iso-2022-jp, *;q=0.5, euc-jp-foobar, *;q=0.0", 0.5, 3},
      {"euc-jp", "shift_jis, iso-2022-jp, *;q=0.5, euc-jp-foobar, *, *;q=0.0", 1.0, 5},
      {"euc-jp", "shift_jis, euc-jp;hi-there;q=0.5, iso-2022-jp", 0.5, 2},
      {"euc-jp", "shift_jis, euc-jp;hi-there;q= 0.5, iso-2022-jp", 0.5, 2},
      {"euc-jp", "shift_jis, euc-jp;hi-there;q = 0.5, iso-2022-jp", 0.5, 2},
      {"euc-jp", "shift_jis, euc-jp;hi-there ; q = 0.5, iso-2022-jp", 0.5, 2},
      {"euc-jp", "shift_jis, euc-jp;hi-there ;; q = 0.5, iso-2022-jp", 0.5, 2},
      {"euc-jp", "shift_jis, euc-jp;hi-there ;; Q = 0.5, iso-2022-jp", 0.5, 2},
      {nullptr, nullptr, 0.0, 0},
    };

    int i, I;
    float Q;

    for (i = 0; test_cases[i].accept_charset; i++) {
      StrList acpt_lang_list(false);
      HttpCompat::parse_comma_list(&acpt_lang_list, test_cases[i].accept_charset,
                                   static_cast<int>(strlen(test_cases[i].accept_charset)));

      Q = HttpCompat::match_accept_charset(test_cases[i].content_charset, static_cast<int>(strlen(test_cases[i].content_charset)),
                                           &acpt_lang_list, &I);

      if ((Q != test_cases[i].Q) || (I != test_cases[i].I)) {
        std::printf("FAILED: (#%d) got { Q = %.3f; I = %d; }, expected { Q = %.3f; I = %d; }, from matching\n  '%s' against '%s'\n",
                    i, Q, I, test_cases[i].Q, test_cases[i].I, test_cases[i].content_charset, test_cases[i].accept_charset);
        CHECK(false);
      }
    }
  }

  SECTION("Test comma vals")
  {
    static struct {
      const char *value;
      int value_count;
      struct {
        int offset;
        int len;
      } pieces[4];
    } tests[] = {
      {",", 2, {{0, 0}, {1, 0}, {-1, 0}, {-1, 0}}},
      {"", 1, {{0, 0}, {-1, 0}, {-1, 0}, {-1, 0}}},
      {" ", 1, {{0, 0}, {-1, 0}, {-1, 0}, {-1, 0}}},
      {", ", 2, {{0, 0}, {1, 0}, {-1, 0}, {-1, 0}}},
      {",,", 3, {{0, 0}, {1, 0}, {2, 0}, {-1, 0}}},
      {" ,", 2, {{0, 0}, {2, 0}, {-1, 0}, {-1, 0}}},
      {" , ", 2, {{0, 0}, {2, 0}, {-1, 0}, {-1, 0}}},
      {"a, ", 2, {{0, 1}, {2, 0}, {-1, 0}, {-1, 0}}},
      {" a, ", 2, {{1, 1}, {3, 0}, {-1, 0}, {-1, 0}}},
      {" ,a", 2, {{0, 0}, {2, 1}, {-1, 0}, {-1, 0}}},
      {" , a", 2, {{0, 0}, {3, 1}, {-1, 0}, {-1, 0}}},
      {"a,a", 2, {{0, 1}, {2, 1}, {-1, 0}, {-1, 0}}},
      {"foo", 1, {{0, 3}, {-1, 0}, {-1, 0}, {-1, 0}}},
      {"foo,", 2, {{0, 3}, {4, 0}, {-1, 0}, {-1, 0}}},
      {"foo, ", 2, {{0, 3}, {4, 0}, {-1, 0}, {-1, 0}}},
      {"foo, bar", 2, {{0, 3}, {5, 3}, {-1, 0}, {-1, 0}}},
      {"foo, bar,", 3, {{0, 3}, {5, 3}, {9, 0}, {-1, 0}}},
      {"foo, bar, ", 3, {{0, 3}, {5, 3}, {9, 0}, {-1, 0}}},
      {
        ",foo,bar,",
        4,
        {{0, 0}, {1, 3}, {5, 3}, {9, 0}},
      },
    };

    HTTPHdr hdr;
    char field_name[32];
    int i, j, len, ntests, ncommavals;

    ntests = sizeof(tests) / sizeof(tests[0]);

    hdr.create(HTTP_TYPE_REQUEST);

    for (i = 0; i < ntests; i++) {
      snprintf(field_name, sizeof(field_name), "Test%d", i);

      MIMEField *f = hdr.field_create(field_name, static_cast<int>(strlen(field_name)));
      REQUIRE(f->m_ptr_value == nullptr);

      hdr.field_attach(f);
      REQUIRE(f->m_ptr_value == nullptr);

      hdr.field_value_set(f, tests[i].value, strlen(tests[i].value));
      REQUIRE(f->m_ptr_value != tests[i].value); // should be copied
      REQUIRE(f->m_len_value == strlen(tests[i].value));
      REQUIRE(memcmp(f->m_ptr_value, tests[i].value, f->m_len_value) == 0);

      ncommavals = mime_field_value_get_comma_val_count(f);
      if (ncommavals != tests[i].value_count) {
        std::printf("FAILED: test #%d (field value '%s') expected val count %d, got %d\n", i + 1, tests[i].value,
                    tests[i].value_count, ncommavals);
        CHECK(false);
      }

      for (j = 0; j < tests[i].value_count; j++) {
        const char *val = mime_field_value_get_comma_val(f, &len, j);
        int offset      = ((val == nullptr) ? -1 : (val - f->m_ptr_value));

        if ((offset != tests[i].pieces[j].offset) || (len != tests[i].pieces[j].len)) {
          std::printf(
            "FAILED: test #%d (field value '%s', commaval idx %d) expected [offset %d, len %d], got [offset %d, len %d]\n", i + 1,
            tests[i].value, j, tests[i].pieces[j].offset, tests[i].pieces[j].len, offset, len);
          CHECK(false);
        }
      }
    }

    hdr.destroy();
  }

  SECTION("Test set comma vals")
  {
    static struct {
      const char *old_raw;
      int idx;
      const char *slice;
      const char *new_raw;
    } tests[] = {
      {"a,b,c", 0, "fred", "fred, b, c"},
      {"a,b,c", 1, "fred", "a, fred, c"},
      {"a,b,c", 2, "fred", "a, b, fred"},
      {"a,b,c", 3, "fred", "a,b,c"},
      {"", 0, "", ""},
      {"", 0, "foo", "foo"},
      {"", 1, "foo", ""},
      {" ", 0, "", ""},
      {" ", 0, "foo", "foo"},
      {" ", 1, "foo", " "},
      {",", 0, "foo", "foo, "},
      {",", 1, "foo", ", foo"},
      {",,", 0, "foo", "foo, , "},
      {",,", 1, "foo", ", foo, "},
      {",,", 2, "foo", ", , foo"},
      {"foo", 0, "abc", "abc"},
      {"foo", 1, "abc", "foo"},
      {"foo", 0, "abc,", "abc,"},
      {"foo", 0, ",abc", ",abc"},
      {",,", 1, ",,,", ", ,,,, "},
      {" a , b , c", 0, "fred", "fred, b, c"},
      {" a , b , c", 1, "fred", "a, fred, c"},
      {" a , b , c", 2, "fred", "a, b, fred"},
      {" a , b , c", 3, "fred", " a , b , c"},
      {"    a   ,   b ", 0, "fred", "fred, b"},
      {"    a   ,   b ", 1, "fred", "a, fred"},
      {"    a   , b ", 1, "fred", "a, fred"},
      {"    a   ,b ", 1, "fred", "a, fred"},
      {"a, , , , e, , g,", 0, "fred", "fred, , , , e, , g, "},
      {"a, , , , e, , g,", 1, "fred", "a, fred, , , e, , g, "},
      {"a, , , , e, , g,", 2, "fred", "a, , fred, , e, , g, "},
      {"a, , , , e, , g,", 5, "fred", "a, , , , e, fred, g, "},
      {"a, , , , e, , g,", 7, "fred", "a, , , , e, , g, fred"},
      {"a, , , , e, , g,", 8, "fred", "a, , , , e, , g,"},
      {"a, \"boo,foo\", c", 0, "wawa", "wawa, \"boo,foo\", c"},
      {"a, \"boo,foo\", c", 1, "wawa", "a, wawa, c"},
      {"a, \"boo,foo\", c", 2, "wawa", "a, \"boo,foo\", wawa"},
    };

    HTTPHdr hdr;
    char field_name[32];
    int i, ntests;

    ntests = sizeof(tests) / sizeof(tests[0]);

    hdr.create(HTTP_TYPE_REQUEST);

    for (i = 0; i < ntests; i++) {
      snprintf(field_name, sizeof(field_name), "Test%d", i);

      MIMEField *f = hdr.field_create(field_name, static_cast<int>(strlen(field_name)));
      hdr.field_value_set(f, tests[i].old_raw, strlen(tests[i].old_raw));
      mime_field_value_set_comma_val(hdr.m_heap, hdr.m_mime, f, tests[i].idx, tests[i].slice, strlen(tests[i].slice));
      REQUIRE(f->m_ptr_value != nullptr);

      if ((f->m_len_value != strlen(tests[i].new_raw)) || (memcmp(f->m_ptr_value, tests[i].new_raw, f->m_len_value) != 0)) {
        std::printf("FAILED:  test #%d (setting idx %d of '%s' to '%s') expected '%s' len %d, got '%.*s' len %d\n", i + 1,
                    tests[i].idx, tests[i].old_raw, tests[i].slice, tests[i].new_raw, static_cast<int>(strlen(tests[i].new_raw)),
                    f->m_len_value, f->m_ptr_value, f->m_len_value);
        CHECK(false);
      }
    }

    hdr.destroy();
  }

  SECTION("Test delete comma vals")
  {
    // TEST NOT IMPLEMENTED
  }

  SECTION("Test extend comma vals")
  {
    // TEST NOT IMPLEMENTED
  }

  SECTION("Test insert comma vals")
  {
    // TEST NOT IMPLEMENTED
  }

  SECTION("Test parse comma list")
  {
    static struct {
      const char *value;
      int count;
      struct {
        int offset;
        int len;
      } pieces[3];
    } tests[] = {
      {"", 1, {{0, 0}, {-1, 0}, {-1, 0}}},
      {",", 2, {{0, 0}, {1, 0}, {-1, 0}}},
      {" ,", 2, {{0, 0}, {2, 0}, {-1, 0}}},
      {", ", 2, {{0, 0}, {1, 0}, {-1, 0}}},
      {" , ", 2, {{0, 0}, {2, 0}, {-1, 0}}},
      {"abc,", 2, {{0, 3}, {4, 0}, {-1, 0}}},
      {"abc, ", 2, {{0, 3}, {4, 0}, {-1, 0}}},
      {"", 1, {{0, 0}, {-1, 0}, {-1, 0}}},
      {" ", 1, {{0, 0}, {-1, 0}, {-1, 0}}},
      {"  ", 1, {{0, 0}, {-1, 0}, {-1, 0}}},
      {"a", 1, {{0, 1}, {-1, 0}, {-1, 0}}},
      {" a", 1, {{1, 1}, {-1, 0}, {-1, 0}}},
      {"  a  ", 1, {{2, 1}, {-1, 0}, {-1, 0}}},
      {"abc,defg", 2, {{0, 3}, {4, 4}, {-1, 0}}},
      {" abc,defg", 2, {{1, 3}, {5, 4}, {-1, 0}}},
      {" abc, defg", 2, {{1, 3}, {6, 4}, {-1, 0}}},
      {" abc , defg", 2, {{1, 3}, {7, 4}, {-1, 0}}},
      {" abc , defg ", 2, {{1, 3}, {7, 4}, {-1, 0}}},
      {" abc , defg, ", 3, {{1, 3}, {7, 4}, {12, 0}}},
      {" abc , defg ,", 3, {{1, 3}, {7, 4}, {13, 0}}},
      {", abc , defg ", 3, {{0, 0}, {2, 3}, {8, 4}}},
      {" ,abc , defg ", 3, {{0, 0}, {2, 3}, {8, 4}}},
      {"a,b", 2, {{0, 1}, {2, 1}, {-1, 0}}},
      {"a,,b", 3, {{0, 1}, {2, 0}, {3, 1}}},
      {"a, ,b", 3, {{0, 1}, {2, 0}, {4, 1}}},
      {"a ,,b", 3, {{0, 1}, {3, 0}, {4, 1}}},
      {",", 2, {{0, 0}, {1, 0}, {-1, 0}}},
      {" ,", 2, {{0, 0}, {2, 0}, {-1, 0}}},
      {", ", 2, {{0, 0}, {1, 0}, {-1, 0}}},
      {" , ", 2, {{0, 0}, {2, 0}, {-1, 0}}},
      {"a,b,", 3, {{0, 1}, {2, 1}, {4, 0}}},
      {"a,b, ", 3, {{0, 1}, {2, 1}, {4, 0}}},
      {"a,b,  ", 3, {{0, 1}, {2, 1}, {4, 0}}},
      {"a,b,  c", 3, {{0, 1}, {2, 1}, {6, 1}}},
      {"a,b,  c ", 3, {{0, 1}, {2, 1}, {6, 1}}},
      {"a,\"b,c\",d", 3, {{0, 1}, {3, 3}, {8, 1}}},
    };

    int i, j, ntests, offset;

    offset = 0;
    ntests = sizeof(tests) / sizeof(tests[0]);

    for (i = 0; i < ntests; i++) {
      StrList list(false);
      HttpCompat::parse_comma_list(&list, tests[i].value);
      if (list.count != tests[i].count) {
        std::printf("FAILED: test #%d (string '%s') expected list count %d, got %d\n", i + 1, tests[i].value, tests[i].count,
                    list.count);
        CHECK(false);
      }

      for (j = 0; j < tests[i].count; j++) {
        Str *cell = list.get_idx(j);
        if (cell != nullptr) {
          offset = cell->str - tests[i].value;
        }

        if (tests[i].pieces[j].offset == -1) // should not have a piece
        {
          if (cell != nullptr) {
            std::printf("FAILED: test #%d (string '%s', idx %d) expected NULL piece, got [offset %d len %d]\n", i + 1,
                        tests[i].value, j, offset, static_cast<int>(cell->len));
            CHECK(false);
          }
        } else // should have a piece
        {
          if (cell == nullptr) {
            std::printf("FAILED: test #%d (string '%s', idx %d) expected [offset %d len %d], got NULL piece\n", i + 1,
                        tests[i].value, j, tests[i].pieces[j].offset, tests[i].pieces[j].len);
            CHECK(false);
          } else if ((offset != tests[i].pieces[j].offset) || (cell->len != static_cast<size_t>(tests[i].pieces[j].len))) {
            std::printf("FAILED: test #%d (string '%s', idx %d) expected [offset %d len %d], got [offset %d len %d]\n", i + 1,
                        tests[i].value, j, tests[i].pieces[j].offset, tests[i].pieces[j].len, offset, static_cast<int>(cell->len));
            CHECK(false);
          }
        }
      }
    }
  }
}

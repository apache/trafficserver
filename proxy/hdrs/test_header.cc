/** @file

  test code for sanity checking the header system is functioning properly

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

#include <stdlib.h>
#include <string.h>

#include "ts/Arena.h"
#include "HTTP.h"
// #include "Marshal.h"
#include "MIME.h"
#include "ts/Regex.h"
#include "URL.h"
#include "HttpCompat.h"

static void test_parse_date();
static void test_format_date();
static void test_url();
static void test_mime();
static void test_http_parser_eos_boundary_cases();
static void test_http();
static void test_http_mutation();
static void test_arena();
static void test_regex();
static void test_accept_language_match();
static void test_str_replace_slice();
static void bri_box(char *s);

int
main(int argc, char *argv[])
{
  hdrtoken_init();
  url_init();
  mime_init();
  http_init();

  test_str_replace_slice();
  test_accept_language_match();
  test_parse_date();
  test_format_date();
  test_url();
  test_arena();
  test_regex();
  test_http_mutation();
  test_http_parser_eos_boundary_cases();
  test_mime();
  test_http();
  return (0);
}

static void
test_parse_date()
{
  static struct {
    const char *fast;
    const char *slow;
  } dates[] = {{"Sun, 06 Nov 1994 08:49:37 GMT", "Sunday, 06-Nov-1994 08:49:37 GMT"},
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
               {NULL, NULL}};

  int i;
  int failures = 0;
  time_t fast_t, slow_t;

  bri_box("test_parse_date");

  for (i = 0; dates[i].fast; i++) {
    fast_t = mime_parse_date(dates[i].fast, dates[i].fast + strlen(dates[i].fast));
    slow_t = mime_parse_date(dates[i].slow, dates[i].slow + strlen(dates[i].slow));
    // compare with strptime here!
    if (fast_t != slow_t) {
      printf("FAILED: date %d (%s) != %d (%s)\n", fast_t, dates[i].fast, slow_t, dates[i].slow);
      ++failures;
    }
  }

  printf("*** %s ***\n", (failures ? "FAILED" : "PASSED"));
}

static void
test_format_date()
{
  static char *dates[] = {"Sun, 06 Nov 1994 08:49:37 GMT", "Sun, 03 Jan 1999 08:49:37 GMT", "Sun, 05 Dec 1999 08:49:37 GMT",
                          "Tue, 25 Apr 2000 20:29:53 GMT", NULL};

  bri_box("test_format_date");

  // (1) Test a few hand-created dates

  int i;
  time_t t, t2;
  char buffer[128], buffer2[128];
  static char *envstr = "TZ=GMT";
  int failures        = 0;

  // shift into GMT timezone for cftime conversions
  putenv(envstr);
  tzset();

  for (i = 0; dates[i]; i++) {
    t = mime_parse_date(dates[i], dates[i] + strlen(dates[i]));

    cftime(buffer, "%a, %d %b %Y %T %Z", &t);
    if (memcmp(dates[i], buffer, 29) != 0) {
      printf("FAILED: original date doesn't match cftime date\n");
      printf("  input date:  %s\n", dates[i]);
      printf("  cftime date: %s\n", buffer);
      ++failures;
    }

    mime_format_date(buffer, t);
    if (memcmp(dates[i], buffer, 29) != 0) {
      printf("FAILED: original date doesn't match mime_format_date date\n");
      printf("  input date:  %s\n", dates[i]);
      printf("  cftime date: %s\n", buffer);
      ++failures;
    }
  }

  // (2) test a few times per day from 1/1/1970 to past 2010

  for (t = 0; t < 40 * 366 * (24 * 60 * 60); t += drand48() * (24 * 60 * 60)) {
    cftime(buffer, "%a, %d %b %Y %T %Z", &t);
    //      printf("%s\n",buffer);

    t2 = mime_parse_date(buffer, buffer + strlen(buffer));
    if (t2 != t) {
      printf("FAILED: parsed time_t doesn't match original time_t\n");
      printf("  input time_t:  %d (%s)\n", t, buffer);
      printf("  parsed time_t: %d\n", t2);
      ++failures;
    }

    mime_format_date(buffer2, t2);
    if (memcmp(buffer, buffer2, 29) != 0) {
      printf("FAILED: formatted date doesn't match original date\n");
      printf("  original date:  %s\n", buffer);
      printf("  formatted date: %s\n", buffer2);
      ++failures;
    }
  }
  printf("*** %s ***\n", (failures ? "FAILED" : "PASSED"));
}

static void
test_url()
{
  static const char *strs[] = {
    "http://some.place/path;params?query#fragment",

    // Start with an easy one...
    "http://trafficserver.apache.org/index.html",

    "cheese://bogosity",

    "some.place", "some.place/", "http://some.place", "http://some.place/", "http://some.place/path",
    "http://some.place/path;params", "http://some.place/path;params?query", "http://some.place/path;params?query#fragment",
    "http://some.place/path?query#fragment", "http://some.place/path#fragment",

    "some.place:80", "some.place:80/", "http://some.place:80", "http://some.place:80/",

    "foo@some.place:80", "foo@some.place:80/", "http://foo@some.place:80", "http://foo@some.place:80/",

    "foo:bar@some.place:80", "foo:bar@some.place:80/", "http://foo:bar@some.place:80", "http://foo:bar@some.place:80/",

    "foo:bar@some.place", "foo:bar@some.place/", "http://foo:bar@some.place", "http://foo:bar@some.place/",

    "pnm://foo:bar@some.place:80/path;params?query#fragment", "rtsp://foo:bar@some.place:80/path;params?query#fragment",
    "rtspu://foo:bar@some.place:80/path;params?query#fragment",
    "/finance/external/cbsm/*http://cbs.marketwatch.com/archive/19990713/news/current/net.htx?source=blq/yhoo&dist=yhoo"};
  static int nstrs = sizeof(strs) / sizeof(strs[0]);

  int err, failed;
  URL url;
  const char *start;
  const char *end;
  int i, old_length, new_length;

  bri_box("test_url");

  failed = 0;
  for (i = 0; i < nstrs; i++) {
    old_length = strlen(strs[i]);
    start      = strs[i];
    end        = start + old_length;

    url.create(NULL);
    err = url.parse(&start, end);
    if (err < 0) {
      failed = 1;
      break;
    }

    char print_buf[1024];
    new_length = 0;
    int offset = 0;
    url.print(print_buf, 1024, &new_length, &offset);
    print_buf[new_length] = '\0';

    char *fail_text = NULL;

    if (old_length == new_length) {
      if (memcmp(print_buf, strs[i], new_length) != 0)
        fail_text = "URLS DIFFER";
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
      printf("%16s: OLD: (%4d) %s\n", fail_text, old_length, strs[i]);
      printf("%16s: NEW: (%4d) %s\n", "", new_length, print_buf);
      obj_describe(url.m_url_impl, true);
    }

    url.destroy();
  }

  printf("*** %s ***\n", (failed ? "FAILED" : "PASSED"));
}

static void
test_mime()
{
  static const char mime[] = {//        "Date: Tuesday, 08-Dec-98 20:32:17 GMT\r\n"
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
                              "word word: word \r\n"
                              "accept: \"fazzle, dazzle\"\r\n"
                              "accept: 1, 2, 3, 4, 5, 6, 7, 8\r\n"
                              "continuation: part1\r\n"
                              " part2\r\n"
                              "scooby: doo\r\n"
                              "scooby : doo\r\n"
                              "bar: foo\r\n"
                              "\r\n"};

  int err;
  MIMEHdr hdr;
  MIMEParser parser;
  const char *start;
  const char *end;

  bri_box("test_mime");

  printf("   <<< MUST BE HAND-VERIFIED >>>\n\n");

  start = mime;
  end   = start + strlen(start);

  mime_parser_init(&parser);

  bool must_copy_strs = 0;

  hdr.create(NULL);
  err = hdr.parse(&parser, &start, end, must_copy_strs, false);

  if (err < 0) {
    return;
  }

  hdr.field_delete("not_there", 9);
  hdr.field_delete("accept", 6);
  hdr.field_delete("scooby", 6);
  hdr.field_delete("scooby", 6);
  hdr.field_delete("bar", 3);
  hdr.field_delete("continuation", 12);

  int count = hdr.fields_count();
  printf("hdr.fields_count() = %d\n", count);

  int i_max_forwards = hdr.value_get_int("Max-Forwards", 12);
  int u_max_forwards = hdr.value_get_uint("Max-Forwards", 12);
  printf("i_max_forwards = %d   u_max_forwards = %d\n", i_max_forwards, u_max_forwards);

  hdr.set_age(9999);

  int length = hdr.length_get();
  printf("hdr.length_get() = %d\n", length);

  time_t t0, t1, t2;

  t0 = hdr.get_date();
  if (t0 == 0)
    printf("FAILED: Initial date is zero but shouldn't be\n");

  t1 = time(NULL);
  hdr.set_date(t1);
  t2 = hdr.get_date();
  if (t1 != t2) {
    printf("FAILED: set_date(%ld) ... get_date = %ld\n\n", t1, t2);
  }

  hdr.value_append("Cache-Control", 13, "no-cache", 8, 1);

  MIMEField *cc_field;
  StrList slist;
  int slist_count;
  cc_field    = hdr.field_find("Cache-Control", 13);
  slist_count = cc_field->value_get_comma_list(&slist); // FIX: correct usage?

  mime_parser_clear(&parser);

  hdr.print(NULL, 0, NULL, NULL);
  printf("\n");

  obj_describe((HdrHeapObjImpl *)(hdr.m_mime), true);

  hdr.fields_clear();

  hdr.destroy();
}

static void
test_http_parser_eos_boundary_cases()
{
  struct {
    char *msg;
    int expected_result;
    int expected_bytes_consumed;
  } tests[] = {{"GET /index.html HTTP/1.0\r\n", PARSE_DONE, 26},
               {"GET /index.html HTTP/1.0\r\n\r\n***BODY****", PARSE_DONE, 28},
               {"GET /index.html HTTP/1.0\r\nUser-Agent: foobar\r\n\r\n***BODY****", PARSE_DONE, 48},
               {"GET", PARSE_ERROR, 3},
               {"GET /index.html", PARSE_ERROR, 15},
               {"GET /index.html\r\n", PARSE_DONE, 17},
               {"GET /index.html HTTP/1.0", PARSE_ERROR, 24},
               {"GET /index.html HTTP/1.0\r", PARSE_ERROR, 25},
               {"GET /index.html HTTP/1.0\n", PARSE_DONE, 25},
               {"GET /index.html HTTP/1.0\n\n", PARSE_DONE, 26},
               {"GET /index.html HTTP/1.0\r\n\r\n", PARSE_DONE, 28},
               {"GET /index.html HTTP/1.0\r\nUser-Agent: foobar", PARSE_ERROR, 44},
               {"GET /index.html HTTP/1.0\r\nUser-Agent: foobar\n", PARSE_DONE, 45},
               {"GET /index.html HTTP/1.0\r\nUser-Agent: foobar\r\n", PARSE_DONE, 46},
               {"GET /index.html HTTP/1.0\r\nUser-Agent: foobar\r\n\r\n", PARSE_DONE, 48},
               {"GET /index.html HTTP/1.0\nUser-Agent: foobar\n", PARSE_DONE, 44},
               {"GET /index.html HTTP/1.0\nUser-Agent: foobar\nBoo: foo\n", PARSE_DONE, 53},
               {"GET /index.html HTTP/1.0\r\nUser-Agent: foobar\r\n", PARSE_DONE, 46},
               {"GET /index.html HTTP/1.0\r\n", PARSE_DONE, 26},
               {"", PARSE_DONE, 0},
               {NULL, 0, 0}};

  int i, ret, bytes_consumed;
  const char *orig_start;
  const char *start;
  const char *end;
  HTTPParser parser;

  int failures = 0;

  bri_box("test_http_parser_eos_boundary_cases");

  http_parser_init(&parser);

  for (i = 0; tests[i].msg != NULL; i++) {
    HTTPHdr req_hdr;

    start = tests[i].msg;
    end   = start + strlen(start); // 1 character past end of string

    req_hdr.create(HTTP_TYPE_REQUEST);

    http_parser_clear(&parser);
    //      http_parser_init (&parser);

    orig_start     = start;
    ret            = req_hdr.parse_req(&parser, &start, end, true);
    bytes_consumed = start - orig_start;

    printf("======== test %d (length=%d, consumed=%d)\n", i, strlen(tests[i].msg), bytes_consumed);
    printf("[%s]\n", tests[i].msg);
    printf("\n[");
    req_hdr.print(NULL, 0, NULL, NULL);
    printf("]\n\n");

    if ((ret != tests[i].expected_result) || (bytes_consumed != tests[i].expected_bytes_consumed)) {
      ++failures;
      printf("FAILED: test %d: retval <expected %d, got %d>, eaten <expected %d, got %d>\n\n", i, tests[i].expected_result, ret,
             tests[i].expected_bytes_consumed, bytes_consumed);
    } else {
      printf("SUCCESS: test %d: retval <expected %d, got %d>, eaten <expected %d, got %d>\n\n", i, tests[i].expected_result, ret,
             tests[i].expected_bytes_consumed, bytes_consumed);
    }

    req_hdr.destroy();
  }

  if (failures)
    printf("*** %s ***\n", (failures ? "FAILED" : "PASSED"));
}

static void
test_http_aux(const char *request, const char *response)
{
  int err;
  HTTPHdr req_hdr, rsp_hdr;
  HTTPParser parser;
  const char *start;
  const char *end;

  bri_box("test_http");

  printf("   <<< MUST BE HAND-VERIFIED >>>\n\n");

  /*** (1) parse the request string into req_hdr ***/

  start = request;
  end   = start + strlen(start); // 1 character past end of string

  http_parser_init(&parser);

  req_hdr.create(HTTP_TYPE_REQUEST);
  rsp_hdr.create(HTTP_TYPE_RESPONSE);

  printf("======== parsing\n\n");
  while (start < end) {
    err = req_hdr.parse_req(&parser, &start, end, false);
    if (err != PARSE_CONT)
      break;
    end = start + strlen(start);
  }
  if (err == PARSE_ERROR)
    printf("  *** PARSE_ERROR ***\n");

  /*** useless copy to exercise copy function ***/

  HTTPHdr new_hdr;
  new_hdr.create(HTTP_TYPE_REQUEST);
  new_hdr.copy(&req_hdr);
  new_hdr.destroy();

  /*** (2) print out the request ***/

  printf("======== real request (length=%d)\n\n", strlen(request));
  printf("%s\n", request);

  printf("\n[");
  req_hdr.print(NULL, 0, NULL, NULL);
  printf("]\n\n");

  obj_describe(req_hdr.m_http, true);

  // req_hdr.destroy ();
  // ink_release_assert(!"req_hdr.destroy() not defined");

  /*** (3) parse the response string into rsp_hdr ***/

  start = response;
  end   = start + strlen(start);

  http_parser_clear(&parser);
  http_parser_init(&parser);

  while (start < end) {
    err = rsp_hdr.parse_resp(&parser, &start, start + 1, false);
    if (err != PARSE_CONT)
      break;
  }
  if (err == PARSE_ERROR)
    printf("  *** PARSE_ERROR ***\n");

  http_parser_clear(&parser);

  /*** (4) print out the response ***/

  printf("\n======== real response (length=%d)\n\n", strlen(response));
  printf("%s\n", response);

  printf("\n[");
  rsp_hdr.print(NULL, 0, NULL, NULL);
  printf("]\n\n");

  obj_describe(rsp_hdr.m_http, true);

#define NNN 1000
  {
    char buf[NNN];
    int bufindex, last_bufindex;
    int dumpoffset;
    int tmp;
    int err;
    int i;

    bufindex = 0;

    do {
      last_bufindex = bufindex;
      tmp           = bufindex;
      buf[0]        = '#'; // make it obvious if hdr.print doesn't print anything
      err           = rsp_hdr.print(buf, NNN, &bufindex, &tmp);

      // printf("test_header: tmp = %d  err = %d  bufindex = %d\n", tmp, err, bufindex);
      putchar('{');
      for (i = 0; i < bufindex - last_bufindex; i++) {
        if (!iscntrl(buf[i]))
          putchar(buf[i]);
        else
          printf("\\%o", buf[i]);
      }
      putchar('}');
    } while (!err);
  }

  // rsp_hdr.print (NULL, 0, NULL, NULL);

  req_hdr.destroy();
  rsp_hdr.destroy();
}

static void
test_http()
{
  printf("   <<< MUST BE HAND-VERIFIED >>>\n\n");

  static const char request0[] = {"GET http://www.news.com:80/ HTTP/1.0\r\n"
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
                                  "\r\n"};

  static const char request09[] = {"GET /index.html\r\n"
                                   "\r\n"};

  static const char request1[] = {"GET http://people.netscape.com/jwz/hacks-1.gif HTTP/1.0\r\n"
                                  "If-Modified-Since: Wednesday, 26-Feb-97 06:58:17 GMT; length=842\r\n"
                                  "Referer: http://people.netscape.com/jwz/index.html\r\n"
                                  "Proxy-Connection: Keep-Alive\r\n"
                                  "User-Agent:  Mozilla/3.01 (X11; I; Linux 2.0.28 i586)\r\n"
                                  "Pragma: no-cache\r\n"
                                  "Host: people.netscape.com\r\n"
                                  "Accept: image/gif, image/x-xbitmap, image/jpeg, image/pjpeg, */*\r\n"
                                  "\r\n"};

  static const char request_no_colon[] = {"GET http://people.netscape.com/jwz/hacks-1.gif HTTP/1.0\r\n"
                                          "If-Modified-Since Wednesday, 26-Feb-97 06:58:17 GMT; length=842\r\n"
                                          "Referer http://people.netscape.com/jwz/index.html\r\n"
                                          "Proxy-Connection Keep-Alive\r\n"
                                          "User-Agent  Mozilla/3.01 (X11; I; Linux 2.0.28 i586)\r\n"
                                          "Pragma no-cache\r\n"
                                          "Host people.netscape.com\r\n"
                                          "Accept image/gif, image/x-xbitmap, image/jpeg, image/pjpeg, */*\r\n"
                                          "\r\n"};

  static const char request_no_val[] = {"GET http://people.netscape.com/jwz/hacks-1.gif HTTP/1.0\r\n"
                                        "If-Modified-Since:\r\n"
                                        "Referer:     "
                                        "Proxy-Connection:\r\n"
                                        "User-Agent:     \r\n"
                                        "Host:::\r\n"
                                        "\r\n"};

  static const char request_multi_fblock[] = {"GET http://people.netscape.com/jwz/hacks-1.gif HTTP/1.0\r\n"
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
                                              "\r\n"};

  static const char request_leading_space[] = {" GET http://www.news.com:80/ HTTP/1.0\r\n"
                                               "Proxy-Connection: Keep-Alive\r\n"
                                               "User-Agent: Mozilla/4.04 [en] (X11; I; Linux 2.0.33 i586)\r\n"
                                               "\r\n"};

  static const char request_padding[] = {"GET http://www.padding.com:80/ HTTP/1.0\r\n"
                                         "X-1: blah1\r\n"
                                         //       "X-2:  blah2\r\n"
                                         "X-3:   blah3\r\n"
                                         //       "X-4:    blah4\r\n"
                                         "X-5:     blah5\r\n"
                                         //       "X-6:      blah6\r\n"
                                         "X-7:       blah7\r\n"
                                         //       "X-8:        blah8\r\n"
                                         "X-9:         blah9\r\n"
                                         "\r\n"};

  static const char request_09p[] = {"GET http://www.news09.com/\r\n"
                                     "\r\n"};

  static const char request_09ht[] = {"GET http://www.news09.com/ HT\r\n"
                                      "\r\n"};

  static const char request_11[] = {"GET http://www.news.com/ HTTP/1.1\r\n"
                                    "Connection: close\r\n"
                                    "\r\n"};

  static const char request_unterminated[] = {"GET http://www.unterminated.com/ HTTP/1.1"};

  static const char request_blank[] = {"\r\n"};

  static const char request_blank2[] = {"\r\n"
                                        "\r\n"};

  static const char request_blank3[] = {"     "
                                        "\r\n"};

  ////////////////////////////////////////////////////

  static const char response0[] = {"HTTP/1.0 200 OK\r\n"
                                   "MIME-Version: 1.0\r\n"
                                   "Server: WebSTAR/2.1 ID/30013\r\n"
                                   "Content-Type: text/html\r\n"
                                   "Content-Length: 939\r\n"
                                   "Last-Modified: Thursday, 01-Jan-04 05:00:00 GMT\r\n"
                                   "\r\n"};

  static const char response1[] = {"HTTP/1.0 200 OK\r\n"
                                   "Server: Netscape-Communications/1.12\r\n"
                                   "Date: Tuesday, 08-Dec-98 20:32:17 GMT\r\n"
                                   "Content-Type: text/html\r\n"
                                   "\r\n"};

  static const char response_no_colon[] = {"HTTP/1.0 200 OK\r\n"
                                           "Server Netscape-Communications/1.12\r\n"
                                           "Date: Tuesday, 08-Dec-98 20:32:17 GMT\r\n"
                                           "Content-Type: text/html\r\n"
                                           "\r\n"};

  static const char response_unterminated[] = {"HTTP/1.0 200 OK"};

  static const char response09[] = {""};

  static const char response_blank[] = {"\r\n"};

  static const char response_blank2[] = {"\r\n"
                                         "\r\n"};

  static const char response_blank3[] = {"     "
                                         "\r\n"};

  test_http_aux(request0, response0);
  test_http_aux(request09, response09);
  test_http_aux(request1, response1);
  test_http_aux(request_no_colon, response_no_colon);
  test_http_aux(request_no_val, response_no_colon);
  test_http_aux(request_leading_space, response0);
  test_http_aux(request_multi_fblock, response0);
  test_http_aux(request_padding, response0);
  test_http_aux(request_09p, response0);
  test_http_aux(request_09ht, response0);
  test_http_aux(request_11, response0);
  test_http_aux(request_unterminated, response_unterminated);
  test_http_aux(request_blank, response_blank);
  test_http_aux(request_blank2, response_blank2);
  test_http_aux(request_blank3, response_blank3);
}

static void
test_http_mutation()
{
  bri_box("test_http_mutation");

  printf("   <<< MUST BE HAND-VERIFIED >>>\n\n");

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

  while (start < end) {
    err = resp_hdr.parse_resp(&parser, &start, end, false);
    if (err != PARSE_CONT)
      break;
    end = start + strlen(start);
  }

  printf("\n======== before mutation ==========\n\n");
  printf("\n[");
  resp_hdr.print(NULL, 0, NULL, NULL);
  printf("]\n\n");

  /*** (2) add in a bunch of header fields ****/
  char field_name[1024];
  char field_value[1024];
  for (i = 1; i <= 100; i++) {
    sprintf(field_name, "Test%d", i);
    sprintf(field_value, "%d %d %d %d %d", i, i, i, i, i);
    resp_hdr.value_set(field_name, strlen(field_name), field_value, strlen(field_value));
  }

  /**** (3) delete all the even numbered fields *****/
  for (i = 2; i <= 100; i += 2) {
    sprintf(field_name, "Test%d", i);
    resp_hdr.field_delete(field_name, strlen(field_name));
  }

  /***** (4) add in secondary fields for all multiples of 3 ***/
  for (i = 3; i <= 100; i += 3) {
    sprintf(field_name, "Test%d", i);
    MIMEField *f = resp_hdr.field_create(field_name, strlen(field_name));
    resp_hdr.field_attach(f);
    sprintf(field_value, "d %d %d %d %d %d", i, i, i, i, i);
    f->value_set(resp_hdr.m_heap, resp_hdr.m_mime, field_value, strlen(field_value));
  }

  /***** (5) append all fields with multiples of 5 ***/
  for (i = 5; i <= 100; i += 5) {
    sprintf(field_name, "Test%d", i);
    sprintf(field_value, "a %d", i);

    resp_hdr.value_append(field_name, strlen(field_name), field_value, strlen(field_value), true);
  }

  /**** (6) delete all multiples of nine *****/
  for (i = 9; i <= 100; i += 9) {
    sprintf(field_name, "Test%d", i);
    resp_hdr.field_delete(field_name, strlen(field_name));
  }

  printf("\n======== mutated response ==========\n\n");
  printf("\n[");
  resp_hdr.print(NULL, 0, NULL, NULL);
  printf("]\n\n");

  resp_hdr.destroy();
}

static int
test_arena_aux(Arena *arena, int len)
{
  char *str      = arena->str_alloc(len);
  int verify_len = arena->str_length(str);

  if (len != verify_len) {
    printf("FAILED: reuqested %d, got %u bytes\n", len, verify_len);
    return (1); // 1 error
  } else {
    return (0); // no errors
  }
}

static void
test_arena()
{
  bri_box("test_arena");

  Arena *arena;
  char *str;
  int failures = 0;

  arena = new Arena;

  failures += test_arena_aux(arena, 1);
  failures += test_arena_aux(arena, 127);
  failures += test_arena_aux(arena, 128);
  failures += test_arena_aux(arena, 129);
  failures += test_arena_aux(arena, 255);
  failures += test_arena_aux(arena, 256);
  failures += test_arena_aux(arena, 16384);
  failures += test_arena_aux(arena, 16385);
  failures += test_arena_aux(arena, 16511);
  failures += test_arena_aux(arena, 16512);
  failures += test_arena_aux(arena, 2097152);
  failures += test_arena_aux(arena, 2097153);
  failures += test_arena_aux(arena, 2097279);
  failures += test_arena_aux(arena, 2097280);

  delete arena;

  printf("*** %s ***\n", (failures ? "FAILED" : "PASSED"));
}

static void
test_regex()
{
  DFA dfa;

  bri_box("test_regex");

  printf("   <<< MUST BE HAND-VERIFIED >>>\n\n");

  dfa.compile("(.*\\.inktomi\\.com#1#)|(.*\\.inktomi\\.org#2#)");
  printf("match www.example.com [%d]\n", dfa.match("www.example.com"));
  printf("match www.apache.org [%d]\n", dfa.match("www.apache.org"));
}

static void
test_accept_language_match()
{
  bri_box("test_accept_language_match");

  struct {
    char *content_language;
    char *accept_language;
    float Q;
    int L;
    int I;
  } test_cases[] = {{"en", "*", 1.0, 1, 1},
                    {"en", "fr", 0.0, 0, 0},
                    {"en", "de, fr, en;q=0.7", 0.7, 2, 3},
                    {"en-cockney", "de, fr, en;q=0.7", 0.7, 3, 3},
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
                    {NULL, NULL, 0.0}};

  int i, I, L;
  float Q;
  int failures = 0;

  for (i = 0; test_cases[i].accept_language; i++) {
    Q = HttpCompat::match_accept_language(test_cases[i].content_language, strlen(test_cases[i].content_language),
                                          test_cases[i].accept_language, strlen(test_cases[i].accept_language), &L, &I);

    if (Q != test_cases[i].Q) {
      printf(
        "FAILED: got { Q = %.3f; L = %d; I = %d; }, expected { Q = %.3f; L = %d; I = %d; }, from matching\n  '%s' against '%s'\n",
        Q, L, I, test_cases[i].Q, test_cases[i].L, test_cases[i].I, test_cases[i].content_language, test_cases[i].accept_language);
      ++failures;
    }
  }

  printf("*** %s ***\n", (failures ? "FAILED" : "PASSED"));
}

static void
test_str_replace_slice()
{
  bri_box("test_str_replace_slice");

  int len;
  char buff[256];
  HdrHeap *heap = new_HdrHeap();
  const char *orig, *targ, *repl, *good, *retr;
  int failures = 0;

  // (1) prepend
  ink_strlcpy(buff, "de, fr, en", sizeof(buff));
  targ = buff + 0;
  repl = "oo, ";
  good = "oo, de, fr, en";
  retr = mime_field_value_str_replace_slice(heap, &len, buff, strlen(buff), targ, 0, repl, strlen(repl));
  if ((len != strlen(good)) || (memcmp(good, retr, len) != 0)) {
    printf("FAILED: expected %d byte str \"%s\", got %d byte str \"%.*s\"\n", strlen(good), good, len, len, retr);
    ++failures;
  }
  // (2) append
  ink_strlcpy(buff, "de, fr, en", sizeof(buff));
  targ = buff + 10;
  repl = ", bloop";
  good = "de, fr, en, bloop";
  retr = mime_field_value_str_replace_slice(heap, &len, buff, strlen(buff), targ, 0, repl, strlen(repl));
  if ((len != strlen(good)) || (memcmp(good, retr, len) != 0)) {
    printf("FAILED: expected %d byte str \"%s\", got %d byte str \"%.*s\"\n", strlen(good), good, len, len, retr);
    ++failures;
  }
  // (3) delete middle
  ink_strlcpy(buff, "de, fr, en", sizeof(buff));
  targ = buff + 4;
  repl = "";
  good = "de, en";
  retr = mime_field_value_str_replace_slice(heap, &len, buff, strlen(buff), targ, 4, repl, strlen(repl));
  if ((len != strlen(good)) || (memcmp(good, retr, len) != 0)) {
    printf("FAILED: expected %d byte str \"%s\", got %d byte str \"%.*s\"\n", strlen(good), good, len, len, retr);
    ++failures;
  }

  printf("*** %s ***\n", (failures ? "FAILED" : "PASSED"));
}

static void
bri_box(char *s)
{
  int i, len;

  len = strlen(s);
  printf("\n+-");
  for (i = 0; i < len; i++)
    putchar('-');
  printf("-+\n");
  printf("| %s |\n", s);
  printf("+-");
  for (i = 0; i < len; i++)
    putchar('-');
  printf("-+\n\n");
}

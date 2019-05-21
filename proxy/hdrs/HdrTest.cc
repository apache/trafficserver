/** @file

  A brief file description

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

/****************************************************************************

   HdrTest.cc

   Description:
       Unit test code for sanity checking the header system is functioning
         properly


 ****************************************************************************/

#include "tscore/ink_platform.h"
#include "tscore/ink_memory.h"
#include "tscore/ink_time.h"

#include "tscore/Arena.h"
#include "HTTP.h"
#include "MIME.h"
#include "tscore/Regex.h"
#include "URL.h"
#include "HttpCompat.h"

#include "HdrTest.h"

//////////////////////
// Main Test Driver //
//////////////////////

int
HdrTest::go(RegressionTest *t, int /* atype ATS_UNUSED */)
{
  HdrTest::rtest = t;
  int status     = 1;

  hdrtoken_init();
  url_init();
  mime_init();
  http_init();

  status = status & test_http_hdr_print_and_copy();
  status = status & test_comma_vals();
  status = status & test_parse_comma_list();
  status = status & test_set_comma_vals();
  status = status & test_delete_comma_vals();
  status = status & test_extend_comma_vals();
  status = status & test_insert_comma_vals();
  status = status & test_accept_language_match();
  status = status & test_accept_charset_match();
  status = status & test_parse_date();
  status = status & test_format_date();
  status = status & test_url();
  status = status & test_arena();
  status = status & test_regex();
  status = status & test_http_mutation();
  status = status & test_mime();
  status = status & test_http();

  return (status ? REGRESSION_TEST_PASSED : REGRESSION_TEST_FAILED);
}

////////////////////////////////////////////////////////////
// Individual Tests --- return 1 on success, 0 on failure //
////////////////////////////////////////////////////////////

int
HdrTest::test_parse_date()
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
  int failures = 0;
  time_t fast_t, slow_t;

  bri_box("test_parse_date");

  for (i = 0; dates[i].fast; i++) {
    fast_t = mime_parse_date(dates[i].fast, dates[i].fast + static_cast<int>(strlen(dates[i].fast)));
    slow_t = mime_parse_date(dates[i].slow, dates[i].slow + static_cast<int>(strlen(dates[i].slow)));
    // compare with strptime here!
    if (fast_t != slow_t) {
      printf("FAILED: date %lu (%s) != %lu (%s)\n", static_cast<unsigned long>(fast_t), dates[i].fast,
             static_cast<unsigned long>(slow_t), dates[i].slow);
      ++failures;
    }
  }

  return (failures_to_status("test_parse_date", failures));
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
HdrTest::test_format_date()
{
  static const char *dates[] = {
    "Sun, 06 Nov 1994 08:49:37 GMT",
    "Sun, 03 Jan 1999 08:49:37 GMT",
    "Sun, 05 Dec 1999 08:49:37 GMT",
    "Tue, 25 Apr 2000 20:29:53 GMT",
    nullptr,
  };

  bri_box("test_format_date");

  // (1) Test a few hand-created dates

  int i;
  time_t t, t2, t3;
  char buffer[128], buffer2[128];
  static const char *envstr = "TZ=GMT0";
  int failures              = 0;

  // shift into GMT timezone for cftime conversions
  putenv(const_cast<char *>(envstr));
  tzset();

  for (i = 0; dates[i]; i++) {
    t = mime_parse_date(dates[i], dates[i] + static_cast<int>(strlen(dates[i])));

    cftime_replacement(buffer, sizeof(buffer), "%a, %d %b %Y %T %Z", &t);
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

  // coverity[dont_call]
  for (t = 0; t < 40 * 366 * (24 * 60 * 60); t += static_cast<int>(drand48() * (24 * 60 * 60))) {
    cftime_replacement(buffer, sizeof(buffer), "%a, %d %b %Y %T %Z", &t);
    t2 = mime_parse_date(buffer, buffer + static_cast<int>(strlen(buffer)));
    if (t2 != t) {
      printf("FAILED: parsed time_t doesn't match original time_t\n");
      printf("  input time_t:  %d (%s)\n", static_cast<int>(t), buffer);
      printf("  parsed time_t: %d\n", static_cast<int>(t2));
      ++failures;
    }
    mime_format_date(buffer2, t);
    if (memcmp(buffer, buffer2, 29) != 0) {
      printf("FAILED: formatted date doesn't match original date\n");
      printf("  original date:  %s\n", buffer);
      printf("  formatted date: %s\n", buffer2);
      ++failures;
    }
    t3 = mime_parse_date(buffer2, buffer2 + static_cast<int>(strlen(buffer2)));
    if (t != t3) {
      printf("FAILED: parsed time_t doesn't match original time_t\n");
      printf("  input time_t:  %d (%s)\n", static_cast<int>(t), buffer2);
      printf("  parsed time_t: %d\n", static_cast<int>(t3));
      ++failures;
    }

    if (failures > 20) {
      // Already too many failures, don't need to continue
      break;
    }
  }

  return (failures_to_status("test_format_date", failures));
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
HdrTest::test_url()
{
  static const char *strs[] = {
    "http://some.place/path;params?query#fragment",

    // Start with an easy one...
    "http://trafficserver.apache.org/index.html",

    // "cheese://bogosity",         This fails, but it's not clear it should work...

    "some.place", "some.place/", "http://some.place", "http://some.place/", "http://some.place/path",
    "http://some.place/path;params", "http://some.place/path;params?query", "http://some.place/path;params?query#fragment",
    "http://some.place/path?query#fragment", "http://some.place/path#fragment",

    "some.place:80", "some.place:80/", "http://some.place:80", "http://some.place:80/",

    "foo@some.place:80", "foo@some.place:80/", "http://foo@some.place:80", "http://foo@some.place:80/",

    "foo:bar@some.place:80", "foo:bar@some.place:80/", "http://foo:bar@some.place:80", "http://foo:bar@some.place:80/",

    // Some address stuff
    "http://172.16.28.101", "http://172.16.28.101:8080", "http://[::]", "http://[::1]", "http://[fc01:172:16:28::101]",
    "http://[fc01:172:16:28::101]:80", "http://[fc01:172:16:28:BAAD:BEEF:DEAD:101]",
    "http://[fc01:172:16:28:BAAD:BEEF:DEAD:101]:8080", "http://172.16.28.101/some/path", "http://172.16.28.101:8080/some/path",
    "http://[::1]/some/path", "http://[fc01:172:16:28::101]/some/path", "http://[fc01:172:16:28::101]:80/some/path",
    "http://[fc01:172:16:28:BAAD:BEEF:DEAD:101]/some/path", "http://[fc01:172:16:28:BAAD:BEEF:DEAD:101]:8080/some/path",
    "http://172.16.28.101/", "http://[fc01:172:16:28:BAAD:BEEF:DEAD:101]:8080/",

    "foo:bar@some.place", "foo:bar@some.place/", "http://foo:bar@some.place", "http://foo:bar@some.place/",
    "http://foo:bar@[::1]:8080/", "http://foo@[::1]",

    "mms://sm02.tsqa.example.com/0102rally.asf", "pnm://foo:bar@some.place:80/path;params?query#fragment",
    "rtsp://foo:bar@some.place:80/path;params?query#fragment", "rtspu://foo:bar@some.place:80/path;params?query#fragment",
    "/finance/external/cbsm/*http://cbs.marketwatch.com/archive/19990713/news/current/net.htx?source=blq/yhoo&dist=yhoo",
    "http://a.b.com/xx.jpg?newpath=http://bob.dave.com"};

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
  };

  int err, failed;
  URL url;
  const char *start;
  const char *end;
  int old_length, new_length;

  bri_box("test_url");

  failed = 0;
  for (unsigned i = 0; i < countof(strs); i++) {
    old_length = static_cast<int>(strlen(strs[i]));
    start      = strs[i];
    end        = start + old_length;

    url.create(nullptr);
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
      printf("%16s: OLD: (%4d) %s\n", fail_text, old_length, strs[i]);
      printf("%16s: NEW: (%4d) %s\n", "", new_length, print_buf);
      obj_describe(url.m_url_impl, true);
    } else {
      printf("%16s: '%s'\n", "PARSE SUCCESS", strs[i]);
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
      printf("Successfully parsed invalid url '%s'", x);
      break;
    }
  }

#if 0
  if (!failed) {
    Note("URL performance test start");
    for (int j = 0 ; j < 100000 ; ++j) {
      for (i = 0 ; i < countof(strs) ; ++i) {
        const char* x = strs[i];
        url.create(NULL);
        err = url.parse(x, strlen(x));
        url.destroy();
      }
    }
    Note("URL performance test end");
  }
#endif

  return (failures_to_status("test_url", failed));
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
HdrTest::test_mime()
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

  bri_box("test_mime");

  printf("   <<< MUST BE HAND-VERIFIED FOR FULL-BENEFIT>>>\n\n");

  start = mime;
  end   = start + strlen(start);

  mime_parser_init(&parser);

  bool must_copy_strs = false;

  hdr.create(nullptr);
  err = hdr.parse(&parser, &start, end, must_copy_strs, false);

  if (err < 0) {
    return (failures_to_status("test_mime", 1));
  }

  // Test the (new) continuation line folding to be correct. This should replace the
  // \r\n with two spaces (so a total of three between "part1" and "part2").
  int length               = 0;
  const char *continuation = hdr.value_get("continuation", 12, &length);

  if ((13 != length)) {
    printf("FAILED: continue header folded line was too short\n");
    return (failures_to_status("test_mime", 1));
  }

  if (strncmp(continuation + 5, "   ", 3)) {
    printf("FAILED: continue header unfolding did not produce correct WS's\n");
    return (failures_to_status("test_mime", 1));
  }

  if (strncmp(continuation, "part1   part2", 13)) {
    printf("FAILED: continue header unfolding was not correct\n");
    return (failures_to_status("test_mime", 1));
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

  length = hdr.length_get();
  printf("hdr.length_get() = %d\n", length);

  time_t t0, t1, t2;

  t0 = hdr.get_date();
  if (t0 == 0) {
    printf("FAILED: Initial date is zero but shouldn't be\n");
    return (failures_to_status("test_mime", 1));
  }

  t1 = time(nullptr);
  hdr.set_date(t1);
  t2 = hdr.get_date();
  if (t1 != t2) {
    printf("FAILED: set_date(%" PRId64 ") ... get_date = %" PRId64 "\n\n", static_cast<int64_t>(t1), static_cast<int64_t>(t2));
    return (failures_to_status("test_mime", 1));
  }

  hdr.value_append("Cache-Control", 13, "no-cache", 8, true);

  MIMEField *cc_field;
  StrList slist;

  cc_field = hdr.field_find("Cache-Control", 13);

  if (cc_field == nullptr) {
    printf("FAILED: missing Cache-Control header\n\n");
    return (failures_to_status("test_mime", 1));
  }

  // TODO: Do we need to check the "count" returned?
  cc_field->value_get_comma_list(&slist); // FIX: correct usage?

  if (cc_field->value_get_index("Private", 7) < 0) {
    printf("Failed: value_get_index of Cache-Control did not find private");
    return (failures_to_status("test_mime", 1));
  }
  if (cc_field->value_get_index("Bogus", 5) >= 0) {
    printf("Failed: value_get_index of Cache-Control incorrectly found bogus");
    return (failures_to_status("test_mime", 1));
  }
  if (hdr.value_get_index("foo", 3, "three", 5) < 0) {
    printf("Failed: value_get_index of foo did not find three");
    return (failures_to_status("test_mime", 1));
  }
  if (hdr.value_get_index("foo", 3, "bar", 3) < 0) {
    printf("Failed: value_get_index of foo did not find bar");
    return (failures_to_status("test_mime", 1));
  }
  if (hdr.value_get_index("foo", 3, "Bogus", 5) >= 0) {
    printf("Failed: value_get_index of foo incorrectly found bogus");
    return (failures_to_status("test_mime", 1));
  }

  mime_parser_clear(&parser);

  hdr.print(nullptr, 0, nullptr, nullptr);
  printf("\n");

  obj_describe((HdrHeapObjImpl *)(hdr.m_mime), true);

  hdr.fields_clear();

  hdr.destroy();

  return (failures_to_status("test_mime", 0));
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
HdrTest::test_http_aux(const char *request, const char *response)
{
  int err;
  HTTPHdr req_hdr, rsp_hdr;
  HTTPParser parser;
  const char *start;
  const char *end;

  int status = 1;

  printf("   <<< MUST BE HAND-VERIFIED FOR FULL BENEFIT >>>\n\n");

  /*** (1) parse the request string into req_hdr ***/

  start = request;
  end   = start + strlen(start); // 1 character past end of string

  http_parser_init(&parser);

  req_hdr.create(HTTP_TYPE_REQUEST);
  rsp_hdr.create(HTTP_TYPE_RESPONSE);

  printf("======== parsing\n\n");
  while (true) {
    err = req_hdr.parse_req(&parser, &start, end, true);
    if (err != PARSE_RESULT_CONT) {
      break;
    }
  }
  if (err == PARSE_RESULT_ERROR) {
    req_hdr.destroy();
    rsp_hdr.destroy();
    return (failures_to_status("test_http_aux", (status == 0)));
  }

  /*** useless copy to exercise copy function ***/

  HTTPHdr new_hdr;
  new_hdr.create(HTTP_TYPE_REQUEST);
  new_hdr.copy(&req_hdr);
  new_hdr.destroy();

  /*** (2) print out the request ***/

  printf("======== real request (length=%d)\n\n", static_cast<int>(strlen(request)));
  printf("%s\n", request);

  printf("\n[");
  req_hdr.print(nullptr, 0, nullptr, nullptr);
  printf("]\n\n");

  obj_describe(req_hdr.m_http, true);

  // req_hdr.destroy ();
  // ink_release_assert(!"req_hdr.destroy() not defined");

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
    return (failures_to_status("test_http_aux", (status == 0)));
  }

  http_parser_clear(&parser);

  /*** (4) print out the response ***/

  printf("\n======== real response (length=%d)\n\n", static_cast<int>(strlen(response)));
  printf("%s\n", response);

  printf("\n[");
  rsp_hdr.print(nullptr, 0, nullptr, nullptr);
  printf("]\n\n");

  obj_describe(rsp_hdr.m_http, true);

#define NNN 1000
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

      // printf("test_header: tmp = %d  err = %d  bufindex = %d\n", tmp, err, bufindex);
      putchar('{');
      for (i = 0; i < bufindex - last_bufindex; i++) {
        if (!iscntrl(buf[i])) {
          putchar(buf[i]);
        } else {
          printf("\\%o", buf[i]);
        }
      }
      putchar('}');
    } while (!err);
  }

  // rsp_hdr.print (NULL, 0, NULL, NULL);

  req_hdr.destroy();
  rsp_hdr.destroy();

  return (failures_to_status("test_http_aux", (status == 0)));
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
HdrTest::test_http_hdr_print_and_copy()
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
  int i, failures;

  failures = 0;

  bri_box("test_http_hdr_print_and_copy");

  for (i = 0; i < ntests; i++) {
    int status = test_http_hdr_print_and_copy_aux(i + 1, tests[i].req, tests[i].req_tgt, tests[i].rsp, tests[i].rsp_tgt);
    if (status == 0) {
      ++failures;
    }

    // Test for expected failures
    // parse with a '\0' in the header.  Should fail
    status = test_http_hdr_null_char(i + 1, tests[i].req, tests[i].req_tgt);
    if (status == 0) {
      ++failures;
    }

    // Parse with a CTL character in the method name.  Should fail
    status = test_http_hdr_ctl_char(i + 1, tests[i].req, tests[i].req_tgt);
    if (status == 0) {
      ++failures;
    }
  }

  return (failures_to_status("test_http_hdr_print_and_copy", failures));
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/
static const char *
comp_http_hdr(HTTPHdr *h1, HTTPHdr *h2)
{
  int h1_len, h2_len;
  int p_index, p_dumpoffset, rval;
  char *h1_pbuf, *h2_pbuf;

  h1_len = h1->length_get();
  h2_len = h2->length_get();

  if (h1_len != h2_len) {
    return "length mismatch";
  }

  h1_pbuf = static_cast<char *>(ats_malloc(h1_len + 1));
  h2_pbuf = static_cast<char *>(ats_malloc(h2_len + 1));

  p_index = p_dumpoffset = 0;
  rval                   = h1->print(h1_pbuf, h1_len, &p_index, &p_dumpoffset);
  if (rval != 1) {
    ats_free(h1_pbuf);
    ats_free(h2_pbuf);
    return "hdr print failed";
  }

  p_index = p_dumpoffset = 0;
  rval                   = h2->print(h2_pbuf, h2_len, &p_index, &p_dumpoffset);
  if (rval != 1) {
    ats_free(h1_pbuf);
    ats_free(h2_pbuf);
    return "hdr print failed";
  }

  rval = memcmp(h1_pbuf, h2_pbuf, h1_len);
  ats_free(h1_pbuf);
  ats_free(h2_pbuf);

  if (rval != 0) {
    return "compare failed";
  } else {
    return nullptr;
  }
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
HdrTest::test_http_hdr_copy_over_aux(int testnum, const char *request, const char *response)
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
    printf("FAILED: (test #%d) parse error parsing request hdr\n", testnum);
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
#if 0
    /*** (4) Copying over yourself ***/
  copy1.copy(&copy1);
  comp_str = comp_http_hdr(&req_hdr, &copy1);
  if (comp_str)
    goto done;

  copy2.copy(&copy2);
  comp_str = comp_http_hdr(&resp_hdr, &copy2);
  if (comp_str)
    goto done;
#endif

  /*** (5) Gender bending copying ***/
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

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/
int
HdrTest::test_http_hdr_null_char(int testnum, const char *request, const char * /*request_tgt*/)
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
    printf("FAILED: (test #%d) Internal buffer too small for null char test\n", testnum);
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
    printf("FAILED: (test #%d) no parse error parsing request with null char\n", testnum);
    return (0);
  }
  return 1;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/
int
HdrTest::test_http_hdr_ctl_char(int testnum, const char *request, const char * /*request_tgt */)
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
    printf("FAILED: (test #%d) Internal buffer too small for ctl char test\n", testnum);
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
    printf("FAILED: (test #%d) no parse error parsing method with ctl char\n", testnum);
    return (0);
  }
  return 1;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
HdrTest::test_http_hdr_print_and_copy_aux(int testnum, const char *request, const char *request_tgt, const char *response,
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

  char *marshal_buf   = static_cast<char *>(ats_malloc(2048));
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
    printf("FAILED: (test #%d) parse error parsing request hdr\n", testnum);
    ats_free(marshal_buf);
    return (0);
  }

  /*** (2) copy the request header ***/
  HTTPHdr new_hdr, marshal_hdr;
  RefCountObj ref;

  // Pretend to pin this object with a refcount.
  ref.refcount_inc();

  int marshal_len = hdr.m_heap->marshal(marshal_buf, marshal_bufsize);
  marshal_hdr.create(HTTP_TYPE_REQUEST);
  marshal_hdr.unmarshal(marshal_buf, marshal_len, &ref);
  new_hdr.create(HTTP_TYPE_REQUEST);
  new_hdr.copy(&marshal_hdr);

  /*** (3) print the request header and copy to buffers ***/

  prt_bufindex = prt_dumpoffset = 0;
  prt_ret                       = hdr.print(prt_buf, prt_bufsize, &prt_bufindex, &prt_dumpoffset);

  cpy_bufindex = cpy_dumpoffset = 0;
  cpy_ret                       = new_hdr.print(cpy_buf, cpy_bufsize, &cpy_bufindex, &cpy_dumpoffset);

  ats_free(marshal_buf);

  if ((prt_ret != 1) || (cpy_ret != 1)) {
    printf("FAILED: (test #%d) couldn't print req hdr or copy --- prt_ret=%d, cpy_ret=%d\n", testnum, prt_ret, cpy_ret);
    return (0);
  }

  if ((static_cast<size_t>(prt_bufindex) != strlen(request_tgt)) || (static_cast<size_t>(cpy_bufindex) != strlen(request_tgt))) {
    printf("FAILED: (test #%d) print req output size mismatch --- tgt=%d, prt_bufsize=%d, cpy_bufsize=%d\n", testnum,
           static_cast<int>(strlen(request_tgt)), prt_bufindex, cpy_bufindex);

    printf("ORIGINAL:\n[%.*s]\n", static_cast<int>(strlen(request)), request);
    printf("TARGET  :\n[%.*s]\n", static_cast<int>(strlen(request_tgt)), request_tgt);
    printf("PRT_BUFF:\n[%.*s]\n", prt_bufindex, prt_buf);
    printf("CPY_BUFF:\n[%.*s]\n", cpy_bufindex, cpy_buf);
    return (0);
  }

  if ((strncasecmp(request_tgt, prt_buf, strlen(request_tgt)) != 0) ||
      (strncasecmp(request_tgt, cpy_buf, strlen(request_tgt)) != 0)) {
    printf("FAILED: (test #%d) print req output mismatch\n", testnum);
    printf("ORIGINAL:\n[%.*s]\n", static_cast<int>(strlen(request)), request);
    printf("TARGET  :\n[%.*s]\n", static_cast<int>(strlen(request_tgt)), request_tgt);
    printf("PRT_BUFF:\n[%.*s]\n", prt_bufindex, prt_buf);
    printf("CPY_BUFF:\n[%.*s]\n", cpy_bufindex, cpy_buf);
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
    printf("FAILED: (test #%d) parse error parsing response hdr\n", testnum);
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
    printf("FAILED: (test #%d) couldn't print rsp hdr or copy --- prt_ret=%d, cpy_ret=%d\n", testnum, prt_ret, cpy_ret);
    return (0);
  }

  if ((static_cast<size_t>(prt_bufindex) != strlen(response_tgt)) || (static_cast<size_t>(cpy_bufindex) != strlen(response_tgt))) {
    printf("FAILED: (test #%d) print rsp output size mismatch --- tgt=%d, prt_bufsize=%d, cpy_bufsize=%d\n", testnum,
           static_cast<int>(strlen(response_tgt)), prt_bufindex, cpy_bufindex);
    printf("ORIGINAL:\n[%.*s]\n", static_cast<int>(strlen(response)), response);
    printf("TARGET  :\n[%.*s]\n", static_cast<int>(strlen(response_tgt)), response_tgt);
    printf("PRT_BUFF:\n[%.*s]\n", prt_bufindex, prt_buf);
    printf("CPY_BUFF:\n[%.*s]\n", cpy_bufindex, cpy_buf);
    return (0);
  }

  if ((strncasecmp(response_tgt, prt_buf, strlen(response_tgt)) != 0) ||
      (strncasecmp(response_tgt, cpy_buf, strlen(response_tgt)) != 0)) {
    printf("FAILED: (test #%d) print rsp output mismatch\n", testnum);
    printf("ORIGINAL:\n[%.*s]\n", static_cast<int>(strlen(response)), response);
    printf("TARGET  :\n[%.*s]\n", static_cast<int>(strlen(response_tgt)), response_tgt);
    printf("PRT_BUFF:\n[%.*s]\n", prt_bufindex, prt_buf);
    printf("CPY_BUFF:\n[%.*s]\n", cpy_bufindex, cpy_buf);
    return (0);
  }

  hdr.destroy();
  new_hdr.destroy();

  if (test_http_hdr_copy_over_aux(testnum, request, response) == 0) {
    return 0;
  }

  return (1);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
HdrTest::test_http()
{
  int status = 1;

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

  status = status & test_http_aux(request0, response0);
  status = status & test_http_aux(request09, response09);
  status = status & test_http_aux(request1, response1);
  status = status & test_http_aux(request_no_colon, response_no_colon);
  status = status & test_http_aux(request_no_val, response_no_colon);
  status = status & test_http_aux(request_leading_space, response0);
  status = status & test_http_aux(request_multi_fblock, response0);
  status = status & test_http_aux(request_padding, response0);
  status = status & test_http_aux(request_09p, response0);
  status = status & test_http_aux(request_09ht, response0);
  status = status & test_http_aux(request_11, response0);
  status = status & test_http_aux(request_unterminated, response_unterminated);
  status = status & test_http_aux(request_blank, response_blank);
  status = status & test_http_aux(request_blank2, response_blank2);
  status = status & test_http_aux(request_blank3, response_blank3);

  return (failures_to_status("test_http", (status == 0)));
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
HdrTest::test_http_mutation()
{
  int status = 1;

  bri_box("test_http_mutation");

  printf("   <<< MUST BE HAND-VERIFIED FOR FULL BENEFIT>>>\n\n");

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

  printf("\n======== before mutation ==========\n\n");
  printf("\n[");
  resp_hdr.print(nullptr, 0, nullptr, nullptr);
  printf("]\n\n");

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

  printf("\n======== mutated response ==========\n\n");
  printf("\n[");
  resp_hdr.print(nullptr, 0, nullptr, nullptr);
  printf("]\n\n");

  resp_hdr.destroy();

  return (failures_to_status("test_http_mutation", (status == 0)));
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
HdrTest::test_arena_aux(Arena *arena, int len)
{
  char *str      = arena->str_alloc(len);
  int verify_len = static_cast<int>(arena->str_length(str));

  if (len != verify_len) {
    printf("FAILED: requested %d, got %d bytes\n", len, verify_len);
    return (1); // 1 error (different return convention)
  } else {
    return (0); // no errors (different return convention)
  }
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
HdrTest::test_arena()
{
  bri_box("test_arena");

  Arena *arena;
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

  return (failures_to_status("test_arena", failures));
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
HdrTest::test_regex()
{
  DFA dfa;
  int status = 1;

  const char *test_harness[] = {"foo", "(.*\\.apache\\.org)", "(.*\\.example\\.com)"};

  bri_box("test_regex");

  dfa.compile(test_harness, SIZEOF(test_harness));
  status = status & (dfa.match("trafficserver.apache.org") == 1);
  status = status & (dfa.match("www.example.com") == 2);
  status = status & (dfa.match("aaaaaafooooooooinktomi....com.org") == -1);
  status = status & (dfa.match("foo") == 0);

  return (failures_to_status("test_regex", (status != 1)));
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
HdrTest::test_accept_language_match()
{
  bri_box("test_accept_language_match");

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
  int failures = 0;

  for (i = 0; test_cases[i].accept_language; i++) {
    StrList acpt_lang_list(false);
    HttpCompat::parse_comma_list(&acpt_lang_list, test_cases[i].accept_language,
                                 static_cast<int>(strlen(test_cases[i].accept_language)));

    Q = HttpCompat::match_accept_language(test_cases[i].content_language, static_cast<int>(strlen(test_cases[i].content_language)),
                                          &acpt_lang_list, &L, &I);

    if ((Q != test_cases[i].Q) || (L != test_cases[i].L) || (I != test_cases[i].I)) {
      printf("FAILED: (#%d) got { Q = %.3f; L = %d; I = %d; }, expected { Q = %.3f; L = %d; I = %d; }, from matching\n  '%s' "
             "against '%s'\n",
             i, Q, L, I, test_cases[i].Q, test_cases[i].L, test_cases[i].I, test_cases[i].content_language,
             test_cases[i].accept_language);
      ++failures;
    }
  }

  return (failures_to_status("test_accept_language_match", failures));
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
HdrTest::test_accept_charset_match()
{
  bri_box("test_accept_charset_match");

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
  int failures = 0;

  for (i = 0; test_cases[i].accept_charset; i++) {
    StrList acpt_lang_list(false);
    HttpCompat::parse_comma_list(&acpt_lang_list, test_cases[i].accept_charset,
                                 static_cast<int>(strlen(test_cases[i].accept_charset)));

    Q = HttpCompat::match_accept_charset(test_cases[i].content_charset, static_cast<int>(strlen(test_cases[i].content_charset)),
                                         &acpt_lang_list, &I);

    if ((Q != test_cases[i].Q) || (I != test_cases[i].I)) {
      printf("FAILED: (#%d) got { Q = %.3f; I = %d; }, expected { Q = %.3f; I = %d; }, from matching\n  '%s' against '%s'\n", i, Q,
             I, test_cases[i].Q, test_cases[i].I, test_cases[i].content_charset, test_cases[i].accept_charset);
      ++failures;
    }
  }

  return (failures_to_status("test_accept_charset_match", failures));
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
HdrTest::test_comma_vals()
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

  bri_box("test_comma_vals");

  HTTPHdr hdr;
  char field_name[32];
  int i, j, len, failures, ntests, ncommavals;

  failures = 0;
  ntests   = sizeof(tests) / sizeof(tests[0]);

  hdr.create(HTTP_TYPE_REQUEST);

  for (i = 0; i < ntests; i++) {
    snprintf(field_name, sizeof(field_name), "Test%d", i);

    MIMEField *f = hdr.field_create(field_name, static_cast<int>(strlen(field_name)));
    ink_release_assert(f->m_ptr_value == nullptr);

    hdr.field_attach(f);
    ink_release_assert(f->m_ptr_value == nullptr);

    hdr.field_value_set(f, tests[i].value, strlen(tests[i].value));
    ink_release_assert(f->m_ptr_value != tests[i].value); // should be copied
    ink_release_assert(f->m_len_value == strlen(tests[i].value));
    ink_release_assert(memcmp(f->m_ptr_value, tests[i].value, f->m_len_value) == 0);

    ncommavals = mime_field_value_get_comma_val_count(f);
    if (ncommavals != tests[i].value_count) {
      ++failures;
      printf("FAILED: test #%d (field value '%s') expected val count %d, got %d\n", i + 1, tests[i].value, tests[i].value_count,
             ncommavals);
    }

    for (j = 0; j < tests[i].value_count; j++) {
      const char *val = mime_field_value_get_comma_val(f, &len, j);
      int offset      = ((val == nullptr) ? -1 : (val - f->m_ptr_value));

      if ((offset != tests[i].pieces[j].offset) || (len != tests[i].pieces[j].len)) {
        ++failures;
        printf("FAILED: test #%d (field value '%s', commaval idx %d) expected [offset %d, len %d], got [offset %d, len %d]\n",
               i + 1, tests[i].value, j, tests[i].pieces[j].offset, tests[i].pieces[j].len, offset, len);
      }
    }
  }

  hdr.destroy();
  return (failures_to_status("test_comma_vals", failures));
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
HdrTest::test_set_comma_vals()
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

  bri_box("test_set_comma_vals");

  HTTPHdr hdr;
  char field_name[32];
  int i, failures, ntests;

  failures = 0;
  ntests   = sizeof(tests) / sizeof(tests[0]);

  hdr.create(HTTP_TYPE_REQUEST);

  for (i = 0; i < ntests; i++) {
    snprintf(field_name, sizeof(field_name), "Test%d", i);

    MIMEField *f = hdr.field_create(field_name, static_cast<int>(strlen(field_name)));
    hdr.field_value_set(f, tests[i].old_raw, strlen(tests[i].old_raw));
    mime_field_value_set_comma_val(hdr.m_heap, hdr.m_mime, f, tests[i].idx, tests[i].slice, strlen(tests[i].slice));
    ink_release_assert(f->m_ptr_value != nullptr);

    if ((f->m_len_value != strlen(tests[i].new_raw)) || (memcmp(f->m_ptr_value, tests[i].new_raw, f->m_len_value) != 0)) {
      ++failures;
      printf("FAILED:  test #%d (setting idx %d of '%s' to '%s') expected '%s' len %d, got '%.*s' len %d\n", i + 1, tests[i].idx,
             tests[i].old_raw, tests[i].slice, tests[i].new_raw, static_cast<int>(strlen(tests[i].new_raw)), f->m_len_value,
             f->m_ptr_value, f->m_len_value);
    }
  }

  hdr.destroy();
  return (failures_to_status("test_set_comma_vals", failures));
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
HdrTest::test_delete_comma_vals()
{
  bri_box("test_delete_comma_vals");
  rprintf(rtest, "  HdrTest test_delete_comma_vals: TEST NOT IMPLEMENTED\n");
  return (1);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
HdrTest::test_extend_comma_vals()
{
  bri_box("test_extend_comma_vals");
  rprintf(rtest, "  HdrTest test_extend_comma_vals: TEST NOT IMPLEMENTED\n");
  return (1);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
HdrTest::test_insert_comma_vals()
{
  bri_box("test_insert_comma_vals");
  rprintf(rtest, "  HdrTest test_insert_comma_vals: TEST NOT IMPLEMENTED\n");
  return (1);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
HdrTest::test_parse_comma_list()
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

  bri_box("test_parse_comma_list");

  int i, j, failures, ntests, offset;

  failures = (offset = 0);
  ntests   = sizeof(tests) / sizeof(tests[0]);

  for (i = 0; i < ntests; i++) {
    StrList list(false);
    HttpCompat::parse_comma_list(&list, tests[i].value);
    if (list.count != tests[i].count) {
      ++failures;
      printf("FAILED: test #%d (string '%s') expected list count %d, got %d\n", i + 1, tests[i].value, tests[i].count, list.count);
    }

    for (j = 0; j < tests[i].count; j++) {
      Str *cell = list.get_idx(j);
      if (cell != nullptr) {
        offset = cell->str - tests[i].value;
      }

      if (tests[i].pieces[j].offset == -1) // should not have a piece
      {
        if (cell != nullptr) {
          ++failures;
          printf("FAILED: test #%d (string '%s', idx %d) expected NULL piece, got [offset %d len %d]\n", i + 1, tests[i].value, j,
                 offset, static_cast<int>(cell->len));
        }
      } else // should have a piece
      {
        if (cell == nullptr) {
          ++failures;
          printf("FAILED: test #%d (string '%s', idx %d) expected [offset %d len %d], got NULL piece\n", i + 1, tests[i].value, j,
                 tests[i].pieces[j].offset, tests[i].pieces[j].len);
        } else if ((offset != tests[i].pieces[j].offset) || (cell->len != static_cast<size_t>(tests[i].pieces[j].len))) {
          ++failures;
          printf("FAILED: test #%d (string '%s', idx %d) expected [offset %d len %d], got [offset %d len %d]\n", i + 1,
                 tests[i].value, j, tests[i].pieces[j].offset, tests[i].pieces[j].len, offset, static_cast<int>(cell->len));
        }
      }
    }
  }

  return (failures_to_status("test_parse_comma_list", failures));
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
HdrTest::bri_box(const char *s)
{
  int i, len;

  len = static_cast<int>(strlen(s));
  printf("\n+-");
  for (i = 0; i < len; i++) {
    putchar('-');
  }
  printf("-+\n");
  printf("| %s |\n", s);
  printf("+-");
  for (i = 0; i < len; i++) {
    putchar('-');
  }
  printf("-+\n\n");
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
HdrTest::failures_to_status(const char *testname, int nfail)
{
  rprintf(rtest, "  HdrTest %s: %s\n", testname, ((nfail > 0) ? "FAILED" : "PASSED"));
  return ((nfail > 0) ? 0 : 1);
}

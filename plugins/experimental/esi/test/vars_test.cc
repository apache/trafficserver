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
#include <stdio.h>
#include <iostream>
#include <assert.h>
#include <string>
#include <stdarg.h>

#include "print_funcs.h"
#include "Variables.h"
#include "Expression.h"
#include "Utils.h"

using std::cout;
using std::cerr;
using std::endl;
using std::string;
using namespace EsiLib;

pthread_key_t threadKey;

void
addToHeaderList(const char *strings[], HttpHeaderList &headers)
{
  for (int i = 0; strings[i]; i += 2) {
    if (i % 4 == 0) {
      headers.push_back(HttpHeader(strings[i], -1, strings[i + 1], -1));
      headers.push_back(HttpHeader());
    } else {
      headers.push_back(HttpHeader(strings[i], strlen(strings[i]), strings[i + 1], strlen(strings[i + 1])));
    }
  }
}

string gFakeDebugLog;

void
fakeDebug(const char *tag, const char *fmt, ...)
{
  static const int LINE_SIZE = 1024;
  char buf[LINE_SIZE];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, LINE_SIZE, fmt, ap);
  printf("Debug (%s): %s\n", tag, buf);
  va_end(ap);
  gFakeDebugLog.append(buf);
}

int
main()
{
  pthread_key_create(&threadKey, NULL);
  Utils::init(&Debug, &Error);

  {
    cout << endl << "===================== Test 1" << endl;
    Variables esi_vars("vars_test", &Debug, &Error);
    const char *strings[] = {"Cookie",
                             "; c1=v1; c2=v2; ;   c3; c4=;    c5=v5  ",
                             "Host",
                             "example.com",
                             "Referer",
                             "google.com",
                             "Blah",
                             "Blah",
                             "Accept-Language",
                             "en-gb , en-us ,  ,",
                             "Accept-Language",
                             "ka-in",
                             0};

    HttpHeaderList headers;
    addToHeaderList(strings, headers);
    esi_vars.populate(headers);
    esi_vars.populate("a=b&c=d&e=f");

    assert(esi_vars.getValue("HTTP_COOKIE{c1}") == "v1");
    assert(esi_vars.getValue("HTTP_COOKIE{c2}") == "v2");
    assert(esi_vars.getValue("HTTP_COOKIE{c3}") == "");
    assert(esi_vars.getValue("HTTP_COOKIE{c4}") == "");
    assert(esi_vars.getValue("HTTP_COOKIE{c5}") == "v5");
    assert(esi_vars.getValue("HTTP_COOKIE{c2}") != "v1");
    assert(esi_vars.getValue("HTTP_COOKIE{C1}") != "v1");
    assert(esi_vars.getValue("HTTP_USER_AGENT").size() == 0);
    assert(esi_vars.getValue("BLAH").size() == 0);
    assert(esi_vars.getValue("HTTP_HOST") == "example.com");
    assert(esi_vars.getValue("HTTP_host") == "example.com");
    assert(esi_vars.getValue("HTTP_REFERER") == "google.com");
    assert(esi_vars.getValue("HTTP_BLAH").size() == 0);
    assert(esi_vars.getValue("HTTP_ACCEPT_LANGUAGE{en-gb}") == "true");
    assert(esi_vars.getValue("HTTP_ACCEPT_LANGUAGE{en-us}") == "true");
    assert(esi_vars.getValue("HTTP_ACCEPT_LANGUAGE{es-us}") == "");
    assert(esi_vars.getValue("QUERY_STRING") == "a=b&c=d&e=f");
    assert(esi_vars.getValue("QUERY_STRING{a}") == "b");
    assert(esi_vars.getValue("QUERY_STRING{e}") == "f");
    assert(esi_vars.getValue("QUERY_STRING{z}") == "");
    assert(esi_vars.getValue("HTTP_COOKIE{c1") == "");
    assert(esi_vars.getValue("HTTP_COOKIEc1") == "");
    assert(esi_vars.getValue("HTTP_COOKIEc1}") == "");
    assert(esi_vars.getValue("{c1}") == "");
    assert(esi_vars.getValue("HTTP_COOKIE{c1{c2}}") == "");
    assert(esi_vars.getValue("HTTP_COOKIE{c1{c2}") == "");
    assert(esi_vars.getValue("HTTP_COOKIE{c1c}2}") == "");
    assert(esi_vars.getValue("HTTP_COOKIE{c1c2}") == "");
    assert(esi_vars.getValue("{c1c2}") == "");
    assert(esi_vars.getValue("HTTP_COOKIE{}") == "");
    assert(esi_vars.getValue("HTTP_COOKIE{c1}c") == "");
    esi_vars.populate(HttpHeader("hosT", -1, "localhost", -1));
    assert(esi_vars.getValue("HTTP_HOST") == "localhost");

    esi_vars.populate(HttpHeader("User-agent", -1, "Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1.6) "
                                                   "Gecko/20091201 Firefox/3.5.6 (.NETgecko CLR 3.5.30729)",
                                 -1));

    /*
    assert(esi_vars.getValue("HTTP_USER_AGENT{vendor}") == "Gecko");
    assert(esi_vars.getValue("HTTP_USER_AGENT{platform}") == "windows_xp");
    assert(esi_vars.getValue("HTTP_USER_AGENT{version}") == "1.9");
    assert(esi_vars.getValue("HTTP_USER_AGENT{blah}") == "");
    */

    assert(esi_vars.getValue("HTTP_ACCEPT_LANGUAGE{ka-in}") == "true");

    esi_vars.clear();
    assert(esi_vars.getValue("QUERY_STRING") == "");
    assert(esi_vars.getValue("QUERY_STRING{a}") == "");
    assert(esi_vars.getValue("HTTP_COOKIE{c1}") == "");
    esi_vars.populate(headers);
    esi_vars.populate("a=b&c=d&e=f");

    Expression esi_expr("vars_test", &Debug, &Error, esi_vars);
    assert(esi_expr.expand(0) == "");
    assert(esi_expr.expand("") == "");
    assert(esi_expr.expand("blah") == "blah");
    assert(esi_expr.expand("blah$(HTTP_HOST") == "");
    assert(esi_expr.expand("blah$A(HTTP_HOST)") == "blah$A(HTTP_HOST)");
    assert(esi_expr.expand("blah$()") == "blah");
    assert(esi_expr.expand("blah-$(HTTP_HOST)") == "blah-example.com");
    assert(esi_expr.expand("blah-$(HTTP_REFERER)") == "blah-google.com");
    assert(esi_expr.expand("blah-$(HTTP_COOKIE{c1})") == "blah-v1");
    assert(esi_expr.expand("blah-$(HTTP_COOKIE{c1a})") == "blah-");
    assert(esi_expr.expand("blah-$(HTTP_COOKIE{c1}$(HTTP_HOST))") == "");
    assert(esi_expr.expand("blah-$(HTTP_COOKIE{c1})-$(HTTP_HOST)") == "blah-v1-example.com");
    assert(esi_expr.expand("$()") == "");
    assert(esi_expr.expand("$(HTTP_COOKIE{c1})$(HTTP_COOKIE{c2})$(HTTP_HOST)") == "v1v2example.com");

    // quotes
    assert(esi_expr.expand("'blah") == "");  // unterminated quote
    assert(esi_expr.expand("\"blah") == ""); // unterminated quote
    assert(esi_expr.expand("'blah'") == "blah");
    assert(esi_expr.expand("\"blah\"") == "blah");
    assert(esi_expr.expand("'$(HTTP_COOKIE{c1})'") == "v1");
    assert(esi_expr.expand("\"$(HTTP_HOST)\"") == "example.com");

    // leading/trailing whitespace
    assert(esi_expr.expand("   blah  ") == "blah");
    assert(esi_expr.expand("   $(HTTP_REFERER) $(HTTP_HOST)  ") == "google.com example.com");
    assert(esi_expr.expand(" ' foo ' ") == " foo ");
    assert(esi_expr.expand(" ' foo '") == " foo ");
    assert(esi_expr.expand("bar ") == "bar");

    // evaluate tests
    assert(esi_expr.evaluate("foo") == true);
    assert(esi_expr.evaluate("") == false);
    assert(esi_expr.evaluate("$(HTTP_HOST)") == true);
    assert(esi_expr.evaluate("$(HTTP_XHOST)") == false);
    assert(esi_expr.evaluate("foo == foo") == true);
    assert(esi_expr.evaluate("'foo' == \"foo\"") == true);
    assert(esi_expr.evaluate("foo == foo1") == false);
    assert(esi_expr.evaluate("'foo' == \"foo1\"") == false);
    assert(esi_expr.evaluate("$(HTTP_REFERER) == google.com") == true);
    assert(esi_expr.evaluate("$(HTTP_HOST)=='example.com'") == true);
    assert(esi_expr.evaluate("$(HTTP_REFERER) != google.com") == false);
    assert(esi_expr.evaluate("$(HTTP_HOST)!='example.com'") == false);
    assert(esi_expr.evaluate("$(HTTP_HOST) == 'facebook.com'") == false);
    assert(esi_expr.evaluate("!") == true);
    assert(esi_expr.evaluate("!abc") == false);
    assert(esi_expr.evaluate("!$(FOO_BAR)") == true);
    assert(esi_expr.evaluate("!$(HTTP_HOST)") == false);
    assert(esi_expr.evaluate("abc!abc") == true);
    assert(esi_expr.evaluate("$(HTTP_COOKIE{c1}) == 'v1'") == true);
    assert(esi_expr.evaluate("$(HTTP_COOKIE{c1b}) == 'v1'") == false);
    assert(esi_expr.evaluate("$(HTTP_COOKIE{c1}) <= 'v2'") == true);
    assert(esi_expr.evaluate("$(HTTP_COOKIE{c1}) < 'v2'") == true);
    assert(esi_expr.evaluate("$(HTTP_COOKIE{c1}) >= 'v0'") == true);
    assert(esi_expr.evaluate("$(HTTP_COOKIE{c1}) > 'v2'") == false);
    assert(esi_expr.evaluate("$(HTTP_COOKIE{c1}) & 'v2'") == true);
    assert(esi_expr.evaluate("$(HTTP_COOKIE{foo}) & $(HTTP_COOKIE{bar})") == false);
    assert(esi_expr.evaluate("'' | $(HTTP_COOKIE{c1})") == true);
    assert(esi_expr.evaluate("$(HTTP_COOKIE{foo}) | $(HTTP_COOKIE{bar})") == false);

    // default value tests
    assert(esi_expr.expand("foo|bar") == "foo|bar");
    assert(esi_expr.expand("$(HTTP_HOST|") == "");
    assert(esi_expr.expand("$(HTTP_HOST|foo") == "");
    assert(esi_expr.expand("$(HTTP_HOST|foo)") == "example.com");
    assert(esi_expr.expand("$(HTTP_XHOST|foo)") == "foo");
    assert(esi_expr.expand("$(|foo)") == "foo");
    assert(esi_expr.expand("$(HTTP_ACCEPT_LANGUAGE{en-uk})") == "");
    assert(esi_expr.expand("$(HTTP_ACCEPT_LANGUAGE{en-uk}|'yes')") == "yes");
    assert(esi_expr.expand("$(HTTP_ACCEPT_LANGUAGE{en-uk}|'yes with space')") == "yes with space");
    assert(esi_expr.expand("$(HTTP_ACCEPT_LANGUAGE{en-gb}|'yes')") == "true");
    assert(esi_expr.expand("$(HTTP_ACCEPT_LANGUAGE{en-gb}|'yes)") == "");
    assert(esi_expr.expand("$(HTTP_ACCEPT_LANGUAGE{en-uk}|'yes)") == "");

    assert(esi_expr.evaluate("$(HTTP_COOKIE{non-existent}) < 7") == false);
    assert(esi_expr.evaluate("$(HTTP_COOKIE{c1}) > $(HTTP_COOKIE{non-existent})") == false);
    assert(esi_expr.evaluate("$(HTTP_COOKIE{non-existent}) <= 7") == false);
    assert(esi_expr.evaluate("$(HTTP_COOKIE{c1}) >= $(HTTP_COOKIE{non-existent})") == false);

    // query string tests
    esi_vars.clear();
    assert(esi_vars.getValue("QUERY_STRING").size() == 0);
    esi_vars.populate("a");
    assert(esi_vars.getValue("QUERY_STRING") == "a");
    assert(esi_vars.getValue("QUERY_STRING{a}").size() == 0);

    esi_vars.clear();
    assert(esi_vars.getValue("QUERY_STRING").size() == 0);
    esi_vars.populate("");
    assert(esi_vars.getValue("QUERY_STRING") == "");
    assert(esi_vars.getValue("QUERY_STRING{a}").size() == 0);

    esi_vars.clear();
    assert(esi_vars.getValue("QUERY_STRING").size() == 0);
    esi_vars.populate("a=b");
    assert(esi_vars.getValue("QUERY_STRING") == "a=b");
    assert(esi_vars.getValue("QUERY_STRING{a}") == "b");

    esi_vars.clear();
    assert(esi_vars.getValue("QUERY_STRING").size() == 0);
    esi_vars.populate("a=b&");
    assert(esi_vars.getValue("QUERY_STRING") == "a=b&");
    assert(esi_vars.getValue("QUERY_STRING{a}") == "b");

    esi_vars.clear();
    assert(esi_vars.getValue("QUERY_STRING").size() == 0);
    esi_vars.populate("&a=b&");
    assert(esi_vars.getValue("QUERY_STRING") == "&a=b&");
    assert(esi_vars.getValue("QUERY_STRING{a}") == "b");

    esi_vars.clear();
    assert(esi_vars.getValue("QUERY_STRING").size() == 0);
    esi_vars.populate("name1=value1&name2=value2&name3=val%32ue");
    assert(esi_vars.getValue("QUERY_STRING") == "name1=value1&name2=value2&name3=val%32ue");
    assert(esi_vars.getValue("QUERY_STRING{name1}") == "value1");
    assert(esi_vars.getValue("QUERY_STRING{name2}") == "value2");
    assert(esi_vars.getValue("QUERY_STRING{name3}") == "val%32ue");
    assert(esi_vars.getValue("QUERY_STRING{name4}") == "");
    assert(esi_vars.getValue("QUERY_STRING{}") == "");
    assert(esi_vars.getValue("QUERY_STRING{foo}") == "");

    esi_vars.clear();
    assert(esi_vars.getValue("QUERY_STRING").size() == 0);
    esi_vars.populate("=");
    assert(esi_vars.getValue("QUERY_STRING") == "=");
    assert(esi_vars.getValue("QUERY_STRING{a}") == "");

    esi_vars.clear();
    assert(esi_vars.getValue("QUERY_STRING").size() == 0);
    esi_vars.populate("a=&");
    assert(esi_vars.getValue("QUERY_STRING") == "a=&");
    assert(esi_vars.getValue("QUERY_STRING{a}") == "");

    esi_vars.clear();
    assert(esi_vars.getValue("QUERY_STRING").size() == 0);
    esi_vars.populate("=b&");
    assert(esi_vars.getValue("QUERY_STRING") == "=b&");
    assert(esi_vars.getValue("QUERY_STRING{a}") == "");

    esi_vars.clear();
    assert(esi_vars.getValue("QUERY_STRING").size() == 0);
    esi_vars.populate("foo=bar&blah=&");
    assert(esi_vars.getValue("QUERY_STRING") == "foo=bar&blah=&");
    assert(esi_vars.getValue("QUERY_STRING{foo}") == "bar");
    assert(esi_vars.getValue("QUERY_STRING{blah}") == "");

    esi_vars.clear();
    assert(esi_vars.getValue("QUERY_STRING").size() == 0);
    esi_vars.populate("=blah&foo=bar");
    assert(esi_vars.getValue("QUERY_STRING") == "=blah&foo=bar");
    assert(esi_vars.getValue("QUERY_STRING{foo}") == "bar");
    assert(esi_vars.getValue("QUERY_STRING{blah}") == "");
  }

  {
    cout << endl << "===================== Test 2" << endl;
    gFakeDebugLog.assign("");
    Variables esi_vars("vars_test", &fakeDebug, &Error);

    esi_vars.populate(HttpHeader("Host", -1, "example.com", -1));
    esi_vars.populate(HttpHeader("Referer", -1, "google.com", -1));
    const char *PARSING_DEBUG_MESSAGE = "Parsing headers";
    assert(gFakeDebugLog.find(PARSING_DEBUG_MESSAGE) >= gFakeDebugLog.size()); // shouldn't have parsed yet

    assert(esi_vars.getValue("HTTP_HOST") == "example.com");
    size_t str_pos = gFakeDebugLog.find(PARSING_DEBUG_MESSAGE);
    assert(str_pos < gFakeDebugLog.size()); // should've parsed now

    assert(esi_vars.getValue("HTTP_REFERER") == "google.com");
    assert(gFakeDebugLog.rfind(PARSING_DEBUG_MESSAGE) == str_pos); // shouldn't have parsed again

    esi_vars.populate(HttpHeader("Host", -1, "localhost", -1));
    assert(esi_vars.getValue("HTTP_HOST") == "localhost");
    assert(gFakeDebugLog.rfind(PARSING_DEBUG_MESSAGE) == str_pos); // should not have parsed all headers
    assert(esi_vars.getValue("HTTP_HOST") == "localhost");         // only this one
    assert(esi_vars.getValue("HTTP_REFERER") == "google.com");

    esi_vars.clear();
    esi_vars.populate(HttpHeader("Host", -1, "home", -1));
    assert(esi_vars.getValue("HTTP_HOST") == "home");
    assert(gFakeDebugLog.rfind(PARSING_DEBUG_MESSAGE) != str_pos); // should have parsed again
    assert(esi_vars.getValue("HTTP_REFERER") == "");
  }

  {
    cout << endl << "===================== Test 3" << endl;
    Variables esi_vars("vars_test", &Debug, &Error);

    esi_vars.populate(HttpHeader("Host", -1, "example.com", -1));
    esi_vars.populate(HttpHeader("Referer", -1, "google.com", -1));
    esi_vars.populate(HttpHeader("Cookie", -1, "age=21; grade=-5; avg=4.3; t1=\" \"; t2=0.0", -1));
    esi_vars.populate(HttpHeader("Cookie", -1, "t3=-0; t4=0; t5=6", -1));

    Expression esi_expr("vars_test", &Debug, &Error, esi_vars);
    assert(esi_expr.evaluate("$(HTTP_COOKIE{age}) >= -9"));
    assert(esi_expr.evaluate("$(HTTP_COOKIE{age}) > 9"));
    assert(esi_expr.evaluate("$(HTTP_COOKIE{age}) < 22"));
    assert(esi_expr.evaluate("$(HTTP_COOKIE{age}) <= 22.1"));
    assert(esi_expr.evaluate("$(HTTP_COOKIE{age}) > 100a")); // non-numerical
    assert(esi_expr.evaluate("$(HTTP_COOKIE{t1})"));         // non-numerical
    assert(esi_expr.evaluate("$(HTTP_COOKIE{grade})"));
    assert(esi_expr.evaluate("$(HTTP_COOKIE{grade}) == -5"));
    assert(esi_expr.evaluate("$(HTTP_COOKIE{grade}) != -5.1"));
    assert(esi_expr.evaluate("!$(HTTP_COOKIE{t2})"));
    assert(esi_expr.evaluate("!$(HTTP_COOKIE{t3})"));
    assert(esi_expr.evaluate("!$(HTTP_COOKIE{t4})"));
    assert(esi_expr.evaluate("+4.3 == $(HTTP_COOKIE{avg})"));
    assert(esi_expr.evaluate("$(HTTP_COOKIE{grade}) < -0x2"));
    assert(esi_expr.evaluate("$(HTTP_COOKIE{t2}) | 1"));
    assert(!esi_expr.evaluate("$(HTTP_COOKIE{t3}) & 1"));
    assert(esi_expr.evaluate("$(HTTP_COOKIE{t5}) == 6"));

    string strange_cookie("c1=123");
    strange_cookie[4] = '\0';
    esi_vars.populate(HttpHeader("Cookie", -1, strange_cookie.data(), strange_cookie.size()));
    assert(esi_vars.getValue("HTTP_COOKIE{c1}").size() == 3);
    assert(esi_vars.getValue("HTTP_COOKIE{c1}")[1] == '\0');
    assert(esi_expr.evaluate("$(HTTP_COOKIE{c1}) != 1"));
  }

  {
    cout << endl << "===================== Test 4" << endl;
    Variables esi_vars("vars_test", &Debug, &Error);
    string cookie_str("FPS=dl; mb=d=OPsv7rvU4FFaAOoIRi75BBuqdMdbMLFuDwQmk6nKrCgno7L4xuN44zm7QBQJRmQSh8ken6GSVk8-&v=1; C=mg=1; "
                      "Y=v=1&n=fmaptagvuff50&l=fc0d94i7/o&p=m2f0000313000400&r=8j&lg=en-US&intl=us; "
                      "F=a=4KvLV9IMvTJnIAqCk25y9Use6hnPALtUf3n78PihlcIqvmzoW.Ax8UyW8_oxtgFNrrdmooqZmPa7WsX4gE."
                      "6sI69wuNwRKrRPFT29h9lhwuxxLz0RuQedVXhJhc323Q-&b=8gQZ"); // TODO - might need to
    esi_vars.populate(HttpHeader("Cookie", -1, cookie_str.data(), cookie_str.size()));

    assert(esi_vars.getValue("HTTP_COOKIE{FPS}") == "dl");
    assert(esi_vars.getValue("HTTP_COOKIE{mb}") ==
           "d=OPsv7rvU4FFaAOoIRi75BBuqdMdbMLFuDwQmk6nKrCgno7L4xuN44zm7QBQJRmQSh8ken6GSVk8-&v=1");
    assert(esi_vars.getValue("HTTP_COOKIE{Y;n}") == "fmaptagvuff50");
    assert(esi_vars.getValue("HTTP_COOKIE{Y;l}") == "fc0d94i7/o");
    assert(esi_vars.getValue("HTTP_COOKIE{Y;intl}") == "us");
    assert(esi_vars.getValue("HTTP_COOKIE{C}") == "mg=1");
    assert(esi_vars.getValue("HTTP_COOKIE{non-existent}") == "");

    assert(esi_vars.getValue("HTTP_COOKIE{Y}") == "v=1&n=fmaptagvuff50&l=fc0d94i7/o&p=m2f0000313000400&r=8j&lg=en-US&intl=us");

    esi_vars.populate(HttpHeader("Host", -1, "www.example.com", -1));
    assert(esi_vars.getValue("HTTP_COOKIE{F}") ==
           "a=4KvLV9IMvTJnIAqCk25y9Use6hnPALtUf3n78PihlcIqvmzoW."
           "Ax8UyW8_oxtgFNrrdmooqZmPa7WsX4gE.6sI69wuNwRKrRPFT29h9lhwuxxLz0RuQedVXhJhc323Q-&b=8gQZ");
    assert(esi_vars.getValue("HTTP_HOST") == "www.example.com");

    esi_vars.populate(HttpHeader("Cookie", -1, "a=b; c=d", -1));
    assert(esi_vars.getValue("HTTP_COOKIE{Y;intl}") == "us");
    assert(esi_vars.getValue("HTTP_COOKIE{F}") ==
           "a=4KvLV9IMvTJnIAqCk25y9Use6hnPALtUf3n78PihlcIqvmzoW."
           "Ax8UyW8_oxtgFNrrdmooqZmPa7WsX4gE.6sI69wuNwRKrRPFT29h9lhwuxxLz0RuQedVXhJhc323Q-&b=8gQZ");
    assert(esi_vars.getValue("HTTP_COOKIE{a}") == "b");
    assert(esi_vars.getValue("HTTP_COOKIE{c}") == "d");
    assert(esi_vars.getValue("HTTP_HOST") == "www.example.com");
    assert(esi_vars.getValue("HTTP_COOKIE{Y;blah}") == "");

    esi_vars.clear();
    esi_vars.populate(HttpHeader("Cookie", -1, "Y=junk", -1));
    assert(esi_vars.getValue("HTTP_COOKIE{Y}") == "junk");
    assert(esi_vars.getValue("HTTP_COOKIE{Y;intl}") == "");
  }

  {
    cout << endl << "===================== Test 5" << endl;
    Variables esi_vars("vars_test", &Debug, &Error);
    esi_vars.populate(HttpHeader("hdr1", -1, "hval1", -1));
    esi_vars.populate(HttpHeader("Hdr2", -1, "hval2", -1));
    esi_vars.populate(HttpHeader("@Intenal-hdr1", -1, "internal-hval1", -1));

    assert(esi_vars.getValue("HTTP_HEADER{hdr1}") == "hval1");
    assert(esi_vars.getValue("HTTP_HEADER{hdr2}") == "");
    assert(esi_vars.getValue("HTTP_HEADER{Hdr2}") == "hval2");
    assert(esi_vars.getValue("HTTP_HEADER{non-existent}") == "");
    assert(esi_vars.getValue("HTTP_HEADER{@Intenal-hdr1}") == "internal-hval1");
  }

  cout << endl << "All tests passed!" << endl;
  return 0;
}

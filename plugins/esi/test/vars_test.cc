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

#include <cstdio>
#include <string>
#include <cstdarg>

#define CATCH_CONFIG_MAIN
#include <catch.hpp>

#include "Variables.h"
#include "Expression.h"
#include "Utils.h"

using std::string;
using namespace EsiLib;

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

extern void enableFakeDebugLog();
extern string gFakeDebugLog;

TEST_CASE("esi vars test")
{
  Utils::HeaderValueList allowlistCookies;

  SECTION("Test 1")
  {
    allowlistCookies.push_back("c1");
    allowlistCookies.push_back("c2");
    allowlistCookies.push_back("c3");
    allowlistCookies.push_back("c4");
    allowlistCookies.push_back("c5");
    int dummy;
    Variables esi_vars(&dummy, allowlistCookies);
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
                             nullptr};

    HttpHeaderList headers;
    addToHeaderList(strings, headers);
    esi_vars.populate(headers);
    esi_vars.populate("a=b&c=d&e=f");

    REQUIRE(esi_vars.getValue("HTTP_COOKIE{c1}") == "v1");
    REQUIRE(esi_vars.getValue("HTTP_COOKIE{c2}") == "v2");
    REQUIRE(esi_vars.getValue("HTTP_COOKIE{c3}") == "");
    REQUIRE(esi_vars.getValue("HTTP_COOKIE{c4}") == "");
    REQUIRE(esi_vars.getValue("HTTP_COOKIE{c5}") == "v5");
    REQUIRE(esi_vars.getValue("HTTP_COOKIE{c2}") != "v1");
    REQUIRE(esi_vars.getValue("HTTP_COOKIE{C1}") != "v1");
    REQUIRE(esi_vars.getValue("HTTP_USER_AGENT").size() == 0);
    REQUIRE(esi_vars.getValue("BLAH").size() == 0);
    REQUIRE(esi_vars.getValue("HTTP_HOST") == "example.com");
    REQUIRE(esi_vars.getValue("HTTP_host") == "example.com");
    REQUIRE(esi_vars.getValue("HTTP_REFERER") == "google.com");
    REQUIRE(esi_vars.getValue("HTTP_BLAH").size() == 0);
    REQUIRE(esi_vars.getValue("HTTP_ACCEPT_LANGUAGE{en-gb}") == "true");
    REQUIRE(esi_vars.getValue("HTTP_ACCEPT_LANGUAGE{en-us}") == "true");
    REQUIRE(esi_vars.getValue("HTTP_ACCEPT_LANGUAGE{es-us}") == "");
    REQUIRE(esi_vars.getValue("QUERY_STRING") == "a=b&c=d&e=f");
    REQUIRE(esi_vars.getValue("QUERY_STRING{a}") == "b");
    REQUIRE(esi_vars.getValue("QUERY_STRING{e}") == "f");
    REQUIRE(esi_vars.getValue("QUERY_STRING{z}") == "");
    REQUIRE(esi_vars.getValue("HTTP_COOKIE{c1") == "");
    REQUIRE(esi_vars.getValue("HTTP_COOKIEc1") == "");
    REQUIRE(esi_vars.getValue("HTTP_COOKIEc1}") == "");
    REQUIRE(esi_vars.getValue("{c1}") == "");
    REQUIRE(esi_vars.getValue("HTTP_COOKIE{c1{c2}}") == "");
    REQUIRE(esi_vars.getValue("HTTP_COOKIE{c1{c2}") == "");
    REQUIRE(esi_vars.getValue("HTTP_COOKIE{c1c}2}") == "");
    REQUIRE(esi_vars.getValue("HTTP_COOKIE{c1c2}") == "");
    REQUIRE(esi_vars.getValue("{c1c2}") == "");
    REQUIRE(esi_vars.getValue("HTTP_COOKIE{}") == "");
    REQUIRE(esi_vars.getValue("HTTP_COOKIE{c1}c") == "");
    esi_vars.populate(HttpHeader("hosT", -1, "localhost", -1));
    REQUIRE(esi_vars.getValue("HTTP_HOST") == "localhost");

    esi_vars.populate(HttpHeader("User-agent", -1,
                                 "Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1.6) "
                                 "Gecko/20091201 Firefox/3.5.6 (.NETgecko CLR 3.5.30729)",
                                 -1));

    REQUIRE(esi_vars.getValue("HTTP_ACCEPT_LANGUAGE{ka-in}") == "true");

    esi_vars.clear();
    REQUIRE(esi_vars.getValue("QUERY_STRING") == "");
    REQUIRE(esi_vars.getValue("QUERY_STRING{a}") == "");
    REQUIRE(esi_vars.getValue("HTTP_COOKIE{c1}") == "");
    esi_vars.populate(headers);
    esi_vars.populate("a=b&c=d&e=f");

    Expression esi_expr(esi_vars);
    REQUIRE(esi_expr.expand(nullptr) == "");
    REQUIRE(esi_expr.expand("") == "");
    REQUIRE(esi_expr.expand("blah") == "blah");
    REQUIRE(esi_expr.expand("blah$(HTTP_HOST") == "");
    REQUIRE(esi_expr.expand("blah$A(HTTP_HOST)") == "blah$A(HTTP_HOST)");
    REQUIRE(esi_expr.expand("blah$()") == "blah");
    REQUIRE(esi_expr.expand("blah-$(HTTP_HOST)") == "blah-example.com");
    REQUIRE(esi_expr.expand("blah-$(HTTP_REFERER)") == "blah-google.com");
    REQUIRE(esi_expr.expand("blah-$(HTTP_COOKIE{c1})") == "blah-v1");
    REQUIRE(esi_expr.expand("blah-$(HTTP_COOKIE{c1a})") == "blah-");
    REQUIRE(esi_expr.expand("blah-$(HTTP_COOKIE{c1}$(HTTP_HOST))") == "");
    REQUIRE(esi_expr.expand("blah-$(HTTP_COOKIE{c1})-$(HTTP_HOST)") == "blah-v1-example.com");
    REQUIRE(esi_expr.expand("$()") == "");
    REQUIRE(esi_expr.expand("$(HTTP_COOKIE{c1})$(HTTP_COOKIE{c2})$(HTTP_HOST)") == "v1v2example.com");

    // quotes
    REQUIRE(esi_expr.expand("'blah") == "");  // unterminated quote
    REQUIRE(esi_expr.expand("\"blah") == ""); // unterminated quote
    REQUIRE(esi_expr.expand("'blah'") == "blah");
    REQUIRE(esi_expr.expand("\"blah\"") == "blah");
    REQUIRE(esi_expr.expand("'$(HTTP_COOKIE{c1})'") == "v1");
    REQUIRE(esi_expr.expand("\"$(HTTP_HOST)\"") == "example.com");

    // leading/trailing whitespace
    REQUIRE(esi_expr.expand("   blah  ") == "blah");
    REQUIRE(esi_expr.expand("   $(HTTP_REFERER) $(HTTP_HOST)  ") == "google.com example.com");
    REQUIRE(esi_expr.expand(" ' foo ' ") == " foo ");
    REQUIRE(esi_expr.expand(" ' foo '") == " foo ");
    REQUIRE(esi_expr.expand("bar ") == "bar");

    // evaluate tests
    REQUIRE(esi_expr.evaluate("foo") == true);
    REQUIRE(esi_expr.evaluate("") == false);
    REQUIRE(esi_expr.evaluate("$(HTTP_HOST)") == true);
    REQUIRE(esi_expr.evaluate("$(HTTP_XHOST)") == false);
    REQUIRE(esi_expr.evaluate("foo == foo") == true);
    REQUIRE(esi_expr.evaluate("'foo' == \"foo\"") == true);
    REQUIRE(esi_expr.evaluate("foo == foo1") == false);
    REQUIRE(esi_expr.evaluate("'foo' == \"foo1\"") == false);
    REQUIRE(esi_expr.evaluate("$(HTTP_REFERER) == google.com") == true);
    REQUIRE(esi_expr.evaluate("$(HTTP_HOST)=='example.com'") == true);
    REQUIRE(esi_expr.evaluate("$(HTTP_REFERER) != google.com") == false);
    REQUIRE(esi_expr.evaluate("$(HTTP_HOST)!='example.com'") == false);
    REQUIRE(esi_expr.evaluate("$(HTTP_HOST) == 'facebook.com'") == false);
    REQUIRE(esi_expr.evaluate("!") == true);
    REQUIRE(esi_expr.evaluate("!abc") == false);
    REQUIRE(esi_expr.evaluate("!$(FOO_BAR)") == true);
    REQUIRE(esi_expr.evaluate("!$(HTTP_HOST)") == false);
    REQUIRE(esi_expr.evaluate("abc!abc") == true);
    REQUIRE(esi_expr.evaluate("$(HTTP_COOKIE{c1}) == 'v1'") == true);
    REQUIRE(esi_expr.evaluate("$(HTTP_COOKIE{c1b}) == 'v1'") == false);
    REQUIRE(esi_expr.evaluate("$(HTTP_COOKIE{c1}) <= 'v2'") == true);
    REQUIRE(esi_expr.evaluate("$(HTTP_COOKIE{c1}) < 'v2'") == true);
    REQUIRE(esi_expr.evaluate("$(HTTP_COOKIE{c1}) >= 'v0'") == true);
    REQUIRE(esi_expr.evaluate("$(HTTP_COOKIE{c1}) > 'v2'") == false);
    REQUIRE(esi_expr.evaluate("$(HTTP_COOKIE{c1}) & 'v2'") == true);
    REQUIRE(esi_expr.evaluate("$(HTTP_COOKIE{foo}) & $(HTTP_COOKIE{bar})") == false);
    REQUIRE(esi_expr.evaluate("'' | $(HTTP_COOKIE{c1})") == true);
    REQUIRE(esi_expr.evaluate("$(HTTP_COOKIE{foo}) | $(HTTP_COOKIE{bar})") == false);

    // default value tests
    REQUIRE(esi_expr.expand("foo|bar") == "foo|bar");
    REQUIRE(esi_expr.expand("$(HTTP_HOST|") == "");
    REQUIRE(esi_expr.expand("$(HTTP_HOST|foo") == "");
    REQUIRE(esi_expr.expand("$(HTTP_HOST|foo)") == "example.com");
    REQUIRE(esi_expr.expand("$(HTTP_XHOST|foo)") == "foo");
    REQUIRE(esi_expr.expand("$(|foo)") == "foo");
    REQUIRE(esi_expr.expand("$(HTTP_ACCEPT_LANGUAGE{en-uk})") == "");
    REQUIRE(esi_expr.expand("$(HTTP_ACCEPT_LANGUAGE{en-uk}|'yes')") == "yes");
    REQUIRE(esi_expr.expand("$(HTTP_ACCEPT_LANGUAGE{en-uk}|'yes with space')") == "yes with space");
    REQUIRE(esi_expr.expand("$(HTTP_ACCEPT_LANGUAGE{en-gb}|'yes')") == "true");
    REQUIRE(esi_expr.expand("$(HTTP_ACCEPT_LANGUAGE{en-gb}|'yes)") == "");
    REQUIRE(esi_expr.expand("$(HTTP_ACCEPT_LANGUAGE{en-uk}|'yes)") == "");

    REQUIRE(esi_expr.evaluate("$(HTTP_COOKIE{non-existent}) < 7") == false);
    REQUIRE(esi_expr.evaluate("$(HTTP_COOKIE{c1}) > $(HTTP_COOKIE{non-existent})") == false);
    REQUIRE(esi_expr.evaluate("$(HTTP_COOKIE{non-existent}) <= 7") == false);
    REQUIRE(esi_expr.evaluate("$(HTTP_COOKIE{c1}) >= $(HTTP_COOKIE{non-existent})") == false);

    // query string tests
    esi_vars.clear();
    REQUIRE(esi_vars.getValue("QUERY_STRING").size() == 0);
    esi_vars.populate("a");
    REQUIRE(esi_vars.getValue("QUERY_STRING") == "a");
    REQUIRE(esi_vars.getValue("QUERY_STRING{a}").size() == 0);

    esi_vars.clear();
    REQUIRE(esi_vars.getValue("QUERY_STRING").size() == 0);
    esi_vars.populate("");
    REQUIRE(esi_vars.getValue("QUERY_STRING") == "");
    REQUIRE(esi_vars.getValue("QUERY_STRING{a}").size() == 0);

    esi_vars.clear();
    REQUIRE(esi_vars.getValue("QUERY_STRING").size() == 0);
    esi_vars.populate("a=b");
    REQUIRE(esi_vars.getValue("QUERY_STRING") == "a=b");
    REQUIRE(esi_vars.getValue("QUERY_STRING{a}") == "b");

    esi_vars.clear();
    REQUIRE(esi_vars.getValue("QUERY_STRING").size() == 0);
    esi_vars.populate("a=b&");
    REQUIRE(esi_vars.getValue("QUERY_STRING") == "a=b&");
    REQUIRE(esi_vars.getValue("QUERY_STRING{a}") == "b");

    esi_vars.clear();
    REQUIRE(esi_vars.getValue("QUERY_STRING").size() == 0);
    esi_vars.populate("&a=b&");
    REQUIRE(esi_vars.getValue("QUERY_STRING") == "&a=b&");
    REQUIRE(esi_vars.getValue("QUERY_STRING{a}") == "b");

    esi_vars.clear();
    REQUIRE(esi_vars.getValue("QUERY_STRING").size() == 0);
    esi_vars.populate("name1=value1&name2=value2&name3=val%32ue");
    REQUIRE(esi_vars.getValue("QUERY_STRING") == "name1=value1&name2=value2&name3=val%32ue");
    REQUIRE(esi_vars.getValue("QUERY_STRING{name1}") == "value1");
    REQUIRE(esi_vars.getValue("QUERY_STRING{name2}") == "value2");
    REQUIRE(esi_vars.getValue("QUERY_STRING{name3}") == "val%32ue");
    REQUIRE(esi_vars.getValue("QUERY_STRING{name4}") == "");
    REQUIRE(esi_vars.getValue("QUERY_STRING{}") == "");
    REQUIRE(esi_vars.getValue("QUERY_STRING{foo}") == "");

    esi_vars.clear();
    REQUIRE(esi_vars.getValue("QUERY_STRING").size() == 0);
    esi_vars.populate("=");
    REQUIRE(esi_vars.getValue("QUERY_STRING") == "=");
    REQUIRE(esi_vars.getValue("QUERY_STRING{a}") == "");

    esi_vars.clear();
    REQUIRE(esi_vars.getValue("QUERY_STRING").size() == 0);
    esi_vars.populate("a=&");
    REQUIRE(esi_vars.getValue("QUERY_STRING") == "a=&");
    REQUIRE(esi_vars.getValue("QUERY_STRING{a}") == "");

    esi_vars.clear();
    REQUIRE(esi_vars.getValue("QUERY_STRING").size() == 0);
    esi_vars.populate("=b&");
    REQUIRE(esi_vars.getValue("QUERY_STRING") == "=b&");
    REQUIRE(esi_vars.getValue("QUERY_STRING{a}") == "");

    esi_vars.clear();
    REQUIRE(esi_vars.getValue("QUERY_STRING").size() == 0);
    esi_vars.populate("foo=bar&blah=&");
    REQUIRE(esi_vars.getValue("QUERY_STRING") == "foo=bar&blah=&");
    REQUIRE(esi_vars.getValue("QUERY_STRING{foo}") == "bar");
    REQUIRE(esi_vars.getValue("QUERY_STRING{blah}") == "");

    esi_vars.clear();
    REQUIRE(esi_vars.getValue("QUERY_STRING").size() == 0);
    esi_vars.populate("=blah&foo=bar");
    REQUIRE(esi_vars.getValue("QUERY_STRING") == "=blah&foo=bar");
    REQUIRE(esi_vars.getValue("QUERY_STRING{foo}") == "bar");
    REQUIRE(esi_vars.getValue("QUERY_STRING{blah}") == "");
  }

  SECTION("Test 2")
  {
    enableFakeDebugLog();
    int dummy;
    Variables esi_vars(&dummy, allowlistCookies);

    esi_vars.populate(HttpHeader("Host", -1, "example.com", -1));
    esi_vars.populate(HttpHeader("Referer", -1, "google.com", -1));
    const char *PARSING_DEBUG_MESSAGE = "Parsing headers";
    REQUIRE(gFakeDebugLog.find(PARSING_DEBUG_MESSAGE) >= gFakeDebugLog.size()); // shouldn't have parsed yet

    REQUIRE(esi_vars.getValue("HTTP_HOST") == "example.com");
    size_t str_pos = gFakeDebugLog.find(PARSING_DEBUG_MESSAGE);
    REQUIRE(str_pos < gFakeDebugLog.size()); // should've parsed now

    REQUIRE(esi_vars.getValue("HTTP_REFERER") == "google.com");
    REQUIRE(gFakeDebugLog.rfind(PARSING_DEBUG_MESSAGE) == str_pos); // shouldn't have parsed again

    esi_vars.populate(HttpHeader("Host", -1, "localhost", -1));
    REQUIRE(esi_vars.getValue("HTTP_HOST") == "localhost");
    REQUIRE(gFakeDebugLog.rfind(PARSING_DEBUG_MESSAGE) == str_pos); // should not have parsed all headers
    REQUIRE(esi_vars.getValue("HTTP_HOST") == "localhost");         // only this one
    REQUIRE(esi_vars.getValue("HTTP_REFERER") == "google.com");

    esi_vars.clear();
    esi_vars.populate(HttpHeader("Host", -1, "home", -1));
    REQUIRE(esi_vars.getValue("HTTP_HOST") == "home");
    REQUIRE(gFakeDebugLog.rfind(PARSING_DEBUG_MESSAGE) != str_pos); // should have parsed again
    REQUIRE(esi_vars.getValue("HTTP_REFERER") == "");
  }

  SECTION("Test 3")
  {
    allowlistCookies.push_back("age");
    allowlistCookies.push_back("grade");
    allowlistCookies.push_back("avg");
    allowlistCookies.push_back("t1");
    allowlistCookies.push_back("t2");
    allowlistCookies.push_back("t3");
    allowlistCookies.push_back("t4");
    allowlistCookies.push_back("t5");
    allowlistCookies.push_back("c1");
    int dummy;
    Variables esi_vars(&dummy, allowlistCookies);

    esi_vars.populate(HttpHeader("Host", -1, "example.com", -1));
    esi_vars.populate(HttpHeader("Referer", -1, "google.com", -1));
    esi_vars.populate(HttpHeader("Cookie", -1, "age=21; grade=-5; avg=4.3; t1=\" \"; t2=0.0", -1));
    esi_vars.populate(HttpHeader("Cookie", -1, "t3=-0; t4=0; t5=6", -1));

    Expression esi_expr(esi_vars);
    REQUIRE(esi_expr.evaluate("$(HTTP_COOKIE{age}) >= -9"));
    REQUIRE(esi_expr.evaluate("$(HTTP_COOKIE{age}) > 9"));
    REQUIRE(esi_expr.evaluate("$(HTTP_COOKIE{age}) < 22"));
    REQUIRE(esi_expr.evaluate("$(HTTP_COOKIE{age}) <= 22.1"));
    REQUIRE(esi_expr.evaluate("$(HTTP_COOKIE{age}) > 100a")); // non-numerical
    REQUIRE(esi_expr.evaluate("$(HTTP_COOKIE{t1})"));         // non-numerical
    REQUIRE(esi_expr.evaluate("$(HTTP_COOKIE{grade})"));
    REQUIRE(esi_expr.evaluate("$(HTTP_COOKIE{grade}) == -5"));
    REQUIRE(esi_expr.evaluate("$(HTTP_COOKIE{grade}) != -5.1"));
    REQUIRE(esi_expr.evaluate("!$(HTTP_COOKIE{t2})"));
    REQUIRE(esi_expr.evaluate("!$(HTTP_COOKIE{t3})"));
    REQUIRE(esi_expr.evaluate("!$(HTTP_COOKIE{t4})"));
    REQUIRE(esi_expr.evaluate("+4.3 == $(HTTP_COOKIE{avg})"));
    REQUIRE(esi_expr.evaluate("$(HTTP_COOKIE{grade}) < -0x2"));
    REQUIRE(esi_expr.evaluate("$(HTTP_COOKIE{t2}) | 1"));
    REQUIRE(!esi_expr.evaluate("$(HTTP_COOKIE{t3}) & 1"));
    REQUIRE(esi_expr.evaluate("$(HTTP_COOKIE{t5}) == 6"));

    string strange_cookie("c1=123");
    strange_cookie[4] = '\0';
    esi_vars.populate(HttpHeader("Cookie", -1, strange_cookie.data(), strange_cookie.size()));
    REQUIRE(esi_vars.getValue("HTTP_COOKIE{c1}").size() == 3);
    REQUIRE(esi_vars.getValue("HTTP_COOKIE{c1}")[1] == '\0');
    REQUIRE(esi_expr.evaluate("$(HTTP_COOKIE{c1}) != 1"));
  }

  SECTION("Test 4")
  {
    allowlistCookies.push_back("FPS");
    allowlistCookies.push_back("mb");
    allowlistCookies.push_back("Y");
    allowlistCookies.push_back("C");
    allowlistCookies.push_back("F");
    allowlistCookies.push_back("a");
    allowlistCookies.push_back("c");
    int dummy;
    Variables esi_vars(&dummy, allowlistCookies);
    string cookie_str("FPS=dl; mb=d=OPsv7rvU4FFaAOoIRi75BBuqdMdbMLFuDwQmk6nKrCgno7L4xuN44zm7QBQJRmQSh8ken6GSVk8-&v=1; C=mg=1; "
                      "Y=v=1&n=fmaptagvuff50&l=fc0d94i7/o&p=m2f0000313000400&r=8j&lg=en-US&intl=us; "
                      "F=a=4KvLV9IMvTJnIAqCk25y9Use6hnPALtUf3n78PihlcIqvmzoW.Ax8UyW8_oxtgFNrrdmooqZmPa7WsX4gE."
                      "6sI69wuNwRKrRPFT29h9lhwuxxLz0RuQedVXhJhc323Q-&b=8gQZ"); // TODO - might need to
    esi_vars.populate(HttpHeader("Cookie", -1, cookie_str.data(), cookie_str.size()));

    REQUIRE(esi_vars.getValue("HTTP_COOKIE{FPS}") == "dl");
    REQUIRE(esi_vars.getValue("HTTP_COOKIE{mb}") ==
            "d=OPsv7rvU4FFaAOoIRi75BBuqdMdbMLFuDwQmk6nKrCgno7L4xuN44zm7QBQJRmQSh8ken6GSVk8-&v=1");
    REQUIRE(esi_vars.getValue("HTTP_COOKIE{Y;n}") == "fmaptagvuff50");
    REQUIRE(esi_vars.getValue("HTTP_COOKIE{Y;l}") == "fc0d94i7/o");
    REQUIRE(esi_vars.getValue("HTTP_COOKIE{Y;intl}") == "us");
    REQUIRE(esi_vars.getValue("HTTP_COOKIE{C}") == "mg=1");
    REQUIRE(esi_vars.getValue("HTTP_COOKIE{non-existent}") == "");

    REQUIRE(esi_vars.getValue("HTTP_COOKIE{Y}") == "v=1&n=fmaptagvuff50&l=fc0d94i7/o&p=m2f0000313000400&r=8j&lg=en-US&intl=us");

    esi_vars.populate(HttpHeader("Host", -1, "www.example.com", -1));
    REQUIRE(esi_vars.getValue("HTTP_COOKIE{F}") ==
            "a=4KvLV9IMvTJnIAqCk25y9Use6hnPALtUf3n78PihlcIqvmzoW."
            "Ax8UyW8_oxtgFNrrdmooqZmPa7WsX4gE.6sI69wuNwRKrRPFT29h9lhwuxxLz0RuQedVXhJhc323Q-&b=8gQZ");
    REQUIRE(esi_vars.getValue("HTTP_HOST") == "www.example.com");

    esi_vars.populate(HttpHeader("Cookie", -1, "a=b; c=d", -1));
    REQUIRE(esi_vars.getValue("HTTP_COOKIE{Y;intl}") == "us");
    REQUIRE(esi_vars.getValue("HTTP_COOKIE{F}") ==
            "a=4KvLV9IMvTJnIAqCk25y9Use6hnPALtUf3n78PihlcIqvmzoW."
            "Ax8UyW8_oxtgFNrrdmooqZmPa7WsX4gE.6sI69wuNwRKrRPFT29h9lhwuxxLz0RuQedVXhJhc323Q-&b=8gQZ");
    REQUIRE(esi_vars.getValue("HTTP_COOKIE{a}") == "b");
    REQUIRE(esi_vars.getValue("HTTP_COOKIE{c}") == "d");
    REQUIRE(esi_vars.getValue("HTTP_HOST") == "www.example.com");
    REQUIRE(esi_vars.getValue("HTTP_COOKIE{Y;blah}") == "");

    esi_vars.clear();
    esi_vars.populate(HttpHeader("Cookie", -1, "Y=junk", -1));
    REQUIRE(esi_vars.getValue("HTTP_COOKIE{Y}") == "junk");
    REQUIRE(esi_vars.getValue("HTTP_COOKIE{Y;intl}") == "");
  }

  SECTION("Test 5")
  {
    int dummy;
    Variables esi_vars(&dummy, allowlistCookies);
    esi_vars.populate(HttpHeader("hdr1", -1, "hval1", -1));
    esi_vars.populate(HttpHeader("Hdr2", -1, "hval2", -1));
    esi_vars.populate(HttpHeader("@Intenal-hdr1", -1, "internal-hval1", -1));
    esi_vars.populate(HttpHeader("cookie", -1, "x=y", -1));

    REQUIRE(esi_vars.getValue("HTTP_HEADER{hdr1}") == "hval1");
    REQUIRE(esi_vars.getValue("HTTP_HEADER{hdr2}") == "");
    REQUIRE(esi_vars.getValue("HTTP_HEADER{Hdr2}") == "hval2");
    REQUIRE(esi_vars.getValue("HTTP_HEADER{non-existent}") == "");
    REQUIRE(esi_vars.getValue("HTTP_HEADER{@Intenal-hdr1}") == "internal-hval1");
    REQUIRE(esi_vars.getValue("HTTP_HEADER{cookie}") == "");
  }

  SECTION("Test 6")
  {
    allowlistCookies.push_back("*");
    int dummy;
    Variables esi_vars(&dummy, allowlistCookies);

    esi_vars.populate(HttpHeader("Host", -1, "example.com", -1));
    esi_vars.populate(HttpHeader("Cookie", -1, "age=21; grade=-5; avg=4.3; t1=\" \"; t2=0.0", -1));
    esi_vars.populate(HttpHeader("Cookie", -1, "t3=-0; t4=0; t5=6", -1));

    REQUIRE(esi_vars.getValue("HTTP_COOKIE{age}") == "21");
    REQUIRE(esi_vars.getValue("HTTP_COOKIE{grade}") == "-5");
    REQUIRE(esi_vars.getValue("HTTP_COOKIE{avg}") == "4.3");
    REQUIRE(esi_vars.getValue("HTTP_COOKIE{t1}") == " ");
    REQUIRE(esi_vars.getValue("HTTP_COOKIE{t2}") == "0.0");
    REQUIRE(esi_vars.getValue("HTTP_COOKIE{t3}") == "-0");
    REQUIRE(esi_vars.getValue("HTTP_COOKIE{t4}") == "0");
    REQUIRE(esi_vars.getValue("HTTP_COOKIE{t5}") == "6");
  }
}

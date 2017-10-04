/** @file
  Test file for basic_string_view class
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

#include "string_view.h"
#include <iostream>
#include <string>
#include <vector>

using namespace std;

constexpr auto npos = ts::string_view::npos;

// ======= test for string_view ========
// test cases:
//[constructor] [operator] [type] [access] [capacity] [modifier] [operation] [compare] [find]

TEST_CASE("constructor calls", "[string_view] [constructor]")
{
  SECTION("Literal look for NULL")
  {
    ts::string_view sv("hello");
    REQUIRE(sv.size() == 5);
    REQUIRE(sv.length() == 5);
    REQUIRE(sv.empty() == false);
    REQUIRE(sv == "hello");

    constexpr ts::string_view a{"evil dave"_sv};
    REQUIRE(a.size() == 9);
    REQUIRE(a.length() == 9);
    REQUIRE(a.empty() == false);
    REQUIRE(a == "evil dave");

    auto b = "grigor rulz"_sv;
    REQUIRE((std::is_same<decltype(b), ts::string_view>::value) == true);
    REQUIRE(b.size() == 11);
    REQUIRE(b.length() == 11);
    REQUIRE(b.empty() == false);
    REQUIRE(b == "grigor rulz");
  }

  SECTION("operator =")
  {
    ts::string_view sv;
    sv = "hello";
    REQUIRE(sv.size() == 5);
    REQUIRE(sv.length() == 5);
    REQUIRE(sv.empty() == false);
    REQUIRE(sv == "hello");
  }

  SECTION("Literal with NULL")
  {
    ts::string_view sv("hello\0world");
    REQUIRE(sv.size() == 5);
    REQUIRE(sv.length() == 5);
    REQUIRE(sv.empty() == false);
    REQUIRE(sv == "hello");
  }

  SECTION("Literal with NULL and size given")
  {
    ts::string_view sv("hello\0world", 11);
    REQUIRE(sv.size() == 11);
    REQUIRE(sv.length() == 11);
    REQUIRE(sv.empty() == false);
    REQUIRE(sv[6] == 'w');
    REQUIRE(sv == ts::string_view("hello\0world", 11));
  }

  SECTION("Literal length given")
  {
    ts::string_view sv("hello", 5);
    REQUIRE(sv.size() == 5);
    REQUIRE(sv.length() == 5);
    REQUIRE(sv.empty() == false);
    REQUIRE(sv == "hello");
  }

  SECTION("Literal length equal 0")
  {
    ts::string_view sv("hello", 0);
    REQUIRE(sv.size() == 0);
    REQUIRE(sv.length() == 0);
    REQUIRE(sv.empty() == true);
    REQUIRE(sv == "");
  }

  SECTION("constructor using std string")
  {
    string std_string = "hello";
    ts::string_view sv(std_string);

    REQUIRE(sv.size() == std_string.size());
    REQUIRE(sv.size() == std_string.size());
    REQUIRE(sv.empty() == false);
    REQUIRE(sv == "hello");
  }

  SECTION("= operator")
  {
    string std_string   = "hello";
    ts::string_view sv  = std_string;
    char str1[10]       = "hello";
    ts::string_view sv2 = str1;
    char const *str2    = "hello";
    ts::string_view sv3 = str2;

    REQUIRE(sv == "hello");
    REQUIRE(sv2 == "hello");
    REQUIRE(sv3 == "hello");
  }
}

TEST_CASE("operators", "[string_view] [operator]")
{
  SECTION("==")
  {
    ts::string_view sv("hello");

    char str1[10]    = "hello";
    char const *str2 = "hello";
    string str3      = "hello";

    REQUIRE(str2 == str3);
    REQUIRE(str1 == str3);

    REQUIRE(sv == "hello");
    REQUIRE(sv == str1);
    REQUIRE(sv == str2);
    REQUIRE(sv == str3);
  }
  SECTION("!=")
  {
    ts::string_view sv("hello");

    char str1[10]    = "hhhhhhhhh";
    char const *str2 = "hella";
    string str3      = "";

    REQUIRE(str2 != str3);
    REQUIRE(str1 != str3);

    REQUIRE(sv != str1);
    REQUIRE(sv != str2);
    REQUIRE(sv != str3);
  }
  SECTION(">")
  {
    ts::string_view sv("hello");

    char str1[10]    = "a";
    char const *str2 = "abcdefg";
    string str3      = "";

    REQUIRE(sv > str1);
    REQUIRE(sv > str2);
    REQUIRE(sv > str3);
  }
  SECTION("<")
  {
    ts::string_view sv("hello");

    char str1[10]    = "z";
    char const *str2 = "zaaaaaa";
    string str3      = "hellz";

    REQUIRE(sv < str1);
    REQUIRE(sv < str2);
    REQUIRE(sv < str3);
  }
  SECTION(">=")
  {
    ts::string_view sv("hello");

    char str1[10]    = "hello";
    char const *str2 = "abcdefg";
    string str3      = "";

    REQUIRE(sv >= str1);
    REQUIRE(sv >= str2);
    REQUIRE(sv >= str3);
  }
  SECTION("<=")
  {
    ts::string_view sv("hello");

    char str1[10]    = "hello";
    char const *str2 = "zaaaaaa";
    string str3      = "hellz";

    REQUIRE(sv <= str1);
    REQUIRE(sv <= str2);
    REQUIRE(sv <= str3);
  }
}

TEST_CASE("Pass in type checking", "[string_view] [type]")
{
  SECTION("char [] type")
  {
    char str[10] = "hello";
    ts::string_view sv(str);
    REQUIRE(sv == "hello");
    REQUIRE(sv.size() == 5);
    REQUIRE(sv.empty() == false);

    char str2[10] = {};
    ts::string_view sv2(str2);
    REQUIRE(sv2 == "");
    REQUIRE(sv2.empty() == true);
  }

  SECTION("char * type")
  {
    char const *str = "hello";
    ts::string_view sv(str);
    REQUIRE(sv == "hello");
    REQUIRE(sv.size() == 5);
    REQUIRE(sv.empty() == false);
  }

  SECTION("literal type")
  {
    ts::string_view sv("hello");
    REQUIRE(sv == "hello");
  }
}

TEST_CASE("Access & iterators", "[string_view] [access]")
{
  SECTION("iterators: begin, end, rbegin, rend")
  {
    ts::string_view sv("abcde");

    REQUIRE(*sv.begin() == 'a');
    REQUIRE(*sv.cbegin() == 'a');
    REQUIRE(*sv.end() == '\0');
    REQUIRE(*sv.cend() == '\0');
    REQUIRE(*sv.rbegin() == 'e');
    REQUIRE(*sv.crbegin() == 'e');
    REQUIRE(*sv.rend() == '\0');
    REQUIRE(*sv.crend() == '\0');

    int n = 0;
    for (auto it : sv) {
      REQUIRE(it == sv[n]);
      n++;
    }
  }

  SECTION("access: [], at, front, back, data")
  {
    ts::string_view sv("abcde");
    REQUIRE(sv[0] == 'a');
    REQUIRE(sv[4] == 'e');

    REQUIRE(sv.at(0) == 'a');
    REQUIRE(sv.at(4) == 'e');

    REQUIRE(sv.front() == 'a');
    REQUIRE(sv.back() == 'e');

    REQUIRE(sv.data()[1] == 'b');
  }

  SECTION("exception case")
  {
    ts::string_view sv("abcde");

    REQUIRE_THROWS_AS(sv.at(100), std::out_of_range);
    REQUIRE_THROWS_AS(sv.at(-1), std::out_of_range);

#if defined(_DEBUG)
    REQUIRE_THROWS_AS(sv[100], std::out_of_range);
    REQUIRE_THROWS_AS(sv[-1], std::out_of_range);
#else
    REQUIRE_NOTHROW(sv[100]);
    REQUIRE_NOTHROW(sv[-1]);
#endif
  }
}

TEST_CASE("Capacity", "[string_view] [capacity]")
{
  SECTION("empty string")
  {
    ts::string_view sv;
    REQUIRE(sv.size() == 0);
    REQUIRE(sv.length() == 0);
    REQUIRE(sv.empty() == true);
    REQUIRE(sv.max_size() == 0xfffffffffffffffe);
  }

  SECTION("literal string")
  {
    ts::string_view sv("abcde");
    REQUIRE(sv.size() == 5);
    REQUIRE(sv.length() == 5);
    REQUIRE(sv.empty() == false);
    REQUIRE(sv.max_size() == 0xfffffffffffffffe);
  }
}

TEST_CASE("Modifier", "[string_view] [modifier]")
{
  SECTION("remove prefix")
  {
    ts::string_view sv("abcde");

    sv.remove_prefix(0);
    REQUIRE(sv == "abcde");

    sv.remove_prefix(3);
    REQUIRE(sv == "de");

    sv.remove_prefix(100);
    REQUIRE(sv == "");
  }

  SECTION("remove suffix")
  {
    ts::string_view sv("abcde");

    sv.remove_suffix(0);
    REQUIRE(sv == "abcde");

    sv.remove_suffix(3);
    REQUIRE(sv == "ab");

    sv.remove_suffix(100);
    REQUIRE(sv == "");
  }

  SECTION("swap")
  {
    ts::string_view sv1("hello");
    ts::string_view sv2("world");

    sv1.swap(sv2);

    REQUIRE(sv1 == "world");
    REQUIRE(sv2 == "hello");
  }
}

TEST_CASE("Operations", "[string_view] [operation]")
{
  SECTION("copy")
  {
    // weird copy

    // char str[10];
    // ts::string_view sv("hello");
    // sv.copy(str, 6, 0);
    // REQUIRE(str == "hello");
  }
  SECTION("substr")
  {
    ts::string_view sv("hello");
    REQUIRE(sv.substr(0, 3) == "hel");
    REQUIRE(sv.substr(1, 3) == "ell");
    REQUIRE(sv.substr(0, 100) == "hello");
  }
  SECTION("exception case")
  {
    ts::string_view sv("hello");
    REQUIRE_THROWS_AS(sv.substr(100, 0), std::out_of_range);
    REQUIRE_THROWS_AS(sv.substr(-1, -1), std::out_of_range);
  }
}

TEST_CASE("Compare", "[string_view] [compare]")
{
  SECTION("compare pass in char")
  {
    ts::string_view sv("hello");
    REQUIRE(sv.compare("hello") == 0);
    REQUIRE(sv.compare("hella") > 0);
    REQUIRE(sv.compare("hellz") < 0);
    REQUIRE(sv.compare("aaaaaaa") > 0);
    REQUIRE(sv.compare("zzzzzzz") < 0);
    REQUIRE(sv.compare("") > 0);

    ts::string_view sv2("hello");
    REQUIRE(sv2.compare(0, 3, "hel") == 0);
    REQUIRE(sv2.compare(1, 3, "ello", 0, 3) == 0);

    ts::string_view sv3("");
    REQUIRE(sv3.compare("hello") < 0);
  }

  SECTION("compare pass in string view")
  {
    ts::string_view sv("hello");
    ts::string_view sv_test1("hello");
    ts::string_view sv_test2("aello");
    ts::string_view sv_test3("zello");
    REQUIRE(sv.compare(sv_test1) == 0);
    REQUIRE(sv.compare(sv_test2) > 0);
    REQUIRE(sv.compare(sv_test3) < 0);

    ts::string_view sv2("hello");
    ts::string_view sv_test4("hel");
    ts::string_view sv_test5("ello");
    REQUIRE(sv.compare(0, 3, sv_test4) == 0);
    REQUIRE(sv.compare(1, 3, sv_test5, 0, 3) == 0);
  }

  SECTION("exception case")
  {
    ts::string_view sv("hello");
    REQUIRE_THROWS_AS(sv.compare(100, 1, "hel"), std::out_of_range);
    REQUIRE_THROWS_AS(sv.compare(100, 100, "hel"), std::out_of_range);
    REQUIRE_THROWS_AS(sv.compare(-1, -1, "hel"), std::out_of_range);
  }
}

TEST_CASE("Find", "[string_view] [find]")
{
  SECTION("find")
  {
    ts::string_view sv("abcdabcd");
    ts::string_view svtest("bcd");

    REQUIRE(sv.find("abcdabcd", 100, 10) == npos);

    REQUIRE(sv.find('a') == 0);
    REQUIRE(sv.find(svtest) == 1);
    REQUIRE(sv.find(svtest, 2) == 5);

    REQUIRE(sv.find("bcd") == 1);
    REQUIRE(sv.find("bcd", 6) == npos);

    REQUIRE(sv.find("bcdx", 0, 3) == 1);
    REQUIRE(sv.find("bcdx", 0, 4) == npos);

    ts::string_view sv2;
    REQUIRE(sv2.find('a') == npos);
  }

  SECTION("rfind")
  {
    ts::string_view sv("abcdabcd");
    ts::string_view svtest("bcd");
    REQUIRE(sv.find('a') == 0);
    REQUIRE(sv.rfind(svtest) == 5);

    REQUIRE(sv.rfind("bcd") == 5);
    REQUIRE(sv.rfind("bcd", 3) == 1);
    REQUIRE(sv.rfind("bcd", 0) == npos);

    REQUIRE(sv.rfind("bcdx", 3, 3) == 1);
    REQUIRE(sv.rfind("bcdx", 3, 4) == npos);
  }

  SECTION("find_first_of")
  {
    ts::string_view sv("abcdefgabcdefg");
    ts::string_view svtest("hijklma");

    REQUIRE(sv.find_first_of('c') == 2);

    REQUIRE(sv.find_first_of(svtest) == 0);
    REQUIRE(sv.find_first_of("hijklmb") == 1);
    REQUIRE(sv.find_first_of("hijklmn") == npos);
    REQUIRE(sv.find_first_of("hijkla", 1) == 7);

    REQUIRE(sv.find_first_of("hijkla", 1, 0) == npos);
    REQUIRE(sv.find_first_of("hijkla", 1, 5) == npos);
    REQUIRE(sv.find_first_of("hijkla", 1, 6) == 7);
  }

  SECTION("find_last_of")
  {
    ts::string_view sv("abcdefgabcdefg");
    ts::string_view svtest("hijklma");

    REQUIRE(sv.find_last_of('c') == 9);

    REQUIRE(sv.find_last_of(svtest) == 7);
    REQUIRE(sv.find_last_of("hijklmb") == 8);
    REQUIRE(sv.find_last_of("hijklmn") == npos);

    REQUIRE(sv.find_last_of("hijkla", 1, 0) == npos);
    REQUIRE(sv.find_last_of("hijkla", 1, 5) == npos);
    REQUIRE(sv.find_last_of("hijkla", 1, 6) == 0);
  }
  SECTION("find_first_not_of")
  {
    ts::string_view sv("abcdefg");
    ts::string_view svtest("abcdxyz");

    REQUIRE(sv.find_first_not_of('x') == 0);

    REQUIRE(sv.find_first_not_of(svtest) == 4);
    REQUIRE(sv.find_first_not_of("abcdxyz") == 4);
    REQUIRE(sv.find_first_not_of("abcdefg") == npos);

    REQUIRE(sv.find_first_not_of("abcdxyz", 1, 0) == 1);
    REQUIRE(sv.find_first_not_of("abcdxyz", 1, 5) == 4);
    REQUIRE(sv.find_first_not_of("aaaaaaaa", 1, 5) == 1);
  }

  SECTION("find_last_not_of")
  {
    ts::string_view sv("abcdefg");
    ts::string_view svtest("abcdxyz");

    REQUIRE(sv.find_last_not_of('x') == 6);

    REQUIRE(sv.find_last_not_of(svtest) == 6);
    REQUIRE(sv.find_last_not_of("abcdxyz") == 6);
    REQUIRE(sv.find_last_not_of("abcdefg") == npos);

    REQUIRE(sv.find_last_not_of("abcdxyz", 1, 0) == 1);
    REQUIRE(sv.find_last_not_of("abcdxyz", 1, 5) == npos);
    REQUIRE(sv.find_last_not_of("aaaaaaaa", 1, 5) == 1);
  }
}

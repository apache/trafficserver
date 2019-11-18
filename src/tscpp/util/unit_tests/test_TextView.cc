/** @file

    TextView unit tests.

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

#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#include "tscpp/util/TextView.h"
#include "catch.hpp"

using ts::TextView;
using namespace std::literals;

TEST_CASE("TextView Constructor", "[libts][TextView]")
{
  static std::string base = "Evil Dave Rulez!";
  TextView tv(base);
  TextView b{base.data(), base.size()};
  TextView c{std::string_view(base)};
}

TEST_CASE("TextView Operations", "[libts][TextView]")
{
  TextView tv{"Evil Dave Rulez"};
  TextView tv_lower{"evil dave rulez"};
  TextView nothing;
  size_t off;

  REQUIRE(tv.find('l') == 3);
  off = tv.find_if([](char c) { return c == 'D'; });
  REQUIRE(off == tv.find('D'));

  REQUIRE(tv);
  REQUIRE(!tv == false);
  if (nothing) {
    REQUIRE(nullptr == "bad operator bool on TextView");
  }
  REQUIRE(!nothing == true);
  REQUIRE(nothing.empty() == true);

  REQUIRE(memcmp(tv, tv) == 0);
  REQUIRE(memcmp(tv, tv_lower) != 0);
  REQUIRE(strcmp(tv, tv) == 0);
  REQUIRE(strcmp(tv, tv_lower) != 0);
  REQUIRE(strcasecmp(tv, tv) == 0);
  REQUIRE(strcasecmp(tv, tv_lower) == 0);
  REQUIRE(strcasecmp(nothing, tv) != 0);
}

TEST_CASE("TextView Trimming", "[libts][TextView]")
{
  TextView tv("  Evil Dave Rulz   ...");
  TextView tv2{"More Text1234567890"};
  REQUIRE("Evil Dave Rulz   ..." == TextView(tv).ltrim_if(&isspace));
  REQUIRE(tv2 == TextView{tv2}.ltrim_if(&isspace));
  REQUIRE("More Text" == TextView{tv2}.rtrim_if(&isdigit));
  REQUIRE("  Evil Dave Rulz   " == TextView(tv).rtrim('.'));
  REQUIRE("Evil Dave Rulz" == TextView(tv).trim(" ."));
}

TEST_CASE("TextView Find", "[libts][TextView]")
{
  TextView addr{"172.29.145.87:5050"};
  REQUIRE(addr.find(':') == 13);
  REQUIRE(addr.rfind(':') == 13);
  REQUIRE(addr.find('.') == 3);
  REQUIRE(addr.rfind('.') == 10);
}

TEST_CASE("TextView Affixes", "[libts][TextView]")
{
  TextView s; // scratch.
  TextView tv1("0123456789;01234567890");
  TextView prefix{tv1.prefix(10)};

  REQUIRE("0123456789" == prefix);
  REQUIRE("67890" == tv1.suffix(5));

  TextView tv2 = tv1.prefix(';');
  REQUIRE(tv2 == "0123456789");

  TextView right{tv1};
  TextView left{right.split_prefix_at(';')};
  REQUIRE(right.size() == 11);
  REQUIRE(left.size() == 10);

  TextView tv3 = "abcdefg:gfedcba";
  left         = tv3;
  right        = left.split_suffix_at(";:,");
  TextView pre{tv3}, post{pre.split_suffix_at(7)};

  REQUIRE(post.size() == 7);
  REQUIRE(right.size() == 7);
  REQUIRE(left.size() == 7);
  REQUIRE(left == "abcdefg");
  REQUIRE(right == "gfedcba");

  TextView addr1{"[fe80::fc54:ff:fe60:d886]"};
  TextView addr2{"[fe80::fc54:ff:fe60:d886]:956"};
  TextView addr3{"192.168.1.1:5050"};

  TextView t = addr1;
  ++t;
  REQUIRE("fe80::fc54:ff:fe60:d886]" == t);
  TextView a = t.take_prefix_at(']');
  REQUIRE("fe80::fc54:ff:fe60:d886" == a);
  REQUIRE(t.empty());

  t = addr2;
  ++t;
  a = t.take_prefix_at(']');
  REQUIRE("fe80::fc54:ff:fe60:d886" == a);
  REQUIRE(':' == *t);
  ++t;
  REQUIRE("956" == t);

  t = addr3;
  TextView sf{t.suffix(':')};
  REQUIRE("5050" == sf);
  REQUIRE(t == addr3);

  t = addr3;
  s = t.split_suffix_at(11);
  REQUIRE("5050" == s);
  REQUIRE("192.168.1.1" == t);

  t = addr3;
  s = t.split_suffix_at(':');
  REQUIRE("5050" == s);
  REQUIRE("192.168.1.1" == t);

  t = addr3;
  s = t.split_suffix_at('Q');
  REQUIRE(s.empty());
  REQUIRE(t == addr3);

  t = addr3;
  s = t.take_suffix_at(':');
  REQUIRE("5050" == s);
  REQUIRE("192.168.1.1" == t);

  t = addr3;
  s = t.take_suffix_at('Q');
  REQUIRE(s == addr3);
  REQUIRE(t.empty());

  auto is_sep{[](char c) { return isspace(c) || ',' == c || ';' == c; }};
  TextView token;
  t = ";; , ;;one;two,th:ree  four,, ; ,,f-ive="sv;
  // Do an unrolled loop.
  REQUIRE(!t.ltrim_if(is_sep).empty());
  REQUIRE(t.take_prefix_if(is_sep) == "one");
  REQUIRE(!t.ltrim_if(is_sep).empty());
  REQUIRE(t.take_prefix_if(is_sep) == "two");
  REQUIRE(!t.ltrim_if(is_sep).empty());
  REQUIRE(t.take_prefix_if(is_sep) == "th:ree");
  REQUIRE(!t.ltrim_if(is_sep).empty());
  REQUIRE(t.take_prefix_if(is_sep) == "four");
  REQUIRE(!t.ltrim_if(is_sep).empty());
  REQUIRE(t.take_prefix_if(is_sep) == "f-ive=");
  REQUIRE(t.empty());

  // Simulate pulling off FQDN pieces in reverse order from a string_view.
  // Simulates operations in HostLookup.cc, where the use of string_view
  // necessitates this workaround of failures in the string_view API. With a
  // TextView, it would just be repeated @c take_suffix_at('.')
  std::string_view fqdn{"bob.ne1.corp.ngeo.com"};
  TextView elt{TextView{fqdn}.suffix('.')};
  REQUIRE(elt == "com");

  // Unroll loop for testing.
  fqdn.remove_suffix(std::min(fqdn.size(), elt.size() + 1));
  elt = TextView{fqdn}.suffix('.');
  REQUIRE(elt == "ngeo");
  fqdn.remove_suffix(std::min(fqdn.size(), elt.size() + 1));
  elt = TextView{fqdn}.suffix('.');
  REQUIRE(elt == "corp");
  fqdn.remove_suffix(std::min(fqdn.size(), elt.size() + 1));
  elt = TextView{fqdn}.suffix('.');
  REQUIRE(elt == "ne1");
  fqdn.remove_suffix(std::min(fqdn.size(), elt.size() + 1));
  elt = TextView{fqdn}.suffix('.');
  REQUIRE(elt == "bob");
  fqdn.remove_suffix(std::min(fqdn.size(), elt.size() + 1));
  elt = TextView{fqdn}.suffix('.');
  REQUIRE(elt.empty());

  // Check some edge cases.
  fqdn  = "."sv;
  token = TextView{fqdn}.take_suffix_at('.');
  REQUIRE(token.size() == 0);
  REQUIRE(token.empty());

  s = "."sv;
  REQUIRE(s.size() == 1);
  REQUIRE(s.rtrim('.').empty());
  token = s.take_suffix_at('.');
  REQUIRE(token.size() == 0);
  REQUIRE(token.empty());

  s = "."sv;
  REQUIRE(s.size() == 1);
  REQUIRE(s.ltrim('.').empty());
  token = s.take_prefix_at('.');
  REQUIRE(token.size() == 0);
  REQUIRE(token.empty());

  auto is_not_alnum = [](char c) { return !isalnum(c); };

  s = "file.cc";
  REQUIRE(s.suffix('.') == "cc");
  REQUIRE(s.suffix_if(is_not_alnum) == "cc");
  REQUIRE(s.prefix('.') == "file");
  REQUIRE(s.prefix_if(is_not_alnum) == "file");
  s.remove_suffix_at('.');
  REQUIRE(s == "file");
  s = "file.cc.org.123";
  REQUIRE(s.suffix('.') == "123");
  REQUIRE(s.prefix('.') == "file");
  s.remove_suffix_if(is_not_alnum);
  REQUIRE(s == "file.cc.org");
  s.remove_suffix_at('.');
  REQUIRE(s == "file.cc");
  s.remove_prefix_at('.');
  REQUIRE(s == "cc");
  s = "file.cc.org.123";
  s.remove_prefix_if(is_not_alnum);
  REQUIRE(s == "cc.org.123");
  s.remove_suffix_at('!');
  REQUIRE(s.empty());
  s = "file.cc.org";
  s.remove_prefix('!');
  REQUIRE(s.empty());
};

TEST_CASE("TextView Formatting", "[libts][TextView]")
{
  TextView a("01234567");
  {
    std::ostringstream buff;
    buff << '|' << a << '|';
    REQUIRE(buff.str() == "|01234567|");
  }
  {
    std::ostringstream buff;
    buff << '|' << std::setw(5) << a << '|';
    REQUIRE(buff.str() == "|01234567|");
  }
  {
    std::ostringstream buff;
    buff << '|' << std::setw(12) << a << '|';
    REQUIRE(buff.str() == "|    01234567|");
  }
  {
    std::ostringstream buff;
    buff << '|' << std::setw(12) << std::right << a << '|';
    REQUIRE(buff.str() == "|    01234567|");
  }
  {
    std::ostringstream buff;
    buff << '|' << std::setw(12) << std::left << a << '|';
    REQUIRE(buff.str() == "|01234567    |");
  }
  {
    std::ostringstream buff;
    buff << '|' << std::setw(12) << std::right << std::setfill('_') << a << '|';
    REQUIRE(buff.str() == "|____01234567|");
  }
  {
    std::ostringstream buff;
    buff << '|' << std::setw(12) << std::left << std::setfill('_') << a << '|';
    REQUIRE(buff.str() == "|01234567____|");
  }
}

TEST_CASE("TextView Conversions", "[libts][TextView]")
{
  TextView n  = "   956783";
  TextView n2 = n;
  TextView n3 = "031";
  TextView n4 = "13f8q";
  TextView n5 = "0x13f8";
  TextView n6 = "0X13f8";
  TextView x;
  n2.ltrim_if(&isspace);

  REQUIRE(956783 == svtoi(n));
  REQUIRE(956783 == svtoi(n2));
  REQUIRE(0x13f8 == svtoi(n4, &x, 16));
  REQUIRE(x == "13f8");
  REQUIRE(0x13f8 == svtoi(n5));
  REQUIRE(0x13f8 == svtoi(n6));

  REQUIRE(25 == svtoi(n3));
  REQUIRE(31 == svtoi(n3, nullptr, 10));
}

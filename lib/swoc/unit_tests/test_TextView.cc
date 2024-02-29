// SPDX-License-Identifier: Apache-2.0
/** @file

    TextView unit tests.
*/

#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <map>
#include <unordered_map>

#include "swoc/TextView.h"
#include "catch.hpp"

using swoc::TextView;
using namespace std::literals;
using namespace swoc::literals;

TEST_CASE("TextView Constructor", "[libswoc][TextView]") {
  static const std::string base = "Evil Dave Rulez!";
  unsigned ux                   = base.size();
  TextView tv(base);
  TextView a{"Evil Dave Rulez"};
  TextView b{base.data(), base.size()};
  TextView c{std::string_view(base)};
  constexpr TextView d{"Grigor!"sv};
  TextView e{base.data(), 15};
  TextView f(base.data(), 15);
  TextView u{base.data(), ux};
  TextView g{base.data(), base.data() + base.size()}; // begin/end pointers.

  // Check the various forms of char pointers work unambiguously.
  TextView bob{"Bob"};
  std::string dave("dave");
  REQUIRE(bob == "Bob"_tv); // Attempt to verify @a bob isn't pointing at a temporary.
  char q[12] = "Bob";
  TextView t_q{q};
  REQUIRE(t_q.data() == q); // must point at @a q.
  char *qp = q;
  TextView t_qp{qp};
  REQUIRE(t_qp.data() == qp); // verify pointer is not pointing at a temporary.
  char const *qcp = "Bob";
  TextView t_qcp{qcp};
  REQUIRE(t_qcp.data() == qcp);

  tv = "Delain"; // assign literal.
  REQUIRE(tv.size() == 6);
  tv = q;                              // Assign array.
  REQUIRE(tv.size() == sizeof(q) - 1); // trailing nul char dropped.
  tv = qp;                             // Assign pointer.
  REQUIRE(tv.data() == qp);
  tv = qcp; // Assign pointer to const.
  REQUIRE(tv.data() == qcp);
  tv = std::string_view(base);
  REQUIRE(tv.size() == base.size());

  qp = nullptr;
  REQUIRE(TextView(qp).size() == 0);
  qcp = nullptr;
  REQUIRE(TextView(qcp).size() == 0);
};

TEST_CASE("TextView Operations", "[libswoc][TextView]") {
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

  // Check generic construction from a "string like" class.
  struct Stringy {
    char const *
    data() const {
      return _data;
    }
    size_t
    size() const {
      return _size;
    }

    char const *_data = nullptr;
    size_t _size;
  };

  char const *stringy_text = "Evil Dave Rulez";
  Stringy stringy{stringy_text, strlen(stringy_text)};

  // Can construct directly.
  TextView from_stringy{stringy};
  REQUIRE(0 == strcmp(from_stringy, stringy_text));

  // Can assign directly.
  TextView assign_stringy;
  REQUIRE(assign_stringy.empty() == true);
  assign_stringy.assign(stringy);
  REQUIRE(0 == strcmp(assign_stringy, stringy_text));

  // Pass as argument to TextView parameter.
  auto stringy_f = [&](TextView txt) -> bool { return 0 == strcmp(txt, stringy_text); };
  REQUIRE(true == stringy_f(stringy));
  REQUIRE(false == stringy_f(tv_lower));
}

TEST_CASE("TextView Trimming", "[libswoc][TextView]") {
  TextView tv("  Evil Dave Rulz   ...");
  TextView tv2{"More Text1234567890"};
  REQUIRE("Evil Dave Rulz   ..." == TextView(tv).ltrim_if(&isspace));
  REQUIRE(tv2 == TextView{tv2}.ltrim_if(&isspace));
  REQUIRE("More Text" == TextView{tv2}.rtrim_if(&isdigit));
  REQUIRE("  Evil Dave Rulz   " == TextView(tv).rtrim('.'));
  REQUIRE("Evil Dave Rulz" == TextView(tv).trim(" ."));

  tv.assign("\r\n");
  tv.rtrim_if([](char c) -> bool { return c == '\r' || c == '\n'; });
  REQUIRE(tv.size() == 0);

  tv.assign("...");
  tv.rtrim('.');
  REQUIRE(tv.size() == 0);

  tv.assign(".,,.;.");
  tv.rtrim(";,."_tv);
  REQUIRE(tv.size() == 0);
}

TEST_CASE("TextView Find", "[libswoc][TextView]") {
  TextView addr{"172.29.145.87:5050"};
  REQUIRE(addr.find(':') == 13);
  REQUIRE(addr.rfind(':') == 13);
  REQUIRE(addr.find('.') == 3);
  REQUIRE(addr.rfind('.') == 10);
}

TEST_CASE("TextView Affixes", "[libswoc][TextView]") {
  TextView s; // scratch.
  TextView tv1("0123456789;01234567890");
  TextView prefix{tv1.prefix(10)};

  REQUIRE("0123456789" == prefix);
  REQUIRE("90" == tv1.suffix(2));
  REQUIRE("67890" == tv1.suffix(5));
  REQUIRE("4567890" == tv1.suffix(7));
  REQUIRE(tv1 == tv1.prefix(9999));
  REQUIRE(tv1 == tv1.suffix(9999));

  TextView tv2 = tv1.prefix_at(';');
  REQUIRE(tv2 == "0123456789");
  REQUIRE(tv1.prefix_at('z').empty());
  REQUIRE(tv1.suffix_at('z').empty());

  s = tv1;
  REQUIRE(s.remove_prefix(10) == ";01234567890");
  s = tv1;
  REQUIRE(s.remove_prefix(9999).empty());
  s = tv1;
  REQUIRE(s.remove_suffix(11) == "0123456789;");
  s = tv1;
  s.remove_suffix(9999);
  REQUIRE(s.empty());
  REQUIRE(s.data() == tv1.data());

  TextView right{tv1};
  TextView left{right.split_prefix_at(';')};
  REQUIRE(right.size() == 11);
  REQUIRE(left.size() == 10);

  TextView tv3 = "abcdefg:gfedcba";
  left         = tv3;
  right        = left.split_suffix_at(";:,");
  TextView pre{tv3}, post{pre.split_suffix(7)};
  REQUIRE(right.size() == 7);
  REQUIRE(left.size() == 7);
  REQUIRE(left == "abcdefg");
  REQUIRE(right == "gfedcba");

  TextView addr1{"[fe80::fc54:ff:fe60:d886]"};
  TextView addr2{"[fe80::fc54:ff:fe60:d886]:956"};
  TextView addr3{"192.168.1.1:5050"};
  TextView host{"evil.dave.rulz"};

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
  TextView sf{t.suffix_at(':')};
  REQUIRE("5050" == sf);
  REQUIRE(t == addr3);

  t = addr3;
  s = t.split_suffix(4);
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

  REQUIRE(host.suffix_at('.') == "rulz");
  REQUIRE(true == host.suffix_at(':').empty());

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
  // necessitates this workaround of failures in the string_view API.
  std::string_view fqdn{"bob.ne1.corp.ngeo.com"};
  TextView elt{TextView{fqdn}.take_suffix_at('.')};
  REQUIRE(elt == "com");
  fqdn.remove_suffix(std::min(fqdn.size(), elt.size() + 1));

  // Unroll loop for testing.
  elt = TextView{fqdn}.take_suffix_at('.');
  REQUIRE(elt == "ngeo");
  fqdn.remove_suffix(std::min(fqdn.size(), elt.size() + 1));
  elt = TextView{fqdn}.take_suffix_at('.');
  REQUIRE(elt == "corp");
  fqdn.remove_suffix(std::min(fqdn.size(), elt.size() + 1));
  elt = TextView{fqdn}.take_suffix_at('.');
  REQUIRE(elt == "ne1");
  fqdn.remove_suffix(std::min(fqdn.size(), elt.size() + 1));
  elt = TextView{fqdn}.take_suffix_at('.');
  REQUIRE(elt == "bob");
  fqdn.remove_suffix(std::min(fqdn.size(), elt.size() + 1));
  elt = TextView{fqdn}.take_suffix_at('.');
  REQUIRE(elt.empty());

  // Do it again, TextView stle.
  t = "bob.ne1.corp.ngeo.com";
  REQUIRE(t.rtrim('.').take_suffix_at('.') == "com"_tv);
  REQUIRE(t.rtrim('.').take_suffix_at('.') == "ngeo"_tv);
  REQUIRE(t.rtrim('.').take_suffix_at('.') == "corp"_tv);
  REQUIRE(t.take_suffix_at('.') == "ne1"_tv);
  REQUIRE(t.take_suffix_at('.') == "bob"_tv);
  REQUIRE(t.size() == 0);

  t = "bob.ne1.corp.ngeo.com";
  REQUIRE(t.remove_suffix_at('.') == "bob.ne1.corp.ngeo"_tv);
  REQUIRE(t.remove_suffix_at('.') == "bob.ne1.corp"_tv);
  REQUIRE(t.remove_suffix_at('.') == "bob.ne1"_tv);
  REQUIRE(t.remove_suffix_at('.') == "bob"_tv);
  REQUIRE(t.remove_suffix_at('.').size() == 0);

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

  s = ".."sv;
  REQUIRE(s.size() == 2);
  token = s.take_suffix_at('.');
  REQUIRE(token.size() == 0);
  REQUIRE(token.empty());
  REQUIRE(s.size() == 1);

  // Check for subtle differences with trailing separator
  token     = "one.ex";
  auto name = token.take_prefix_at('.');
  REQUIRE(name.size() > 0);
  REQUIRE(token.size() > 0);

  token = "one";
  name  = token.take_prefix_at('.');
  REQUIRE(name.size() > 0);
  REQUIRE(token.size() == 0);
  REQUIRE(token.data() == name.end());

  token = "one.";
  name  = token.take_prefix_at('.');
  REQUIRE(name.size() > 0);
  REQUIRE(token.size() == 0);
  REQUIRE(token.data() == name.end() + 1);

  auto is_not_alnum = [](char c) { return !isalnum(c); };

  s = "file.cc";
  REQUIRE(s.suffix_at('.') == "cc");
  REQUIRE(s.suffix_if(is_not_alnum) == "cc");
  REQUIRE(s.prefix_at('.') == "file");
  REQUIRE(s.prefix_if(is_not_alnum) == "file");
  s.remove_suffix_at('.');
  REQUIRE(s == "file");
  s = "file.cc.org.123";
  REQUIRE(s.suffix_at('.') == "123");
  REQUIRE(s.prefix_at('.') == "file");
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
  s.remove_prefix_at('!');
  REQUIRE(s == "file.cc.org");

  static constexpr TextView ctv{"http://delain.nl/albums/Lucidity.html"};
  static constexpr TextView ctv_scheme{ctv.prefix(4)};
  static constexpr TextView ctv_stem{ctv.suffix(4)};
  static constexpr TextView ctv_host{ctv.substr(7, 9)};
  REQUIRE(ctv.starts_with("http"_tv) == true);
  REQUIRE(ctv.ends_with(".html") == true);
  REQUIRE(ctv.starts_with("https"_tv) == false);
  REQUIRE(ctv.ends_with(".jpg") == false);
  REQUIRE(ctv.starts_with_nocase("HttP"_tv) == true);
  REQUIRE(ctv.starts_with_nocase("HttP") == true);
  REQUIRE(ctv.starts_with("HttP") == false);
  REQUIRE(ctv.starts_with("http") == true);
  REQUIRE(ctv.starts_with('h') == true);
  REQUIRE(ctv.starts_with('H') == false);
  REQUIRE(ctv.starts_with_nocase('H') == true);
  REQUIRE(ctv.starts_with('q') == false);
  REQUIRE(ctv.starts_with_nocase('Q') == false);
  REQUIRE(ctv.ends_with("htML"_tv) == false);
  REQUIRE(ctv.ends_with_nocase("htML"_tv) == true);
  REQUIRE(ctv.ends_with("htML") == false);
  REQUIRE(ctv.ends_with_nocase("htML") == true);

  REQUIRE(ctv_scheme == "http"_tv);
  REQUIRE(ctv_stem == "html"_tv);
  REQUIRE(ctv_host == "delain.nl"_tv);

  // Checking that constexpr works for this constructor as long as npos isn't used.
  static constexpr TextView ctv2{"http://delain.nl/albums/Interlude.html", 38};
  TextView ctv4{"http://delain.nl/albums/Interlude.html", 38};
  // This doesn't compile because it causes strlen to be called which isn't constexpr compatible.
  // static constexpr TextView ctv3 {"http://delain.nl/albums/Interlude.html", TextView::npos};
  // This works because it's not constexpr.
  TextView ctv3{"http://delain.nl/albums/Interlude.html", TextView::npos};
  REQUIRE(ctv2 == ctv3);
};

TEST_CASE("TextView Formatting", "[libswoc][TextView]") {
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

TEST_CASE("TextView Conversions", "[libswoc][TextView]") {
  TextView n  = "   956783";
  TextView n2 = n;
  TextView n3 = "031";
  TextView n4 = "13f8q";
  TextView n5 = "0x13f8";
  TextView n6 = "0X13f8";
  TextView n7 = "-2345679";
  TextView n8 = "+2345679";
  TextView x;
  n2.ltrim_if(&isspace);

  REQUIRE(956783 == svtoi(n));
  REQUIRE(956783 == svtoi(n2));
  REQUIRE(956783 == svtoi(n2, &x));
  REQUIRE(x.data() == n2.data());
  REQUIRE(x.size() == n2.size());
  REQUIRE(0x13f8 == svtoi(n4, &x, 16));
  REQUIRE(x == "13f8");
  REQUIRE(0x13f8 == svtoi(n5));
  REQUIRE(0x13f8 == svtoi(n6));

  REQUIRE(25 == svtoi(n3));
  REQUIRE(31 == svtoi(n3, nullptr, 10));

  REQUIRE(-2345679 == svtoi(n7));
  REQUIRE(-2345679 == svtoi(n7, &x));
  REQUIRE(x == n7);
  REQUIRE(2345679 == svtoi(n8));
  REQUIRE(2345679 == svtoi(n8, &x));
  REQUIRE(x == n8);
  REQUIRE(0b10111 == svtoi("0b10111"_tv));

  x = n4;
  REQUIRE(13 == swoc::svto_radix<10>(x));
  REQUIRE(x.size() + 2 == n4.size());
  x = n4;
  REQUIRE(0x13f8 == swoc::svto_radix<16>(x));
  REQUIRE(x.size() + 4 == n4.size());
  x = n4;
  REQUIRE(7 == swoc::svto_radix<4>(x));
  REQUIRE(x.size() + 2 == n4.size());
  x = n3;
  REQUIRE(31 == swoc::svto_radix<10>(x));
  REQUIRE(x.size() == 0);
  x = n3;
  REQUIRE(25 == swoc::svto_radix<8>(x));
  REQUIRE(x.size() == 0);

  // Check overflow conditions
  static constexpr auto UMAX = std::numeric_limits<uintmax_t>::max();
  static constexpr auto IMAX = std::numeric_limits<intmax_t>::max();
  static constexpr auto IMIN = std::numeric_limits<intmax_t>::min();

  // One less than max.
  x.assign("18446744073709551614");
  REQUIRE(UMAX - 1 == swoc::svto_radix<10>(x));
  REQUIRE(x.size() == 0);

  // Exactly max.
  x.assign("18446744073709551615");
  REQUIRE(UMAX == swoc::svto_radix<10>(x));
  REQUIRE(x.size() == 0);
  x.assign("18446744073709551615");
  CHECK(UMAX == svtou(x));

  // Should overflow and clamp.
  x.assign("18446744073709551616");
  REQUIRE(UMAX == swoc::svto_radix<10>(x));
  REQUIRE(x.size() == 0);

  // Even more digits.
  x.assign("18446744073709551616123456789");
  REQUIRE(UMAX == swoc::svto_radix<10>(x));
  REQUIRE(x.size() == 0);

  // This is a special value - where N*10 > N while also overflowing. The final "1" causes this.
  // Be sure overflow is detected.
  x.assign("27381885734412615681");
  REQUIRE(UMAX == swoc::svto_radix<10>(x));

  x.assign("9223372036854775807");
  CHECK(svtou(x) == IMAX);
  CHECK(svtoi(x) == IMAX);
  x.assign("9223372036854775808");
  CHECK(svtou(x) == uintmax_t(IMAX) + 1);
  CHECK(svtoi(x) == IMAX);

  x.assign("-9223372036854775807");
  CHECK(svtoi(x) == IMIN + 1);
  x.assign("-9223372036854775808");
  CHECK(svtoi(x) == IMIN);
  x.assign("-9223372036854775809");
  CHECK(svtoi(x) == IMIN);

  // floating point is never exact, so "good enough" is all that iisnts measureable. This checks the
  // value is within one epsilon (minimum change possible) of the compiler generated value.
  auto fcmp = [](double lhs, double rhs) {
    double tolerance = std::max({1.0, std::fabs(lhs), std::fabs(rhs)}) * std::numeric_limits<double>::epsilon();
    return std::fabs(lhs - rhs) <= tolerance;
  };

  REQUIRE(1.0 == swoc::svtod("1.0"));
  REQUIRE(2.0 == swoc::svtod("2.0"));
  REQUIRE(true == fcmp(0.1, swoc::svtod("0.1")));
  REQUIRE(true == fcmp(0.1, swoc::svtod(".1")));
  REQUIRE(true == fcmp(0.02, swoc::svtod("0.02")));
  REQUIRE(true == fcmp(2.718281828, swoc::svtod("2.718281828")));
  REQUIRE(true == fcmp(-2.718281828, swoc::svtod("-2.718281828")));
  REQUIRE(true == fcmp(2.718281828, swoc::svtod("+2.718281828")));
  REQUIRE(true == fcmp(0.004, swoc::svtod("4e-3")));
  REQUIRE(true == fcmp(4e-3, swoc::svtod("4e-3")));
  REQUIRE(true == fcmp(500000, swoc::svtod("5e5")));
  REQUIRE(true == fcmp(5e5, swoc::svtod("5e+5")));
  REQUIRE(true == fcmp(678900, swoc::svtod("6.789E5")));
  REQUIRE(true == fcmp(6.789e5, swoc::svtod("6.789E+5")));
}

TEST_CASE("TransformView", "[libswoc][TransformView]") {
  std::string_view source{"Evil Dave Rulz"};
  std::string_view rot13("Rivy Qnir Ehym");

  // Because, sadly, the type of @c tolower varies between compilers since @c noexcept
  // is part of the signature in C++17.
  swoc::TransformView<decltype(&tolower), std::string_view> xv1(&tolower, source);
  auto xv2 = swoc::transform_view_of(&tolower, source);
  // Rot13 transform
  auto rotter = swoc::transform_view_of(
    [](char c) { return isalpha(c) ? c > 'Z' ? ('a' + ((c - 'a' + 13) % 26)) : ('A' + ((c - 'A' + 13) % 26)) : c; }, source);
  auto identity = swoc::transform_view_of(source);

  TextView tv{source};

  REQUIRE(xv1 == xv2);

  // Do this with inline post-fix increments.
  bool match_p = true;
  while (xv1) {
    if (*xv1++ != tolower(*tv++)) {
      match_p = false;
      break;
    }
  }
  REQUIRE(match_p);
  REQUIRE(xv1 != xv2);

  // Do this one with separate pre-fix increments.
  tv      = source;
  match_p = true;
  while (xv2) {
    if (*xv2 != tolower(*tv)) {
      match_p = false;
      break;
    }
    ++xv2;
    ++tv;
  }

  REQUIRE(match_p);

  std::string check;
  std::copy(rotter.begin(), rotter.end(), std::back_inserter(check));
  REQUIRE(check == rot13);

  check.clear();
  for (auto c : identity) {
    check.push_back(c);
  }
  REQUIRE(check == source);

  check.clear();
  check.append(rotter.begin(), rotter.end());
  REQUIRE(check == rot13);
}

TEST_CASE("TextView compat", "[libswoc][TextView]") {
  struct Thing {
    int n = 0;
  };
  std::map<TextView, Thing> map;
  std::unordered_map<TextView, Thing> umap;

  // This isn't rigorous, it's mainly testing compilation.
  map.insert({"bob"_tv, Thing{2}});
  map["dave"] = Thing{3};
  umap.insert({"bob"_tv, Thing{4}});
  umap["dave"] = Thing{6};

  REQUIRE(map["bob"].n == 2);
  REQUIRE(umap["dave"].n == 6);
}

TEST_CASE("TextView tokenizing", "[libswoc][TextView]") {
  TextView src = "alpha,bravo,,charlie";
  auto tokens  = {"alpha", "bravo", "", "charlie"};
  for (auto token : tokens) {
    REQUIRE(src.take_prefix_at(',') == token);
  }
}

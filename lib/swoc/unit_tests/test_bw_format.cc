// SPDX-License-Identifier: Apache-2.0
/** @file

    Unit tests for BufferFormat and bwprint.
 */

#include <chrono>
#include <iostream>
#include <variant>

#include <netinet/in.h>

#include "swoc/MemSpan.h"
#include "swoc/BufferWriter.h"
#include "swoc/bwf_std.h"
#include "swoc/bwf_ex.h"

#include "catch.hpp"

using namespace std::literals;
using namespace swoc::literals;
using swoc::TextView;
using swoc::bwprint, swoc::bwappend;

TEST_CASE("Buffer Writer << operator", "[bufferwriter][stream]") {
  swoc::LocalBufferWriter<50> bw;

  bw << "The" << ' ' << "quick" << ' ' << "brown fox";

  REQUIRE(bw.view() == "The quick brown fox");

  bw.clear();
  bw << "x=" << bw.capacity();
  REQUIRE(bw.view() == "x=50");
}

TEST_CASE("bwprint basics", "[bwprint]") {
  swoc::LocalBufferWriter<256> bw;
  std::string_view fmt1{"Some text"sv};
  swoc::bwf::Format fmt2("left >{0:<9}< right >{0:>9}< center >{0:^9}<");
  std::string_view text{"0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"};
  static const swoc::bwf::Format bad_arg_fmt{"{{BAD_ARG_INDEX:{} of {}}}"};

  bw.print(fmt1);
  REQUIRE(bw.view() == fmt1);
  bw.clear().print("Some text"); // check that a literal string works as expected.
  REQUIRE(bw.view() == fmt1);
  bw.clear().print("Some text"_tv); // check that a literal TextView works.
  REQUIRE(bw.view() == fmt1);
  bw.clear().print("Arg {}", 1);
  REQUIRE(bw.view() == "Arg 1");
  bw.clear().print("arg 1 {1} and 2 {2} and 0 {0}", "zero", "one", "two");
  REQUIRE(bw.view() == "arg 1 one and 2 two and 0 zero");
  bw.clear().print("args {2}{0}{1}", "zero", "one", "two");
  REQUIRE(bw.view() == "args twozeroone");
  bw.clear().print("left |{:<10}|", "text");
  REQUIRE(bw.view() == "left |text      |");
  bw.clear().print("right |{:>10}|", "text");
  REQUIRE(bw.view() == "right |      text|");
  bw.clear().print("right |{:.>10}|", "text");
  REQUIRE(bw.view() == "right |......text|");
  bw.clear().print("center |{:.^10}|", "text");
  REQUIRE(bw.view() == "center |...text...|");
  bw.clear().print("center |{:.^11}|", "text");
  REQUIRE(bw.view() == "center |...text....|");
  bw.clear().print("center |{:^^10}|", "text");
  REQUIRE(bw.view() == "center |^^^text^^^|");
  bw.clear().print("center |{:%3A^10}|", "text");
  REQUIRE(bw.view() == "center |:::text:::|");
  bw.clear().print("left >{0:<9}< right >{0:>9}< center >{0:^9}<", 956);
  REQUIRE(bw.view() == "left >956      < right >      956< center >   956   <");

  bw.clear().print("Format |{:>#010x}|", -956);
  REQUIRE(bw.view() == "Format |0000-0x3bc|");
  bw.clear().print("Format |{:<#010x}|", -956);
  REQUIRE(bw.view() == "Format |-0x3bc0000|");
  bw.clear().print("Format |{:#010x}|", -956);
  REQUIRE(bw.view() == "Format |-0x00003bc|");

  bw.clear().print("{{BAD_ARG_INDEX:{} of {}}}", 17, 23);
  REQUIRE(bw.view() == "{BAD_ARG_INDEX:17 of 23}");

  bw.clear().print("Arg {0} Arg {3}", 0, 1);
  REQUIRE(bw.view() == "Arg 0 Arg {BAD_ARG_INDEX:3 of 2}");

  bw.clear().print("{{stuff}} Arg {0} Arg {}", 0, 1, 2);
  REQUIRE(bw.view() == "{stuff} Arg 0 Arg 0");
  bw.clear().print("{{stuff}} Arg {0} Arg {} {}", 0, 1, 2);
  REQUIRE(bw.view() == "{stuff} Arg 0 Arg 0 1");
  bw.clear();
  bw.print("Arg {0} Arg {} and {{stuff}}", 3, 4);
  REQUIRE(bw.view() == "Arg 3 Arg 3 and {stuff}");
  bw.clear().print("Arg {{{0}}} Arg {} and {{stuff}}", 5, 6);
  REQUIRE(bw.view() == "Arg {5} Arg 5 and {stuff}");
  bw.clear().print("Arg {{{0}}} Arg {} {1} {} {0} and {{stuff}}", 5, 6);
  REQUIRE(bw.view() == "Arg {5} Arg 5 6 6 5 and {stuff}");
  bw.clear();
  bw.print("Arg {0} Arg {{}}{{}} {} and {} {{stuff}}", 7, 8);
  REQUIRE(bw.view() == "Arg 7 Arg {}{} 7 and 8 {stuff}");
  bw.clear();
  bw.print("Arg {} Arg {{{{}}}} {} {1} {0}", 9, 10);
  REQUIRE(bw.view() == "Arg 9 Arg {{}} 10 10 9");

  bw.clear();
  bw.print("Arg {} Arg {{{{}}}} {}", 9, 10);
  REQUIRE(bw.view() == "Arg 9 Arg {{}} 10");
  bw.clear().print(bad_arg_fmt, 17, 23);
  REQUIRE(bw.view() == "{BAD_ARG_INDEX:17 of 23}");

  bw.clear().print("{leif}");
  REQUIRE(bw.view() == "{~leif~}"); // expected to be missing.

  bw.clear().print(fmt2, 956);
  REQUIRE(bw.view() == "left >956      < right >      956< center >   956   <");

  // Check leading space printing.
  bw.clear().print(" {}", fmt1);
  REQUIRE(bw.view() == " Some text");

  std::string_view fmt_sv = "Answer: \"{}\" Surprise!";
  std::string_view answer = "Evil Dave";
  bw.clear().print(fmt_sv, answer);
  REQUIRE(bw.view().size() == fmt_sv.size() + answer.size() - 2);
}

TEST_CASE("BWFormat numerics", "[bwprint][bwformat]") {
  swoc::LocalBufferWriter<256> bw;

  void *ptr = reinterpret_cast<void *>(0XBADD0956);
  bw.clear();
  bw.print("{}", ptr);
  REQUIRE(bw.view() == "0xbadd0956");
  bw.clear();
  bw.print("{:X}", ptr);
  REQUIRE(bw.view() == "0XBADD0956");
  int *int_ptr = static_cast<int *>(ptr);
  bw.clear();
  bw.print("{}", int_ptr);
  REQUIRE(bw.view() == "0xbadd0956");
  char char_ptr[] = "delain";
  bw.clear();
  bw.print("{:x}", static_cast<char *>(ptr));
  REQUIRE(bw.view() == "0xbadd0956");
  bw.clear();
  bw.print("{}", char_ptr);
  REQUIRE(bw.view() == "delain");

  swoc::MemSpan span{ptr, 0x200};
  bw.clear().print("{}", span);
  REQUIRE(bw.view() == "0x200@0xbadd0956");

  swoc::MemSpan cspan{char_ptr, 6};
  bw.clear().print("{:x}", cspan);
  REQUIRE(bw.view() == "64 65 6c 61 69 6e");
  bw.clear().print("{:#x}", cspan);
  REQUIRE(bw.view() == "0x64 0x65 0x6c 0x61 0x69 0x6e");
  bw.clear().print("{:#.2x}", cspan);
  REQUIRE(bw.view() == "0x6465 0x6c61 0x696e");
  bw.clear().print("{:x}", cspan.rebind());
  REQUIRE(bw.view() == "64656c61696e");

  TextView sv{"abc123"};
  bw.clear();
  bw.print("{}", sv);
  REQUIRE(bw.view() == sv);
  bw.clear();
  bw.print("{:x}", sv);
  REQUIRE(bw.view() == "616263313233");
  bw.clear();
  bw.print("{:#x}", sv);
  REQUIRE(bw.view() == "0x616263313233");
  bw.clear();
  bw.print("|{:16x}|", sv);
  REQUIRE(bw.view() == "|616263313233    |");
  bw.clear();
  bw.print("|{:>16x}|", sv);
  REQUIRE(bw.view() == "|    616263313233|");
  bw.clear().print("|{:^16x}|", sv);
  REQUIRE(bw.view() == "|  616263313233  |");
  bw.clear().print("|{:>16.2x}|", sv);
  REQUIRE(bw.view() == "|            6162|");

  // Substrings by argument adjustment.
  bw.clear().print("|{:<0,7x}|", sv.prefix(4));
  REQUIRE(bw.view() == "|6162633|");
  bw.clear().print("|{:<5,7x}|", sv.prefix(2));
  REQUIRE(bw.view() == "|6162 |");
  bw.clear().print("|{:<5,7x}|", sv.prefix(3));
  REQUIRE(bw.view() == "|616263|");
  bw.clear().print("|{:<7x}|", sv.prefix(3));
  REQUIRE(bw.view() == "|616263 |");

  // Substrings by precision - should be same output.
  bw.clear().print("|{:<0.4,7x}|", sv);
  REQUIRE(bw.view() == "|6162633|");
  bw.clear().print("|{:<5.2,7x}|", sv);
  REQUIRE(bw.view() == "|6162 |");
  bw.clear().print("|{:<5.3,7x}|", sv);
  REQUIRE(bw.view() == "|616263|");
  bw.clear().print("|{:<7.3x}|", sv);
  REQUIRE(bw.view() == "|616263 |");

  bw.clear();
  bw.print("|{}|", true);
  REQUIRE(bw.view() == "|1|");
  bw.clear();
  bw.print("|{}|", false);
  REQUIRE(bw.view() == "|0|");
  bw.clear();
  bw.print("|{:s}|", true);
  REQUIRE(bw.view() == "|true|");
  bw.clear();
  bw.print("|{:S}|", false);
  REQUIRE(bw.view() == "|FALSE|");
  bw.clear();
  bw.print("|{:>9s}|", false);
  REQUIRE(bw.view() == "|    false|");
  bw.clear();
  bw.print("|{:^10s}|", true);
  REQUIRE(bw.view() == "|   true   |");

  // Test clipping a bit.
  swoc::LocalBufferWriter<20> bw20;
  bw20.print("0123456789abc|{:^10s}|", true);
  REQUIRE(bw20.view() == "0123456789abc|   tru");
  bw20.clear();
  bw20.print("012345|{:^10s}|6789abc", true);
  REQUIRE(bw20.view() == "012345|   true   |67");

  bw.clear().print("Char '{}'", 'a');
  REQUIRE(bw.view() == "Char 'a'");
  bw.clear().print("Byte '{}'", uint8_t{'a'});
  REQUIRE(bw.view() == "Byte '97'");

  SECTION("Hexadecimal buffers") {
    swoc::MemSpan<void const> cvs{"Evil Dave Rulz"_tv}; // TextView intermediate keeps nul byte out.
    TextView const edr_in_hex{"4576696c20446176652052756c7a"};
    bw.clear().format(swoc::bwf::Spec(":x"), cvs);
    REQUIRE(bw.view() == edr_in_hex);
    bw.clear().format(swoc::bwf::Spec::DEFAULT, swoc::bwf::UnHex(edr_in_hex));
    REQUIRE(bw.view() == "Evil Dave Rulz");
    bw.clear().format(swoc::bwf::Spec::DEFAULT, swoc::bwf::UnHex("112233445566778800"));
    REQUIRE(memcmp(bw.view(), "\x11\x22\x33\x44\x55\x66\x77\x88\x00"_tv) == 0);
    // Check if max width in the spec works - should leave bytes from the previous.
    bw.clear().format(swoc::bwf::Spec{":,2"}, swoc::bwf::UnHex("deadbeef"));
    REQUIRE(memcmp(TextView(bw.data(), 4), "\xde\xad\x33\x44"_tv) == 0);
    std::string text, hex;
    bwprint(hex, "{:x}", cvs);
    bwprint(text, "{}", swoc::bwf::UnHex(edr_in_hex));
    REQUIRE(hex == edr_in_hex);
    REQUIRE(text == TextView(cvs.rebind<char const>()));
  }
}

TEST_CASE("bwstring", "[bwprint][bwappend][bwstring]") {
  std::string s;
  swoc::TextView fmt("{} -- {}");
  std::string_view text{"e99a18c428cb38d5f260853678922e03"};

  bwprint(s, fmt, "string", 956);
  REQUIRE(s.size() == 13);
  REQUIRE(s == "string -- 956");

  bwprint(s, fmt, 99999, text);
  REQUIRE(s == "99999 -- e99a18c428cb38d5f260853678922e03");

  bwprint(s, "{} .. |{:,20}|", 32767, text);
  REQUIRE(s == "32767 .. |e99a18c428cb38d5f260|");

  swoc::LocalBufferWriter<128> bw;
  char buff[128];
  snprintf(buff, sizeof(buff), "|%s|", bw.print("Deep Silent Complete by {}\0", "Nightwish"sv).data());
  REQUIRE(std::string_view(buff) == "|Deep Silent Complete by Nightwish|");
  snprintf(buff, sizeof(buff), "|%s|", bw.clear().print("Deep Silent Complete by {}\0elided junk", "Nightwish"sv).data());
  REQUIRE(std::string_view(buff) == "|Deep Silent Complete by Nightwish|");

  // Special tests for clang analyzer failures - special asserts are needed to make it happy but
  // those can break functionality.
  fmt = "Did you know? {}{} is {}"sv;
  s.resize(0);
  bwprint(s, fmt, "Lady "sv, "Persia"sv, "not mean");
  REQUIRE(s == "Did you know? Lady Persia is not mean");
  s.resize(0);
  bwprint(s, fmt, ""sv, "Phil", "correct");
  REQUIRE(s == "Did you know? Phil is correct");
  s.resize(0);
  bwprint(s, fmt, std::string_view(), "Leif", "confused");
  REQUIRE(s == "Did you know? Leif is confused");

  {
    std::string out;
    bwprint(out, fmt, ""sv, "Phil", "correct");
    REQUIRE(out == "Did you know? Phil is correct");
  }
  {
    std::string out;
    bwprint(out, fmt, std::string_view(), "Leif", "confused");
    REQUIRE(out == "Did you know? Leif is confused");
  }

  char const *null_string{nullptr};
  bwprint(s, "Null {0:x}.{0}", null_string);
  REQUIRE(s == "Null 0x0.");
  bwprint(s, "Null {0:X}.{0}", nullptr);
  REQUIRE(s == "Null 0X0.");
  bwprint(s, "Null {0:p}.{0:P}.{0:s}.{0:S}", null_string);
  REQUIRE(s == "Null 0x0.0X0.null.NULL");

  {
    std::string x;
    bwappend(x, "Phil");
    REQUIRE(x == "Phil");
    bwappend(x, " is {} most of the time", "correct"_tv);
    REQUIRE(x == "Phil is correct most of the time");
    x.resize(0); // try it with already sufficient capacity.
    bwappend(x, "Dave");
    REQUIRE(x == "Dave");
    bwappend(x, " is {} some of the time", "correct"_tv);
    REQUIRE(x == "Dave is correct some of the time");
  }
}

TEST_CASE("BWFormat integral", "[bwprint][bwformat]") {
  swoc::LocalBufferWriter<256> bw;
  swoc::bwf::Spec spec;
  uint32_t num = 30;
  int num_neg  = -30;

  // basic
  bwformat(bw, spec, num);
  REQUIRE(bw.view() == "30");
  bw.clear();
  bwformat(bw, spec, num_neg);
  REQUIRE(bw.view() == "-30");
  bw.clear();

  // radix
  swoc::bwf::Spec spec_hex;
  spec_hex._radix_lead_p = true;
  spec_hex._type         = 'x';
  bwformat(bw, spec_hex, num);
  REQUIRE(bw.view() == "0x1e");
  bw.clear();

  swoc::bwf::Spec spec_dec;
  spec_dec._type = '0';
  bwformat(bw, spec_dec, num);
  REQUIRE(bw.view() == "30");
  bw.clear();

  swoc::bwf::Spec spec_bin;
  spec_bin._radix_lead_p = true;
  spec_bin._type         = 'b';
  bwformat(bw, spec_bin, num);
  REQUIRE(bw.view() == "0b11110");
  bw.clear();

  int one     = 1;
  int two     = 2;
  int three_n = -3;
  // alignment
  swoc::bwf::Spec left;
  left._align = swoc::bwf::Spec::Align::LEFT;
  left._min   = 5;
  swoc::bwf::Spec right;
  right._align = swoc::bwf::Spec::Align::RIGHT;
  right._min   = 5;
  swoc::bwf::Spec center;
  center._align = swoc::bwf::Spec::Align::CENTER;
  center._min   = 5;

  bwformat(bw, left, one);
  bwformat(bw, right, two);
  REQUIRE(bw.view() == "1        2");
  bwformat(bw, right, two);
  REQUIRE(bw.view() == "1        2    2");
  bwformat(bw, center, three_n);
  REQUIRE(bw.view() == "1        2    2 -3  ");

  std::atomic<int> ax{0};
  bw.clear().print("ax == {}", ax);
  REQUIRE(bw.view() == "ax == 0");
  ++ax;
  bw.clear().print("ax == {}", ax);
  REQUIRE(bw.view() == "ax == 1");
}

TEST_CASE("BWFormat floating", "[bwprint][bwformat]") {
  swoc::LocalBufferWriter<256> bw;
  swoc::bwf::Spec spec;

  bw.clear();
  bw.print("{}", 3.14);
  REQUIRE(bw.view() == "3.14");
  bw.clear();
  bw.print("{} {:.2} {:.0} ", 32.7, 32.7, 32.7);
  REQUIRE(bw.view() == "32.70 32.70 32 ");
  bw.clear();
  bw.print("{} neg {:.3}", -123.2, -123.2);
  REQUIRE(bw.view() == "-123.20 neg -123.200");
  bw.clear();
  bw.print("zero {} quarter {} half {} 3/4 {}", 0, 0.25, 0.50, 0.75);
  REQUIRE(bw.view() == "zero 0 quarter 0.25 half 0.50 3/4 0.75");
  bw.clear();
  bw.print("long {:.11}", 64.9);
  REQUIRE(bw.view() == "long 64.90000000000");
  bw.clear();

  double n   = 180.278;
  double neg = -238.47;
  bwformat(bw, spec, n);
  REQUIRE(bw.view() == "180.28");
  bw.clear();
  bwformat(bw, spec, neg);
  REQUIRE(bw.view() == "-238.47");
  bw.clear();

  spec._prec = 5;
  bwformat(bw, spec, n);
  REQUIRE(bw.view() == "180.27800");
  bw.clear();
  bwformat(bw, spec, neg);
  REQUIRE(bw.view() == "-238.47000");
  bw.clear();

  float f    = 1234;
  float fneg = -1;
  bwformat(bw, spec, f);
  REQUIRE(bw.view() == "1234");
  bw.clear();
  bwformat(bw, spec, fneg);
  REQUIRE(bw.view() == "-1");
  bw.clear();
  f          = 1234.5667;
  spec._prec = 4;
  bwformat(bw, spec, f);
  REQUIRE(bw.view() == "1234.5667");
  bw.clear();

  bw << 1234 << .567;
  REQUIRE(bw.view() == "12340.57");
  bw.clear();
  bw << f;
  REQUIRE(bw.view() == "1234.57");
  bw.clear();
  bw << n;
  REQUIRE(bw.view() == "180.28");
  bw.clear();
  bw << f << n;
  REQUIRE(bw.view() == "1234.57180.28");
  bw.clear();

  double edge = 0.345;
  spec._prec  = 3;
  bwformat(bw, spec, edge);
  REQUIRE(bw.view() == "0.345");
  bw.clear();
  edge = .1234;
  bwformat(bw, spec, edge);
  REQUIRE(bw.view() == "0.123");
  bw.clear();
  edge = 1.0;
  bwformat(bw, spec, edge);
  REQUIRE(bw.view() == "1");
  bw.clear();

  // alignment
  double first  = 1.23;
  double second = 2.35;
  double third  = -3.5;
  swoc::bwf::Spec left;
  left._align = swoc::bwf::Spec::Align::LEFT;
  left._min   = 5;
  swoc::bwf::Spec right;
  right._align = swoc::bwf::Spec::Align::RIGHT;
  right._min   = 5;
  swoc::bwf::Spec center;
  center._align = swoc::bwf::Spec::Align::CENTER;
  center._min   = 5;

  bwformat(bw, left, first);
  bwformat(bw, right, second);
  REQUIRE(bw.view() == "1.23  2.35");
  bwformat(bw, right, second);
  REQUIRE(bw.view() == "1.23  2.35 2.35");
  bwformat(bw, center, third);
  REQUIRE(bw.view() == "1.23  2.35 2.35-3.50");
  bw.clear();

  double over = 1.4444444;
  swoc::bwf::Spec over_min;
  over_min._prec = 7;
  over_min._min  = 5;
  bwformat(bw, over_min, over);
  REQUIRE(bw.view() == "1.4444444");
  bw.clear();

  // Edge
  bw.print("{}", (1.0 / 0.0));
  REQUIRE(bw.view() == "Inf");
  bw.clear();

  double inf = std::numeric_limits<double>::infinity();
  bw.print("  {} ", inf);
  REQUIRE(bw.view() == "  Inf ");
  bw.clear();

  double nan_1 = std::nan("1");
  bw.print("{} {}", nan_1, nan_1);
  REQUIRE(bw.view() == "NaN NaN");
  bw.clear();

  double z = 0.0;
  bw.print("{}  ", z);
  REQUIRE(bw.view() == "0  ");
  bw.clear();
}

TEST_CASE("bwstring std formats", "[libswoc][bwprint]") {
  std::string_view text{"0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"};
  swoc::LocalBufferWriter<120> w;

  w.print("{}", swoc::bwf::Errno(13));
  REQUIRE(w.view() == "EACCES: Permission denied [13]"sv);
  w.clear().print("{}", swoc::bwf::Errno(134));
  REQUIRE(w.view().substr(0, 22) == "Unknown: Unknown error"sv);
  w.clear().print("{:s}", swoc::bwf::Errno(13));
  REQUIRE(w.view() == "EACCES: Permission denied"sv);
  w.clear().print("{:S}", swoc::bwf::Errno(13));
  REQUIRE(w.view() == "EACCES: Permission denied"sv);
  w.clear().print("{:s:s}", swoc::bwf::Errno(13));
  REQUIRE(w.view() == "EACCES"sv);
  w.clear().print("{:s:l}", swoc::bwf::Errno(13));
  REQUIRE(w.view() == "Permission denied"sv);
  w.clear().print("{:s:sl}", swoc::bwf::Errno(13));
  REQUIRE(w.view() == "EACCES: Permission denied"sv);
  w.clear().print("{:d}", swoc::bwf::Errno(13));
  REQUIRE(w.view() == "[13]"sv);
  w.clear().print("{:g}", swoc::bwf::Errno(13));
  REQUIRE(w.view() == "EACCES: Permission denied [13]"sv);
  w.clear().print("{:g:s}", swoc::bwf::Errno(13));
  REQUIRE(w.view() == "EACCES [13]"sv);
  w.clear().print("{::s}", swoc::bwf::Errno(13));
  REQUIRE(w.view() == "EACCES [13]"sv);
  w.clear().print("{::l}", swoc::bwf::Errno(13));
  REQUIRE(w.view() == "Permission denied [13]"sv);

  time_t t = 1528484137;
  // default is GMT
  w.clear().print("{} is {}", t, swoc::bwf::Date(t));
  REQUIRE(w.view() == "1528484137 is 2018 Jun 08 18:55:37");
  w.clear().print("{} is {}", t, swoc::bwf::Date(t, "%a, %d %b %Y at %H.%M.%S"));
  REQUIRE(w.view() == "1528484137 is Fri, 08 Jun 2018 at 18.55.37");
  // OK to be explicit
  w.clear().print("{} is {::gmt}", t, swoc::bwf::Date(t));
  REQUIRE(w.view() == "1528484137 is 2018 Jun 08 18:55:37");
  w.clear().print("{} is {::gmt}", t, swoc::bwf::Date(t, "%a, %d %b %Y at %H.%M.%S"));
  REQUIRE(w.view() == "1528484137 is Fri, 08 Jun 2018 at 18.55.37");
  // Local time - set it to something specific or the test will be geographically sensitive.
  setenv("TZ", "CST6", 1);
  tzset();
  w.clear().print("{} is {::local}", t, swoc::bwf::Date(t));
  REQUIRE(w.view() == "1528484137 is 2018 Jun 08 12:55:37");
  w.clear().print("{} is {::local}", t, swoc::bwf::Date(t, "%a, %d %b %Y at %H.%M.%S"));
  REQUIRE(w.view() == "1528484137 is Fri, 08 Jun 2018 at 12.55.37");

  unsigned v = htonl(0xdeadbeef);
  w.clear().print("{}", swoc::bwf::As_Hex(v));
  REQUIRE(w.view() == "deadbeef");
  w.clear().print("{:x}", swoc::bwf::As_Hex(v));
  REQUIRE(w.view() == "deadbeef");
  w.clear().print("{:X}", swoc::bwf::As_Hex(v));
  REQUIRE(w.view() == "DEADBEEF");
  w.clear().print("{:#X}", swoc::bwf::As_Hex(v));
  REQUIRE(w.view() == "0XDEADBEEF");
  w.clear().print("{} bytes {} digits {}", sizeof(double), std::numeric_limits<double>::digits10, swoc::bwf::As_Hex(2.718281828));
  REQUIRE(w.view() == "8 bytes 15 digits 9b91048b0abf0540");

  // Verify these compile and run, not really much hope to check output.
  w.clear().print("|{}|   |{}|", swoc::bwf::Date(), swoc::bwf::Date("%a, %d %b %Y"));

  w.clear().print("name = {}", swoc::bwf::FirstOf("Persia"));
  REQUIRE(w.view() == "name = Persia");
  w.clear().print("name = {}", swoc::bwf::FirstOf("Persia", "Evil Dave"));
  REQUIRE(w.view() == "name = Persia");
  w.clear().print("name = {}", swoc::bwf::FirstOf("", "Evil Dave"));
  REQUIRE(w.view() == "name = Evil Dave");
  w.clear().print("name = {}", swoc::bwf::FirstOf(nullptr, "Evil Dave"));
  REQUIRE(w.view() == "name = Evil Dave");
  w.clear().print("name = {}", swoc::bwf::FirstOf("Persia", "Evil Dave", "Leif"));
  REQUIRE(w.view() == "name = Persia");
  w.clear().print("name = {}", swoc::bwf::FirstOf("Persia", nullptr, "Leif"));
  REQUIRE(w.view() == "name = Persia");
  w.clear().print("name = {}", swoc::bwf::FirstOf("", nullptr, "Leif"));
  REQUIRE(w.view() == "name = Leif");

  const char *empty{nullptr};
  std::string s1{"Persia"};
  std::string_view s2{"Evil Dave"};
  swoc::TextView s3{"Leif"};
  w.clear().print("name = {}", swoc::bwf::FirstOf(empty, s3));
  REQUIRE(w.view() == "name = Leif");
  w.clear().print("name = {}", swoc::bwf::FirstOf(s2, s3));
  REQUIRE(w.view() == "name = Evil Dave");
  w.clear().print("name = {}", swoc::bwf::FirstOf(s1, empty, s2));
  REQUIRE(w.view() == "name = Persia");
  w.clear().print("name = {}", swoc::bwf::FirstOf(empty, s2, s1, s3));
  REQUIRE(w.view() == "name = Evil Dave");
  w.clear().print("name = {}", swoc::bwf::FirstOf(empty, empty, s3, empty, s2, s1));
  REQUIRE(w.view() == "name = Leif");

  w.clear().print("Lower - |{:s}|", text);
  REQUIRE(w.view() == "Lower - |0123456789abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz|");
  w.clear().print("Upper - |{:S}|", text);
  REQUIRE(w.view() == "Upper - |0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZ|");

  w.clear().print("Leading{}{}{}.", swoc::bwf::Optional(" | {}  |", s1), swoc::bwf::Optional(" <{}>", empty),
                  swoc::bwf::If(!s3.empty(), " [{}]", s3));
  REQUIRE(w.view() == "Leading | Persia  | [Leif].");
  // Do it again, but this time as C strings (char * variants).
  w.clear().print("Leading{}{}{}.", swoc::bwf::Optional(" | {}  |", s3.data()), swoc::bwf::Optional(" <{}>", empty),
                  swoc::bwf::If(!s3.empty(), " [{}]", s1.c_str()));
  REQUIRE(w.view() == "Leading | Leif  | [Persia].");
  // Play with string_view
  w.clear().print("Clone?{}{}.", swoc::bwf::Optional(" #. {}", s2), swoc::bwf::Optional(" #. {}", s2.data()));
  REQUIRE(w.view() == "Clone? #. Evil Dave #. Evil Dave.");
  s2 = "";
  w.clear().print("Leading{}{}{}", swoc::bwf::If(true, " true"), swoc::bwf::If(false, " false"), swoc::bwf::If(true, " Persia"));
  REQUIRE(w.view() == "Leading true Persia");
  // Differentiate because the C string variant will generate output, as it's not nullptr,
  // but is a pointer to an empty string.
  w.clear().print("Clone?{}{}.", swoc::bwf::Optional(" 1. {}", s2), swoc::bwf::Optional(" 2. {}", s2.data()));
  REQUIRE(w.view() == "Clone? 2. .");
  s2 = std::string_view{};
  w.clear().print("Clone?{}{}.", swoc::bwf::Optional(" #. {}", s2), swoc::bwf::Optional(" #. {}", s2.data()));
  REQUIRE(w.view() == "Clone?.");

  SECTION("exception") {
    std::runtime_error e("Sureness out of bounds");
    w.clear().print("{}", e);
    REQUIRE(w.view() == "Exception - Sureness out of bounds");
  }
};

// Normally there's no point in running the performance tests, but it's worth keeping the code
// for when additional testing needs to be done.
#if 0
TEST_CASE("bwperf", "[bwprint][performance]")
{
  // Force these so I can easily change the set of tests.
  auto start            = std::chrono::high_resolution_clock::now();
  auto delta = std::chrono::high_resolution_clock::now() - start;
  constexpr int N_LOOPS = 1000000;

  static constexpr const char * FMT = "Format |{:#010x}| '{}'";
  static constexpr swoc::TextView fmt{FMT, strlen(FMT)};
  static constexpr std::string_view text{"e99a18c428cb38d5f260853678922e03"sv};
  swoc::LocalBufferWriter<256> bw;

  swoc::bwf::Spec spec;

  bw.clear();
  bw.print(fmt, -956, text);
  REQUIRE(bw.view() == "Format |-0x00003bc| 'e99a18c428cb38d5f260853678922e03'");

  start = std::chrono::high_resolution_clock::now();
  for (int i = 0; i < N_LOOPS; ++i) {
    bw.clear();
    bw.print(fmt, -956, text);
  }
  delta = std::chrono::high_resolution_clock::now() - start;
  std::cout << "bw.print() " << delta.count() << "ns or " << std::chrono::duration_cast<std::chrono::milliseconds>(delta).count()
            << "ms" << std::endl;

  swoc::bwf::Format pre_fmt(fmt);
  start = std::chrono::high_resolution_clock::now();
  for (int i = 0; i < N_LOOPS; ++i) {
    bw.clear();
    bw.print(pre_fmt, -956, text);
  }
  delta = std::chrono::high_resolution_clock::now() - start;
  std::cout << "Preformatted: " << delta.count() << "ns or "
            << std::chrono::duration_cast<std::chrono::milliseconds>(delta).count() << "ms" << std::endl;

  char buff[256];
  start = std::chrono::high_resolution_clock::now();
  for (int i = 0; i < N_LOOPS; ++i) {
    snprintf(buff, sizeof(buff), "Format |%#0x10| '%.*s'", -956, static_cast<int>(text.size()), text.data());
  }
  delta = std::chrono::high_resolution_clock::now() - start;
  std::cout << "snprint Timing is " << delta.count() << "ns or "
            << std::chrono::duration_cast<std::chrono::milliseconds>(delta).count() << "ms" << std::endl;
}
#endif

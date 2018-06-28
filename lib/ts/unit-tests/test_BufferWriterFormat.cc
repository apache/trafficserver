/** @file

    Unit tests for BufferFormat and bwprint.

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
#include <chrono>
#include <iostream>
#include <ts/BufferWriter.h>
#include <ts/MemSpan.h>
#include <ts/INK_MD5.h>
#include <ts/CryptoHash.h>

TEST_CASE("Buffer Writer << operator", "[bufferwriter][stream]")
{
  ts::LocalBufferWriter<50> bw;

  bw << "The" << ' ' << "quick" << ' ' << "brown fox";

  REQUIRE(bw.view() == "The quick brown fox");

  bw.reduce(0);
  bw << "x=" << bw.capacity();
  REQUIRE(bw.view() == "x=50");
}

TEST_CASE("bwprint basics", "[bwprint]")
{
  ts::LocalBufferWriter<256> bw;
  ts::string_view fmt1{"Some text"_sv};

  bw.print(fmt1);
  REQUIRE(bw.view() == fmt1);
  bw.reduce(0);
  bw.print("Arg {}", 1);
  REQUIRE(bw.view() == "Arg 1");
  bw.reduce(0);
  bw.print("arg 1 {1} and 2 {2} and 0 {0}", "zero", "one", "two");
  REQUIRE(bw.view() == "arg 1 one and 2 two and 0 zero");
  bw.reduce(0);
  bw.print("args {2}{0}{1}", "zero", "one", "two");
  REQUIRE(bw.view() == "args twozeroone");
  bw.reduce(0);
  bw.print("left |{:<10}|", "text");
  REQUIRE(bw.view() == "left |text      |");
  bw.reduce(0);
  bw.print("right |{:>10}|", "text");
  REQUIRE(bw.view() == "right |      text|");
  bw.reduce(0);
  bw.print("right |{:.>10}|", "text");
  REQUIRE(bw.view() == "right |......text|");
  bw.reduce(0);
  bw.print("center |{:.=10}|", "text");
  REQUIRE(bw.view() == "center |...text...|");
  bw.reduce(0);
  bw.print("center |{:.=11}|", "text");
  REQUIRE(bw.view() == "center |...text....|");
  bw.reduce(0);
  bw.print("center |{:==10}|", "text");
  REQUIRE(bw.view() == "center |===text===|");
  bw.reduce(0);
  bw.print("center |{:%3A=10}|", "text");
  REQUIRE(bw.view() == "center |:::text:::|");
  bw.reduce(0);
  bw.print("left >{0:<9}< right >{0:>9}< center >{0:=9}<", 956);
  REQUIRE(bw.view() == "left >956      < right >      956< center >   956   <");

  bw.reduce(0);
  bw.print("Format |{:>#010x}|", -956);
  REQUIRE(bw.view() == "Format |0000-0x3bc|");
  bw.reduce(0);
  bw.print("Format |{:<#010x}|", -956);
  REQUIRE(bw.view() == "Format |-0x3bc0000|");
  bw.reduce(0);
  bw.print("Format |{:#010x}|", -956);
  REQUIRE(bw.view() == "Format |-0x00003bc|");

  bw.reduce(0);
  bw.print("{{BAD_ARG_INDEX:{} of {}}}", 17, 23);
  REQUIRE(bw.view() == "{BAD_ARG_INDEX:17 of 23}");

  bw.reduce(0);
  bw.print("Arg {0} Arg {3}", 1, 2);
  REQUIRE(bw.view() == "Arg 1 Arg {BAD_ARG_INDEX:3 of 2}");

  bw.reduce(0);
  bw.print("{{stuff}} Arg {0} Arg {}", 1, 2);
  REQUIRE(bw.view() == "{stuff} Arg 1 Arg 2");
  bw.reduce(0);
  bw.print("Arg {0} Arg {} and {{stuff}}", 3, 4);
  REQUIRE(bw.view() == "Arg 3 Arg 4 and {stuff}");
  bw.reduce(0);
  bw.print("Arg {{{0}}} Arg {} and {{stuff}}", 5, 6);
  REQUIRE(bw.view() == "Arg {5} Arg 6 and {stuff}");
  bw.reduce(0);
  bw.print("Arg {0} Arg {{}}{{}} {} and {{stuff}}", 7, 8);
  REQUIRE(bw.view() == "Arg 7 Arg {}{} 8 and {stuff}");
  bw.reduce(0);
  bw.print("Arg {0} Arg {{{{}}}} {}", 9, 10);
  REQUIRE(bw.view() == "Arg 9 Arg {{}} 10");

  bw.reduce(0);
  bw.print("Arg {0} Arg {{{{}}}} {}", 9, 10);
  REQUIRE(bw.view() == "Arg 9 Arg {{}} 10");
  bw.reduce(0);
  bw.print("Time is {now}");
  //  REQUIRE(bw.view() == "Time is");
}

TEST_CASE("BWFormat", "[bwprint][bwformat]")
{
  ts::LocalBufferWriter<256> bw;
  ts::BWFormat fmt("left >{0:<9}< right >{0:>9}< center >{0:=9}<");
  ts::string_view text{"0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"};

  bw.reduce(0);
  static const ts::BWFormat bad_arg_fmt{"{{BAD_ARG_INDEX:{} of {}}}"};
  bw.print(bad_arg_fmt, 17, 23);
  REQUIRE(bw.view() == "{BAD_ARG_INDEX:17 of 23}");

  bw.reduce(0);
  bw.print(fmt, 956);
  REQUIRE(bw.view() == "left >956      < right >      956< center >   956   <");

  bw.reduce(0);
  bw.print("Text: _{0:.10,20}_", text);
  REQUIRE(bw.view() == "Text: _abcdefghijklmnopqrst_");
  bw.reduce(0);
  bw.print("Text: _{0:-<20.52,20}_", text);
  REQUIRE(bw.view() == "Text: _QRSTUVWXYZ----------_");

  void *ptr = reinterpret_cast<void *>(0XBADD0956);
  bw.reduce(0);
  bw.print("{}", ptr);
  REQUIRE(bw.view() == "0xbadd0956");
  bw.reduce(0);
  bw.print("{:X}", ptr);
  REQUIRE(bw.view() == "0XBADD0956");
  int *int_ptr = static_cast<int *>(ptr);
  bw.reduce(0);
  bw.print("{}", int_ptr);
  REQUIRE(bw.view() == "0xbadd0956");
  auto char_ptr = "good";
  bw.reduce(0);
  bw.print("{:x}", static_cast<char *>(ptr));
  REQUIRE(bw.view() == "0xbadd0956");
  bw.reduce(0);
  bw.print("{}", char_ptr);
  REQUIRE(bw.view() == "good");

  ts::MemSpan span{ptr, 0x200};
  bw.reduce(0);
  bw.print("{}", span);
  REQUIRE(bw.view() == "0x200@0xbadd0956");

  bw.reduce(0);
  bw.print("{::d}", ts::MemSpan(const_cast<char *>(char_ptr), 4));
  REQUIRE(bw.view() == "676f6f64");
  bw.reduce(0);
  bw.print("{:#:d}", ts::MemSpan(const_cast<char *>(char_ptr), 4));
  REQUIRE(bw.view() == "0x676f6f64");

  ts::string_view sv{"abc123"};
  bw.reduce(0);
  bw.print("{}", sv);
  REQUIRE(bw.view() == sv);
  bw.reduce(0);
  bw.print("{:x}", sv);
  REQUIRE(bw.view() == "616263313233");
  bw.reduce(0);
  bw.print("{:#x}", sv);
  REQUIRE(bw.view() == "0x616263313233");
  bw.reduce(0);
  bw.print("|{:16x}|", sv);
  REQUIRE(bw.view() == "|616263313233    |");
  bw.reduce(0);
  bw.print("|{:>16x}|", sv);
  REQUIRE(bw.view() == "|    616263313233|");
  bw.reduce(0);
  bw.print("|{:=16x}|", sv);
  REQUIRE(bw.view() == "|  616263313233  |");
  bw.reduce(0);
  bw.print("|{:>16.2x}|", sv);
  REQUIRE(bw.view() == "|        63313233|");
  bw.reduce(0);
  bw.print("|{:<0.2,5x}|", sv);
  REQUIRE(bw.view() == "|63313|");

  bw.reduce(0);
  bw.print("|{}|", true);
  REQUIRE(bw.view() == "|1|");
  bw.reduce(0);
  bw.print("|{}|", false);
  REQUIRE(bw.view() == "|0|");
  bw.reduce(0);
  bw.print("|{:s}|", true);
  REQUIRE(bw.view() == "|true|");
  bw.reduce(0);
  bw.print("|{:S}|", false);
  REQUIRE(bw.view() == "|FALSE|");
  bw.reduce(0);
  bw.print("|{:>9s}|", false);
  REQUIRE(bw.view() == "|    false|");
  bw.reduce(0);
  bw.print("|{:=10s}|", true);
  REQUIRE(bw.view() == "|   true   |");

  // Test clipping a bit.
  ts::LocalBufferWriter<20> bw20;
  bw20.print("0123456789abc|{:=10s}|", true);
  REQUIRE(bw20.view() == "0123456789abc|   tru");
  bw20.reduce(0);
  bw20.print("012345|{:=10s}|6789abc", true);
  REQUIRE(bw20.view() == "012345|   true   |67");

  INK_MD5 md5;
  bw.reduce(0);
  bw.print("{}", md5);
  REQUIRE(bw.view() == "00000000000000000000000000000000");
  CryptoContext().hash_immediate(md5, sv.data(), sv.size());
  bw.reduce(0);
  bw.print("{}", md5);
  REQUIRE(bw.view() == "e99a18c428cb38d5f260853678922e03");
}

TEST_CASE("bwstring", "[bwprint][bwstring]")
{
  std::string s;
  ts::TextView fmt("{} -- {}");
  ts::string_view text{"e99a18c428cb38d5f260853678922e03"};

  ts::bwprint(s, fmt, "string", 956);
  REQUIRE(s.size() == 13);
  REQUIRE(s == "string -- 956");

  ts::bwprint(s, fmt, 99999, text);
  REQUIRE(s == "99999 -- e99a18c428cb38d5f260853678922e03");

  ts::bwprint(s, "{} .. |{:,20}|", 32767, text);
  REQUIRE(s == "32767 .. |e99a18c428cb38d5f260|");
}

TEST_CASE("BWFormat integral", "[bwprint][bwformat]")
{
  ts::LocalBufferWriter<256> bw;
  ts::BWFSpec spec;
  uint32_t num = 30;
  int num_neg  = -30;

  // basic
  bwformat(bw, spec, num);
  REQUIRE(bw.view() == "30");
  bw.reduce(0);
  bwformat(bw, spec, num_neg);
  REQUIRE(bw.view() == "-30");
  bw.reduce(0);

  // radix
  ts::BWFSpec spec_hex;
  spec_hex._radix_lead_p = true;
  spec_hex._type         = 'x';
  bwformat(bw, spec_hex, num);
  REQUIRE(bw.view() == "0x1e");
  bw.reduce(0);

  ts::BWFSpec spec_dec;
  spec_dec._type = '0';
  bwformat(bw, spec_dec, num);
  REQUIRE(bw.view() == "30");
  bw.reduce(0);

  ts::BWFSpec spec_bin;
  spec_bin._radix_lead_p = true;
  spec_bin._type         = 'b';
  bwformat(bw, spec_bin, num);
  REQUIRE(bw.view() == "0b11110");
  bw.reduce(0);

  int one     = 1;
  int two     = 2;
  int three_n = -3;
  // alignment
  ts::BWFSpec left;
  left._align = ts::BWFSpec::Align::LEFT;
  left._min   = 5;
  ts::BWFSpec right;
  right._align = ts::BWFSpec::Align::RIGHT;
  right._min   = 5;
  ts::BWFSpec center;
  center._align = ts::BWFSpec::Align::CENTER;
  center._min   = 5;

  bwformat(bw, left, one);
  bwformat(bw, right, two);
  REQUIRE(bw.view() == "1        2");
  bwformat(bw, right, two);
  REQUIRE(bw.view() == "1        2    2");
  bwformat(bw, center, three_n);
  REQUIRE(bw.view() == "1        2    2 -3  ");
}

TEST_CASE("BWFormat floating", "[bwprint][bwformat]")
{
  ts::LocalBufferWriter<256> bw;
  ts::BWFSpec spec;

  bw.reduce(0);
  bw.print("{}", 3.14);
  REQUIRE(bw.view() == "3.14");
  bw.reduce(0);
  bw.print("{} {:.2} {:.0} ", 32.7, 32.7, 32.7);
  REQUIRE(bw.view() == "32.70 32.70 32 ");
  bw.reduce(0);
  bw.print("{} neg {:.3}", -123.2, -123.2);
  REQUIRE(bw.view() == "-123.20 neg -123.200");
  bw.reduce(0);
  bw.print("zero {} quarter {} half {} 3/4 {}", 0, 0.25, 0.50, 0.75);
  REQUIRE(bw.view() == "zero 0 quarter 0.25 half 0.50 3/4 0.75");
  bw.reduce(0);
  bw.print("long {:.11}", 64.9);
  REQUIRE(bw.view() == "long 64.90000000000");
  bw.reduce(0);

  double n   = 180.278;
  double neg = -238.47;
  bwformat(bw, spec, n);
  REQUIRE(bw.view() == "180.28");
  bw.reduce(0);
  bwformat(bw, spec, neg);
  REQUIRE(bw.view() == "-238.47");
  bw.reduce(0);

  spec._prec = 5;
  bwformat(bw, spec, n);
  REQUIRE(bw.view() == "180.27800");
  bw.reduce(0);
  bwformat(bw, spec, neg);
  REQUIRE(bw.view() == "-238.47000");
  bw.reduce(0);

  float f    = 1234;
  float fneg = -1;
  bwformat(bw, spec, f);
  REQUIRE(bw.view() == "1234");
  bw.reduce(0);
  bwformat(bw, spec, fneg);
  REQUIRE(bw.view() == "-1");
  bw.reduce(0);
  f          = 1234.5667;
  spec._prec = 4;
  bwformat(bw, spec, f);
  REQUIRE(bw.view() == "1234.5667");
  bw.reduce(0);

  bw << 1234 << .567;
  REQUIRE(bw.view() == "12340.57");
  bw.reduce(0);
  bw << f;
  REQUIRE(bw.view() == "1234.57");
  bw.reduce(0);
  bw << n;
  REQUIRE(bw.view() == "180.28");
  bw.reduce(0);
  bw << f << n;
  REQUIRE(bw.view() == "1234.57180.28");
  bw.reduce(0);

  double edge = 0.345;
  spec._prec  = 3;
  bwformat(bw, spec, edge);
  REQUIRE(bw.view() == "0.345");
  bw.reduce(0);
  edge = .1234;
  bwformat(bw, spec, edge);
  REQUIRE(bw.view() == "0.123");
  bw.reduce(0);
  edge = 1.0;
  bwformat(bw, spec, edge);
  REQUIRE(bw.view() == "1");
  bw.reduce(0);

  // alignment
  double first  = 1.23;
  double second = 2.35;
  double third  = -3.5;
  ts::BWFSpec left;
  left._align = ts::BWFSpec::Align::LEFT;
  left._min   = 5;
  ts::BWFSpec right;
  right._align = ts::BWFSpec::Align::RIGHT;
  right._min   = 5;
  ts::BWFSpec center;
  center._align = ts::BWFSpec::Align::CENTER;
  center._min   = 5;

  bwformat(bw, left, first);
  bwformat(bw, right, second);
  REQUIRE(bw.view() == "1.23  2.35");
  bwformat(bw, right, second);
  REQUIRE(bw.view() == "1.23  2.35 2.35");
  bwformat(bw, center, third);
  REQUIRE(bw.view() == "1.23  2.35 2.35-3.50");
  bw.reduce(0);

  double over = 1.4444444;
  ts::BWFSpec over_min;
  over_min._prec = 7;
  over_min._min  = 5;
  bwformat(bw, over_min, over);
  REQUIRE(bw.view() == "1.4444444");
  bw.reduce(0);

  // Edge
  bw.print("{}", (1.0 / 0.0));
  REQUIRE(bw.view() == "Inf");
  bw.reduce(0);

  double inf = std::numeric_limits<double>::infinity();
  bw.print("  {} ", inf);
  REQUIRE(bw.view() == "  Inf ");
  bw.reduce(0);

  double nan_1 = std::nan("1");
  bw.print("{} {}", nan_1, nan_1);
  REQUIRE(bw.view() == "NaN NaN");
  bw.reduce(0);

  double z = 0.0;
  bw.print("{}  ", z);
  REQUIRE(bw.view() == "0  ");
  bw.reduce(0);
}

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
  static constexpr ts::TextView fmt{FMT, strlen(FMT)};
  static constexpr ts::string_view text{"e99a18c428cb38d5f260853678922e03"_sv};
  ts::LocalBufferWriter<256> bw;

  ts::BWFSpec spec;

  bw.reduce(0);
  bw.print(fmt, -956, text);
  REQUIRE(bw.view() == "Format |-0x00003bc| 'e99a18c428cb38d5f260853678922e03'");

  start = std::chrono::high_resolution_clock::now();
  for (int i = 0; i < N_LOOPS; ++i) {
    bw.reduce(0);
    bw.print(fmt, -956, text);
  }
  delta = std::chrono::high_resolution_clock::now() - start;
  std::cout << "bw.print() " << delta.count() << "ns or " << std::chrono::duration_cast<std::chrono::milliseconds>(delta).count()
            << "ms" << std::endl;

  ts::BWFormat pre_fmt(fmt);
  start = std::chrono::high_resolution_clock::now();
  for (int i = 0; i < N_LOOPS; ++i) {
    bw.reduce(0);
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

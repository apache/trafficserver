// SPDX-License-Identifier: Apache-2.0
/** @file

    MemSpan unit tests.

*/

#include "catch.hpp"

#include <iostream>
#include <vector>

#include "swoc/MemSpan.h"
#include "swoc/TextView.h"
#include "swoc/MemArena.h"

using swoc::MemSpan;
using swoc::TextView;
using namespace swoc::literals;

TEST_CASE("MemSpan", "[libswoc][MemSpan]") {
  int32_t idx[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  char buff[1024];

  MemSpan<char> span(buff, sizeof(buff));
  MemSpan<char> left = span.prefix(512);
  memset(span, ' ');
  REQUIRE(left.size() == 512);
  REQUIRE(span.size() == 1024);
  span.remove_prefix(512);
  REQUIRE(span.size() == 512);
  REQUIRE(left.end() == span.begin());

  left.assign(buff, sizeof(buff));
  span = left.suffix(768);
  REQUIRE(span.size() == 768);
  left.remove_suffix(768);
  REQUIRE(left.end() == span.begin());
  REQUIRE(left.size() + span.size() == 1024);

  MemSpan<int32_t> idx_span(idx);
  REQUIRE(idx_span.size() == 11);
  REQUIRE(idx_span.data_size() == sizeof(idx));
  REQUIRE(idx_span.data() == idx);

  auto sp2 = idx_span.rebind<int16_t>();
  REQUIRE(sp2.data_size() == idx_span.data_size());
  REQUIRE(sp2.size() == 2 * idx_span.size());
  REQUIRE(sp2[0] == 0);
  REQUIRE(sp2[1] == 0);
  // exactly one of { le, be } must be true.
  bool le = sp2[2] == 1 && sp2[3] == 0;
  bool be = sp2[2] == 0 && sp2[3] == 1;
  REQUIRE(le != be);
  auto idx2 = sp2.rebind<int32_t>(); // still the same if converted back to original?
  REQUIRE(idx_span.is_same(idx2));

  // Verify attempts to rebind on non-integral sized arrays fails.
  span.assign(buff, 1022);
  REQUIRE(span.data_size() == 1022);
  REQUIRE(span.size() == 1022);
  auto vs = span.rebind<void>();
  REQUIRE_THROWS_AS(span.rebind<uint32_t>(), std::invalid_argument);
  REQUIRE_THROWS_AS(vs.rebind<uint32_t>(), std::invalid_argument);
  vs.rebind<void>(); // check for void -> void rebind compiling.
  REQUIRE_FALSE(std::is_const_v<decltype(vs)::value_type>);

  // Check for defaulting to a void rebind.
  auto vsv = span.rebind();
  REQUIRE(vsv.size() == 1022);
  // default rebind of non-const type should be non-const (i.e. void).
  REQUIRE_FALSE(std::is_const_v<decltype(vs)::value_type>);
  auto vcs = vs.rebind<void const>();
  REQUIRE(vcs.size() == 1022);
  REQUIRE(std::is_const_v<decltype(vcs)::value_type>);
  MemSpan<char const> char_cv{buff, 64};
  auto void_cv = char_cv.rebind();
  REQUIRE(std::is_const_v<decltype(void_cv)::value_type>);

  // Check for assignment to void.
  vs = span;
  REQUIRE(vs.size() == 1022);

  // Test C array constructors.
  MemSpan<char> a{buff};
  REQUIRE(a.size() == sizeof(buff));
  REQUIRE(a.data() == buff);
  float floats[] = {1.1, 2.2, 3.3, 4.4, 5.5};
  MemSpan<float> fspan{floats};
  REQUIRE(fspan.size() == 5);
  REQUIRE(fspan[3] == 4.4f);
  MemSpan<float> f2span{floats, floats + 5};
  REQUIRE(fspan.data() == f2span.data());
  REQUIRE(fspan.size() == f2span.size());
  REQUIRE(fspan.is_same(f2span));

  // Deduction guides for char because of there being so many choices.
  MemSpan da{buff};
  REQUIRE(a.size() == sizeof(buff));
  REQUIRE(a.data() == buff);

  unsigned char ucb[512];
  MemSpan ucspan{ucb};
  memset(ucspan, 0);
  REQUIRE(ucspan[0] == 0);
  REQUIRE(ucspan[511] == 0);
  REQUIRE(ucspan[111] == 0);
  REQUIRE(ucb[0] == 0);
  REQUIRE(ucb[511] == 0);
  ucspan.remove_suffix(1);
  ucspan.remove_prefix(1);
  memset(ucspan, '@');
  REQUIRE(ucspan[0] == '@');
  REQUIRE(ucspan[509] == '@');
  REQUIRE(ucb[0] == 0);
  REQUIRE(ucb[511] == 0);
  REQUIRE(ucb[510] == '@');
};

TEST_CASE("MemSpan modifiers", "[libswoc][MemSpan]") {
  std::string text{"Evil Dave Rulz"};
  char *pre  = text.data();
  char *post = text.data() + text.size();

  SECTION("Typed") {
    MemSpan span{text};

    REQUIRE(0 == memcmp(span.clip_prefix(5), MemSpan(pre, 5)));
    REQUIRE(0 == memcmp(span, MemSpan(pre + 5, text.size() - 5)));
    span.assign(text.data(), text.size());
    REQUIRE(0 == memcmp(span.clip_suffix(5), MemSpan(post - 5, 5)));
    REQUIRE(0 == memcmp(span, MemSpan(pre, text.size() - 5)));

    MemSpan s1{"Evil Dave Rulz"};
    REQUIRE(s1.size() == 14); // terminal nul is not in view.
    uint8_t bytes[]{5, 4, 3, 2, 1, 0};
    MemSpan s2{bytes};
    REQUIRE(s2.size() == sizeof(bytes)); // terminal nul is in view
  }

  SECTION("void") {
    MemSpan<void> span{text};

    REQUIRE(0 == memcmp(span.clip_prefix(5), MemSpan<void>(pre, 5)));
    REQUIRE(0 == memcmp(span, MemSpan<void>(pre + 5, text.size() - 5)));
    span.assign(text.data(), text.size());
    REQUIRE(0 == memcmp(span.clip_suffix(5), MemSpan<void>(post - 5, 5)));
    REQUIRE(0 == memcmp(span, MemSpan<void>(pre, text.size() - 5)));

    // By design, MemSpan<void> won't construct from a literal string because it's const.
    // MemSpan<void> s1{"Evil Dave Rulz"}; // Should not compile.

    uint8_t bytes[]{5, 4, 3, 2, 1, 0};
    MemSpan<void> s2{bytes};
    REQUIRE(s2.size() == sizeof(bytes)); // terminal nul is in view
  }

  SECTION("void const") {
    MemSpan<void const> span{text};

    REQUIRE(0 == memcmp(span.clip_prefix(5), MemSpan<void const>(pre, 5)));
    REQUIRE(0 == memcmp(span, MemSpan<void const>(pre + 5, text.size() - 5)));
    span.assign(text.data(), text.size());
    REQUIRE(0 == memcmp(span.clip_suffix(5), MemSpan<void const>(post - 5, 5)));
    REQUIRE(0 == memcmp(span, MemSpan<void const>(pre, text.size() - 5)));

    MemSpan<void const> s1{"Evil Dave Rulz"};
    REQUIRE(s1.size() == 14); // terminal nul is not in view.
    uint8_t bytes[]{5, 4, 3, 2, 1, 0};
    MemSpan<void const> s2{bytes};
    REQUIRE(s2.size() == sizeof(bytes)); // terminal nul is in view
  }
}

TEST_CASE("MemSpan construct", "[libswoc][MemSpan]") {
  static unsigned counter = 0;
  struct Thing {
    Thing(TextView s) : _s(s) { ++counter; }
    ~Thing() { --counter; }

    unsigned _n = 56;
    std::string _s;
  };

  char buff[sizeof(Thing) * 7];
  auto span{MemSpan(buff).rebind<Thing>()};

  span.make("default"_tv);
  REQUIRE(counter == span.length());
  REQUIRE(span[2]._s == "default");
  REQUIRE(span[4]._n == 56);
  span.destroy();
  REQUIRE(counter == 0);
}

TEST_CASE("MemSpan<void>", "[libswoc][MemSpan]") {
  TextView tv = "bike shed";
  char buff[1024];

  MemSpan<void> span(buff, sizeof(buff));
  MemSpan<void const> cspan(span);
  MemSpan<void const> ccspan(tv.data(), tv.size());
  CHECK_FALSE(cspan.is_same(ccspan));
  ccspan = span;

  //  auto bad_span = ccspan.rebind<uint8_t>(); // should not compile.

  auto left = span.prefix(512);
  REQUIRE(left.size() == 512);
  REQUIRE(span.size() == 1024);
  span.remove_prefix(512);
  REQUIRE(span.size() == 512);
  REQUIRE(left.data_end() == span.data());

  left.assign(buff, sizeof(buff));
  span = left.suffix(700);
  REQUIRE(span.size() == 700);
  left.remove_suffix(700);
  REQUIRE(left.data_end() == span.data());
  REQUIRE(left.size() + span.size() == 1024);

  MemSpan<void> a(buff, sizeof(buff));
  MemSpan<void> b;
  b = a.align<int>();
  REQUIRE(b.data() == a.data());
  REQUIRE(b.size() == a.size());

  b = a.suffix(a.size() - 2).align<int>();
  REQUIRE(b.data() != a.data());
  REQUIRE(b.size() != a.size());
  auto i = a.rebind<int>();
  REQUIRE(b.data() == i.data() + 1);

  b = a.suffix(a.size() - 2).align(alignof(int));
  REQUIRE(b.data() == i.data() + 1);
  REQUIRE(b.rebind<int>().size() == i.size() - 1);
};

TEST_CASE("MemSpan conversions", "[libswoc][MemSpan]") {
  std::array<int, 10> a1;
  std::string_view sv{"Evil Dave"};
  swoc::TextView tv{sv};
  std::string str{sv};
  auto const &ra1 = a1;
  auto ms1        = MemSpan<int>(a1); // construct from array
  auto ms2        = MemSpan(a1);      // construct from array, deduction guide
  REQUIRE(ms2.size() == a1.size());
  auto ms3 = MemSpan<int const>(ra1); // construct from const array
  REQUIRE(ms3.size() == ra1.size());
  [[maybe_unused]] auto ms4 = MemSpan(ra1); // construct from const array, deduction guided.
  // Construct a span of constant from a const ref to an array with non-const type.
  MemSpan<int const> ms5{ra1};
  REQUIRE(ms5.size() == ra1.size());
  // Construct a span of constant from a ref to an array with non-const type.
  MemSpan<int const> ms6{a1};

  MemSpan<void> va1{a1};
  REQUIRE(va1.size() == a1.size() * sizeof(*(a1.data())));
  MemSpan<void const> cva1{a1};
  REQUIRE(cva1.size() == a1.size() * sizeof(*(a1.data())));

  [[maybe_unused]] MemSpan<int const> c1 = ms1; // Conversion from T to T const.

  MemSpan<char const> c2{sv.data(), sv.size()};
  [[maybe_unused]] MemSpan<void const> vc2{c2};
  // Generic construction from STL containers.
  MemSpan<char const> c3{sv};
  [[maybe_unused]] MemSpan<char> c7{str};
  [[maybe_unused]] MemSpan<void> c4{str};
  auto const &cstr{str};
  MemSpan<char const> c8{cstr};
  REQUIRE(c8.size() == cstr.size());
  // [[maybe_unused]] MemSpan<char> c9{cstr}; // should not compile, const container to non-const span.

  [[maybe_unused]] MemSpan<void const> c5{str};
  [[maybe_unused]] MemSpan<void const> c6{sv};

  [[maybe_unused]] MemSpan c10{sv};
  [[maybe_unused]] MemSpan c11{tv};

  char const *args[] = {"alpha", "bravo", "charlie", "delta"};
  MemSpan<char const *> span_args{args};
  MemSpan<char const *> span2_args{span_args};
  REQUIRE(span_args.size() == 4);
  REQUIRE(span2_args.size() == 4);

  auto f = [&]() -> TextView { return sv; };
  MemSpan fs1{f()};
  auto fc = [&]() -> TextView const { return sv; };
  MemSpan fs2{fc()};
}

TEST_CASE("MemSpan arena", "[libswoc][MemSpan]") {
  swoc::MemArena a;

  struct Thing {
    size_t _n  = 0;
    void *_ptr = nullptr;
  };

  auto span         = a.alloc(sizeof(Thing)).rebind<Thing>();
  MemSpan<void> raw = span;
  REQUIRE(raw.size() == sizeof(Thing));
  MemSpan<void const> craw = raw;
  REQUIRE(raw.size() == craw.size());
  craw = span;
  REQUIRE(raw.size() == craw.size());

  REQUIRE(raw.rebind<Thing>().length() == 1);
}

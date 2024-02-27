/** @file

    MemArena unit tests.

    @section license License

    Licensed to the Apache Software Foundation (ASF) under one or more contributor license
    agreements.  See the NOTICE file distributed with this work for additional information regarding
    copyright ownership.  The ASF licenses this file to you under the Apache License, Version 2.0
    (the "License"); you may not use this file except in compliance with the License.  You may
    obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software distributed under the
    License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
    express or implied. See the License for the specific language governing permissions and
    limitations under the License.
*/

#include <array>
#include <string_view>
#include <string>
#include <map>
#include <set>
#include <random>

#include "swoc/MemArena.h"
#include "swoc/TextView.h"
#include "catch.hpp"

using swoc::MemSpan;
using swoc::MemArena;
using swoc::FixedArena;
using std::string_view;
using swoc::TextView;
using namespace std::literals;

static constexpr std::string_view CHARS{"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789/."};
std::uniform_int_distribution<short> char_gen{0, short(CHARS.size() - 1)};
std::minstd_rand randu;

namespace {
TextView
localize(MemArena &arena, TextView const &view) {
  auto span = arena.alloc(view.size()).rebind<char>();
  memcpy(span, view);
  return span;
}
} // namespace

TEST_CASE("MemArena generic", "[libswoc][MemArena]") {
  swoc::MemArena arena{64};
  REQUIRE(arena.size() == 0);
  REQUIRE(arena.reserved_size() == 0);
  arena.alloc(0);
  REQUIRE(arena.size() == 0);
  REQUIRE(arena.reserved_size() >= 64);
  REQUIRE(arena.remaining() >= 64);

  swoc::MemSpan span1 = arena.alloc(32);
  REQUIRE(span1.size() == 32);
  REQUIRE(arena.remaining() >= 32);

  swoc::MemSpan span2 = arena.alloc(32);
  REQUIRE(span2.size() == 32);

  REQUIRE(span1.data() != span2.data());
  REQUIRE(arena.size() == 64);

  auto extent{arena.reserved_size()};
  span1 = arena.alloc(128);
  REQUIRE(extent < arena.reserved_size());

  arena.clear();
  arena.alloc(17);
  span1 = arena.alloc(16, 8);
  REQUIRE((uintptr_t(span1.data()) & 0x7) == 0);
  REQUIRE(span1.size() == 16);
  span2 = arena.alloc(16, 16);
  REQUIRE((uintptr_t(span2.data()) & 0xF) == 0);
  REQUIRE(span2.size() == 16);
  REQUIRE(span2.data() >= span1.data_end());
}

TEST_CASE("MemArena freeze and thaw", "[libswoc][MemArena]") {
  MemArena arena;
  MemSpan span1{arena.alloc(1024)};
  REQUIRE(span1.size() == 1024);
  REQUIRE(arena.size() == 1024);
  REQUIRE(arena.reserved_size() >= 1024);

  arena.freeze();

  REQUIRE(arena.size() == 0);
  REQUIRE(arena.allocated_size() == 1024);
  REQUIRE(arena.reserved_size() >= 1024);

  arena.thaw();
  REQUIRE(arena.size() == 0);
  REQUIRE(arena.allocated_size() == 0);
  REQUIRE(arena.reserved_size() == 0);

  span1 = arena.alloc(1024);
  arena.freeze();
  auto extent{arena.reserved_size()};
  arena.alloc(512);
  REQUIRE(arena.reserved_size() > extent); // new extent should be bigger.
  arena.thaw();
  REQUIRE(arena.size() == 512);
  REQUIRE(arena.reserved_size() >= 1024);

  arena.clear();
  REQUIRE(arena.size() == 0);
  REQUIRE(arena.reserved_size() == 0);

  span1 = arena.alloc(262144);
  arena.freeze();
  extent = arena.reserved_size();
  arena.alloc(512);
  REQUIRE(arena.reserved_size() > extent); // new extent should be bigger.
  arena.thaw();
  REQUIRE(arena.size() == 512);
  REQUIRE(arena.reserved_size() >= 262144);

  arena.clear();

  span1  = arena.alloc(262144);
  extent = arena.reserved_size();
  arena.freeze();
  for (int i = 0; i < 262144 / 512; ++i)
    arena.alloc(512);
  REQUIRE(arena.reserved_size() > extent); // Bigger while frozen memory is still around.
  arena.thaw();
  REQUIRE(arena.size() == 262144);
  REQUIRE(arena.reserved_size() == extent); // should be identical to before freeze.

  arena.alloc(512);
  arena.alloc(768);
  arena.freeze(32000);
  arena.thaw();
  arena.alloc(0);
  REQUIRE(arena.reserved_size() >= 32000);
  REQUIRE(arena.reserved_size() < 2 * 32000);
}

TEST_CASE("MemArena helper", "[libswoc][MemArena]") {
  struct Thing {
    int ten{10};
    std::string name{"name"};

    Thing() {}
    Thing(int x) : ten(x) {}
    Thing(std::string const &s) : name(s) {}
    Thing(int x, std::string_view s) : ten(x), name(s) {}
    Thing(std::string const &s, int x) : ten(x), name(s) {}
  };

  swoc::MemArena arena{256};
  REQUIRE(arena.size() == 0);
  swoc::MemSpan s = arena.alloc(56).rebind<char>();
  REQUIRE(arena.size() == 56);
  REQUIRE(arena.remaining() >= 200);
  void *ptr = s.begin();

  REQUIRE(arena.contains((char *)ptr));
  REQUIRE(arena.contains((char *)ptr + 100)); // even though span isn't this large, this pointer should still be in arena
  REQUIRE(!arena.contains((char *)ptr + 300));
  REQUIRE(!arena.contains((char *)ptr - 1));

  arena.freeze(128);
  REQUIRE(arena.contains((char *)ptr));
  REQUIRE(arena.contains((char *)ptr + 100));
  swoc::MemSpan s2 = arena.alloc(10).rebind<char>();
  void *ptr2       = s2.begin();
  REQUIRE(arena.contains((char *)ptr));
  REQUIRE(arena.contains((char *)ptr2));
  REQUIRE(arena.allocated_size() == 56 + 10);

  arena.thaw();
  REQUIRE(!arena.contains((char *)ptr));
  REQUIRE(arena.contains((char *)ptr2));

  Thing *thing_one{arena.make<Thing>()};

  REQUIRE(thing_one->ten == 10);
  REQUIRE(thing_one->name == "name");

  thing_one = arena.make<Thing>(17, "bob"sv);

  REQUIRE(thing_one->name == "bob");
  REQUIRE(thing_one->ten == 17);

  thing_one = arena.make<Thing>("Dave", 137);

  REQUIRE(thing_one->name == "Dave");
  REQUIRE(thing_one->ten == 137);

  thing_one = arena.make<Thing>(9999);

  REQUIRE(thing_one->ten == 9999);
  REQUIRE(thing_one->name == "name");

  thing_one = arena.make<Thing>("Persia");

  REQUIRE(thing_one->ten == 10);
  REQUIRE(thing_one->name == "Persia");
}

TEST_CASE("MemArena large alloc", "[libswoc][MemArena]") {
  swoc::MemArena arena;
  auto s = arena.alloc(4000);
  REQUIRE(s.size() == 4000);

  decltype(s) s_a[10];
  s_a[0] = arena.alloc(100);
  s_a[1] = arena.alloc(200);
  s_a[2] = arena.alloc(300);
  s_a[3] = arena.alloc(400);
  s_a[4] = arena.alloc(500);
  s_a[5] = arena.alloc(600);
  s_a[6] = arena.alloc(700);
  s_a[7] = arena.alloc(800);
  s_a[8] = arena.alloc(900);
  s_a[9] = arena.alloc(1000);

  // ensure none of the spans have any overlap in memory.
  for (int i = 0; i < 10; ++i) {
    s = s_a[i];
    for (int j = i + 1; j < 10; ++j) {
      REQUIRE(s_a[i].data() != s_a[j].data());
    }
  }
}

TEST_CASE("MemArena block allocation", "[libswoc][MemArena]") {
  swoc::MemArena arena{64};
  swoc::MemSpan s  = arena.alloc(32).rebind<char>();
  swoc::MemSpan s2 = arena.alloc(16).rebind<char>();
  swoc::MemSpan s3 = arena.alloc(16).rebind<char>();

  REQUIRE(s.size() == 32);
  REQUIRE(arena.allocated_size() == 64);

  REQUIRE(arena.contains((char *)s.begin()));
  REQUIRE(arena.contains((char *)s2.begin()));
  REQUIRE(arena.contains((char *)s3.begin()));

  REQUIRE((char *)s.begin() + 32 == (char *)s2.begin());
  REQUIRE((char *)s.begin() + 48 == (char *)s3.begin());
  REQUIRE((char *)s2.begin() + 16 == (char *)s3.begin());

  REQUIRE(s.end() == s2.begin());
  REQUIRE(s2.end() == s3.begin());
  REQUIRE((char *)s.begin() + 64 == s3.end());
}

TEST_CASE("MemArena full blocks", "[libswoc][MemArena]") {
  // couple of large allocations - should be exactly sized in the generation.
  size_t init_size = 32000;
  swoc::MemArena arena(init_size);

  MemSpan m1{arena.alloc(init_size - 64).rebind<uint8_t>()};
  MemSpan m2{arena.alloc(32000).rebind<unsigned char>()};
  MemSpan m3{arena.alloc(64000).rebind<char>()};

  REQUIRE(arena.remaining() >= 64);
  REQUIRE(arena.reserved_size() > 32000 + 64000 + init_size);
  REQUIRE(arena.reserved_size() < 2 * (32000 + 64000 + init_size));

  // Let's see if that memory is really there.
  memset(m1, 0xa5);
  memset(m2, 0xc2);
  memset(m3, 0x56);

  REQUIRE(std::all_of(m1.begin(), m1.end(), [](uint8_t c) { return 0xa5 == c; }));
  REQUIRE(std::all_of(m2.begin(), m2.end(), [](unsigned char c) { return 0xc2 == c; }));
  REQUIRE(std::all_of(m3.begin(), m3.end(), [](char c) { return 0x56 == c; }));
}

TEST_CASE("MemArena esoterica", "[libswoc][MemArena]") {
  MemArena a1;
  MemSpan<char> span;

  {
    MemArena alpha{1020};
    alpha.alloc(1);
    REQUIRE(alpha.remaining() >= 1019);
  }

  {
    MemArena alpha{4092};
    alpha.alloc(1);
    REQUIRE(alpha.remaining() >= 4091);
  }

  {
    MemArena alpha{4096};
    alpha.alloc(1);
    REQUIRE(alpha.remaining() >= 4095);
  }

  {
    MemArena a2{512};
    span = a2.alloc(128).rebind<char>();
    REQUIRE(a2.contains(span.data()));
    a1 = std::move(a2);
  }
  REQUIRE(a1.contains(span.data()));
  REQUIRE(a1.remaining() >= 384);

  {
    MemArena *arena = MemArena::construct_self_contained();
    arena->~MemArena();
  }

  {
    MemArena *arena = MemArena::construct_self_contained();
    MemArena::destroyer(arena);
  }

  {
    std::unique_ptr<MemArena, void (*)(MemArena *)> arena(MemArena::construct_self_contained(),
                                                          [](MemArena *arena) -> void { arena->~MemArena(); });
    static constexpr unsigned MAX = 512;
    std::uniform_int_distribution<unsigned> length_gen{6, MAX};
    char buffer[MAX];
    for (unsigned i = 0; i < 50; ++i) {
      auto n = length_gen(randu);
      for (unsigned k = 0; k < n; ++k) {
        buffer[k] = CHARS[char_gen(randu)];
      }
      localize(*arena, {buffer, n});
    }
    // Really, at this point just make sure there's no memory corruption on destruction.
  }

  { // as previous but delay construction. Use internal functor instead of a lambda.
    std::unique_ptr<MemArena, void (*)(MemArena *)> arena(nullptr, MemArena::destroyer);
    arena.reset(MemArena::construct_self_contained());
    static constexpr unsigned MAX = 512;
    std::uniform_int_distribution<unsigned> length_gen{6, MAX};
    char buffer[MAX];
    for (unsigned i = 0; i < 50; ++i) {
      auto n = length_gen(randu);
      for (unsigned k = 0; k < n; ++k) {
        buffer[k] = CHARS[char_gen(randu)];
      }
      localize(*arena, {buffer, n});
    }
    // Really, at this point just make sure there's no memory corruption on destruction.
  }

  { // Construct immediately in the unique pointer.
    MemArena::unique_ptr arena(MemArena::construct_self_contained(), MemArena::destroyer);
    static constexpr unsigned MAX = 512;
    std::uniform_int_distribution<unsigned> length_gen{6, MAX};
    char buffer[MAX];
    for (unsigned i = 0; i < 50; ++i) {
      auto n = length_gen(randu);
      for (unsigned k = 0; k < n; ++k) {
        buffer[k] = CHARS[char_gen(randu)];
      }
      localize(*arena, {buffer, n});
    }
    // Really, at this point just make sure there's no memory corruption on destruction.
  }

  { // as previous but delay construction. Use destroy_at instead of a lambda.
    MemArena::unique_ptr arena(nullptr, MemArena::destroyer);
    arena.reset(MemArena::construct_self_contained());
  }

  { // And what if the arena is never constructed?
    struct Thing {
      int x;
      std::unique_ptr<MemArena, void (*)(MemArena *)> arena{nullptr, std::destroy_at<MemArena>};
    } thing;
    thing.x = 56; // force access to instance.
  }
}

// --- temporary allocation
TEST_CASE("MemArena temporary", "[libswoc][MemArena][tmp]") {
  MemArena arena;
  std::vector<std::string_view> strings;

  static constexpr short MAX{8000};
  static constexpr int N{100};

  std::uniform_int_distribution<unsigned> length_gen{100, MAX};
  std::array<char, MAX> url;

  REQUIRE(arena.remaining() == 0);
  int i;
  unsigned max{0};
  for (i = 0; i < N; ++i) {
    auto n = length_gen(randu);
    max    = std::max(max, n);
    arena.require(n);
    auto span = arena.remnant().rebind<char>();
    if (span.size() < n)
      break;
    for (auto j = n; j > 0; --j) {
      span[j - 1] = url[j - 1] = CHARS[char_gen(randu)];
    }
    if (string_view{span.data(), n} != string_view{url.data(), n})
      break;
  }
  REQUIRE(i == N);            // did all the loops.
  REQUIRE(arena.size() == 0); // nothing actually allocated.
  // Hard to get a good value, but shouldn't be more than twice.
  REQUIRE(arena.reserved_size() < 2 * MAX);
  // Should be able to allocate at least the longest string without increasing the reserve size.
  unsigned rsize = arena.reserved_size();
  auto count     = max;
  std::uniform_int_distribution<unsigned> alloc_size{32, 128};
  while (count >= 128) { // at least the max distribution value
    auto k = alloc_size(randu);
    arena.alloc(k);
    count -= k;
  }
  REQUIRE(arena.reserved_size() == rsize);

  // Check for switching full blocks - calculate something like the total free space
  // and then try to allocate most of it without increasing the reserved size.
  count = rsize - (max - count);
  while (count >= 128) {
    auto k = alloc_size(randu);
    arena.alloc(k);
    count -= k;
  }
  REQUIRE(arena.reserved_size() == rsize);
}

TEST_CASE("FixedArena", "[libswoc][FixedArena]") {
  struct Thing {
    int x = 0;
    std::string name;
  };
  MemArena arena;
  FixedArena<Thing> fa{arena};

  [[maybe_unused]] Thing *one = fa.make();
  Thing *two                  = fa.make();
  two->x                      = 17;
  two->name                   = "Bob";
  fa.destroy(two);
  Thing *three = fa.make();
  REQUIRE(three == two);  // reused instance.
  REQUIRE(three->x == 0); // but reconstructed.
  REQUIRE(three->name.empty() == true);
  fa.destroy(three);
  std::array<Thing *, 17> things;
  for (auto &ptr : things) {
    ptr = fa.make();
  }
  two = things[things.size() - 1];
  for (auto &ptr : things) {
    fa.destroy(ptr);
  }
  three = fa.make();
  REQUIRE(two == three);
};

TEST_CASE("MemArena disard", "[libswoc][MemArena][discard]") {
  MemArena a{512};
  a.require(0); // force allocation.
  auto x = a.remaining();
  CHECK(x >= 512);
  auto span_1 = a.alloc(256);
  REQUIRE(a.remaining() == (x-256));
  a.discard(span_1);
  CHECK(a.remaining() == x);
  span_1 = a.alloc(100);
  auto span_2 = a.alloc(50);
  auto span_3 = a.alloc(50);
  CHECK(a.remaining() == x - 200);
  a.discard(span_3);
  CHECK(a.remaining() == x - 150);
  a.discard(span_1); // expected to fail.
  CHECK(a.remaining() == x - 150);
  a.discard(span_2);
  CHECK(a.remaining() == x - 100);

  a.discard(512);
  CHECK(a.remaining() == x);

  auto b1 = a.alloc(400);
  span_1 = a.alloc(x - 400);
  CHECK(a.remaining() == 0);
  CHECK(a.allocated_size() == x);

  span_2 = a.alloc(50);
  auto b2n = a.remaining();
  REQUIRE(b2n > 50);
  a.discard(span_2);
  REQUIRE(a.remaining() == b2n + span_2.size());
  REQUIRE(a.allocated_size() == span_1.size() + b1.size());
  a.discard(b1); // expected to fail.
  REQUIRE(a.remaining() == b2n + span_2.size());
  REQUIRE(a.allocated_size() == span_1.size() + b1.size());
  a.discard(span_1);
  REQUIRE(a.allocated_size() == b1.size());

  // Try to exercise "last full block" logic.
  a.clear(512);
  span_1 = a.alloc(a.remaining()); // fill first block.
  a.require(1);
  span_2 = a.alloc(a.remaining()); // fill another block.
  span_3 = a.alloc(100); // force another block.
  [[maybe_unused]] auto span_4 = a.alloc(a.remaining() - 100); // use most of it.
  auto span_5 = a.alloc(100); // fill it.
  REQUIRE(a.remaining() == 0);
  auto span_6 = a.alloc(100); // force 4th block.
  REQUIRE(a.remaining() > 0);
  a.discard(span_6); // make 4th block empty.
  REQUIRE(a.remaining() != 100);
  a.discard(span_5);
  REQUIRE(a.remaining() == 100); // 3rd block pull to front because it's no longer empty.
}

// RHEL 7 compatibility - std::pmr::string isn't available even though the header exists unless
// _GLIBCXX_USE_CXX11_ABI is defined and non-zero. It appears to always be defined for the RHEL
// toolsets, so if undefined that's OK.
#if __has_include(<memory_resource>) && ( !defined(_GLIBCXX_USE_CXX11_ABI) || _GLIBCXX_USE_CXX11_ABI)
struct PMR {
  bool *_flag;
  PMR(bool &flag) : _flag(&flag) {}
  PMR(PMR &&that) : _flag(that._flag) { that._flag = nullptr; }
  ~PMR() {
    if (_flag)
      *_flag = true;
  }
};

// External container using a MemArena.
TEST_CASE("PMR 1", "[libswoc][arena][pmr]") {
  static const std::pmr::string BRAVO{"bravo bravo bravo bravo"}; // avoid small string opt.
  using C       = std::pmr::map<std::pmr::string, PMR>;
  bool flags[3] = {false, false, false};
  MemArena arena;
  {
    C c{&arena};

    REQUIRE(arena.size() == 0);

    c.insert(C::value_type{"alpha", PMR(flags[0])});
    c.insert(C::value_type{BRAVO, PMR(flags[1])});
    c.insert(C::value_type{"charlie", PMR(flags[2])});

    REQUIRE(arena.size() > 0);

    auto spot = c.find(BRAVO);
    REQUIRE(spot != c.end());
    REQUIRE(arena.contains(&*spot));
    REQUIRE(arena.contains(spot->first.data()));
  }
  // Check the map was destructed.
  REQUIRE(flags[0] == true);
  REQUIRE(flags[1] == true);
  REQUIRE(flags[2] == true);
}

// Container inside MemArena, using the MemArena.
TEST_CASE("PMR 2", "[libswoc][arena][pmr]") {
  using C       = std::pmr::map<std::pmr::string, PMR>;
  bool flags[3] = {false, false, false};
  {
    static const std::pmr::string BRAVO{"bravo bravo bravo bravo"}; // avoid small string opt.
    MemArena arena;
    REQUIRE(arena.size() == 0);
    C *c      = arena.make<C>(&arena);
    auto base = arena.size();
    REQUIRE(base > 0);

    c->insert(C::value_type{"alpha", PMR(flags[0])});
    c->insert(C::value_type{BRAVO, PMR(flags[1])});
    c->insert(C::value_type{"charlie", PMR(flags[2])});

    REQUIRE(arena.size() > base);

    auto spot = c->find(BRAVO);
    REQUIRE(spot != c->end());
    REQUIRE(arena.contains(&*spot));
    REQUIRE(arena.contains(spot->first.data()));
  }
  // Check the map was not destructed.
  REQUIRE(flags[0] == false);
  REQUIRE(flags[1] == false);
  REQUIRE(flags[2] == false);
}

// Container inside MemArena, using the MemArena.
TEST_CASE("PMR 3", "[libswoc][arena][pmr]") {
  using C = std::pmr::set<std::pmr::string>;
  MemArena arena;
  REQUIRE(arena.size() == 0);
  C *c      = arena.make<C>(&arena);
  auto base = arena.size();
  REQUIRE(base > 0);

  c->insert("alpha");
  c->insert("bravo");
  c->insert("charlie");
  c->insert("delta");
  c->insert("foxtrot");
  c->insert("golf");

  REQUIRE(arena.size() > base);

  c->erase("charlie");
  c->erase("delta");
  c->erase("alpha");

  // This includes all of the strings.
  auto pre = arena.allocated_size();
  arena.freeze();
  // Copy the set into the arena.
  C *gc       = arena.make<C>(&arena);
  *gc         = *c;
  auto frozen = arena.allocated_size();
  REQUIRE(frozen > pre);
  // Sparse set should be in the frozen memory, and discarded.
  arena.thaw();
  auto post = arena.allocated_size();
  REQUIRE(frozen > post);
  REQUIRE(pre > post);
}

TEST_CASE("MemArena static", "[libswoc][MemArena][static]") {
  static constexpr size_t SIZE = 2048;
  std::byte buffer[SIZE];
  MemArena arena{
    {buffer, SIZE}
  };

  REQUIRE(arena.remaining() > 0);
  REQUIRE(arena.remaining() < SIZE);
  REQUIRE(arena.size() == 0);

  // Allocate something and make sure it's in the static area.
  auto span = arena.alloc(1024);
  REQUIRE(true == (buffer <= span.data() && span.data() < buffer + SIZE));
  span = arena.remnant(); // require the remnant to still be in the buffer.
  REQUIRE(true == (buffer <= span.data() && span.data() < buffer + SIZE));

  // This can't fit, must be somewhere other than the buffer.
  span = arena.alloc(SIZE);
  REQUIRE(false == (buffer <= span.data() && span.data() < buffer + SIZE));

  MemArena arena2{std::move(arena)};
  REQUIRE(arena2.size() > 0);

  arena2.freeze();
  arena2.thaw();

  REQUIRE(arena.size() == 0);
  REQUIRE(arena2.size() == 0);
  // Now let @a arena2 destruct.
}

#endif // has memory_resource header.

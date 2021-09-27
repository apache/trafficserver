/** @file

    MemArena unit tests.

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

#include <catch.hpp>

#include <string_view>
#include "tscore/MemArena.h"
using ts::MemSpan;
using ts::MemArena;
using namespace std::literals;

TEST_CASE("MemArena generic", "[libts][MemArena]")
{
  ts::MemArena arena{64};
  REQUIRE(arena.size() == 0);
  REQUIRE(arena.reserved_size() == 0);
  arena.alloc(0);
  REQUIRE(arena.size() == 0);
  REQUIRE(arena.reserved_size() >= 64);

  auto span1 = arena.alloc(32);
  REQUIRE(span1.size() == 32);

  auto span2 = arena.alloc(32);
  REQUIRE(span2.size() == 32);

  REQUIRE(span1.data() != span2.data());
  REQUIRE(arena.size() == 64);

  auto extent{arena.reserved_size()};
  span1 = arena.alloc(128);
  REQUIRE(extent < arena.reserved_size());
}

TEST_CASE("MemArena freeze and thaw", "[libts][MemArena]")
{
  MemArena arena;
  auto span1{arena.alloc(1024)};
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

TEST_CASE("MemArena helper", "[libts][MemArena]")
{
  struct Thing {
    int ten{10};
    std::string_view name{"name"};

    Thing() {}
    Thing(int const x) : ten(x) {}
    Thing(std::string_view const sv) : name(sv) {}
    Thing(int const x, std::string_view const sv) : ten(x), name(sv) {}
    Thing(std::string_view const sv, int const x) : ten(x), name(sv) {}
  };

  ts::MemArena arena{256};
  REQUIRE(arena.size() == 0);
  ts::MemSpan<char> s = arena.alloc(56).rebind<char>();
  REQUIRE(arena.size() == 56);
  void *ptr = s.begin();

  REQUIRE(arena.contains((char *)ptr));
  REQUIRE(arena.contains((char *)ptr + 100)); // even though span isn't this large, this pointer should still be in arena
  REQUIRE(!arena.contains((char *)ptr + 300));
  REQUIRE(!arena.contains((char *)ptr - 1));

  arena.freeze(128);
  REQUIRE(arena.contains((char *)ptr));
  REQUIRE(arena.contains((char *)ptr + 100));
  ts::MemSpan<char> s2 = arena.alloc(10).rebind<char>();
  void *ptr2           = s2.begin();
  REQUIRE(arena.contains((char *)ptr));
  REQUIRE(arena.contains((char *)ptr2));
  REQUIRE(arena.allocated_size() == 56 + 10);

  arena.thaw();
  REQUIRE(!arena.contains((char *)ptr));
  REQUIRE(arena.contains((char *)ptr2));

  Thing *thing_one{arena.make<Thing>()};

  REQUIRE(thing_one->ten == 10);
  REQUIRE(thing_one->name == "name");

  std::string_view const bob{"bob"};
  thing_one = arena.make<Thing>(17, bob);

  REQUIRE(thing_one->name == "bob");
  REQUIRE(thing_one->ten == 17);

  std::string const dave{"Dave"};
  thing_one = arena.make<Thing>(dave, 137);

  REQUIRE(thing_one->name == "Dave");
  REQUIRE(thing_one->ten == 137);

  thing_one = arena.make<Thing>(9999);

  REQUIRE(thing_one->ten == 9999);
  REQUIRE(thing_one->name == "name");

  std::string const persia{"Persia"};
  thing_one = arena.make<Thing>(persia);

  REQUIRE(thing_one->ten == 10);
  REQUIRE(thing_one->name == "Persia");
}

TEST_CASE("MemArena large alloc", "[libts][MemArena]")
{
  ts::MemArena arena;
  ts::MemSpan s = arena.alloc(4000);
  REQUIRE(s.size() == 4000);

  ts::MemSpan<void> s_a[10];
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

TEST_CASE("MemArena block allocation", "[libts][MemArena]")
{
  ts::MemArena arena{64};
  ts::MemSpan<char> s  = arena.alloc(32).rebind<char>();
  ts::MemSpan<char> s2 = arena.alloc(16).rebind<char>();
  ts::MemSpan<char> s3 = arena.alloc(16).rebind<char>();

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

TEST_CASE("MemArena full blocks", "[libts][MemArena]")
{
  // couple of large allocations - should be exactly sized in the generation.
  size_t init_size = 32000;
  ts::MemArena arena(init_size);

  MemSpan<char> m1{arena.alloc(init_size - 64).rebind<char>()};
  MemSpan<char> m2{arena.alloc(32000).rebind<char>()};
  MemSpan<char> m3{arena.alloc(64000).rebind<char>()};

  REQUIRE(arena.remaining() >= 64);
  REQUIRE(arena.reserved_size() > 32000 + 64000 + init_size);
  REQUIRE(arena.reserved_size() < 2 * (32000 + 64000 + init_size));

  // Let's see if that memory is really there.
  memset(m1.data(), 0xa5, m1.size());
  memset(m2.data(), 0xc2, m2.size());
  memset(m3.data(), 0x56, m3.size());

  REQUIRE(std::all_of(m1.begin(), m1.end(), [](uint8_t c) { return 0xa5 == c; }));
  REQUIRE(std::all_of(m2.begin(), m2.end(), [](uint8_t c) { return 0xc2 == c; }));
  REQUIRE(std::all_of(m3.begin(), m3.end(), [](uint8_t c) { return 0x56 == c; }));
}

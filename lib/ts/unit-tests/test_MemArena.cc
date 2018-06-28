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

#include <ts/MemArena.h>
using ts::MemSpan;
using ts::MemArena;

TEST_CASE("MemArena generic", "[libts][MemArena]")
{
  ts::MemArena arena{64};
  REQUIRE(arena.size() == 0);
  REQUIRE(arena.extent() >= 64);

  ts::MemSpan span1 = arena.alloc(32);
  REQUIRE(span1.size() == 32);

  ts::MemSpan span2 = arena.alloc(32);
  REQUIRE(span2.size() == 32);

  REQUIRE(span1.data() != span2.data());
  REQUIRE(arena.size() == 64);

  auto extent{arena.extent()};
  span1 = arena.alloc(128);
  REQUIRE(extent < arena.extent());
}

TEST_CASE("MemArena freeze and thaw", "[libts][MemArena]")
{
  MemArena arena;
  MemSpan span1{arena.alloc(1024)};
  REQUIRE(span1.size() == 1024);
  REQUIRE(arena.size() == 1024);

  arena.freeze();

  REQUIRE(arena.size() == 0);
  REQUIRE(arena.allocated_size() == 1024);
  REQUIRE(arena.extent() >= 1024);

  arena.thaw();
  REQUIRE(arena.size() == 0);
  REQUIRE(arena.extent() == 0);

  arena.reserve(2000);
  arena.alloc(512);
  arena.alloc(1024);
  REQUIRE(arena.extent() >= 1536);
  REQUIRE(arena.extent() < 3000);
  auto extent = arena.extent();

  arena.freeze();
  arena.alloc(512);
  REQUIRE(arena.extent() > extent); // new extent should be bigger.
  arena.thaw();
  REQUIRE(arena.size() == 512);
  REQUIRE(arena.extent() > 1536);

  arena.clear();
  REQUIRE(arena.size() == 0);
  REQUIRE(arena.extent() == 0);

  arena.alloc(512);
  arena.alloc(768);
  arena.freeze(32000);
  arena.thaw();
  arena.alloc(1);
  REQUIRE(arena.extent() >= 32000);
}

TEST_CASE("MemArena helper", "[libts][MemArena]")
{
  ts::MemArena arena{256};
  REQUIRE(arena.size() == 0);
  ts::MemSpan s = arena.alloc(56);
  REQUIRE(arena.size() == 56);
  void *ptr = s.begin();

  REQUIRE(arena.contains((char *)ptr));
  REQUIRE(arena.contains((char *)ptr + 100)); // even though span isn't this large, this pointer should still be in arena
  REQUIRE(!arena.contains((char *)ptr + 300));
  REQUIRE(!arena.contains((char *)ptr - 1));

  arena.freeze(128);
  REQUIRE(arena.contains((char *)ptr));
  REQUIRE(arena.contains((char *)ptr + 100));
  ts::MemSpan s2 = arena.alloc(10);
  void *ptr2     = s2.begin();
  REQUIRE(arena.contains((char *)ptr));
  REQUIRE(arena.contains((char *)ptr2));
  REQUIRE(arena.allocated_size() == 56 + 10);

  arena.thaw();
  REQUIRE(!arena.contains((char *)ptr));
  REQUIRE(arena.contains((char *)ptr2));
}

TEST_CASE("MemArena large alloc", "[libts][MemArena]")
{
  ts::MemArena arena;
  ts::MemSpan s = arena.alloc(4000);
  REQUIRE(s.size() == 4000);

  ts::MemSpan s_a[10];
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
  ts::MemSpan s  = arena.alloc(32);
  ts::MemSpan s2 = arena.alloc(16);
  ts::MemSpan s3 = arena.alloc(16);

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
  ts::MemArena arena;
  size_t init_size = 32000;

  arena.reserve(init_size);
  MemSpan m1{arena.alloc(init_size - 64)};
  MemSpan m2{arena.alloc(32000)};
  MemSpan m3{arena.alloc(64000)};

  REQUIRE(arena.remaining() >= 64);
  REQUIRE(arena.extent() > 32000 + 64000 + init_size);
  REQUIRE(arena.extent() < 2 * (32000 + 64000 + init_size));

  // Let's see if that memory is really there.
  memset(m1.data(), 0xa5, m1.size());
  memset(m2.data(), 0xc2, m2.size());
  memset(m3.data(), 0x56, m3.size());

  REQUIRE(std::all_of(m1.begin(), m1.end(), [](uint8_t c) { return 0xa5 == c; }));
  REQUIRE(std::all_of(m2.begin(), m2.end(), [](uint8_t c) { return 0xc2 == c; }));
  REQUIRE(std::all_of(m3.begin(), m3.end(), [](uint8_t c) { return 0x56 == c; }));
}

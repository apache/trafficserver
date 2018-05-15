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

TEST_CASE("MemArena generic", "[libts][MemArena]")
{
  ts::MemArena arena{64};
  REQUIRE(arena.size() == 0);
  ts::MemSpan span1 = arena.alloc(32);
  ts::MemSpan span2 = arena.alloc(32);

  REQUIRE(span1.size() == 32);
  REQUIRE(span2.size() == 32);
  REQUIRE(span1.data() != span2.data());

  arena.freeze();
  REQUIRE(arena.size() == 0);
  REQUIRE(arena.allocated_size() == 64);

  span1 = arena.alloc(64);
  REQUIRE(span1.size() == 64);
  REQUIRE(arena.size() == 64);
  arena.thaw();
  REQUIRE(arena.size() == 64);
  REQUIRE(arena.allocated_size() == 64);

  arena.freeze();
  span1 = arena.alloc(128);
  REQUIRE(span1.size() == 128);
  REQUIRE(arena.size() == 128);
  REQUIRE(arena.allocated_size() == 192);
  REQUIRE(arena.remaining() == 0);
  REQUIRE(arena.contains((char *)span1.data()));

  arena.thaw();
  REQUIRE(arena.size() == 128);
  REQUIRE(arena.remaining() == 0);
}

TEST_CASE("MemArena freeze and thaw", "[libts][MemArena]")
{
  ts::MemArena arena{64};
  arena.freeze();
  REQUIRE(arena.size() == 0);
  arena.alloc(64);
  REQUIRE(arena.size() == 64);
  arena.thaw();
  REQUIRE(arena.size() == 64);
  arena.freeze();
  arena.thaw();
  REQUIRE(arena.size() == 0);
  REQUIRE(arena.remaining() == 0);

  arena.alloc(1024);
  REQUIRE(arena.size() == 1024);
  arena.freeze();
  REQUIRE(arena.size() == 0);
  REQUIRE(arena.allocated_size() == 1024);
  REQUIRE(arena.extent() >= 1024);
  arena.thaw();
  REQUIRE(arena.size() == 0);
  REQUIRE(arena.extent() == 0);

  arena.freeze(64); // scale down
  arena.alloc(64);
  REQUIRE(arena.size() == 64);
  REQUIRE(arena.remaining() == 0);

  arena.clear();
  REQUIRE(arena.size() == 0);
  REQUIRE(arena.remaining() == 0);
  REQUIRE(arena.allocated_size() == 0);
}

TEST_CASE("MemArena helper", "[libts][MemArena]")
{
  ts::MemArena arena{256};
  REQUIRE(arena.size() == 0);
  REQUIRE(arena.remaining() == 256);
  ts::MemSpan s = arena.alloc(56);
  REQUIRE(arena.size() == 56);
  REQUIRE(arena.remaining() == 200);
  void *ptr = s.begin();

  REQUIRE(arena.contains((char *)ptr));
  REQUIRE(arena.contains((char *)ptr + 100)); // even though span isn't this large, this pointer should still be in arena
  REQUIRE(!arena.contains((char *)ptr + 300));
  REQUIRE(!arena.contains((char *)ptr - 1));
  REQUIRE(arena.contains((char *)ptr + 255));
  REQUIRE(!arena.contains((char *)ptr + 256));

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

  REQUIRE(arena.remaining() == 128 - 10);
  REQUIRE(arena.allocated_size() == 10);
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
  REQUIRE(arena.remaining() == 0);
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
  arena.alloc(init_size - 64);
  arena.alloc(32000); // should in its own box - exactly sized.
  arena.alloc(64000); // same here.

  REQUIRE(arena.remaining() >= 64);
  REQUIRE(arena.extent() > 32000 + 64000 + init_size);
}

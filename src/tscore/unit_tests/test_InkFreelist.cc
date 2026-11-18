/** @file

    Behavior and concurrency stress tests for InkFreeList

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

#include <catch2/catch_test_macros.hpp>

#include <tscore/ink_queue.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <thread>

namespace
{

// Deterministic per-thread PRNG so failures are reproducible.
struct XorShift {
  uint64_t state;

  explicit XorShift(uint64_t seed) : state(seed | 1) {}

  uint32_t
  next()
  {
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return static_cast<uint32_t>(state >> 32);
  }
};

constexpr int NUM_THREADS = 6;

} // end anonymous namespace

TEST_CASE("InkFreeList: concurrent new/free", "[libts][InkFreeList]")
{
  constexpr int      SLOTS    = 32;
  constexpr int      FL_OPS   = 50000;
  constexpr uint32_t OBJ_SIZE = 128;

  InkFreeList *f = ink_freelist_create("test_InkFreeList", OBJ_SIZE, 64, 8);

  auto worker = [&](int me) {
    XorShift rng(0xc2b2ae3d27d4eb4full * (me + 1));
    void    *slots[SLOTS] = {nullptr};

    for (int op = 0; op < FL_OPS; op++) {
      int i = rng.next() % SLOTS;
      if (slots[i] != nullptr) {
        ink_freelist_free(f, slots[i]);
        slots[i] = nullptr;
      } else {
        slots[i] = ink_freelist_new(f);
        memset(slots[i], me, OBJ_SIZE);
      }
    }
    for (auto &slot : slots) {
      if (slot != nullptr) {
        ink_freelist_free(f, slot);
      }
    }
  };

  std::vector<std::thread> threads;
  for (int t = 0; t < NUM_THREADS; t++) {
    threads.emplace_back(worker, t);
  }
  for (auto &t : threads) {
    t.join();
  }

  REQUIRE(f->used == 0);
}

TEST_CASE("Freelist", "[libts][InkFreeList]")
{
  // The alignment is deliberately larger than the cell size, so cells after
  // the first in a chunk are aligned only if the freelist rounds the cell
  // stride up to the alignment.
  constexpr std::uint32_t alignment{2 * sizeof(std::intptr_t)};

  // There is no error reporting for this routine. The allocation
  // is assumed to never fail. Free cells hold the freelist's next pointer,
  // so the cell type must be at least pointer-sized.
  InkFreeList *f{ink_freelist_create("test#1", sizeof(std::intptr_t), 1, alignment)};

  SECTION("Freelist allocates aligned pointers")
  {
    // A chunk's first cell is page-aligned regardless of the stride. Two
    // consecutive allocations from a fresh chunk are neighboring cells, so at
    // least one of them is at an odd index and would be misaligned by an
    // unrounded stride.
    void *a{ink_freelist_new(f)};
    void *b{ink_freelist_new(f)};

    CHECK(!(reinterpret_cast<std::uintptr_t>(a) & (alignment - 1)));
    CHECK(!(reinterpret_cast<std::uintptr_t>(b) & (alignment - 1)));

    ink_freelist_free(f, a);
    ink_freelist_free(f, b);
  }

  SECTION("Two new freelist allocations")
  {
    std::intptr_t *a{new (ink_freelist_new(f)) std::intptr_t{1}};
    std::intptr_t *b{new (ink_freelist_new(f)) std::intptr_t{2}};

    CHECK(((*a == 1) && (*b == 2)));

    ink_freelist_free(f, a);
    ink_freelist_free(f, b);
  }

  SECTION("Freelist stack behavior")
  {
    void *a{ink_freelist_new(f)};
    void *b{ink_freelist_new(f)};

    ink_freelist_free(f, a);
    ink_freelist_free(f, b);

    void *x{ink_freelist_new(f)};
    void *y{ink_freelist_new(f)};

    CHECK((a == y && b == x));

    ink_freelist_free(f, x);
    ink_freelist_free(f, y);
  }
}

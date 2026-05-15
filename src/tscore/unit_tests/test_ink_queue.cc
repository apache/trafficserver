/*

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

#define CATCH_CONFIG_ENABLE_BENCHMARKING
#include <catch2/catch_all.hpp>

#include <tscore/ink_queue.h>

#include <atomic>
#include <cstdint>
#include <thread>

TEST_CASE("Freelist", "[freelist]")
{
  // There is no error reporting for this routine. The allocation
  // is assumed to never fail.
  InkFreeList *f{ink_freelist_create("test#1", sizeof(std::int32_t), 1, alignof(std::int32_t))};

  SECTION("Freelist allocates aligned pointers")
  {
    InkFreeList *f{ink_freelist_create("test#1", sizeof(std::int32_t), 1, alignof(std::int32_t))};
    void        *addr{ink_freelist_new(f)};

    CHECK(!(reinterpret_cast<std::uintptr_t>(addr) & (alignof(std::int32_t) - 1)));

    ink_freelist_free(f, addr);
  }

  SECTION("Two new freelist allocations")
  {
    std::int32_t *a{new (ink_freelist_new(f)) std::int32_t{1}};
    std::int32_t *b{new (ink_freelist_new(f)) std::int32_t{2}};

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

    ink_freelist_free(f, a);
    ink_freelist_free(f, b);
  }
}

TEST_CASE("Empty atomic list", "[atomiclist]")
{
  InkAtomicList l;
  ink_atomiclist_init(&l, "test#1", 0);

  CHECK(nullptr == ink_atomiclist_pop(&l));
}

TEST_CASE("Popall from empty atomic list", "[atomiclist]")
{
  InkAtomicList l;
  ink_atomiclist_init(&l, "test#1", 0);

  CHECK(nullptr == ink_atomiclist_popall(&l));
}

TEST_CASE("Atomic list", "[atomiclist]")
{
  InkAtomicList l;
  ink_atomiclist_init(&l, "test#1", 0);

  // We allocate memory this way to ensure proper alignment for atomiclist.
  InkFreeList *f{ink_freelist_create("test#1", sizeof(std::int32_t), 1, alignof(std::int32_t))};

  SECTION("Atomic list becomes empty after push and pop")
  {
    ink_atomiclist_push(&l, ink_freelist_new(f));
    void *a{ink_atomiclist_pop(&l)};

    CHECK(nullptr == ink_atomiclist_pop(&l));

    ink_freelist_free(f, a);
  }

  SECTION("Atomic list stack behavior")
  {
    void *a{ink_freelist_new(f)};
    void *b{ink_freelist_new(f)};

    ink_atomiclist_push(&l, a);
    ink_atomiclist_push(&l, b);

    void *x{ink_atomiclist_pop(&l)};
    void *y{ink_atomiclist_pop(&l)};

    CHECK((a == y && b == x));

    ink_freelist_free(f, a);
    ink_freelist_free(f, b);
  }

  SECTION("Atomic list remove head")
  {
    void *a{ink_freelist_new(f)};
    void *b{ink_freelist_new(f)};

    ink_atomiclist_push(&l, a);
    ink_atomiclist_push(&l, b);

    CHECK(b == ink_atomiclist_remove(&l, b));

    ink_freelist_free(f, a);
    ink_freelist_free(f, b);
  }

  SECTION("Atomic list remove tail")
  {
    void *a{ink_freelist_new(f)};
    void *b{ink_freelist_new(f)};

    ink_atomiclist_push(&l, a);
    ink_atomiclist_push(&l, b);

    CHECK(a == ink_atomiclist_remove(&l, a));

    ink_freelist_free(f, a);
    ink_freelist_free(f, b);
  }

  SECTION("Atomic list becomes empty after popall")
  {
    void *a{ink_freelist_new(f)};
    void *b{ink_freelist_new(f)};

    ink_atomiclist_push(&l, a);
    ink_atomiclist_push(&l, b);

    ink_atomiclist_popall(&l);
    CHECK(nullptr == ink_atomiclist_popall(&l));

    ink_freelist_free(f, a);
    ink_freelist_free(f, b);
  }

  SECTION("Atomic list popall behavior")
  {
    void *a{ink_freelist_new(f)};
    void *b{ink_freelist_new(f)};

    ink_atomiclist_push(&l, a);
    ink_atomiclist_push(&l, b);

    head_p *head{reinterpret_cast<head_p *>(ink_atomiclist_popall(&l))};
    head_p *tail{reinterpret_cast<head_p *>(FREELIST_POINTER(*head))};

    CHECK(((head == b) && (tail == a)));

    ink_freelist_free(f, a);
    ink_freelist_free(f, b);
  }
}

TEST_CASE("Freelist benchmarks", "[freelist][bench]")
{
  BENCHMARK_ADVANCED("Single threaded alloc")(Catch::Benchmark::Chronometer meter)
  {
    InkFreeList *f{ink_freelist_create("test#1", sizeof(std::int32_t), 4, alignof(std::int32_t))};

    ink_freelist_new(f);

    meter.measure([&]() { return ink_freelist_new(f); });
  };

  BENCHMARK_ADVANCED("Single threaded free")(Catch::Benchmark::Chronometer meter)
  {
    InkFreeList *f{ink_freelist_create("test#1", sizeof(std::int32_t), 4, alignof(std::int32_t))};

    std::vector<void *> ptrs;
    ptrs.resize(meter.runs());
    for (auto &x : ptrs) {
      x = ink_freelist_new(f);
    }

    meter.measure([&](int i) { return ink_freelist_free(f, ptrs[i]); });
  };

  /*
  BENCHMARK_ADVANCED("yay")(Catch::Benchmark::Chronometer meter)
  {
    InkFreeList *f{ink_freelist_create("yay", 4, 8, 16)};

    std::atomic<bool> done(false);

    auto const use_memory{[&]() {
      void         *storage{ink_freelist_new(f)};
      std::int32_t *n{new (storage) std::int32_t{}};
      *n = 10;
      ink_freelist_free(f, n);
    }};

    auto const distract{[&]() {
      while (!done) {
        use_memory();
      }
    }};

    std::jthread contender{distract};

    meter.measure([&]() {
      for (int i{0}; i < 10; ++i) {
        use_memory();
      }
    });

    done = true;
  };
  */
}

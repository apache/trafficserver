/** @file

    Freelist and Atomiclist tests

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

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>
#include <thread>
#include <vector>

// Chunk-seeded cells on the free-chain at process exit must not appear
// as direct leaks. The one issued cell is reachable via the registered root
// region; the remaining chunk_size-1 seeded cells are also covered because
// the entire chunk is a registered LSan root region.
#ifdef __SANITIZE_ADDRESS__
#define TS_ASAN_ENABLED
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define TS_ASAN_ENABLED
#endif
#endif

#ifdef TS_ASAN_ENABLED
TEST_CASE("FreelistChunkSeededCellsNotDirectLeak", "[freelist][lsan]")
{
  InkFreeList *f{ink_freelist_create("lsan_seeded", 8, 8, 8)};

  // Force the first chunk allocation. Do not return this cell — intentionally
  // held to represent an "in-use" cell so LSan sees it reachable via the
  // root region scan rather than as a dropped pointer.
  (void)ink_freelist_new(f);

  // The remaining chunk_size-1 seeded cells are on the free-chain. At process
  // exit LSan must classify them as "still reachable" through the registered
  // root region, not as direct leaks.
  CHECK(true);
}

// Caller-returned cells on the free-chain at process exit must not
// appear as direct leaks.
TEST_CASE("FreelistReturnedCellsNotDirectLeak", "[freelist][lsan]")
{
  InkFreeList *f{ink_freelist_create("lsan_returned", 8, 8, 8)};

  // Issue several cells then return them all via single-item free.
  void *a{ink_freelist_new(f)};
  void *b{ink_freelist_new(f)};
  void *c{ink_freelist_new(f)};
  void *d{ink_freelist_new(f)};

  ink_freelist_free(f, a);
  ink_freelist_free(f, b);
  ink_freelist_free(f, c);
  ink_freelist_free(f, d);

  // All four returned cells are now on the free-chain. At process exit LSan
  // must classify them as "still reachable" through the registered root
  // region, not as direct leaks.
  CHECK(true);
}
#endif // TS_ASAN_ENABLED

TEST_CASE("Freelist", "[freelist]")
{
  // There is no error reporting for this routine. The allocation
  // is assumed to never fail.
  InkFreeList *f{ink_freelist_create("test#1", sizeof(std::int32_t), 1, alignof(std::int32_t))};

  SECTION("Freelist allocates aligned pointers")
  {
    void *addr{ink_freelist_new(f)};

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
  struct test_type {
    std::int32_t i;
    void        *next;
  };

  InkAtomicList l;
  ink_atomiclist_init(&l, "test#1", offsetof(test_type, next));

  // We allocate memory this way to ensure proper alignment for atomiclist.
  InkFreeList *f{ink_freelist_create("test#1", sizeof(test_type), 1, alignof(test_type))};

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

    void  *head{ink_atomiclist_popall(&l)};
    void **head_{reinterpret_cast<void **>(reinterpret_cast<unsigned char *>(head) + l.offset)};
    void  *tail{*head_};

    CHECK(((head == b) && (tail == a)));

    ink_freelist_free(f, a);
    ink_freelist_free(f, b);
  }
}

/** @file

  Concurrency stress tests for InkAtomicList and InkFreeList.

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

#include <atomic>
#include <cstdint>
#include <cstring>
#include <thread>
#include <vector>

#include "tscore/ink_queue.h"

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

struct Item {
  Item            *next = nullptr;
  std::atomic<int> in_hand{0};
  uint32_t         owner  = 0;
  uint32_t         serial = 0;
  uint32_t         check  = 0;
};

constexpr int NUM_LISTS        = 8;
constexpr int NUM_THREADS      = 6;
constexpr int ITEMS_PER_THREAD = 2000;
constexpr int OPS_PER_THREAD   = 100000;

// Claim exclusive ownership of a popped item. A second concurrent claim means
// the same item was reachable from two places, i.e. the list is corrupt.
bool
claim(Item *item)
{
  return item->in_hand.exchange(1, std::memory_order_acq_rel) == 0;
}

void
release(Item *item)
{
  item->in_hand.store(0, std::memory_order_release);
}

} // end anonymous namespace

TEST_CASE("InkAtomicList: concurrent push/pop/popall conserves items", "[libts][InkAtomicList]")
{
  InkAtomicList     lists[NUM_LISTS];
  std::vector<Item> items(static_cast<size_t>(NUM_THREADS) * ITEMS_PER_THREAD);

  for (int i = 0; i < NUM_LISTS; i++) {
    ink_atomiclist_init(&lists[i], "test_InkAtomicList", offsetof(Item, next));
  }

  std::atomic<int> claim_failures{0};
  std::atomic<int> check_failures{0};

  auto worker = [&](int me) {
    XorShift rng(0x9e3779b97f4a7c15ull * (me + 1));

    for (int k = 0; k < ITEMS_PER_THREAD; k++) {
      Item *item   = &items[static_cast<size_t>(me) * ITEMS_PER_THREAD + k];
      item->owner  = me;
      item->serial = k;
      item->check  = item->owner ^ item->serial ^ 0xdeadbeef;
      ink_atomiclist_push(&lists[k % NUM_LISTS], item);
    }

    for (int op = 0; op < OPS_PER_THREAD; op++) {
      InkAtomicList *l = &lists[rng.next() % NUM_LISTS];

      if ((op & 1023) == 0) {
        // Drain a whole list and scatter it back.
        Item *chain = static_cast<Item *>(ink_atomiclist_popall(l));
        while (chain != nullptr) {
          Item *next_item = chain->next;
          if (!claim(chain)) {
            claim_failures++;
          }
          if (chain->check != (chain->owner ^ chain->serial ^ 0xdeadbeef)) {
            check_failures++;
          }
          release(chain);
          ink_atomiclist_push(&lists[rng.next() % NUM_LISTS], chain);
          chain = next_item;
        }
      } else {
        Item *item = static_cast<Item *>(ink_atomiclist_pop(l));
        if (item == nullptr) {
          continue;
        }
        if (!claim(item)) {
          claim_failures++;
        }
        if (item->check != (item->owner ^ item->serial ^ 0xdeadbeef)) {
          check_failures++;
        }
        release(item);
        ink_atomiclist_push(&lists[rng.next() % NUM_LISTS], item);
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

  REQUIRE(claim_failures == 0);
  REQUIRE(check_failures == 0);

  // Every item must be reachable exactly once across all lists.
  size_t drained = 0;
  for (int i = 0; i < NUM_LISTS; i++) {
    Item *chain = static_cast<Item *>(ink_atomiclist_popall(&lists[i]));
    while (chain != nullptr) {
      REQUIRE(claim(chain));
      REQUIRE(chain->check == (chain->owner ^ chain->serial ^ 0xdeadbeef));
      drained++;
      chain = chain->next;
    }
  }
  REQUIRE(drained == items.size());
}

TEST_CASE("InkAtomicList: remove", "[libts][InkAtomicList]")
{
  InkAtomicList l;
  Item          items[3];

  ink_atomiclist_init(&l, "test_InkAtomicList_remove", offsetof(Item, next));
  for (auto &item : items) {
    ink_atomiclist_push(&l, &item);
  }

  // Remove from the middle, the head, then a missing item.
  REQUIRE(ink_atomiclist_remove(&l, &items[1]) == &items[1]);
  REQUIRE(ink_atomiclist_remove(&l, &items[2]) == &items[2]);
  REQUIRE(ink_atomiclist_remove(&l, &items[1]) == nullptr);
  REQUIRE(ink_atomiclist_pop(&l) == &items[0]);
  REQUIRE(INK_ATOMICLIST_EMPTY(l));
}

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

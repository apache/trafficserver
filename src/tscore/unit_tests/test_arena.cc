/** @file

  A brief file description

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

/****************************************************************************

   test_arena.cc

   Description:

   A short test program intended to be used with Purify to detect problems
   with the arena code


 ****************************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "tscore/Arena.h"
#include <cstdio>
#include <memory>

void
fill_test_data(char *ptr, int size, int seed)
{
  char a = 'a' + (seed % 52);

  for (int i = 0; i < size; i++) {
    ptr[i] = a;
    a      = (a + 1) % 52;
  }
}

TEST_CASE("test arena", "[libts][arena]")
{
  const int           sizes_to_test   = 12;
  const int           regions_to_test = 1024 * 2;
  std::vector<char *> test_regions{regions_to_test};
  auto                a = std::make_unique<Arena>();

  for (int i = 0; i < sizes_to_test; i++) {
    int test_size = 1 << i;

    // Allocate and fill the array
    int j = 0;
    for (j = 0; j < regions_to_test; j++) {
      test_regions[j] = static_cast<char *>(a->alloc(test_size));
      fill_test_data(test_regions[j], test_size, j);
    }

    int failures = 0;
    // Now check to make sure the data is correct
    for (j = 0; j < regions_to_test; j++) {
      char a = 'a' + (j % 52);
      for (int k = 0; k < test_size; k++) {
        if (test_regions[j][k] != a) {
          failures++;
        }
        a = (a + 1) % 52;
      }
    }
    REQUIRE(failures == 0);
    // Now free the regions
    for (j = 0; j < regions_to_test; j++) {
      a->free(test_regions[j], test_size);
    }

    a->reset();
  }
}

// Arena::free() only rewinds when the freed range ends exactly at the block's
// water level, so the order in which allocations are released decides whether
// the space comes back. Callers that free out of order silently keep the arena
// growing, which matters for the long-lived per-connection arenas in HPACK and
// QPACK.
//
// Arena has no water level accessor. The address str_alloc() returns is the
// observable proxy: if a release rewound the block, the next allocation of the
// same size comes back at the same address.
//
// Note the warm-up loops below. Arena::free() walks the block list with
// `while (b->next)` and so never inspects the last block, which means nothing
// rewinds while the arena still holds a single block. The loops push the arena
// past that first block so the behaviour under test is reachable at all.

TEST_CASE("arena releases the most recent allocation", "[libts][arena]")
{
  auto sizes = {size_t{1}, size_t{16}, size_t{127}, size_t{128}, size_t{129}, size_t{1000}, size_t{4096}, size_t{65535}};

  for (auto size : sizes) {
    Arena arena;

    for (int i = 0; i < 40; i++) {
      arena.str_alloc(64);
    }

    char *first = arena.str_alloc(size);

    REQUIRE(arena.str_length(first) == size);

    arena.str_free(first);

    REQUIRE(static_cast<void *>(arena.str_alloc(size)) == static_cast<void *>(first));
  }
}

TEST_CASE("arena reclaims two allocations freed in reverse order", "[libts][arena]")
{
  Arena arena;

  for (int i = 0; i < 40; i++) {
    arena.str_alloc(64);
  }

  char *name_first = nullptr;

  for (int i = 0; i < 100; i++) {
    char *name  = arena.str_alloc(20);
    char *value = arena.str_alloc(60);

    if (i == 0) {
      name_first = name;
    }

    arena.str_free(value);
    arena.str_free(name);

    REQUIRE(static_cast<void *>(name) == static_cast<void *>(name_first));
  }
}

TEST_CASE("arena does not reclaim allocations freed in allocation order", "[libts][arena]")
{
  Arena arena;

  for (int i = 0; i < 40; i++) {
    arena.str_alloc(64);
  }

  char *name  = arena.str_alloc(20);
  char *value = arena.str_alloc(60);

  // name does not end at the water level while value is still outstanding, so
  // this free is a no-op and only value's space comes back.
  arena.str_free(name);
  arena.str_free(value);

  CHECK(static_cast<void *>(arena.str_alloc(20)) != static_cast<void *>(name));
}

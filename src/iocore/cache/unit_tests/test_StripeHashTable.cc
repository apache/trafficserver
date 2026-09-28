/** @file

  Catch-based unit tests for the stripe assignment hash table.

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

#include "main.h"
#include "test_doubles.h"

#include "../P_CacheHosting.h"
#include "../P_CacheInternal.h"
#include "../StripeSM.h"

#include <array>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <memory>

int  cache_vols           = 1;
bool reuse_existing_cache = false;

namespace
{

constexpr int      NUM_STRIPES  = 26;
constexpr int      SAMPLE_IDX   = 16;
constexpr uint64_t DIR_SKIP     = 8192;
constexpr uint64_t STRIPE_BYTES = 1024ULL * 1024 * 1024 * 911;

void
set_hash_id(StripeSM &stripe, int idx)
{
  char buf[64];

  snprintf(buf, sizeof(buf), "/dev/sd%c %" PRIu64 ":%" PRIu64, 'a' + idx, DIR_SKIP, static_cast<uint64_t>(stripe.len));
  CryptoContext().hash_immediate(stripe.hash_id, buf, strlen(buf));
}

} // namespace

// build_vol_hash_table gives each slot to the stripe owning the nearest random point, and a stripe's points depend only
// on its own hash_id and len, so resizing one stripe cannot move a slot between two other stripes.
TEST_CASE("Resizing one stripe moves hash table slots only to or from that stripe", "[cache][hosting]")
{
  CacheDisk disk;

  init_disk(disk);

  std::array<std::unique_ptr<StripeSM>, NUM_STRIPES> stripes;
  StripeSM                                          *stripe_ptrs[NUM_STRIPES];

  for (int i = 0; i < NUM_STRIPES; ++i) {
    stripes[i]     = std::make_unique<StripeSM>(&disk, static_cast<off_t>(STRIPE_BYTES / STORE_BLOCK_SIZE), 0);
    stripe_ptrs[i] = stripes[i].get();
    set_hash_id(*stripes[i], i);
  }

  CacheHostRecord before;

  before.stripes  = stripe_ptrs;
  before.num_vols = NUM_STRIPES;
  build_vol_hash_table(&before);

  StripeSM &sample = *stripes[SAMPLE_IDX];

  sample.len = 1024ULL * 1024 * 1024 * (1024 + 128);
  set_hash_id(sample, SAMPLE_IDX);

  CacheHostRecord after;

  after.stripes  = stripe_ptrs;
  after.num_vols = NUM_STRIPES;
  build_vol_hash_table(&after);

  REQUIRE(before.vol_hash_table != nullptr);
  REQUIRE(after.vol_hash_table != nullptr);

  int slots_before         = 0;
  int slots_after          = 0;
  int moved                = 0;
  int moved_between_others = 0;

  for (int i = 0; i < STRIPE_HASH_TABLE_SIZE; ++i) {
    unsigned short const was = before.vol_hash_table[i];
    unsigned short const is  = after.vol_hash_table[i];

    if (was == SAMPLE_IDX) {
      ++slots_before;
    }
    if (is == SAMPLE_IDX) {
      ++slots_after;
    }
    if (was != is) {
      ++moved;
      if (was != SAMPLE_IDX && is != SAMPLE_IDX) {
        ++moved_between_others;
      }
    }
  }

  INFO("moved " << moved << " of " << STRIPE_HASH_TABLE_SIZE << " slots; sample owned " << slots_before << ", now " << slots_after);
  CHECK(moved_between_others == 0);
  CHECK(slots_after > slots_before);

  // stripe_ptrs is a stack array; ~CacheHostRecord would ats_free() it.
  before.stripes = nullptr;
  after.stripes  = nullptr;
}

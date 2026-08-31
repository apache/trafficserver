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

#include "iocore/eventsystem/Event.h"
#include "main.h"

#include "../P_CacheDir.h"
#include "../P_CacheInternal.h"

#include "tscore/Random.h"

// Required by main.h
int  cache_vols           = 1;
bool reuse_existing_cache = false;

namespace
{
DbgCtl dbg_ctl_cache_dir_test{"cache_dir_test"};

unsigned int regress_rand_seed = 0;

void
regress_rand_init(unsigned int i)
{
  regress_rand_seed = i;
}

static void
regress_rand_CacheKey(CacheKey *key)
{
  unsigned int *x = reinterpret_cast<unsigned int *>(key);
  for (int i = 0; i < 4; i++) {
    x[i] = next_rand(&regress_rand_seed);
  }
}

void
dir_corrupt_bucket(Dir *b, int s, StripeSM *stripe)
{
  int  l   = (static_cast<int>(stripe->directory.bucket_length(b, s) * ts::Random::drandom()));
  Dir *e   = b;
  Dir *seg = stripe->directory.get_segment(s);
  for (int i = 0; i < l; i++) {
    ink_release_assert(e);
    e = next_dir(e, seg);
  }
  ink_release_assert(e);
  dir_set_next(e, dir_to_offset(e, seg));
}

} // namespace

class CacheDirTest : public CacheInit
{
public:
  int
  cache_init_success_callback(int /* event ATS_UNUSED */, void * /* e ATS_UNUSED */) override
  {
    ink_hrtime ttime;

    REQUIRE(CacheProcessor::IsCacheEnabled() == CacheInitState::INITIALIZED);
    REQUIRE(gnstripes >= 1);

    StripeSM *stripe = gstripes[0];
    EThread  *thread = this_ethread();
    MUTEX_TRY_LOCK(lock, stripe->mutex, thread);
    if (!lock.is_locked()) {
      CONT_SCHED_LOCK_RETRY(this);
      return EVENT_DONE;
    }

    stripe->clear_dir();

    // coverity[var_decl]
    Dir dir;
    dir_clear(&dir);
    dir_set_phase(&dir, 0);
    dir_set_head(&dir, true);
    dir_set_offset(&dir, 1);

    stripe->directory.header->agg_pos = stripe->directory.header->write_pos += 1024;

    CacheKey key;
    rand_CacheKey(&key);

    int  s   = key.slice32(0) % stripe->directory.segments, i, j;
    Dir *seg = stripe->directory.get_segment(s);

    // test insert
    int inserted = 0;
    int free     = stripe->directory.freelist_length(s);
    int n        = free;
    while (n--) {
      if (!stripe->directory.insert(&key, stripe, &dir)) {
        break;
      }
      inserted++;
    }
    CHECK(static_cast<unsigned int>(inserted - free) <= 1);

    // test delete
    for (i = 0; i < stripe->directory.buckets; i++) {
      for (j = 0; j < DIR_DEPTH; j++) {
        dir_set_offset(dir_bucket_row(dir_bucket(i, seg), j), 0); // delete
      }
    }
    stripe->directory.clean_segment(s, stripe);
    int newfree = stripe->directory.freelist_length(s);
    CHECK(static_cast<unsigned int>(newfree - free) <= 1);

    // test insert-delete
    regress_rand_init(13);
    ttime = ink_get_hrtime();
    for (i = 0; i < newfree; i++) {
      regress_rand_CacheKey(&key);
      stripe->directory.insert(&key, stripe, &dir);
    }
    uint64_t us = (ink_get_hrtime() - ttime) / HRTIME_USECOND;
    // On windows us is sometimes 0. I don't know why.
    // printout the insert rate only if its not 0
    if (us) {
      Dbg(dbg_ctl_cache_dir_test, "insert rate = %d / second", static_cast<int>((newfree * static_cast<uint64_t>(1000000)) / us));
    }
    regress_rand_init(13);
    ttime = ink_get_hrtime();
    for (i = 0; i < newfree; i++) {
      Dir *last_collision = nullptr;
      regress_rand_CacheKey(&key);
      CHECK(stripe->directory.probe(&key, stripe, &dir, &last_collision));
    }
    us = (ink_get_hrtime() - ttime) / HRTIME_USECOND;
    // On windows us is sometimes 0. I don't know why.
    // printout the probe rate only if its not 0
    if (us) {
      Dbg(dbg_ctl_cache_dir_test, "probe rate = %d / second", static_cast<int>((newfree * static_cast<uint64_t>(1000000)) / us));
    }

    for (int c = 0; c < stripe->directory.entries() * 0.75; c++) {
      regress_rand_CacheKey(&key);
      stripe->directory.insert(&key, stripe, &dir);
    }

    Dir dir1;
    memset(static_cast<void *>(&dir1), 0, sizeof(dir1));
    int s1, b1;

    Dbg(dbg_ctl_cache_dir_test, "corrupt_bucket test");
    for (int ntimes = 0; ntimes < 10; ntimes++) {
      // Reset every iteration: Directory::check() fails from the first corruption onward, so without this only the
      // first iteration would assert anything.
      stripe->clear_dir();

      rand_CacheKey(&key);
      s1 = key.slice32(0) % stripe->directory.segments;
      b1 = key.slice32(1) % stripe->directory.buckets;

      // Valid entries, so probe() below walks past them instead of deleting them as it goes.
      stripe->directory.header->agg_pos = stripe->directory.header->write_pos += 1024;
      dir_clear(&dir1);
      dir_set_offset(&dir1, 1);
      REQUIRE(stripe->dir_valid(&dir1));
      for (int i = 0; i < 5; i++) {
        stripe->directory.insert(&key, stripe, &dir1);
      }
      dir_corrupt_bucket(dir_bucket(b1, stripe->directory.get_segment(s1)), s1, stripe);

      // Detection: a cycle makes the chain longer than its segment can hold.
      CHECK(!stripe->directory.check());
      CHECK(stripe->directory.bucket_length(dir_bucket(b1, stripe->directory.get_segment(s1)), s1) == -1);

      // A reader must terminate on a looped chain. Vary only slice32(2), the tag, so the probe lands on the same
      // segment and bucket but matches no entry and therefore walks the whole cycle.
      CacheKey miss_key = key;
      miss_key.u32[2]   = ~key.u32[2];
      Dir  probed;
      Dir *probe_collision = nullptr;
      dir_clear(&probed);
      CHECK(stripe->directory.probe(&miss_key, stripe, &probed, &probe_collision) == 0);

      // The reader leaves the loop in place for a writer to repair.
      CHECK(!stripe->directory.check());

      // insert() only walks the chain once the bucket's own rows are full, which the five entries above ensure, and
      // that walk must repair.
      stripe->directory.insert(&key, stripe, &dir1);
      CHECK(stripe->directory.check());

      // overwrite() repairs from its own walk, so give it a freshly corrupted chain.
      for (int i = 0; i < 5; i++) {
        stripe->directory.insert(&key, stripe, &dir1);
      }
      dir_corrupt_bucket(dir_bucket(b1, stripe->directory.get_segment(s1)), s1, stripe);

      // Target an offset absent from the chain so the search actually walks it rather than matching the head entry
      // immediately.
      Dir absent;
      dir_clear(&absent);
      dir_set_offset(&absent, 999);
      stripe->directory.overwrite(&key, stripe, &dir1, &absent, false);
      CHECK(stripe->directory.check());
    }
    stripe->clear_dir();

    // Teardown
    test_done();
    delete this;

    return EVENT_DONE;
  }
};

TEST_CASE("CacheDir")
{
  init_cache(0);

  CacheDirTest *init = new CacheDirTest;

  this_ethread()->schedule_imm(init);
  this_thread()->execute();

  return;
}

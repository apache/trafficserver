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
  int  l   = (static_cast<int>(dir_bucket_length(b, s, stripe) * ts::Random::drandom()));
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
    int free     = dir_freelist_length(stripe, s);
    int n        = free;
    while (n--) {
      if (!dir_insert(&key, stripe, &dir)) {
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
    dir_clean_segment(s, stripe);
    int newfree = dir_freelist_length(stripe, s);
    CHECK(static_cast<unsigned int>(newfree - free) <= 1);

    // test insert-delete
    regress_rand_init(13);
    ttime = ink_get_hrtime();
    for (i = 0; i < newfree; i++) {
      regress_rand_CacheKey(&key);
      dir_insert(&key, stripe, &dir);
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
      CHECK(dir_probe(&key, stripe, &dir, &last_collision));
    }
    us = (ink_get_hrtime() - ttime) / HRTIME_USECOND;
    // On windows us is sometimes 0. I don't know why.
    // printout the probe rate only if its not 0
    if (us) {
      Dbg(dbg_ctl_cache_dir_test, "probe rate = %d / second", static_cast<int>((newfree * static_cast<uint64_t>(1000000)) / us));
    }

    for (int c = 0; c < stripe->directory.entries() * 0.75; c++) {
      regress_rand_CacheKey(&key);
      dir_insert(&key, stripe, &dir);
    }

    Dir dir1;
    memset(static_cast<void *>(&dir1), 0, sizeof(dir1));
    int s1, b1;

    Dbg(dbg_ctl_cache_dir_test, "corrupt_bucket test");
    for (int ntimes = 0; ntimes < 10; ntimes++) {
#ifdef LOOP_CHECK_MODE
      // dir_probe in bucket with loop
      rand_CacheKey(&key);
      s1 = key.slice32(0) % vol->segments;
      b1 = key.slice32(1) % vol->buckets;
      dir_corrupt_bucket(dir_bucket(b1, vol->directory.get_segment(s1)), s1, vol);
      dir_insert(&key, vol, &dir);
      Dir *last_collision = 0;
      dir_probe(&key, vol, &dir, &last_collision);

      rand_CacheKey(&key);
      s1 = key.slice32(0) % vol->segments;
      b1 = key.slice32(1) % vol->buckets;
      dir_corrupt_bucket(dir_bucket(b1, vol->directory.get_segment(s1)), s1, vol);

      last_collision = 0;
      dir_probe(&key, vol, &dir, &last_collision);

      // dir_overwrite in bucket with loop
      rand_CacheKey(&key);
      s1 = key.slice32(0) % vol->segments;
      b1 = key.slice32(1) % vol->buckets;
      CacheKey key1;
      key1.b[1] = 127;
      dir1      = dir;
      dir_set_offset(&dir1, 23);
      dir_insert(&key1, vol, &dir1);
      dir_insert(&key, vol, &dir);
      key1.b[1] = 80;
      dir_insert(&key1, vol, &dir1);
      dir_corrupt_bucket(dir_bucket(b1, vol->directory.get_segment(s1)), s1, vol);
      dir_overwrite(&key, vol, &dir, &dir, 1);

      rand_CacheKey(&key);
      s1       = key.slice32(0) % vol->segments;
      b1       = key.slice32(1) % vol->buckets;
      key.b[1] = 23;
      dir_insert(&key, vol, &dir1);
      dir_corrupt_bucket(dir_bucket(b1, vol->directory.get_segment(s1)), s1, vol);
      dir_overwrite(&key, vol, &dir, &dir, 0);

      rand_CacheKey(&key);
      s1        = key.slice32(0) % vol->segments;
      Dir *seg1 = vol->directory.get_segment(s1);
      // dir_freelist_length in freelist with loop
      dir_corrupt_bucket(dir_from_offset(vol->header->freelist[s], seg1), s1, vol);
      dir_freelist_length(vol, s1);

      rand_CacheKey(&key);
      s1 = key.slice32(0) % vol->segments;
      b1 = key.slice32(1) % vol->buckets;
      // dir_bucket_length in bucket with loop
      dir_corrupt_bucket(dir_bucket(b1, vol->directory.get_segment(s1)), s1, vol);
      dir_bucket_length(dir_bucket(b1, vol->directory.get_segment(s1)), s1, vol);
      CHECK(check_dir(vol));
#else
      // test corruption detection
      rand_CacheKey(&key);
      s1 = key.slice32(0) % stripe->directory.segments;
      b1 = key.slice32(1) % stripe->directory.buckets;

      dir_insert(&key, stripe, &dir1);
      dir_insert(&key, stripe, &dir1);
      dir_insert(&key, stripe, &dir1);
      dir_insert(&key, stripe, &dir1);
      dir_insert(&key, stripe, &dir1);
      dir_corrupt_bucket(dir_bucket(b1, stripe->directory.get_segment(s1)), s1, stripe);
      CHECK(!check_dir(stripe));
#endif
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

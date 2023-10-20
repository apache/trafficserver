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

#include "main.h"

#include "P_Cache.h"

// Required by main.h
int cache_vols            = 1;
bool reuse_existing_cache = false;

namespace
{

DbgCtl dbg_ctl_cache_test{"cache_test"};

}

TEST_CASE("CacheDiskReplacement")
{
  static int const MAX_VOLS           = 26; // maximum values used in any test.
  static uint64_t DEFAULT_SKIP        = 8192;
  static uint64_t DEFAULT_STRIPE_SIZE = 1024ULL * 1024 * 1024 * 911; // 911G
  CacheDisk disk;                                                    // Only need one because it's just checked for failure.
  CacheHostRecord hr1, hr2;
  Vol *sample;
  static int const sample_idx = 16;
  Vol vols[MAX_VOLS];
  Vol *vol_ptrs[MAX_VOLS]; // array of pointers.
  char buff[2048];

  disk.num_errors = 0;

  for (int i = 0; i < MAX_VOLS; ++i) {
    vol_ptrs[i]  = vols + i;
    vols[i].disk = &disk;
    vols[i].len  = DEFAULT_STRIPE_SIZE;
    snprintf(buff, sizeof(buff), "/dev/sd%c %" PRIu64 ":%" PRIu64, 'a' + i, DEFAULT_SKIP, vols[i].len);
    CryptoContext().hash_immediate(vols[i].hash_id, buff, strlen(buff));
  }

  hr1.vol_hash_table = nullptr;
  hr1.vols           = vol_ptrs;
  hr1.num_vols       = MAX_VOLS;
  build_vol_hash_table(&hr1);

  hr2.vol_hash_table = nullptr;
  hr2.vols           = vol_ptrs;
  hr2.num_vols       = MAX_VOLS;

  sample      = vols + sample_idx;
  sample->len = 1024ULL * 1024 * 1024 * (1024 + 128); // 1.1 TB
  snprintf(buff, sizeof(buff), "/dev/sd%c %" PRIu64 ":%" PRIu64, 'a' + sample_idx, DEFAULT_SKIP, sample->len);
  CryptoContext().hash_immediate(sample->hash_id, buff, strlen(buff));
  build_vol_hash_table(&hr2);

  // See what the difference is
  int to = 0, from = 0;
  int then = 0, now = 0;
  for (int i = 0; i < VOL_HASH_TABLE_SIZE; ++i) {
    if (hr1.vol_hash_table[i] == sample_idx) {
      ++then;
    }
    if (hr2.vol_hash_table[i] == sample_idx) {
      ++now;
    }
    if (hr1.vol_hash_table[i] != hr2.vol_hash_table[i]) {
      if (hr1.vol_hash_table[i] == sample_idx) {
        ++from;
      } else {
        ++to;
      }
    }
  }
  Dbg(dbg_ctl_cache_test,
      "Cache stability difference - "
      "delta = %d of %d : %d to, %d from, originally %d slots, now %d slots (net gain = %d/%d)",
      to + from, VOL_HASH_TABLE_SIZE, to, from, then, now, now - then, to - from);

  hr1.vols = nullptr;
  hr2.vols = nullptr;
}

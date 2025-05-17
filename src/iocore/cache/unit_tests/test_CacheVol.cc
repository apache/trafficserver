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

#include "../P_CacheHosting.h"
#include "../Stripe.h"

#include "tscore/Diags.h"
#include "tsutil/PostScript.h"

// Required by main.h
int  cache_vols           = 1;
bool reuse_existing_cache = false;

extern int             gndisks;
extern CacheDisk     **gdisks;
extern Queue<CacheVol> cp_list;
extern int             cp_list_len;
extern ConfigVolumes   config_volumes;

extern void cplist_init();
extern int  cplist_reconfigure();

namespace
{

DbgCtl dbg_ctl_cache_hosting{"cache_hosting"};
DbgCtl dbg_ctl_matcher{"matcher"};
DbgCtl dbg_ctl_cache_vol_test{"cache_vol_test"};

/* Test the cache volume with different configurations */
#define MEGS_128              (128 * 1024 * 1024)
#define ROUND_TO_VOL_SIZE(_x) (((_x) + (MEGS_128 - 1)) & ~(MEGS_128 - 1))

static int configs = 4;

Queue<CacheVol> saved_cp_list;
int             saved_cp_list_len;
ConfigVolumes   saved_config_volumes;
int             saved_gnstripes;

int  ClearConfigVol(ConfigVolumes *configp);
int  ClearCacheVolList(Queue<CacheVol> *cpl, int len);
int  create_config(int i);
void execute_and_verify();
void save_state();
void restore_state();

int
create_config(int num)
{
  int i       = 0;
  int vol_num = 1;
  // clear all old configurations before adding new test cases
  config_volumes.clear_all();
  switch (num) {
  case 0:
    for (i = 0; i < gndisks; i++) {
      CacheDisk *d      = gdisks[i];
      int        blocks = d->num_usable_blocks;
      if (blocks < STORE_BLOCKS_PER_STRIPE) {
        Warning("Cannot run Cache_vol regression: not enough disk space");
        return 0;
      }
      /* create 128 MB volumes */
      for (; blocks >= STORE_BLOCKS_PER_STRIPE; blocks -= STORE_BLOCKS_PER_STRIPE) {
        if (vol_num > 255) {
          break;
        }
        ConfigVol *cp  = new ConfigVol();
        cp->number     = vol_num++;
        cp->scheme     = CacheType::HTTP;
        cp->size       = 128;
        cp->in_percent = false;
        cp->cachep     = nullptr;
        config_volumes.cp_queue.enqueue(cp);
        config_volumes.num_volumes++;
        config_volumes.num_http_volumes++;
      }
    }
    Dbg(dbg_ctl_cache_vol_test, "%d 128 Megabyte Volumes", vol_num - 1);
    break;

  case 1: {
    for (i = 0; i < gndisks; i++) {
      gdisks[i]->delete_all_volumes();
    }

    // calculate the total free space
    off_t total_space = 0;
    for (i = 0; i < gndisks; i++) {
      off_t vol_blocks = gdisks[i]->num_usable_blocks;
      /* round down the blocks to the nearest
         multiple of STORE_BLOCKS_PER_STRIPE */
      vol_blocks   = (vol_blocks / STORE_BLOCKS_PER_STRIPE) * STORE_BLOCKS_PER_STRIPE;
      total_space += vol_blocks;
    }

    // make sure we have at least 1280 M bytes
    if (total_space < ((10 << 27) >> STORE_BLOCK_SHIFT)) {
      // Skip this test case due to small space
      Warning("Not enough space for 10 volume");
      return 0;
    }

    vol_num = 1;
    Dbg(dbg_ctl_cache_vol_test, "Cleared  disk");
    for (i = 0; i < 10; i++) {
      ConfigVol *cp  = new ConfigVol();
      cp->number     = vol_num++;
      cp->scheme     = CacheType::HTTP;
      cp->size       = 10;
      cp->percent    = 10;
      cp->in_percent = true;
      cp->cachep     = nullptr;
      config_volumes.cp_queue.enqueue(cp);
      config_volumes.num_volumes++;
      config_volumes.num_http_volumes++;
    }
    Dbg(dbg_ctl_cache_vol_test, "10 volume, 10 percent each");
  } break;

  case 2:
  case 3:

  {
    /* calculate the total disk space */
    InkRand *gen         = &this_ethread()->generator;
    off_t    total_space = 0;
    vol_num              = 1;
    if (num == 2) {
      Dbg(dbg_ctl_cache_vol_test, "Random Volumes after clearing the disks");
    } else {
      Dbg(dbg_ctl_cache_vol_test, "Random Volumes without clearing the disks");
    }

    for (i = 0; i < gndisks; i++) {
      off_t vol_blocks = gdisks[i]->num_usable_blocks;
      /* round down the blocks to the nearest
         multiple of STORE_BLOCKS_PER_STRIPE */
      vol_blocks   = (vol_blocks / STORE_BLOCKS_PER_STRIPE) * STORE_BLOCKS_PER_STRIPE;
      total_space += vol_blocks;

      if (num == 2) {
        gdisks[i]->delete_all_volumes();
      } else {
        gdisks[i]->cleared = 0;
      }
    }

    if (total_space == 0) {
      Warning("Not enough space to test");
      return 0;
    }

    while (total_space > 0) {
      if (vol_num > 255) {
        break;
      }
      off_t modu = MAX_STRIPE_SIZE;
      if (total_space < (MAX_STRIPE_SIZE >> STORE_BLOCK_SHIFT)) {
        modu = total_space * STORE_BLOCK_SIZE;
      }

      off_t random_size = (gen->random() % modu) + 1;
      /* convert to 128 megs multiple */
      CacheType scheme = (random_size % 2) ? CacheType::HTTP : CacheType::RTSP;
      random_size      = ROUND_TO_VOL_SIZE(random_size);
      off_t blocks     = random_size / STORE_BLOCK_SIZE;
      ink_assert(blocks <= total_space);
      total_space -= blocks;

      ConfigVol *cp = new ConfigVol();

      cp->number     = vol_num++;
      cp->scheme     = scheme;
      cp->size       = random_size >> 20;
      cp->percent    = 0;
      cp->in_percent = false;
      cp->cachep     = nullptr;
      config_volumes.cp_queue.enqueue(cp);
      config_volumes.num_volumes++;
      if (cp->scheme == CacheType::HTTP) {
        config_volumes.num_http_volumes++;
        Dbg(dbg_ctl_cache_vol_test, "volume=%d scheme=http size=%zd", cp->number, static_cast<size_t>(cp->size));
      } else {
        // ToDo: Assert ?
      }
    }
  } break;

  default:
    return 1;
  }
  return 1;
}

void
execute_and_verify()
{
  ts::PostScript clear([&]() -> void {
    ClearConfigVol(&config_volumes);
    ClearCacheVolList(&cp_list, cp_list_len);
  });

  cplist_init();

  if (cplist_reconfigure() < 0) {
    Warning("reconfigure failed");
    return;
  }

  /* compare the volumes */
  REQUIRE(cp_list_len == config_volumes.num_volumes);

  /* check that the volumes and sizes
     match the configuration */
  int        matched = 0;
  ConfigVol *cp      = config_volumes.cp_queue.head;
  CacheVol  *cachep;

  for (int i = 0; i < config_volumes.num_volumes; i++) {
    cachep = cp_list.head;
    while (cachep) {
      if (cachep->vol_number == cp->number) {
        // Configuration and Actual volumes should match
        REQUIRE(cachep->scheme == cp->scheme);
        REQUIRE(cachep->size == (cp->size << (20 - STORE_BLOCK_SHIFT)));
        REQUIRE(cachep == cp->cachep);

        /* check that the number of volumes match the ones
           on disk */
        int d_no;
        int m_vols = 0;
        for (d_no = 0; d_no < gndisks; d_no++) {
          if (cachep->disk_stripes[d_no]) {
            DiskStripe *dp = cachep->disk_stripes[d_no];
            // DiskStripes and CacheVols should match
            REQUIRE(dp->vol_number == cachep->vol_number);

            /* check the diskvolblock queue */
            DiskStripeBlockQueue *dpbq = dp->dpb_queue.head;
            while (dpbq) {
              // DiskStripe and DiskStripeBlocks should match
              REQUIRE(dpbq->b->number == cachep->vol_number);
              dpbq = dpbq->link.next;
            }

            m_vols += dp->num_volblocks;
          }
        }
        // Num volumes in CacheVol and DiskStripe should match
        REQUIRE(m_vols == cachep->num_vols);

        matched++;
        break;
      }
      cachep = cachep->link.next;
    }
  }

  // Num of Volumes created and configured should match
  REQUIRE(matched == config_volumes.num_volumes);

  for (int i = 0; i < gndisks; i++) {
    CacheDisk *d = gdisks[i];
    if (dbg_ctl_cache_hosting.on()) {
      Dbg(dbg_ctl_cache_hosting, "Disk: %d: Stripe Blocks: %u: Free space: %" PRIu64, i, d->header->num_diskvol_blks,
          d->free_space);
      for (int j = 0; j < static_cast<int>(d->header->num_volumes); j++) {
        Dbg(dbg_ctl_cache_hosting, "\tStripe: %d Size: %" PRIu64, d->disk_stripes[j]->vol_number, d->disk_stripes[j]->size);
      }
      for (int j = 0; j < static_cast<int>(d->header->num_diskvol_blks); j++) {
        Dbg(dbg_ctl_cache_hosting, "\tBlock No: %d Size: %" PRIu64 " Free: %u", d->header->vol_info[j].number,
            d->header->vol_info[j].len, d->header->vol_info[j].free);
      }
    }
  }
}

int
ClearConfigVol(ConfigVolumes *configp)
{
  int        i  = 0;
  ConfigVol *cp = nullptr;
  while ((cp = configp->cp_queue.dequeue())) {
    delete cp;
    i++;
  }
  if (i != configp->num_volumes) {
    Warning("failed");
    return 0;
  }
  configp->num_volumes      = 0;
  configp->num_http_volumes = 0;
  return 1;
}

int
ClearCacheVolList(Queue<CacheVol> *cpl, int len)
{
  int       i  = 0;
  CacheVol *cp = nullptr;
  while ((cp = cpl->dequeue())) {
    for (int d_no = 0; d_no < gndisks; d_no++) {
      cp->disk_stripes[d_no]->disk->delete_volume(cp->vol_number);
      cp->disk_stripes[d_no] = nullptr;
    }
    ats_free(cp->disk_stripes);
    ats_free(cp->stripes);
    delete (cp);
    i++;
  }

  if (i != len) {
    Warning("Failed");
    return 0;
  }
  return 1;
}

void
save_state()
{
  saved_cp_list     = cp_list;
  saved_cp_list_len = cp_list_len;
  memcpy(&saved_config_volumes, &config_volumes, sizeof(ConfigVolumes));
  saved_gnstripes = gnstripes;
  memset(static_cast<void *>(&cp_list), 0, sizeof(Queue<CacheVol>));
  memset(static_cast<void *>(&config_volumes), 0, sizeof(ConfigVolumes));
  gnstripes = 0;
}

void
restore_state()
{
  cp_list     = saved_cp_list;
  cp_list_len = saved_cp_list_len;
  memcpy(&config_volumes, &saved_config_volumes, sizeof(ConfigVolumes));
  gnstripes = saved_gnstripes;
}
} // end anonymous namespace

class CacheVolTest : public CacheInit
{
public:
  int
  cache_init_success_callback(int /* event ATS_UNUSED */, void * /* e ATS_UNUSED */) override
  {
    // Test
    ClearCacheVolList(&cp_list, cp_list_len);

    save_state();
    srand48(time(nullptr));

    for (int i = 0; i < configs; i++) {
      Dbg(dbg_ctl_cache_vol_test, "config case = %d", i);

      if (create_config(i)) {
        execute_and_verify();
      }
    }
    restore_state();

    // Teardown
    test_done();
    delete this;

    return 0;
  }
};

TEST_CASE("CacheVol")
{
  init_cache(0);

  CacheVolTest *init = new CacheVolTest;

  this_ethread()->schedule_imm(init);
  this_thread()->execute();
  return;
}

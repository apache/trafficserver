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

#pragma once

#include "iocore/aio/AIO.h"
#include "iocore/cache/Cache.h"

extern int cache_config_max_disk_errors;

#define DISK_BAD(_x)           ((_x)->num_errors >= cache_config_max_disk_errors)
#define DISK_BAD_SIGNALLED(_x) (_x->num_errors > cache_config_max_disk_errors)
#define SET_DISK_BAD(_x)       (_x->num_errors = cache_config_max_disk_errors)
#define SET_DISK_OKAY(_x)      (_x->num_errors = 0)

#define DISK_HEADER_MAGIC 0xABCD1237

/* each disk vol block has a corresponding StripeSM object */
struct CacheDisk;

struct DiskStripeBlock {
  uint64_t     offset; // offset in bytes from the start of the disk
  uint64_t     len;    // length in store blocks
  int          number;
  unsigned int type : 3;
  unsigned int free : 1;
};

struct DiskStripeBlockQueue {
  DiskStripeBlock *b         = nullptr;
  int              new_block = 0; /* whether an existing vol or a new one */
  LINK(DiskStripeBlockQueue, link);

  DiskStripeBlockQueue() {}
};

struct DiskStripe {
  int                         num_volblocks; /* number of disk volume blocks in this volume */
  int                         vol_number;    /* the volume number of this volume */
  uint64_t                    size;          /* size in store blocks */
  CacheDisk                  *disk;
  Queue<DiskStripeBlockQueue> dpb_queue;
};

struct DiskHeader {
  unsigned int    magic;
  unsigned int    num_volumes;      /* number of discrete volumes (DiskStripe) */
  unsigned int    num_free;         /* number of disk volume blocks free */
  unsigned int    num_used;         /* number of disk volume blocks in use */
  unsigned int    num_diskvol_blks; /* number of disk volume blocks */
  uint64_t        num_blocks;
  DiskStripeBlock vol_info[1];
};

struct CacheDisk : public Continuation {
  DiskHeader  *header     = nullptr;
  char        *path       = nullptr;
  int          header_len = 0;
  AIOCallback  io;
  off_t        len               = 0; // in blocks (STORE_BLOCK)
  off_t        start             = 0;
  off_t        skip              = 0;
  off_t        num_usable_blocks = 0;
  int          hw_sector_size    = 0;
  int          fd                = -1;
  off_t        free_space        = 0;
  off_t        wasted_space      = 0;
  DiskStripe **disk_stripes      = nullptr;
  DiskStripe  *free_blocks       = nullptr;
  int          num_errors        = 0;
  int          cleared           = 0;
  bool         read_only_p       = false;
  bool         online = true; /* flag marking cache disk online or offline (because of too many failures or by the operator). */

  // Extra configuration values
  int            forced_volume_num = -1; ///< Volume number for this disk.
  ats_scoped_str hash_base_string;       ///< Base string for hash seed.

  CacheDisk() : Continuation(new_ProxyMutex()) {}

  ~CacheDisk() override;

  int              open(bool clear);
  int              open(char *s, off_t blocks, off_t skip, int hw_sector_size, int fildes, bool clear);
  int              clearDisk();
  int              clearDone(int event, void *data);
  int              openStart(int event, void *data);
  int              openDone(int event, void *data);
  int              sync();
  int              syncDone(int event, void *data);
  DiskStripeBlock *create_volume(int number, off_t size, CacheType scheme);
  int              delete_volume(int number);
  int              delete_all_volumes();
  void             update_header();
  DiskStripe      *get_diskvol(int vol_number);
  void             incrErrors(const AIOCallback *io);
};

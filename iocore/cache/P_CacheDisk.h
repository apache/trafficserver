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

#ifndef _P_CACHE_DISK_H__
#define _P_CACHE_DISK_H__

#include "I_Cache.h"

extern int cache_config_max_disk_errors;

#define DISK_BAD(_x)                    (_x->num_errors >= cache_config_max_disk_errors)
#define DISK_BAD_SIGNALLED(_x)          (_x->num_errors > cache_config_max_disk_errors)
#define SET_DISK_BAD(_x)                (_x->num_errors = cache_config_max_disk_errors)
#define SET_DISK_OKAY(_x)               (_x->num_errors = 0)

#define PART_BLOCK_SIZE                 (1024 * 1024 * 128)
#define MIN_PART_SIZE                   PART_BLOCK_SIZE
#define ROUND_DOWN_TO_PART_BLOCK(_x)    (((_x) &~ (PART_BLOCK_SIZE - 1)))
#define PART_BLOCK_SHIFT                27
#define ROUND_DOWN_TO_STORE_BLOCK(_x)   (((_x) >> STORE_BLOCK_SHIFT) << STORE_BLOCK_SHIFT)

#define STORE_BLOCKS_PER_PART  (PART_BLOCK_SIZE / STORE_BLOCK_SIZE)
#define DISK_HEADER_MAGIC               0xABCD1236

/* each disk part block has a corresponding Part object */
struct CacheDisk;

struct DiskPartBlock
{
  off_t offset;
  unsigned short number;
  unsigned int len:26;
  unsigned int type:3;
  unsigned int free:1;
  unsigned int unused:2;
};

struct DiskPartBlockQueue
{
  DiskPartBlock *b;
  int new_block;                /* whether an existing part or a new one */
  LINK(DiskPartBlockQueue, link);
  DiskPartBlockQueue():b(NULL), new_block(0)
  {
  }
};

struct DiskPart
{
  int num_partblocks;           /* number of disk partition blocks in this discrete
                                   partition */
  int part_number;              /* the partition number of this partition */
  off_t size;               /* size in store blocks */
  CacheDisk *disk;
  Queue<DiskPartBlockQueue> dpb_queue;
};

struct DiskHeader
{
  unsigned int magic;
  unsigned int num_partitions;  /* number of discrete partitions (DiskPart) */
  unsigned int num_free;        /* number of disk partition blocks free */
  unsigned int num_used;        /* number of disk partition blocks in use */
  unsigned int num_diskpart_blks;       /* number of disk partition blocks */
  off_t num_blocks;
  DiskPartBlock part_info[1];
};

struct CacheDisk:public Continuation
{

  DiskHeader *header;
  char *path;
  int header_len;
  AIOCallbackInternal io;
  off_t len;                // in blocks (STORE_BLOCK)
  off_t start;
  off_t skip;
  int num_usable_blocks;
  int hw_sector_size;
  int fd;
  off_t free_space;
  off_t wasted_space;
  DiskPart **disk_parts;
  DiskPart *free_blocks;
  int num_errors;
  int cleared;

  int open(bool clear);
    CacheDisk():Continuation(new_ProxyMutex()), header(NULL),
    path(NULL), header_len(0), len(0), start(0), skip(0),
    num_usable_blocks(0), fd(-1), free_space(0), wasted_space(0),
    disk_parts(NULL), free_blocks(NULL), num_errors(0), cleared(0)
  {
  }

   ~CacheDisk();
  int open(char *s, off_t blocks, off_t skip, int hw_sector_size, int fildes, bool clear);
  int clearDisk();
  int clearDone(int event, void *data);
  int openStart(int event, void *data);
  int openDone(int event, void *data);
  int sync();
  int syncDone(int event, void *data);
  DiskPartBlock *create_partition(int number, off_t size, int scheme);
  int delete_partition(int number);
  int delete_all_partitions();
  void update_header();
  DiskPart *get_diskpart(int part_number);

};


#endif

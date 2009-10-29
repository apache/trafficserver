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

#ifndef _P_RAM_CACHE_H__
#define _P_RAM_CACHE_H__


#include "I_Cache.h"

#define RAM_CACHE_SEEN_PER_OBJECT    2
#define RAM_CACHE_FAST_LOAD_SIZE     32768
#define n_partitions 1

struct Part;

struct RamCacheEntry
{
  INK_MD5 key;
  inku32 auxkey1;
  inku32 auxkey2;
    Link<RamCacheEntry> lru_link;
    Link<RamCacheEntry> hash_link;
    Ptr<IOBufferData> data;
};

struct RamCachePartition
{
  int cur_bytes;
  int cur_objects;
    DLL<RamCacheEntry> *bucket;
    Queue<RamCacheEntry> lru;
  unsigned short *seen;
  ProxyMutexPtr lock;

    RamCachePartition():cur_bytes(0), cur_objects(0), bucket(0), seen(0), lock(NULL)
  {
  }
};

struct RamCache
{
  // Partition Read-only data
  ink64 bytes;
  ink64 objects;
  ink64 partition_size;
  ink64 seen_size;
  int cutoff_size;
  RamCachePartition *partition;
  RamCachePartition one_partition;
  Part *part;                   // back pointer to partition

  // returns 1 on found/stored, 0 on not found/stored
  // if provided, auxkey1 and auxkey2 must match
  int get(INK_MD5 * key, Ptr<IOBufferData> *ret_data, inku32 auxkey1 = 0, inku32 auxkey2 = 0);
  int put(INK_MD5 * key, IOBufferData * data, EThread * t, inku32 auxkey1 = 0, inku32 auxkey2 = 0);
  int fixup(INK_MD5 * key, inku32 old_auxkey1, inku32 old_auxkey2, inku32 new_auxkey1, inku32 new_auxkey2);
  // also returns -1 if locked 
  int get_lock(INK_MD5 * key, Ptr<IOBufferData> *ret_data, EThread * t, inku32 auxkey1 = 0, inku32 auxkey2 = 0);
  int put_lock(INK_MD5 * key, IOBufferData * data, EThread * t, inku32 auxkey1 = 0, inku32 auxkey2 = 0);

  void remove_entry(RamCacheEntry * ee, RamCachePartition * p, EThread * t);

  void print_stats(FILE * fp, int verbose = 0);


  void init(ink64 bytes, ink64 objects, int cutoff, Part * _part, ProxyMutex * mutex = NULL);
    RamCache():bytes(0), objects(0), partition_size(0), seen_size(0), cutoff_size(0), partition(0), part(NULL)
  {
  }
};

extern ClassAllocator<RamCacheEntry> ramCacheEntryAllocator;

#endif /* _P_RAM_CACHE_H__ */

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

  MultiCache.h


 ****************************************************************************/

#ifndef _MultiCache_h_
#define _MultiCache_h_

#include "I_EventSystem.h"
#include "I_Store.h"

//
// Constants
//

#define MULTI_CACHE_MAX_LEVELS 3
#define MULTI_CACHE_MAX_BUCKET_SIZE 256
#define MULTI_CACHE_MAX_FILES 256
#define MULTI_CACHE_PARTITIONS 64

#define MULTI_CACHE_EVENT_SYNC MULTI_CACHE_EVENT_EVENTS_START

// for heap_offset() and heap_size(), indicates no data
#define MULTI_CACHE_HEAP_NONE -1

#define MULTI_CACHE_MAGIC_NUMBER 0x0BAD2D8

// Update these if there is a change to MultiCacheBase
// There is a separate HOST_DB_CACHE_[MAJOR|MINOR]_VERSION
#define MULTI_CACHE_MAJOR_VERSION 2
#define MULTI_CACHE_MINOR_VERSION 1
// 2.1 - IPv6 compatible

#define MULTI_CACHE_HEAP_HIGH_WATER 0.8

#define MULTI_CACHE_HEAP_INITIAL sizeof(uint32_t)
#define MULTI_CACHE_HEAP_ALIGNMENT 8

// unused.. possible optimization
#define MULTI_CACHE_OFFSET_PARITION(_x) ((_x) % MULTI_CACHE_PARTITIONS)
#define MULTI_CACHE_OFFSET_INDEX(_x) ((_x) / MULTI_CACHE_PARTITIONS)
#define MULTI_CACHE_OFFSET(_p, _o) ((_p) + (_o)*MULTI_CACHE_PARTITIONS)

class ProxyMutex;
class Continuation;

//
// Types
//

// MultiCacheBlock
// This is an abstract class which simply documents the operations
// required by the templated cache operations.

struct MultiCacheBlock {
  uint64_t tag();
  bool is_deleted();
  void set_deleted();
  bool is_empty();
  void set_empty();
  void reset();
  void set_full(uint64_t folded_md5, int buckets);
  int
  heap_size()
  {
    return 0;
  }
  int *
  heap_offset_ptr()
  {
    return NULL;
  }
};

struct RebuildMC {
  bool rebuild;
  bool check;
  bool fix;
  char *data;
  int partition;

  int deleted;
  int backed;
  int duplicates;
  int corrupt;
  int stale;
  int good;
  int total;
};

struct MultiCacheHeader {
  unsigned int magic;
  VersionNumber version;

  unsigned int levels;

  int tag_bits;
  int max_hits;
  int elementsize;

  int buckets;
  int level_offset[MULTI_CACHE_MAX_LEVELS];
  int elements[MULTI_CACHE_MAX_LEVELS];
  int bucketsize[MULTI_CACHE_MAX_LEVELS];

  int totalelements;
  unsigned int totalsize;

  int nominal_elements;

  // optional heap
  int heap_size;
  volatile int heap_halfspace;
  volatile int heap_used[2];

  MultiCacheHeader();
};

// size of block of unsunk pointers with respect to the number of
// elements
#define MULTI_CACHE_UNSUNK_PTR_BLOCK_SIZE(_e) ((_e / 8) / MULTI_CACHE_PARTITIONS)

struct UnsunkPtr {
  int offset;
  int *poffset; // doubles as freelist pointer
};

struct MultiCacheBase;

struct UnsunkPtrRegistry {
  MultiCacheBase *mc;
  int n;
  UnsunkPtr *ptrs;
  UnsunkPtr *next_free;
  UnsunkPtrRegistry *next;

  UnsunkPtr *ptr(int i);
  UnsunkPtr *alloc(int *p, int base = 0);
  void alloc_data();

  UnsunkPtrRegistry();
  ~UnsunkPtrRegistry();
};

//
// Broken SunCC
//
#define PtrMutex Ptr<ProxyMutex>

//
// used by windows only - to keep track
// of mapping handles
//
struct Unmaper {
  void *hMap;
  char *pAddr;
};

typedef int three_ints[3];
typedef int two_ints[2];

struct MultiCacheHeapGC;

struct MultiCacheBase : public MultiCacheHeader {
  Store *store;
  char filename[PATH_NAME_MAX];
  MultiCacheHeader *mapped_header;

  MultiCacheHeader header_snap;

  // mmap-ed region
  //
  char *data;
  char *lowest_level_data;

  // equal to data + level_offset[3] + bucketsize[3] * buckets;
  char *heap;

  // interface functions
  //
  int
  halfspace_size()
  {
    return heap_size / 2;
  }

  // Stats support
  //
  int hit_stat[MULTI_CACHE_MAX_LEVELS];
  int miss_stat;

  unsigned int
  lowest_level_data_size()
  {
    return (buckets + 3) / 4;
  }
  unsigned int
  lowest_level(unsigned int bucket)
  {
    unsigned int i = (unsigned char)lowest_level_data[bucket / 4];
    return 3 & (i >> (buckets % 4));
  }
  void
  set_lowest_level(unsigned int bucket, unsigned int lowest)
  {
    unsigned char p = (unsigned char)lowest_level_data[bucket / 4];
    p &= ~(3 << (buckets % 4));
    p |= (lowest & 3) << (buckets % 4);
    lowest_level_data[bucket / 4] = (char)p;
  }

  // Fixed point, 8 bits shifted left
  int buckets_per_partitionF8;

  int
  partition_of_bucket(int b)
  {
    return ((b << 8) + 0xFF) / buckets_per_partitionF8;
  }
  int
  first_bucket_of_partition(int p)
  {
    return ((buckets_per_partitionF8 * p) >> 8);
  }
  int
  last_bucket_of_partition(int p)
  {
    return first_bucket_of_partition(p + 1) - 1;
  }
  int
  buckets_of_partition(int p)
  {
    return last_bucket_of_partition(p) - first_bucket_of_partition(p) + 1;
  }

  int open(Store *store, const char *config_filename, char *db_filename = NULL, int db_size = -1, bool reconfigure = false,
           bool fix = false, bool silent = false);

  // 1 for success, 0 for no config file, -1 for failure
  int read_config(const char *config_filename, Store &store, char *fn = NULL, int *pi = NULL, int *pbuckets = NULL);
  int write_config(const char *config_filename, int nominal_size, int buckets);
  int initialize(Store *store, char *filename, int elements, int buckets = 0, unsigned int levels = 2,
                 int level0_elements_per_bucket = 4, int level1_elements_per_bucket = 32, int level2_elements_per_bucket = 1);
  int mmap_data(bool private_flag = false, bool zero_fill = false);
  char *mmap_region(int blocks, int *fds, char *cur, size_t &total_length, bool private_flag, int zero_fill = 0);
  int blocks_in_level(unsigned int level);

  bool verify_header();

  int unmap_data();
  void reset();
  void clear(); // this zeros the data
  void clear_but_heap();

  virtual MultiCacheBase *
  dup()
  {
    ink_assert(0);
    return NULL;
  }

  virtual size_t
  estimated_heap_bytes_per_entry() const
  {
    return 0;
  }

  void print_info(FILE *fp);

//
// Rebuild the database, also perform checks, and fixups
// ** cannot be called on a running system **
// "this" must be initialized.
//
#define MC_REBUILD 0
#define MC_REBUILD_CHECK 1
#define MC_REBUILD_FIX 2
  int rebuild(MultiCacheBase &old, int kind = MC_REBUILD); // 0 on success

  virtual void
  rebuild_element(int buck, char *elem, RebuildMC &r)
  {
    (void)buck;
    (void)elem;
    (void)r;
    ink_assert(0);
  }

  //
  // Check the database
  // ** cannot be called on a running system **
  //  assumes that the configuration is correct
  //
  int check(const char *config_filename, bool fix = false);

  ProxyMutex *
  lock_for_bucket(int bucket)
  {
    return locks[partition_of_bucket(bucket)];
  }
  uint64_t
  make_tag(uint64_t folded_md5)
  {
    uint64_t ttag = folded_md5 / (uint64_t)buckets;
    if (!ttag)
      return 1LL;
    // beeping gcc 2.7.2 is broken
    if (tag_bits > 32) {
      uint64_t mask = 0x100000000LL << (tag_bits - 32);
      mask          = mask - 1;
      return ttag & mask;
    } else {
      uint64_t mask = 1LL;
      mask <<= tag_bits;
      mask = mask - 1;
      return ttag & mask;
    }
  }

  int sync_all();
  int sync_heap(int part); // part varies between 0 and MULTI_CACHE_PARTITIONS
  int sync_header();
  int sync_partition(int partition);
  void sync_partitions(Continuation *cont);

  MultiCacheBase();
  virtual ~MultiCacheBase() { reset(); }
  virtual int
  get_elementsize()
  {
    ink_assert(0);
    return 0;
  }

  // Heap support
  UnsunkPtrRegistry unsunk[MULTI_CACHE_PARTITIONS];

  // -1 on error
  int ptr_to_partition(char *);
  // the user must pass in the offset field within the
  // MultiCacheBlock object.  The offset will be inserted
  // into the object on success and a pointer to the data
  // returned.  On failure, NULL is returned;
  void *alloc(int *poffset, int size);
  void update(int *poffset, int *old_poffset);
  void *ptr(int *poffset, int partition);
  int
  valid_offset(int offset)
  {
    int max;
    if (offset < halfspace_size())
      max = heap_used[0];
    else
      max = halfspace_size() + heap_used[1];
    return offset < max;
  }
  int
  valid_heap_pointer(char *p)
  {
    if (p < heap + halfspace_size())
      return p < heap + heap_used[0];
    else
      return p < heap + halfspace_size() + heap_used[1];
  }
  void copy_heap_data(char *src, int s, int *pi, int partition, MultiCacheHeapGC *gc);
  int
  halfspace_of(int o)
  {
    return o < halfspace_size() ? 0 : 1;
  }
  UnsunkPtrRegistry *fixup_heap_offsets(int partition, int before_used, UnsunkPtrRegistry *r = NULL, int base = 0);

  virtual void
  copy_heap(int partition, MultiCacheHeapGC *gc)
  {
    (void)partition;
    (void)gc;
  }

  //
  // Private
  //
  void
  alloc_mutexes()
  {
    for (int i = 0; i < MULTI_CACHE_PARTITIONS; i++)
      locks[i] = new_ProxyMutex();
  }
  PtrMutex locks[MULTI_CACHE_PARTITIONS]; // 1 lock per (buckets/partitions)
};

template <class C> struct MultiCache : public MultiCacheBase {
  int
  get_elementsize()
  {
    return sizeof(C);
  }

  MultiCacheBase *
  dup()
  {
    return new MultiCache<C>;
  }

  void rebuild_element(int buck, char *elem, RebuildMC &r);
  // -1 is corrupt, 0 == void (do not insert), 1 is OK
  virtual int
  rebuild_callout(C *c, RebuildMC &r)
  {
    (void)c;
    (void)r;
    return 1;
  }

  virtual void
  rebuild_insert_callout(C *c, RebuildMC &r)
  {
    (void)c;
    (void)r;
  }

  //
  // template operations
  //
  int level_of_block(C *b);
  bool match(uint64_t folded_md5, C *block);
  C *cache_bucket(uint64_t folded_md5, unsigned int level);
  C *insert_block(uint64_t folded_md5, C *new_block, unsigned int level);
  void flush(C *b, int bucket, unsigned int level);
  void delete_block(C *block);
  C *lookup_block(uint64_t folded_md5, unsigned int level);
  void copy_heap(int paritition, MultiCacheHeapGC *);
};

inline uint64_t
fold_md5(INK_MD5 const &md5)
{
  return md5.fold();
}

template <class C>
inline int
MultiCache<C>::level_of_block(C *b)
{
  if ((char *)b - data >= level_offset[1]) {
    if ((char *)b - data >= level_offset[2])
      return 2;
    return 1;
  }
  return 0;
}

template <class C>
inline C *
MultiCache<C>::cache_bucket(uint64_t folded_md5, unsigned int level)
{
  int bucket   = (int)(folded_md5 % buckets);
  char *offset = data + level_offset[level] + bucketsize[level] * bucket;
  return (C *)offset;
}

//
// Insert an entry
//
template <class C>
inline C *
MultiCache<C>::insert_block(uint64_t folded_md5, C *new_block, unsigned int level)
{
  C *b     = cache_bucket(folded_md5, level);
  C *block = NULL, *empty = NULL;
  int bucket = (int)(folded_md5 % buckets);
  int hits   = 0;

  // Find the entry
  //
  uint64_t tag = make_tag(folded_md5);
  int n_empty  = 0;

  for (block = b; block < b + elements[level]; block++) {
    if (block->is_empty() && !empty) {
      n_empty++;
      empty = block;
    }
    if (tag == block->tag())
      goto Lfound;
    hits += block->hits;
  }
  if (empty) {
    block = empty;
    goto Lfound;
  }

  {
    C *best   = NULL;
    int again = 1;
    do {
      // Find an entry previously backed to a higher level.
      // self scale the hits number within the bucket
      //
      unsigned int dec = 0;
      if (hits > ((max_hits / 2) + 1) * elements[level])
        dec = 1;
      for (block = b; block < b + elements[level]; block++) {
        if (block->backed && (!best || best->hits > block->hits))
          best = block;
        if (block->hits)
          block->hits -= dec;
      }
      if (best) {
        block = best;
        goto Lfound;
      }
      flush(b, bucket, level);
    } while (again--);
    ink_assert(!"cache flush failure");
  }

Lfound:
  if (new_block) {
    *block   = *new_block;
    int *hop = new_block->heap_offset_ptr();
    if (hop)
      update(block->heap_offset_ptr(), hop);
    block->backed = 0;
  } else
    block->reset();
  block->set_full(folded_md5, buckets);
  ink_assert(block->tag() == tag);
  return block;
}

#define REBUILD_FOLDED_MD5(_cl) ((_cl->tag() * (uint64_t)buckets + (uint64_t)bucket))

//
// This function ejects some number of entries.
//
template <class C>
inline void
MultiCache<C>::flush(C *b, int bucket, unsigned int level)
{
  C *block = NULL;
  // The comparison against the constant is redundant, but it
  // quiets the array_bounds error generated by g++ 4.9.2
  if (level < levels - 1 && level < (MULTI_CACHE_MAX_LEVELS - 1)) {
    if (level >= lowest_level(bucket))
      set_lowest_level(bucket, level + 1);
    for (block = b; block < b + elements[level]; block++) {
      ink_assert(!block->is_empty());
      insert_block(REBUILD_FOLDED_MD5(block), block, level + 1);
      block->backed = true;
    }
  } else {
    for (block = b; block < b + elements[level]; block++)
      if (!block->is_empty())
        block->backed = true;
  }
}

//
// Match a cache line and a folded md5 key
//
template <class C>
inline bool
MultiCache<C>::match(uint64_t folded_md5, C *block)
{
  return block->tag() == make_tag(folded_md5);
}

//
// This code is a bit of a mess and should probably be rewritten
//
template <class C>
inline void
MultiCache<C>::delete_block(C *b)
{
  if (b->backed) {
    unsigned int l = level_of_block(b);
    if (l < levels - 1) {
      int bucket = (((char *)b - data) - level_offset[l]) / bucketsize[l];
      C *x       = (C *)(data + level_offset[l + 1] + bucket * bucketsize[l + 1]);
      for (C *y = x; y < x + elements[l + 1]; y++)
        if (b->tag() == y->tag())
          delete_block(y);
    }
  }
  b->set_empty();
}

//
// Lookup an entry up to some level in the cache
//
template <class C>
inline C *
MultiCache<C>::lookup_block(uint64_t folded_md5, unsigned int level)
{
  C *b         = cache_bucket(folded_md5, 0);
  uint64_t tag = make_tag(folded_md5);
  int i        = 0;
  // Level 0
  for (i = 0; i < elements[0]; i++)
    if (tag == b[i].tag())
      return &b[i];
  if (level <= 0)
    return NULL;
  // Level 1
  b = cache_bucket(folded_md5, 1);
  for (i = 0; i < elements[1]; i++)
    if (tag == b[i].tag())
      return &b[i];
  if (level <= 1)
    return NULL;
  // Level 2
  b = cache_bucket(folded_md5, 2);
  for (i = 0; i < elements[2]; i++)
    if (tag == b[i].tag())
      return &b[i];
  return NULL;
}

template <class C>
inline void
MultiCache<C>::rebuild_element(int bucket, char *elem, RebuildMC &r)
{
  C *e = (C *)elem;
  if (!e->is_empty()) {
    r.total++;
    if (e->is_deleted())
      r.deleted++;
    if (e->backed)
      r.backed++;
    int res = rebuild_callout(e, r);
    if (res < 0)
      r.corrupt++;
    else if (!res)
      r.stale++;
    else {
      r.good++;
      if (lookup_block(REBUILD_FOLDED_MD5(e), levels - 1))
        if (!e->backed)
          r.duplicates++;
      C *new_e = insert_block(REBUILD_FOLDED_MD5(e), e, 0);
      rebuild_insert_callout(new_e, r);
    }
  }
}

template <class C>
inline void
MultiCache<C>::copy_heap(int partition, MultiCacheHeapGC *gc)
{
  int b = first_bucket_of_partition(partition);
  int n = buckets_of_partition(partition);
  for (unsigned int level = 0; level < levels; level++) {
    int e   = n * elements[level];
    char *d = data + level_offset[level] + b * bucketsize[level];
    C *x    = (C *)d;
    for (int i = 0; i < e; i++) {
      int s = x[i].heap_size();
      if (s) {
        int *pi = x[i].heap_offset_ptr();
        if (pi) {
          char *src = (char *)ptr(pi, partition);
          if (src) {
            if (heap_halfspace) {
              if (src >= heap + halfspace_size())
                continue;
            } else if (src < heap + halfspace_size())
              continue;
            copy_heap_data(src, s, pi, partition, gc);
          }
        }
      }
    }
  }
}

// store either free or in the cache, can be stolen for reconfiguration
void stealStore(Store &s, int blocks);
#endif /* _MultiCache_h_ */

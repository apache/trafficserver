/** @file

  Preservation of documents that would be overwritten by the write head.

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

#include "AggregateWriteBuffer.h"
#include "iocore/cache/CacheDefs.h"
#include "P_CacheDir.h"
#include "Stripe.h"

#include "iocore/eventsystem/EThread.h"
#include "iocore/eventsystem/ProxyAllocator.h"

#include "tscore/Allocator.h"
#include "tscore/CryptoHash.h"
#include "tscore/ink_platform.h"
#include "tscore/List.h"

#define EVACUATION_BUCKET_SIZE (2 * EVACUATION_SIZE) // 16MB
#define EVACUATION_SIZE        (2 * AGG_SIZE)        // 8MB
#define PIN_SCAN_EVERY         16                    // scan every 1/16 of disk

#define dir_offset_evac_bucket(_o) (_o / (EVACUATION_BUCKET_SIZE / CACHE_BLOCK_SIZE))
#define dir_evac_bucket(_e)        dir_offset_evac_bucket(dir_offset(_e))
#define offset_evac_bucket(_d, _o) \
  dir_offset_evac_bucket((_d->offset_to_vol_offset(_o)

class CacheEvacuateDocVC;

// Key and Earliest key for each fragment that needs to be evacuated
struct EvacuationKey {
  SLink<EvacuationKey> link;
  CryptoHash           key;
  CryptoHash           earliest_key;
};

struct EvacuationBlock {
  union {
    unsigned int init;
    struct {
      unsigned int done          : 1; // has been evacuated
      unsigned int pinned        : 1; // check pinning timeout
      unsigned int evacuate_head : 1; // check pinning timeout
      unsigned int unused        : 29;
    } f;
  };

  int readers;
  Dir dir;
  Dir new_dir;
  // we need to have a list of evacuationkeys because of collision.
  EvacuationKey       evac_frags;
  CacheEvacuateDocVC *earliest_evacuator;
  LINK(EvacuationBlock, link);
};

/**
 * Represents the collection of documents that must be rewritten to the cache
 * to avoid being overwritten. The documents themselves are not owned by this
 * table, but are referenced by it via a cache directory entry. If any
 * directory entry stored in this table is invalidated, this table is also
 * invalidated. Once a document has been rewritten, mark its block as done
 * and it will be removed on the next call to periodic_scan.
 *
 * This class is not safe for concurrent access. It should be protected
 * by a lock.
 *
 * @see Stripe
 */
class PreservationTable
{
public:
  int evacuate_size{};

  /**
   * The table of preserved documents.
   *
   * This is implemented as a hash table using separate chaining.
   */
  DLL<EvacuationBlock> *evacuate{nullptr};

  /**
   * Check whether the hash table may be indexed with the given offset.
   *
   * @param bucket An index into the hash table.
   * @return Returns true if the index is valid, false otherwise.
   */
  bool evac_bucket_valid(off_t bucket) const;

  /**
   * Force the preservation of the given document.
   *
   * @param dir The directory entry for the document to preserve.
   * @param pinned Whether the document is pinned (0 or 1).
   */
  void force_evacuate_head(Dir const *evac_dir, int pinned);

  /**
   * Acquire the evacuation block for @a dir.
   *
   * Any number of readers may acquire the block at a time to prevent the
   * block from being removed from the table. If no block for the directory
   * entry is in the table yet, one will be added with @a key.
   *
   * @param dir The directory entry to acquire.
   * @param key The key for the directory entry.
   * @return Returns 1 if a new block was created, otherwise 0.
   */
  int acquire(Dir const &dir, CacheKey const &key);

  /** Release the evacuation block for @a dir.
   *
   * When a block has been released once for every time it was acquired, it
   * may be removed from the table, invalidating all pointers to it. Note that
   * releasing more than once from the same reader may cause the block to be
   * removed from the table while other readers that acquired it think it's
   * valid. Be careful.
   *
   * A block that was evacuated with force_evacuate_head will not be removed
   * from the table when it is released.
   *
   * @param dir The directory entry to release.
   * @see force_evacuate_head
   */
  void release(Dir const &dir);

  /**
   * Remove completed documents from the table and add pinned documents.
   *
   * Documents that were acquired by a reader and not released are not removed.
   * Invalidates pointers to evacuation blocks unless they have been acquired.
   *
   * @param Stripe The stripe to scan for pinned documents to preserve.
   */
  void periodic_scan(Stripe *stripe);

private:
  void cleanup(Stripe const *stripe);
  void remove_finished_blocks(Stripe const *stripe, int bucket);
  void scan_for_pinned_documents(Stripe const *stripe);

  EvacuationBlock *find(Dir const &dir, int bucket) const;
};

inline bool
PreservationTable::evac_bucket_valid(off_t bucket) const
{
  return (bucket >= 0 && bucket < evacuate_size);
}

extern ClassAllocator<EvacuationBlock> evacuationBlockAllocator;
extern ClassAllocator<EvacuationKey>   evacuationKeyAllocator;

inline EvacuationBlock *
evacuation_block_exists(Dir const *dir, PreservationTable *stripe)
{
  auto bucket = dir_evac_bucket(dir);
  if (stripe->evac_bucket_valid(bucket)) {
    EvacuationBlock *b = stripe->evacuate[bucket].head;
    for (; b; b = b->link.next) {
      if (dir_offset(&b->dir) == dir_offset(dir)) {
        return b;
      }
    }
  }
  return nullptr;
}

inline EvacuationBlock *
new_EvacuationBlock()
{
  EvacuationBlock *b      = THREAD_ALLOC(evacuationBlockAllocator, this_ethread());
  b->init                 = 0;
  b->readers              = 0;
  b->earliest_evacuator   = nullptr;
  b->evac_frags.link.next = nullptr;
  return b;
}

inline void
free_EvacuationBlock(EvacuationBlock *b)
{
  EvacuationKey *e = b->evac_frags.link.next;
  while (e) {
    EvacuationKey *n = e->link.next;
    evacuationKeyAllocator.free(e);
    e = n;
  }
  THREAD_FREE(b, evacuationBlockAllocator, this_ethread());
}

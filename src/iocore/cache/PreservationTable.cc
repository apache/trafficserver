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

#include "P_CacheDir.h"
#include "P_CacheInternal.h"
#include "PreservationTable.h"

#include "AggregateWriteBuffer.h"
#include "Stripe.h"

#include "tsutil/DbgCtl.h"

#include <cinttypes>

DEF_DBG(cache_evac)

void
PreservationTable::force_evacuate_head(Dir const *evac_dir, int pinned)
{
  auto bucket = dir_evac_bucket(evac_dir);
  if (!evac_bucket_valid(bucket)) {
    DDbg(get_dbg_cache_evac(), "dir_evac_bucket out of bounds, skipping evacuate: %" PRId64 "(%d), %d, %d", bucket, evacuate_size,
         (int)dir_offset(evac_dir), (int)dir_phase(evac_dir));
    return;
  }

  // build an evacuation block for the object
  EvacuationBlock *b = evacuation_block_exists(evac_dir, this);
  // if we have already started evacuating this document, its too late
  // to evacuate the head...bad luck
  if (b && b->f.done) {
    return;
  }

  if (!b) {
    b      = new_EvacuationBlock();
    b->dir = *evac_dir;
    DDbg(get_dbg_cache_evac(), "force: %d, %d", (int)dir_offset(evac_dir), (int)dir_phase(evac_dir));
    evacuate[bucket].push(b);
  }
  b->f.pinned        = pinned;
  b->f.evacuate_head = 1;
  b->evac_frags.key.clear(); // ensure that the block gets evacuated no matter what
  b->readers = 0;            // ensure that the block does not disappear
}

void
PreservationTable::periodic_scan(Stripe *stripe)
{
  cleanup(stripe);
  scan_for_pinned_documents(stripe);
  if (stripe->header->write_pos == stripe->start) {
    stripe->scan_pos = stripe->start;
  }
  stripe->scan_pos += stripe->len / PIN_SCAN_EVERY;
}

void
PreservationTable::scan_for_pinned_documents(Stripe const *stripe)
{
  if (cache_config_permit_pinning) {
    // we can't evacuate anything between header->write_pos and
    // header->write_pos + AGG_SIZE.
    int ps = stripe->offset_to_vol_offset(stripe->header->write_pos + AGG_SIZE);
    int pe = stripe->offset_to_vol_offset(stripe->header->write_pos + 2 * EVACUATION_SIZE + (stripe->len / PIN_SCAN_EVERY));
    int vol_end_offset    = stripe->offset_to_vol_offset(stripe->len + stripe->skip);
    int before_end_of_vol = pe < vol_end_offset;
    DDbg(get_dbg_cache_evac(), "scan %d %d", ps, pe);
    for (int i = 0; i < stripe->direntries(); i++) {
      // is it a valid pinned object?
      if (!dir_is_empty(&stripe->dir[i]) && dir_pinned(&stripe->dir[i]) && dir_head(&stripe->dir[i])) {
        // select objects only within this PIN_SCAN region
        int o = dir_offset(&stripe->dir[i]);
        if (dir_phase(&stripe->dir[i]) == stripe->header->phase) {
          if (before_end_of_vol || o >= (pe - vol_end_offset)) {
            continue;
          }
        } else {
          if (o < ps || o >= pe) {
            continue;
          }
        }
        force_evacuate_head(&stripe->dir[i], 1);
      }
    }
  }
}

void
PreservationTable::cleanup(Stripe const *stripe)
{
  int64_t eo = ((stripe->header->write_pos - stripe->start) / CACHE_BLOCK_SIZE) + 1;
  int64_t e  = dir_offset_evac_bucket(eo);
  int64_t sx = e - (evacuate_size / PIN_SCAN_EVERY) - 1;
  int64_t s  = sx;
  int     i;

  if (e > evacuate_size) {
    e = evacuate_size;
  }
  if (sx < 0) {
    s = 0;
  }
  for (i = s; i < e; i++) {
    remove_finished_blocks(stripe, i);
  }

  // if we have wrapped, handle the end bit
  if (sx <= 0) {
    s = evacuate_size + sx - 2;
    if (s < 0) {
      s = 0;
    }
    for (i = s; i < evacuate_size; i++) {
      remove_finished_blocks(stripe, i);
    }
  }
}

inline void
PreservationTable::remove_finished_blocks(Stripe const *stripe, int bucket)
{
  EvacuationBlock *b = evac_bucket_valid(bucket) ? evacuate[bucket].head : nullptr;
  while (b) {
    if (b->f.done && ((stripe->header->phase != dir_phase(&b->dir) && stripe->header->write_pos > stripe->vol_offset(&b->dir)) ||
                      (stripe->header->phase == dir_phase(&b->dir) && stripe->header->write_pos <= stripe->vol_offset(&b->dir)))) {
      EvacuationBlock *x = b;
      DDbg(get_dbg_cache_evac(), "evacuate cleanup free %X offset %d", (int)b->evac_frags.key.slice32(0), (int)dir_offset(&b->dir));
      b = b->link.next;
      evacuate[bucket].remove(x);
      free_EvacuationBlock(x);
      continue;
    }
    b = b->link.next;
  }
}

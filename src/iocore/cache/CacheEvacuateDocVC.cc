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

// make sure there are no incomplete types

// aio
#include "../aio/P_AIO.h"

// inkcache
#include "iocore/cache/CacheDefs.h"
#include "P_CacheHttp.h"
#include "P_CacheInternal.h"
#include "P_CacheVol.h"
#include "iocore/cache/CacheEvacuateDocVC.h"

// tscore
#include "tscore/Diags.h"
#include "tscore/ink_assert.h"

// ts
#include "tsutil/DbgCtl.h"

namespace
{
DbgCtl dbg_ctl_cache_evac{"cache_evac"};
} // end anonymous namespace

int
CacheEvacuateDocVC::evacuateDocDone(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */)
{
  ink_assert(this->stripe->mutex->thread_holding == this_ethread());
  Doc *doc = reinterpret_cast<Doc *>(this->buf->data());
  DDbg(dbg_ctl_cache_evac, "evacuateDocDone %X o %d p %d new_o %d new_p %d", (int)key.slice32(0),
       (int)dir_offset(&this->overwrite_dir), (int)dir_phase(&this->overwrite_dir), (int)dir_offset(&this->dir),
       (int)dir_phase(&this->dir));
  int i = dir_evac_bucket(&this->overwrite_dir);
  // nasty beeping race condition, need to have the EvacuationBlock here
  EvacuationBlock *b = this->stripe->evac_bucket_valid(i) ? this->stripe->evacuate[i].head : nullptr;
  for (; b; b = b->link.next) {
    if (dir_offset(&b->dir) == dir_offset(&this->overwrite_dir)) {
      // If the document is single fragment (although not tied to the vector),
      // then we don't have to put the directory entry in the lookaside
      // buffer. But, we have no way of finding out if the document is
      // single fragment. doc->single_fragment() can be true for a multiple
      // fragment document since total_len and doc->len could be equal at
      // the time we write the fragment down. To be on the safe side, we
      // only overwrite the entry in the directory if its not a head.
      if (!dir_head(&this->overwrite_dir)) {
        // find the earliest key
        EvacuationKey *evac = &b->evac_frags;
        for (; evac && !(evac->key == doc->key); evac = evac->link.next) {
          ;
        }
        ink_assert(evac);
        if (!evac) {
          break;
        }
        if (evac->earliest_key.fold()) {
          DDbg(dbg_ctl_cache_evac, "evacdocdone: evacuating key %X earliest %X", evac->key.slice32(0),
               evac->earliest_key.slice32(0));
          EvacuationBlock *eblock = nullptr;
          Dir dir_tmp;
          dir_lookaside_probe(&evac->earliest_key, this->stripe, &dir_tmp, &eblock);
          if (eblock) {
            CacheEvacuateDocVC *earliest_evac  = eblock->earliest_evacuator;
            earliest_evac->total_len          += doc->data_len();
            if (earliest_evac->total_len == earliest_evac->doc_len) {
              dir_lookaside_fixup(&evac->earliest_key, this->stripe);
              free_CacheVC(earliest_evac);
            }
          }
        }
        dir_overwrite(&doc->key, this->stripe, &this->dir, &this->overwrite_dir);
      }
      // if the tag in the overwrite_dir matches the first_key in the
      // document, then it has to be the vector. We guarantee that
      // the first_key and the earliest_key will never collide (see
      // Cache::open_write). Once we know its the vector, we can
      // safely overwrite the first_key in the directory.
      if (dir_head(&this->overwrite_dir) && b->f.evacuate_head) {
        DDbg(dbg_ctl_cache_evac, "evacuateDocDone evacuate_head %X %X hlen %d offset %d", (int)key.slice32(0),
             (int)doc->key.slice32(0), doc->hlen, (int)dir_offset(&this->overwrite_dir));

        if (dir_compare_tag(&this->overwrite_dir, &doc->first_key)) {
          OpenDirEntry *cod;
          DDbg(dbg_ctl_cache_evac, "evacuating vector: %X %d", (int)doc->first_key.slice32(0),
               (int)dir_offset(&this->overwrite_dir));
          if ((cod = this->stripe->open_read(&doc->first_key))) {
            // writer  exists
            DDbg(dbg_ctl_cache_evac, "overwriting the open directory %X %d %d", (int)doc->first_key.slice32(0),
                 (int)dir_offset(&cod->first_dir), (int)dir_offset(&this->dir));
            cod->first_dir = this->dir;
          }
          if (dir_overwrite(&doc->first_key, this->stripe, &this->dir, &this->overwrite_dir)) {
            int64_t o = dir_offset(&this->overwrite_dir), n = dir_offset(&this->dir);
            this->stripe->ram_cache->fixup(&doc->first_key, static_cast<uint64_t>(o), static_cast<uint64_t>(n));
          }
        } else {
          DDbg(dbg_ctl_cache_evac, "evacuating earliest: %X %d", (int)doc->key.slice32(0), (int)dir_offset(&this->overwrite_dir));
          ink_assert(dir_compare_tag(&this->overwrite_dir, &doc->key));
          ink_assert(b->earliest_evacuator == this);
          this->total_len    += doc->data_len();
          this->first_key     = doc->first_key;
          this->earliest_dir  = this->dir;
          if (dir_probe(&this->first_key, this->stripe, &this->dir, &last_collision) > 0) {
            dir_lookaside_insert(b, this->stripe, &this->earliest_dir);
            // read the vector
            SET_HANDLER(&CacheEvacuateDocVC::evacuateReadHead);
            int ret = do_read_call(&this->first_key);
            if (ret == EVENT_RETURN) {
              return handleEvent(AIO_EVENT_DONE, nullptr);
            }
            return ret;
          }
        }
      }
      break;
    }
  }
  return free_CacheVC(this);
}

int
CacheEvacuateDocVC::evacuateReadHead(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */)
{
  // The evacuator vc shares the lock with the volition mutex
  ink_assert(this->stripe->mutex->thread_holding == this_ethread());
  cancel_trigger();
  Doc *doc                     = reinterpret_cast<Doc *>(this->buf->data());
  CacheHTTPInfo *alternate_tmp = nullptr;
  if (!io.ok()) {
    goto Ldone;
  }
  // a directory entry which is no longer valid may have been overwritten
  if (!dir_valid(this->stripe, &this->dir)) {
    last_collision = nullptr;
    goto Lcollision;
  }
  if (doc->magic != DOC_MAGIC || !(doc->first_key == this->first_key)) {
    goto Lcollision;
  }
  alternate_tmp = nullptr;
  if (doc->doc_type == CACHE_FRAG_TYPE_HTTP && doc->hlen) {
    // its an http document
    if (this->load_http_info(&vector, doc) != doc->hlen) {
      Note("bad vector detected during evacuation");
      goto Ldone;
    }
    alternate_index = get_alternate_index(&vector, earliest_key);
    if (alternate_index < 0) {
      goto Ldone;
    }
    alternate_tmp = vector.get(alternate_index);
    doc_len       = alternate_tmp->object_size_get();
    Dbg(dbg_ctl_cache_evac, "evacuateReadHead http earliest %X first: %X len: %" PRId64, earliest_key.slice32(0),
        this->first_key.slice32(0), doc_len);
  } else {
    // non-http document
    CacheKey next_key;
    next_CacheKey(&next_key, &doc->key);
    if (!(next_key == earliest_key)) {
      goto Ldone;
    }
    doc_len = doc->total_len;
    DDbg(dbg_ctl_cache_evac, "evacuateReadHead non-http earliest %X first: %X len: %" PRId64, earliest_key.slice32(0),
         this->first_key.slice32(0), doc_len);
  }
  if (doc_len == this->total_len) {
    // the whole document has been evacuated. Insert the directory
    // entry in the directory.
    dir_lookaside_fixup(&earliest_key, this->stripe);
    return free_CacheVC(this);
  }
  return EVENT_CONT;
Lcollision:
  if (dir_probe(&this->first_key, this->stripe, &this->dir, &last_collision)) {
    int ret = do_read_call(&this->first_key);
    if (ret == EVENT_RETURN) {
      return handleEvent(AIO_EVENT_DONE, nullptr);
    }
    return ret;
  }
Ldone:
  dir_lookaside_remove(&earliest_key, this->stripe);
  return free_CacheVC(this);
}

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

#include "P_Cache.h"

#define UINT_WRAP_LTE(_x, _y) (((_y) - (_x)) < INT_MAX) // exploit overflow
#define UINT_WRAP_GTE(_x, _y) (((_x) - (_y)) < INT_MAX) // exploit overflow
#define UINT_WRAP_LT(_x, _y) (((_x) - (_y)) >= INT_MAX) // exploit overflow

// Given a key, finds the index of the alternate which matches
// used to get the alternate which is actually present in the document
int
get_alternate_index(CacheHTTPInfoVector *cache_vector, CacheKey key)
{
  int alt_count = cache_vector->count();
  CacheHTTPInfo *obj;
  if (!alt_count) {
    return -1;
  }
  for (int i = 0; i < alt_count; i++) {
    obj = cache_vector->get(i);
    if (obj->compare_object_key(&key)) {
      // Debug("cache_key", "Resident alternate key  %X", key.slice32(0));
      return i;
    }
  }
  return -1;
}

// Adds/Deletes alternate to the od->vector (write_vector). If the vector
// is empty, deletes the directory entry pointing to the vector. Each
// CacheVC must write the vector down to disk after making changes. If we
// wait till the last writer, that writer will have the responsibility of
// of writing the vector even if the http state machine aborts.  This
// makes it easier to handle situations where writers abort.
int
CacheVC::updateVector(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */)
{
  cancel_trigger();
  if (od->reading_vec || od->writing_vec) {
    VC_SCHED_LOCK_RETRY();
  }
  int ret = 0;
  {
    CACHE_TRY_LOCK(lock, vol->mutex, mutex->thread_holding);
    if (!lock.is_locked() || od->writing_vec) {
      VC_SCHED_LOCK_RETRY();
    }

    int vec = alternate.valid();
    if (f.update) {
      // all Update cases. Need to get the alternate index.
      alternate_index = get_alternate_index(write_vector, update_key);
      Debug("cache_update", "updating alternate index %d frags %d", alternate_index,
            alternate_index >= 0 ? write_vector->get(alternate_index)->get_frag_offset_count() : -1);
      // if its an alternate delete
      if (!vec) {
        ink_assert(!total_len);
        if (alternate_index >= 0) {
          write_vector->remove(alternate_index, true);
          alternate_index = CACHE_ALT_REMOVED;
          if (!write_vector->count()) {
            dir_delete(&first_key, vol, &od->first_dir);
          }
        }
        // the alternate is not there any more. somebody might have
        // deleted it. Just close this writer
        if (alternate_index != CACHE_ALT_REMOVED || !write_vector->count()) {
          SET_HANDLER(&CacheVC::openWriteCloseDir);
          return openWriteCloseDir(EVENT_IMMEDIATE, nullptr);
        }
      }
      if (update_key == od->single_doc_key && (total_len || !vec)) {
        od->move_resident_alt = false;
      }
    }
    if (cache_config_http_max_alts > 1 && write_vector->count() >= cache_config_http_max_alts && alternate_index < 0) {
      if (od->move_resident_alt && get_alternate_index(write_vector, od->single_doc_key) == 0) {
        od->move_resident_alt = false;
      }
      write_vector->remove(0, true);
    }
    if (vec) {
      /* preserve fragment offset data from old info. This method is
         called iff the update is a header only update so the fragment
         data should remain valid.
      */
      if (alternate_index >= 0) {
        alternate.copy_frag_offsets_from(write_vector->get(alternate_index));
      }
      alternate_index = write_vector->insert(&alternate, alternate_index);
    }

    if (od->move_resident_alt && first_buf.get() && !od->has_multiple_writers()) {
      Doc *doc         = (Doc *)first_buf->data();
      int small_doc    = (int64_t)doc->data_len() < (int64_t)cache_config_alt_rewrite_max_size;
      int have_res_alt = doc->key == od->single_doc_key;
      // if the new alternate is not written with the vector
      // then move the old one with the vector
      // if its a header only update move the resident alternate
      // with the vector.
      // We are sure that the body of the resident alternate that we are
      // rewriting has not changed and the alternate is not being deleted,
      // since we set od->move_resident_alt  to 0 in that case
      // (in updateVector)
      if (small_doc && have_res_alt && (fragment || (f.update && !total_len))) {
        // for multiple fragment document, we must have done
        // CacheVC:openWriteCloseDataDone
        ink_assert(!fragment || f.data_done);
        od->move_resident_alt  = false;
        f.rewrite_resident_alt = 1;
        write_len              = doc->data_len();
        Debug("cache_update_alt", "rewriting resident alt size: %d key: %X, first_key: %X", write_len, doc->key.slice32(0),
              first_key.slice32(0));
      }
    }
    header_len      = write_vector->marshal_length();
    od->writing_vec = true;
    f.use_first_key = 1;
    SET_HANDLER(&CacheVC::openWriteCloseHeadDone);
    ret = do_write_call();
  }
  if (ret == EVENT_RETURN) {
    return handleEvent(AIO_EVENT_DONE, nullptr);
  }
  return ret;
}
/*
   The following fields of the CacheVC are used when writing down a fragment.
   Make sure that each of the fields is set to a valid value before calling
   this function
   - frag_type. Checked to see if a vector needs to be marshalled.
   - f.use_first_key. To decide if the vector should be marshalled and to set
     the doc->key to the appropriate key (first_key or earliest_key)
   - f.evac_vector. If set, the writer is pushed in the beginning of the
     agg queue. And if !f.evac_vector && !f.update the alternate->object_size
     is set to vc->total_len
   - f.readers.  If set, assumes that this is an evacuation, so the write
     is not aborted even if vol->agg_todo_size > agg_write_backlog
   - f.evacuator. If this is an evacuation.
   - f.rewrite_resident_alt. The resident alternate is rewritten.
   - f.update. Used only if the write_vector needs to be written to disk.
     Used to set the length of the alternate to total_len.
   - write_vector. Used only if frag_type == CACHE_FRAG_TYPE_HTTP &&
     (f.use_fist_key || f.evac_vector) is set. Write_vector is written to disk
   - alternate_index. Used only if write_vector needs to be written to disk.
     Used to find out the VC's alternate in the write_vector and set its
     length to tatal_len.
   - write_len. The number of bytes for this fragment.
   - total_len. The total number of bytes for the document so far.
     Doc->total_len and alternate's total len is set to this value.
   - first_key. Doc's first_key is set to this value.
   - pin_in_cache. Doc's pinned value is set to this + Thread::get_hrtime().
   - earliest_key. If f.use_first_key, Doc's key is set to this value.
   - key. If !f.use_first_key, Doc's key is set to this value.
   - blocks. Used only if write_len is set. Data to be written
   - offset. Used only if write_len is set. offset into the block to copy
     the data from.
   - buf. Used only if f.evacuator is set. Should point to the old document.
   The functions sets the length, offset, pinned, head and phase of vc->dir.
   */

int
CacheVC::handleWrite(int event, Event * /* e ATS_UNUSED */)
{
  // plain write case
  ink_assert(!trigger);
  frag_len = 0;

  set_agg_write_in_progress();
  POP_HANDLER;
  agg_len = vol->round_to_approx_size(write_len + header_len + frag_len + sizeof(Doc));
  vol->agg_todo_size += agg_len;
  bool agg_error = (agg_len > AGG_SIZE || header_len + sizeof(Doc) > MAX_FRAG_SIZE ||
                    (!f.readers && (vol->agg_todo_size > cache_config_agg_write_backlog + AGG_SIZE) && write_len));
#ifdef CACHE_AGG_FAIL_RATE
  agg_error = agg_error || ((uint32_t)mutex->thread_holding->generator.random() < (uint32_t)(UINT_MAX * CACHE_AGG_FAIL_RATE));
#endif
  bool max_doc_error = (cache_config_max_doc_size && (cache_config_max_doc_size < vio.ndone ||
                                                      (vio.nbytes != INT64_MAX && (cache_config_max_doc_size < vio.nbytes))));

  if (agg_error || max_doc_error) {
    CACHE_INCREMENT_DYN_STAT(cache_write_backlog_failure_stat);
    CACHE_INCREMENT_DYN_STAT(base_stat + CACHE_STAT_FAILURE);
    vol->agg_todo_size -= agg_len;
    io.aio_result = AIO_SOFT_FAILURE;
    if (event == EVENT_CALL) {
      return EVENT_RETURN;
    }
    return handleEvent(AIO_EVENT_DONE, nullptr);
  }
  ink_assert(agg_len <= AGG_SIZE);
  if (f.evac_vector) {
    vol->agg.push(this);
  } else {
    vol->agg.enqueue(this);
  }
  if (!vol->is_io_in_progress()) {
    return vol->aggWrite(event, this);
  }
  return EVENT_CONT;
}

static char *
iobufferblock_memcpy(char *p, int len, IOBufferBlock *ab, int offset)
{
  IOBufferBlock *b = ab;
  while (b && len >= 0) {
    char *start   = b->_start;
    char *end     = b->_end;
    int max_bytes = end - start;
    max_bytes -= offset;
    if (max_bytes <= 0) {
      offset = -max_bytes;
      b      = b->next.get();
      continue;
    }
    int bytes = len;
    if (bytes >= max_bytes) {
      bytes = max_bytes;
    }
    ::memcpy(p, start + offset, bytes);
    p += bytes;
    len -= bytes;
    b      = b->next.get();
    offset = 0;
  }
  return p;
}

EvacuationBlock *
Vol::force_evacuate_head(Dir *evac_dir, int pinned)
{
  // build an evacuation block for the object
  EvacuationBlock *b = evacuation_block_exists(evac_dir, this);
  // if we have already started evacuating this document, its too late
  // to evacuate the head...bad luck
  if (b && b->f.done) {
    return b;
  }

  if (!b) {
    b      = new_EvacuationBlock(mutex->thread_holding);
    b->dir = *evac_dir;
    DDebug("cache_evac", "force: %d, %d", (int)dir_offset(evac_dir), (int)dir_phase(evac_dir));
    evacuate[dir_evac_bucket(evac_dir)].push(b);
  }
  b->f.pinned        = pinned;
  b->f.evacuate_head = 1;
  b->evac_frags.key  = zero_key; // ensure that the block gets
  // evacuated no matter what
  b->readers = 0; // ensure that the block does not disappear
  return b;
}

void
Vol::scan_for_pinned_documents()
{
  if (cache_config_permit_pinning) {
    // we can't evacuate anything between header->write_pos and
    // header->write_pos + AGG_SIZE.
    int ps                = this->offset_to_vol_offset(header->write_pos + AGG_SIZE);
    int pe                = this->offset_to_vol_offset(header->write_pos + 2 * EVACUATION_SIZE + (len / PIN_SCAN_EVERY));
    int vol_end_offset    = this->offset_to_vol_offset(len + skip);
    int before_end_of_vol = pe < vol_end_offset;
    DDebug("cache_evac", "scan %d %d", ps, pe);
    for (int i = 0; i < this->direntries(); i++) {
      // is it a valid pinned object?
      if (!dir_is_empty(&dir[i]) && dir_pinned(&dir[i]) && dir_head(&dir[i])) {
        // select objects only within this PIN_SCAN region
        int o = dir_offset(&dir[i]);
        if (dir_phase(&dir[i]) == header->phase) {
          if (before_end_of_vol || o >= (pe - vol_end_offset)) {
            continue;
          }
        } else {
          if (o < ps || o >= pe) {
            continue;
          }
        }
        force_evacuate_head(&dir[i], 1);
        //      DDebug("cache_evac", "scan pinned at offset %d %d %d %d %d %d",
        //            (int)dir_offset(&b->dir), ps, o , pe, i, (int)b->f.done);
      }
    }
  }
}

/* NOTE:: This state can be called by an AIO thread, so DON'T DON'T
   DON'T schedule any events on this thread using VC_SCHED_XXX or
   mutex->thread_holding->schedule_xxx_local(). ALWAYS use
   eventProcessor.schedule_xxx().
   */
int
Vol::aggWriteDone(int event, Event *e)
{
  cancel_trigger();

  // ensure we have the cacheDirSync lock if we intend to call it later
  // retaking the current mutex recursively is a NOOP
  CACHE_TRY_LOCK(lock, dir_sync_waiting ? cacheDirSync->mutex : mutex, mutex->thread_holding);
  if (!lock.is_locked()) {
    eventProcessor.schedule_in(this, HRTIME_MSECONDS(cache_config_mutex_retry_delay));
    return EVENT_CONT;
  }
  if (io.ok()) {
    header->last_write_pos = header->write_pos;
    header->write_pos += io.aiocb.aio_nbytes;
    ink_assert(header->write_pos >= start);
    DDebug("cache_agg", "Dir %s, Write: %" PRIu64 ", last Write: %" PRIu64 "", hash_text.get(), header->write_pos,
           header->last_write_pos);
    ink_assert(header->write_pos == header->agg_pos);
    if (header->write_pos + EVACUATION_SIZE > scan_pos) {
      periodic_scan();
    }
    agg_buf_pos = 0;
    header->write_serial++;
  } else {
    // delete all the directory entries that we inserted
    // for fragments is this aggregation buffer
    Debug("cache_disk_error", "Write error on disk %s\n \
              write range : [%" PRIu64 " - %" PRIu64 " bytes]  [%" PRIu64 " - %" PRIu64 " blocks] \n",
          hash_text.get(), (uint64_t)io.aiocb.aio_offset, (uint64_t)io.aiocb.aio_offset + io.aiocb.aio_nbytes,
          (uint64_t)io.aiocb.aio_offset / CACHE_BLOCK_SIZE,
          (uint64_t)(io.aiocb.aio_offset + io.aiocb.aio_nbytes) / CACHE_BLOCK_SIZE);
    Dir del_dir;
    dir_clear(&del_dir);
    for (int done = 0; done < agg_buf_pos;) {
      Doc *doc = (Doc *)(agg_buffer + done);
      dir_set_offset(&del_dir, header->write_pos + done);
      dir_delete(&doc->key, this, &del_dir);
      done += round_to_approx_size(doc->len);
    }
    agg_buf_pos = 0;
  }
  set_io_not_in_progress();
  // callback ready sync CacheVCs
  CacheVC *c = nullptr;
  while ((c = sync.dequeue())) {
    if (UINT_WRAP_LTE(c->write_serial + 2, header->write_serial)) {
      c->initial_thread->schedule_imm_signal(c, AIO_EVENT_DONE);
    } else {
      sync.push(c); // put it back on the front
      break;
    }
  }
  if (dir_sync_waiting) {
    dir_sync_waiting = false;
    cacheDirSync->handleEvent(EVENT_IMMEDIATE, nullptr);
  }
  if (agg.head || sync.head) {
    return aggWrite(event, e);
  }
  return EVENT_CONT;
}

CacheVC *
new_DocEvacuator(int nbytes, Vol *vol)
{
  CacheVC *c        = new_CacheVC(vol);
  ProxyMutex *mutex = vol->mutex.get();
  c->base_stat      = cache_evacuate_active_stat;
  CACHE_INCREMENT_DYN_STAT(c->base_stat + CACHE_STAT_ACTIVE);
  c->buf          = new_IOBufferData(iobuffer_size_to_index(nbytes, MAX_BUFFER_SIZE_INDEX), MEMALIGNED);
  c->vol          = vol;
  c->f.evacuator  = 1;
  c->earliest_key = zero_key;
  SET_CONTINUATION_HANDLER(c, &CacheVC::evacuateDocDone);
  return c;
}

int
CacheVC::evacuateReadHead(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */)
{
  // The evacuator vc shares the lock with the volition mutex
  ink_assert(vol->mutex->thread_holding == this_ethread());
  cancel_trigger();
  Doc *doc                     = (Doc *)buf->data();
  CacheHTTPInfo *alternate_tmp = nullptr;
  if (!io.ok()) {
    goto Ldone;
  }
  // a directory entry which is nolonger valid may have been overwritten
  if (!dir_valid(vol, &dir)) {
    last_collision = nullptr;
    goto Lcollision;
  }
  if (doc->magic != DOC_MAGIC || !(doc->first_key == first_key)) {
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
    Debug("cache_evac", "evacuateReadHead http earliest %X first: %X len: %" PRId64, first_key.slice32(0), earliest_key.slice32(0),
          doc_len);
  } else {
    // non-http document
    CacheKey next_key;
    next_CacheKey(&next_key, &doc->key);
    if (!(next_key == earliest_key)) {
      goto Ldone;
    }
    doc_len = doc->total_len;
    DDebug("cache_evac", "evacuateReadHead non-http earliest %X first: %X len: %" PRId64, first_key.slice32(0),
           earliest_key.slice32(0), doc_len);
  }
  if (doc_len == total_len) {
    // the whole document has been evacuated. Insert the directory
    // entry in the directory.
    dir_lookaside_fixup(&earliest_key, vol);
    return free_CacheVC(this);
  }
  return EVENT_CONT;
Lcollision:
  if (dir_probe(&first_key, vol, &dir, &last_collision)) {
    int ret = do_read_call(&first_key);
    if (ret == EVENT_RETURN) {
      return handleEvent(AIO_EVENT_DONE, nullptr);
    }
    return ret;
  }
Ldone:
  dir_lookaside_remove(&earliest_key, vol);
  return free_CacheVC(this);
}

int
CacheVC::evacuateDocDone(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */)
{
  ink_assert(vol->mutex->thread_holding == this_ethread());
  Doc *doc = (Doc *)buf->data();
  DDebug("cache_evac", "evacuateDocDone %X o %d p %d new_o %d new_p %d", (int)key.slice32(0), (int)dir_offset(&overwrite_dir),
         (int)dir_phase(&overwrite_dir), (int)dir_offset(&dir), (int)dir_phase(&dir));
  int i = dir_evac_bucket(&overwrite_dir);
  // nasty beeping race condition, need to have the EvacuationBlock here
  EvacuationBlock *b = vol->evacuate[i].head;
  for (; b; b = b->link.next) {
    if (dir_offset(&b->dir) == dir_offset(&overwrite_dir)) {
      // If the document is single fragment (although not tied to the vector),
      // then we don't have to put the directory entry in the lookaside
      // buffer. But, we have no way of finding out if the document is
      // single fragment. doc->single_fragment() can be true for a multiple
      // fragment document since total_len and doc->len could be equal at
      // the time we write the fragment down. To be on the safe side, we
      // only overwrite the entry in the directory if its not a head.
      if (!dir_head(&overwrite_dir)) {
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
          DDebug("cache_evac", "evacdocdone: evacuating key %X earliest %X", evac->key.slice32(0), evac->earliest_key.slice32(0));
          EvacuationBlock *eblock = nullptr;
          Dir dir_tmp;
          dir_lookaside_probe(&evac->earliest_key, vol, &dir_tmp, &eblock);
          if (eblock) {
            CacheVC *earliest_evac = eblock->earliest_evacuator;
            earliest_evac->total_len += doc->data_len();
            if (earliest_evac->total_len == earliest_evac->doc_len) {
              dir_lookaside_fixup(&evac->earliest_key, vol);
              free_CacheVC(earliest_evac);
            }
          }
        }
        dir_overwrite(&doc->key, vol, &dir, &overwrite_dir);
      }
      // if the tag in the overwrite_dir matches the first_key in the
      // document, then it has to be the vector. We gaurantee that
      // the first_key and the earliest_key will never collide (see
      // Cache::open_write). Once we know its the vector, we can
      // safely overwrite the first_key in the directory.
      if (dir_head(&overwrite_dir) && b->f.evacuate_head) {
        DDebug("cache_evac", "evacuateDocDone evacuate_head %X %X hlen %d offset %d", (int)key.slice32(0), (int)doc->key.slice32(0),
               doc->hlen, (int)dir_offset(&overwrite_dir));

        if (dir_compare_tag(&overwrite_dir, &doc->first_key)) {
          OpenDirEntry *cod;
          DDebug("cache_evac", "evacuating vector: %X %d", (int)doc->first_key.slice32(0), (int)dir_offset(&overwrite_dir));
          if ((cod = vol->open_read(&doc->first_key))) {
            // writer  exists
            DDebug("cache_evac", "overwriting the open directory %X %d %d", (int)doc->first_key.slice32(0),
                   (int)dir_offset(&cod->first_dir), (int)dir_offset(&dir));
            cod->first_dir = dir;
          }
          if (dir_overwrite(&doc->first_key, vol, &dir, &overwrite_dir)) {
            int64_t o = dir_offset(&overwrite_dir), n = dir_offset(&dir);
            vol->ram_cache->fixup(&doc->first_key, (uint32_t)(o >> 32), (uint32_t)o, (uint32_t)(n >> 32), (uint32_t)n);
          }
        } else {
          DDebug("cache_evac", "evacuating earliest: %X %d", (int)doc->key.slice32(0), (int)dir_offset(&overwrite_dir));
          ink_assert(dir_compare_tag(&overwrite_dir, &doc->key));
          ink_assert(b->earliest_evacuator == this);
          total_len += doc->data_len();
          first_key    = doc->first_key;
          earliest_dir = dir;
          if (dir_probe(&first_key, vol, &dir, &last_collision) > 0) {
            dir_lookaside_insert(b, vol, &earliest_dir);
            // read the vector
            SET_HANDLER(&CacheVC::evacuateReadHead);
            int ret = do_read_call(&first_key);
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

static int
evacuate_fragments(CacheKey *key, CacheKey *earliest_key, int force, Vol *vol)
{
  Dir dir, *last_collision = nullptr;
  int i = 0;
  while (dir_probe(key, vol, &dir, &last_collision)) {
    // next fragment cannot be a head...if it is, it must have been a
    // directory collision.
    if (dir_head(&dir)) {
      continue;
    }
    EvacuationBlock *b = evacuation_block_exists(&dir, vol);
    if (!b) {
      b                          = new_EvacuationBlock(vol->mutex->thread_holding);
      b->dir                     = dir;
      b->evac_frags.key          = *key;
      b->evac_frags.earliest_key = *earliest_key;
      vol->evacuate[dir_evac_bucket(&dir)].push(b);
      i++;
    } else {
      ink_assert(dir_offset(&dir) == dir_offset(&b->dir));
      ink_assert(dir_phase(&dir) == dir_phase(&b->dir));
      EvacuationKey *evac_frag = evacuationKeyAllocator.alloc();
      evac_frag->key           = *key;
      evac_frag->earliest_key  = *earliest_key;
      evac_frag->link.next     = b->evac_frags.link.next;
      b->evac_frags.link.next  = evac_frag;
    }
    if (force) {
      b->readers = 0;
    }
    DDebug("cache_evac", "next fragment %X Earliest: %X offset %d phase %d force %d", (int)key->slice32(0),
           (int)earliest_key->slice32(0), (int)dir_offset(&dir), (int)dir_phase(&dir), force);
  }
  return i;
}

int
Vol::evacuateWrite(CacheVC *evacuator, int event, Event *e)
{
  // push to front of aggregation write list, so it is written first

  evacuator->agg_len = round_to_approx_size(((Doc *)evacuator->buf->data())->len);
  agg_todo_size += evacuator->agg_len;
  /* insert the evacuator after all the other evacuators */
  CacheVC *cur   = (CacheVC *)agg.head;
  CacheVC *after = nullptr;
  for (; cur && cur->f.evacuator; cur = (CacheVC *)cur->link.next) {
    after = cur;
  }
  ink_assert(evacuator->agg_len <= AGG_SIZE);
  agg.insert(evacuator, after);
  return aggWrite(event, e);
}

int
Vol::evacuateDocReadDone(int event, Event *e)
{
  cancel_trigger();
  if (event != AIO_EVENT_DONE) {
    return EVENT_DONE;
  }
  ink_assert(is_io_in_progress());
  set_io_not_in_progress();
  ink_assert(mutex->thread_holding == this_ethread());
  Doc *doc = (Doc *)doc_evacuator->buf->data();
  CacheKey next_key;
  EvacuationBlock *b = nullptr;
  if (doc->magic != DOC_MAGIC) {
    Debug("cache_evac", "DOC magic: %X %d", (int)dir_tag(&doc_evacuator->overwrite_dir),
          (int)dir_offset(&doc_evacuator->overwrite_dir));
    ink_assert(doc->magic == DOC_MAGIC);
    goto Ldone;
  }
  DDebug("cache_evac", "evacuateDocReadDone %X offset %d", (int)doc->key.slice32(0),
         (int)dir_offset(&doc_evacuator->overwrite_dir));

  b = evacuate[dir_evac_bucket(&doc_evacuator->overwrite_dir)].head;
  while (b) {
    if (dir_offset(&b->dir) == dir_offset(&doc_evacuator->overwrite_dir)) {
      break;
    }
    b = b->link.next;
  }
  if (!b) {
    goto Ldone;
  }
  if ((b->f.pinned && !b->readers) && doc->pinned < (uint32_t)(Thread::get_hrtime() / HRTIME_SECOND)) {
    goto Ldone;
  }

  if (dir_head(&b->dir) && b->f.evacuate_head) {
    ink_assert(!b->evac_frags.key.fold());
    // if its a head (vector), evacuation is real simple...we just
    // need to write this vector down and overwrite the directory entry.
    if (dir_compare_tag(&b->dir, &doc->first_key)) {
      doc_evacuator->key = doc->first_key;
      b->evac_frags.key  = doc->first_key;
      DDebug("cache_evac", "evacuating vector %X offset %d", (int)doc->first_key.slice32(0),
             (int)dir_offset(&doc_evacuator->overwrite_dir));
      b->f.unused = 57;
    } else {
      // if its an earliest fragment (alternate) evacuation, things get
      // a little tricky. We have to propagate the earliest key to the next
      // fragments for this alternate. The last fragment to be evacuated
      // fixes up the lookaside buffer.
      doc_evacuator->key          = doc->key;
      doc_evacuator->earliest_key = doc->key;
      b->evac_frags.key           = doc->key;
      b->evac_frags.earliest_key  = doc->key;
      b->earliest_evacuator       = doc_evacuator;
      DDebug("cache_evac", "evacuating earliest %X %X evac: %p offset: %d", (int)b->evac_frags.key.slice32(0),
             (int)doc->key.slice32(0), doc_evacuator, (int)dir_offset(&doc_evacuator->overwrite_dir));
      b->f.unused = 67;
    }
  } else {
    // find which key matches the document
    EvacuationKey *ek = &b->evac_frags;
    for (; ek && !(ek->key == doc->key); ek = ek->link.next) {
      ;
    }
    if (!ek) {
      b->f.unused = 77;
      goto Ldone;
    }
    doc_evacuator->key          = ek->key;
    doc_evacuator->earliest_key = ek->earliest_key;
    DDebug("cache_evac", "evacuateDocReadDone key: %X earliest: %X", (int)ek->key.slice32(0), (int)ek->earliest_key.slice32(0));
    b->f.unused = 87;
  }
  // if the tag in the c->dir does match the first_key in the
  // document, then it has to be the earliest fragment. We gaurantee that
  // the first_key and the earliest_key will never collide (see
  // Cache::open_write).
  if (!dir_head(&b->dir) || !dir_compare_tag(&b->dir, &doc->first_key)) {
    next_CacheKey(&next_key, &doc->key);
    evacuate_fragments(&next_key, &doc_evacuator->earliest_key, !b->readers, this);
  }
  return evacuateWrite(doc_evacuator, event, e);
Ldone:
  free_CacheVC(doc_evacuator);
  doc_evacuator = nullptr;
  return aggWrite(event, e);
}

int
Vol::evac_range(off_t low, off_t high, int evac_phase)
{
  off_t s = this->offset_to_vol_offset(low);
  off_t e = this->offset_to_vol_offset(high);
  int si  = dir_offset_evac_bucket(s);
  int ei  = dir_offset_evac_bucket(e);

  for (int i = si; i <= ei; i++) {
    EvacuationBlock *b     = evacuate[i].head;
    EvacuationBlock *first = nullptr;
    int64_t first_offset   = INT64_MAX;
    for (; b; b = b->link.next) {
      int64_t offset = dir_offset(&b->dir);
      int phase      = dir_phase(&b->dir);
      if (offset >= s && offset < e && !b->f.done && phase == evac_phase) {
        if (offset < first_offset) {
          first        = b;
          first_offset = offset;
        }
      }
    }
    if (first) {
      first->f.done       = 1;
      io.aiocb.aio_fildes = fd;
      io.aiocb.aio_nbytes = dir_approx_size(&first->dir);
      io.aiocb.aio_offset = this->vol_offset(&first->dir);
      if ((off_t)(io.aiocb.aio_offset + io.aiocb.aio_nbytes) > (off_t)(skip + len)) {
        io.aiocb.aio_nbytes = skip + len - io.aiocb.aio_offset;
      }
      doc_evacuator                = new_DocEvacuator(io.aiocb.aio_nbytes, this);
      doc_evacuator->overwrite_dir = first->dir;

      io.aiocb.aio_buf = doc_evacuator->buf->data();
      io.action        = this;
      io.thread        = AIO_CALLBACK_THREAD_ANY;
      DDebug("cache_evac", "evac_range evacuating %X %d", (int)dir_tag(&first->dir), (int)dir_offset(&first->dir));
      SET_HANDLER(&Vol::evacuateDocReadDone);
      ink_assert(ink_aio_read(&io) >= 0);
      return -1;
    }
  }
  return 0;
}

static int
agg_copy(char *p, CacheVC *vc)
{
  Vol *vol = vc->vol;
  off_t o  = vol->header->write_pos + vol->agg_buf_pos;

  if (!vc->f.evacuator) {
    Doc *doc                   = (Doc *)p;
    IOBufferBlock *res_alt_blk = nullptr;

    uint32_t len = vc->write_len + vc->header_len + vc->frag_len + sizeof(Doc);
    ink_assert(vc->frag_type != CACHE_FRAG_TYPE_HTTP || len != sizeof(Doc));
    ink_assert(vol->round_to_approx_size(len) == vc->agg_len);
    // update copy of directory entry for this document
    dir_set_approx_size(&vc->dir, vc->agg_len);
    dir_set_offset(&vc->dir, vol->offset_to_vol_offset(o));
    ink_assert(vol->vol_offset(&vc->dir) < (vol->skip + vol->len));
    dir_set_phase(&vc->dir, vol->header->phase);

    // fill in document header
    doc->magic       = DOC_MAGIC;
    doc->len         = len;
    doc->hlen        = vc->header_len;
    doc->doc_type    = vc->frag_type;
    doc->v_major     = CACHE_DB_MAJOR_VERSION;
    doc->v_minor     = CACHE_DB_MINOR_VERSION;
    doc->unused      = 0; // force this for forward compatibility.
    doc->total_len   = vc->total_len;
    doc->first_key   = vc->first_key;
    doc->sync_serial = vol->header->sync_serial;
    vc->write_serial = doc->write_serial = vol->header->write_serial;
    doc->checksum                        = DOC_NO_CHECKSUM;
    if (vc->pin_in_cache) {
      dir_set_pinned(&vc->dir, 1);
      doc->pinned = (uint32_t)(Thread::get_hrtime() / HRTIME_SECOND) + vc->pin_in_cache;
    } else {
      dir_set_pinned(&vc->dir, 0);
      doc->pinned = 0;
    }

    if (vc->f.use_first_key) {
      if (doc->data_len() || vc->f.allow_empty_doc) {
        doc->key = vc->earliest_key;
      } else { // the vector is being written by itself
        prev_CacheKey(&doc->key, &vc->earliest_key);
      }
      dir_set_head(&vc->dir, true);
    } else {
      doc->key = vc->key;
      dir_set_head(&vc->dir, !vc->fragment);
    }

    if (vc->f.rewrite_resident_alt) {
      ink_assert(vc->f.use_first_key);
      Doc *res_doc   = (Doc *)vc->first_buf->data();
      res_alt_blk    = new_IOBufferBlock(vc->first_buf, res_doc->data_len(), sizeof(Doc) + res_doc->hlen);
      doc->key       = res_doc->key;
      doc->total_len = res_doc->data_len();
    }
    // update the new_info object_key, and total_len and dirinfo
    if (vc->header_len) {
      ink_assert(vc->f.use_first_key);
      if (vc->frag_type == CACHE_FRAG_TYPE_HTTP) {
        ink_assert(vc->write_vector->count() > 0);
        if (!vc->f.update && !vc->f.evac_vector) {
          ink_assert(!(vc->first_key == zero_key));
          CacheHTTPInfo *http_info = vc->write_vector->get(vc->alternate_index);
          http_info->object_size_set(vc->total_len);
        }
        // update + data_written =>  Update case (b)
        // need to change the old alternate's object length
        if (vc->f.update && vc->total_len) {
          CacheHTTPInfo *http_info = vc->write_vector->get(vc->alternate_index);
          http_info->object_size_set(vc->total_len);
        }
        ink_assert(!(((uintptr_t)&doc->hdr()[0]) & HDR_PTR_ALIGNMENT_MASK));
        ink_assert(vc->header_len == vc->write_vector->marshal(doc->hdr(), vc->header_len));
      } else {
        memcpy(doc->hdr(), vc->header_to_write, vc->header_len);
      }
      // the single fragment flag is not used in the write call.
      // putting it in for completeness.
      vc->f.single_fragment = doc->single_fragment();
    }
    // move data
    if (vc->write_len) {
      {
        ProxyMutex *mutex ATS_UNUSED = vc->vol->mutex.get();
        ink_assert(mutex->thread_holding == this_ethread());
        CACHE_DEBUG_SUM_DYN_STAT(cache_write_bytes_stat, vc->write_len);
      }
      if (vc->f.rewrite_resident_alt) {
        iobufferblock_memcpy(doc->data(), vc->write_len, res_alt_blk, 0);
      } else {
        iobufferblock_memcpy(doc->data(), vc->write_len, vc->blocks.get(), vc->offset);
      }
#ifdef VERIFY_JTEST_DATA
      if (f.use_first_key && header_len) {
        int ib = 0, xd = 0;
        char xx[500];
        new_info.request_get().url_get().print(xx, 500, &ib, &xd);
        char *x = xx;
        for (int q = 0; q < 3; q++)
          x = strchr(x + 1, '/');
        ink_assert(!memcmp(doc->hdr(), x, ib - (x - xx)));
      }
#endif
    }
    if (cache_config_enable_checksum) {
      doc->checksum = 0;
      for (char *b = doc->hdr(); b < (char *)doc + doc->len; b++) {
        doc->checksum += *b;
      }
    }
    if (vc->frag_type == CACHE_FRAG_TYPE_HTTP && vc->f.single_fragment) {
      ink_assert(doc->hlen);
    }

    if (res_alt_blk) {
      res_alt_blk->free();
    }

    return vc->agg_len;
  } else {
    // for evacuated documents, copy the data, and update directory
    Doc *doc = (Doc *)vc->buf->data();
    int l    = vc->vol->round_to_approx_size(doc->len);
    {
      ProxyMutex *mutex ATS_UNUSED = vc->vol->mutex.get();
      ink_assert(mutex->thread_holding == this_ethread());
      CACHE_DEBUG_INCREMENT_DYN_STAT(cache_gc_frags_evacuated_stat);
      CACHE_DEBUG_SUM_DYN_STAT(cache_gc_bytes_evacuated_stat, l);
    }

    doc->sync_serial  = vc->vol->header->sync_serial;
    doc->write_serial = vc->vol->header->write_serial;

    memcpy(p, doc, doc->len);

    vc->dir = vc->overwrite_dir;
    dir_set_offset(&vc->dir, vc->vol->offset_to_vol_offset(o));
    dir_set_phase(&vc->dir, vc->vol->header->phase);
    return l;
  }
}

inline void
Vol::evacuate_cleanup_blocks(int i)
{
  EvacuationBlock *b = evacuate[i].head;
  while (b) {
    if (b->f.done && ((header->phase != dir_phase(&b->dir) && header->write_pos > this->vol_offset(&b->dir)) ||
                      (header->phase == dir_phase(&b->dir) && header->write_pos <= this->vol_offset(&b->dir)))) {
      EvacuationBlock *x = b;
      DDebug("cache_evac", "evacuate cleanup free %X offset %d", (int)b->evac_frags.key.slice32(0), (int)dir_offset(&b->dir));
      b = b->link.next;
      evacuate[i].remove(x);
      free_EvacuationBlock(x, mutex->thread_holding);
      continue;
    }
    b = b->link.next;
  }
}

void
Vol::evacuate_cleanup()
{
  int64_t eo = ((header->write_pos - start) / CACHE_BLOCK_SIZE) + 1;
  int64_t e  = dir_offset_evac_bucket(eo);
  int64_t sx = e - (evacuate_size / PIN_SCAN_EVERY) - 1;
  int64_t s  = sx;
  int i;

  if (e > evacuate_size) {
    e = evacuate_size;
  }
  if (sx < 0) {
    s = 0;
  }
  for (i = s; i < e; i++) {
    evacuate_cleanup_blocks(i);
  }

  // if we have wrapped, handle the end bit
  if (sx <= 0) {
    s = evacuate_size + sx - 2;
    if (s < 0) {
      s = 0;
    }
    for (i = s; i < evacuate_size; i++) {
      evacuate_cleanup_blocks(i);
    }
  }
}

void
Vol::periodic_scan()
{
  evacuate_cleanup();
  scan_for_pinned_documents();
  if (header->write_pos == start) {
    scan_pos = start;
  }
  scan_pos += len / PIN_SCAN_EVERY;
}

void
Vol::agg_wrap()
{
  header->write_pos = start;
  header->phase     = !header->phase;

  header->cycle++;
  header->agg_pos = header->write_pos;
  dir_lookaside_cleanup(this);
  dir_clean_vol(this);
  {
    Vol *vol = this;
    CACHE_INCREMENT_DYN_STAT(cache_directory_wrap_stat);
    Note("Cache volume %d on disk '%s' wraps around", vol->cache_vol->vol_number, vol->hash_text.get());
  }
  periodic_scan();
}

/* NOTE: This state can be called by an AIO thread, so DON'T DON'T
   DON'T schedule any events on this thread using VC_SCHED_XXX or
   mutex->thread_holding->schedule_xxx_local(). ALWAYS use
   eventProcessor.schedule_xxx().
   Also, make sure that any functions called by this also use
   the eventProcessor to schedule events
*/
int
Vol::aggWrite(int event, void * /* e ATS_UNUSED */)
{
  ink_assert(!is_io_in_progress());

  Que(CacheVC, link) tocall;
  CacheVC *c;

  cancel_trigger();

Lagain:
  // calculate length of aggregated write
  for (c = (CacheVC *)agg.head; c;) {
    int writelen = c->agg_len;
    // [amc] this is checked multiple places, on here was it strictly less.
    ink_assert(writelen <= AGG_SIZE);
    if (agg_buf_pos + writelen > AGG_SIZE || header->write_pos + agg_buf_pos + writelen > (skip + len)) {
      break;
    }
    DDebug("agg_read", "copying: %d, %" PRIu64 ", key: %d", agg_buf_pos, header->write_pos + agg_buf_pos, c->first_key.slice32(0));
    int wrotelen = agg_copy(agg_buffer + agg_buf_pos, c);
    ink_assert(writelen == wrotelen);
    agg_todo_size -= writelen;
    agg_buf_pos += writelen;
    CacheVC *n = (CacheVC *)c->link.next;
    agg.dequeue();
    if (c->f.sync && c->f.use_first_key) {
      CacheVC *last = sync.tail;
      while (last && UINT_WRAP_LT(c->write_serial, last->write_serial)) {
        last = (CacheVC *)last->link.prev;
      }
      sync.insert(c, last);
    } else if (c->f.evacuator) {
      c->handleEvent(AIO_EVENT_DONE, nullptr);
    } else {
      tocall.enqueue(c);
    }
    c = n;
  }

  // if we got nothing...
  if (!agg_buf_pos) {
    if (!agg.head && !sync.head) { // nothing to get
      return EVENT_CONT;
    }
    if (header->write_pos == start) {
      // write aggregation too long, bad bad, punt on everything.
      Note("write aggregation exceeds vol size");
      ink_assert(!tocall.head);
      ink_assert(false);
      while ((c = agg.dequeue())) {
        agg_todo_size -= c->agg_len;
        if (c->initial_thread != nullptr) {
          c->initial_thread->schedule_imm_signal(c, AIO_EVENT_DONE);
        } else {
          eventProcessor.schedule_imm_signal(c, ET_CALL, AIO_EVENT_DONE);
        }
      }
      return EVENT_CONT;
    }
    // start back
    if (agg.head) {
      agg_wrap();
      goto Lagain;
    }
  }

  // evacuate space
  off_t end = header->write_pos + agg_buf_pos + EVACUATION_SIZE;
  if (evac_range(header->write_pos, end, !header->phase) < 0) {
    goto Lwait;
  }
  if (end > skip + len) {
    if (evac_range(start, start + (end - (skip + len)), header->phase) < 0) {
      goto Lwait;
    }
  }

  // if agg.head, then we are near the end of the disk, so
  // write down the aggregation in whatever size it is.
  if (agg_buf_pos < AGG_HIGH_WATER && !agg.head && !sync.head && !dir_sync_waiting) {
    goto Lwait;
  }

  // write sync marker
  if (!agg_buf_pos) {
    ink_assert(sync.head);
    int l       = round_to_approx_size(sizeof(Doc));
    agg_buf_pos = l;
    Doc *d      = (Doc *)agg_buffer;
    memset(static_cast<void *>(d), 0, sizeof(Doc));
    d->magic        = DOC_MAGIC;
    d->len          = l;
    d->sync_serial  = header->sync_serial;
    d->write_serial = header->write_serial;
  }

  // set write limit
  header->agg_pos = header->write_pos + agg_buf_pos;

  io.aiocb.aio_fildes = fd;
  io.aiocb.aio_offset = header->write_pos;
  io.aiocb.aio_buf    = agg_buffer;
  io.aiocb.aio_nbytes = agg_buf_pos;
  io.action           = this;
  /*
    Callback on AIO thread so that we can issue a new write ASAP
    as all writes are serialized in the volume.  This is not necessary
    for reads proceed independently.
   */
  io.thread = AIO_CALLBACK_THREAD_AIO;
  SET_HANDLER(&Vol::aggWriteDone);
  ink_aio_write(&io);

Lwait:
  int ret = EVENT_CONT;
  while ((c = tocall.dequeue())) {
    if (event == EVENT_CALL && c->mutex->thread_holding == mutex->thread_holding) {
      ret = EVENT_RETURN;
    } else if (c->initial_thread != nullptr) {
      c->initial_thread->schedule_imm_signal(c, AIO_EVENT_DONE);
    } else {
      eventProcessor.schedule_imm_signal(c, ET_CALL, AIO_EVENT_DONE);
    }
  }
  return ret;
}

int
CacheVC::openWriteCloseDir(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */)
{
  cancel_trigger();
  {
    CACHE_TRY_LOCK(lock, vol->mutex, mutex->thread_holding);
    if (!lock.is_locked()) {
      SET_HANDLER(&CacheVC::openWriteCloseDir);
      ink_assert(!is_io_in_progress());
      VC_SCHED_LOCK_RETRY();
    }
    vol->close_write(this);
    if (closed < 0 && fragment) {
      dir_delete(&earliest_key, vol, &earliest_dir);
    }
  }
  if (is_debug_tag_set("cache_update")) {
    if (f.update && closed > 0) {
      if (!total_len && alternate_index != CACHE_ALT_REMOVED) {
        Debug("cache_update", "header only %d (%" PRIu64 ", %" PRIu64 ")", DIR_MASK_TAG(first_key.slice32(2)), update_key.b[0],
              update_key.b[1]);

      } else if (total_len && alternate_index != CACHE_ALT_REMOVED) {
        Debug("cache_update", "header body, %d, (%" PRIu64 ", %" PRIu64 "), (%" PRIu64 ", %" PRIu64 ")",
              DIR_MASK_TAG(first_key.slice32(2)), update_key.b[0], update_key.b[1], earliest_key.b[0], earliest_key.b[1]);
      } else if (!total_len && alternate_index == CACHE_ALT_REMOVED) {
        Debug("cache_update", "alt delete, %d, (%" PRIu64 ", %" PRIu64 ")", DIR_MASK_TAG(first_key.slice32(2)), update_key.b[0],
              update_key.b[1]);
      }
    }
  }
  // update the appropriate stat variable
  // These variables may not give the current no of documents with
  // one, two and three or more fragments. This is because for
  // updates we dont decrement the variable corresponding the old
  // size of the document
  if ((closed == 1) && (total_len > 0 || f.allow_empty_doc)) {
    DDebug("cache_stats", "Fragment = %d", fragment);
    switch (fragment) {
    case 0:
      CACHE_INCREMENT_DYN_STAT(cache_single_fragment_document_count_stat);
      break;
    case 1:
      CACHE_INCREMENT_DYN_STAT(cache_two_fragment_document_count_stat);
      break;
    default:
      CACHE_INCREMENT_DYN_STAT(cache_three_plus_plus_fragment_document_count_stat);
      break;
    }
  }
  if (f.close_complete) {
    recursive++;
    ink_assert(!vol || this_ethread() != vol->mutex->thread_holding);
    vio.cont->handleEvent(VC_EVENT_WRITE_COMPLETE, (void *)&vio);
    recursive--;
  }
  return free_CacheVC(this);
}

int
CacheVC::openWriteCloseHeadDone(int event, Event *e)
{
  if (event == AIO_EVENT_DONE) {
    set_io_not_in_progress();
  } else if (is_io_in_progress()) {
    return EVENT_CONT;
  }
  {
    CACHE_TRY_LOCK(lock, vol->mutex, mutex->thread_holding);
    if (!lock.is_locked()) {
      VC_LOCK_RETRY_EVENT();
    }
    od->writing_vec = false;
    if (!io.ok()) {
      goto Lclose;
    }
    ink_assert(f.use_first_key);
    if (!od->dont_update_directory) {
      if (dir_is_empty(&od->first_dir)) {
        dir_insert(&first_key, vol, &dir);
      } else {
        // multiple fragment vector write
        dir_overwrite(&first_key, vol, &dir, &od->first_dir, false);
        // insert moved resident alternate
        if (od->move_resident_alt) {
          if (dir_valid(vol, &od->single_doc_dir)) {
            dir_insert(&od->single_doc_key, vol, &od->single_doc_dir);
          }
          od->move_resident_alt = false;
        }
      }
      od->first_dir = dir;
      if (frag_type == CACHE_FRAG_TYPE_HTTP && f.single_fragment) {
        // fragment is tied to the vector
        od->move_resident_alt = true;
        if (!f.rewrite_resident_alt) {
          od->single_doc_key = earliest_key;
        }
        dir_assign(&od->single_doc_dir, &dir);
        dir_set_tag(&od->single_doc_dir, od->single_doc_key.slice32(2));
      }
    }
  }
Lclose:
  return openWriteCloseDir(event, e);
}

int
CacheVC::openWriteCloseHead(int event, Event *e)
{
  cancel_trigger();
  f.use_first_key = 1;
  if (io.ok()) {
    ink_assert(fragment || (length == (int64_t)total_len));
  } else {
    return openWriteCloseDir(event, e);
  }
  if (f.data_done) {
    write_len = 0;
  } else {
    write_len = length;
  }
  if (frag_type == CACHE_FRAG_TYPE_HTTP) {
    SET_HANDLER(&CacheVC::updateVector);
    return updateVector(EVENT_IMMEDIATE, nullptr);
  } else {
    header_len = header_to_write_len;
    SET_HANDLER(&CacheVC::openWriteCloseHeadDone);
    return do_write_lock();
  }
}

int
CacheVC::openWriteCloseDataDone(int event, Event *e)
{
  int ret = 0;
  cancel_trigger();

  if (event == AIO_EVENT_DONE) {
    set_io_not_in_progress();
  } else if (is_io_in_progress()) {
    return EVENT_CONT;
  }
  if (!io.ok()) {
    return openWriteCloseDir(event, e);
  }
  {
    CACHE_TRY_LOCK(lock, vol->mutex, this_ethread());
    if (!lock.is_locked()) {
      VC_LOCK_RETRY_EVENT();
    }
    if (!fragment) {
      ink_assert(key == earliest_key);
      earliest_dir = dir;
    } else {
      // Store the offset only if there is a table.
      // Currently there is no alt (and thence no table) for non-HTTP.
      if (alternate.valid()) {
        alternate.push_frag_offset(write_pos);
      }
    }
    fragment++;
    write_pos += write_len;
    dir_insert(&key, vol, &dir);
    blocks = iobufferblock_skip(blocks.get(), &offset, &length, write_len);
    next_CacheKey(&key, &key);
    if (length) {
      write_len = length;
      if (write_len > MAX_FRAG_SIZE) {
        write_len = MAX_FRAG_SIZE;
      }
      if ((ret = do_write_call()) == EVENT_RETURN) {
        goto Lcallreturn;
      }
      return ret;
    }
    f.data_done = 1;
    return openWriteCloseHead(event, e); // must be called under vol lock from here
  }
Lcallreturn:
  return handleEvent(AIO_EVENT_DONE, nullptr);
}

int
CacheVC::openWriteClose(int event, Event *e)
{
  cancel_trigger();
  if (is_io_in_progress()) {
    if (event != AIO_EVENT_DONE) {
      return EVENT_CONT;
    }
    set_io_not_in_progress();
    if (!io.ok()) {
      return openWriteCloseDir(event, e);
    }
  }
  if (closed > 0 || f.allow_empty_doc) {
    if (total_len == 0) {
      if (f.update || f.allow_empty_doc) {
        return updateVector(event, e);
      } else {
        // If we've been CLOSE'd but nothing has been written then
        // this close is transformed into an abort.
        closed = -1;
        return openWriteCloseDir(event, e);
      }
    }
    if (length && (fragment || length > static_cast<int>(MAX_FRAG_SIZE))) {
      SET_HANDLER(&CacheVC::openWriteCloseDataDone);
      write_len = length;
      if (write_len > MAX_FRAG_SIZE) {
        write_len = MAX_FRAG_SIZE;
      }
      return do_write_lock_call();
    } else {
      return openWriteCloseHead(event, e);
    }
  } else {
    return openWriteCloseDir(event, e);
  }
}

int
CacheVC::openWriteWriteDone(int event, Event *e)
{
  cancel_trigger();
  if (event == AIO_EVENT_DONE) {
    set_io_not_in_progress();
  } else if (is_io_in_progress()) {
    return EVENT_CONT;
  }
  // In the event of VC_EVENT_ERROR, the cont must do an io_close
  if (!io.ok()) {
    if (closed) {
      closed = -1;
      return die();
    }
    SET_HANDLER(&CacheVC::openWriteMain);
    return calluser(VC_EVENT_ERROR);
  }
  {
    CACHE_TRY_LOCK(lock, vol->mutex, mutex->thread_holding);
    if (!lock.is_locked()) {
      VC_LOCK_RETRY_EVENT();
    }
    // store the earliest directory. Need to remove the earliest dir
    // in case the writer aborts.
    if (!fragment) {
      ink_assert(key == earliest_key);
      earliest_dir = dir;
    } else {
      // Store the offset only if there is a table.
      // Currently there is no alt (and thence no table) for non-HTTP.
      if (alternate.valid()) {
        alternate.push_frag_offset(write_pos);
      }
    }
    ++fragment;
    write_pos += write_len;
    dir_insert(&key, vol, &dir);
    DDebug("cache_insert", "WriteDone: %X, %X, %d", key.slice32(0), first_key.slice32(0), write_len);
    blocks = iobufferblock_skip(blocks.get(), &offset, &length, write_len);
    next_CacheKey(&key, &key);
  }
  if (closed) {
    return die();
  }
  SET_HANDLER(&CacheVC::openWriteMain);
  return openWriteMain(event, e);
}

static inline int
target_fragment_size()
{
  return cache_config_target_fragment_size - sizeof(Doc);
}

int
CacheVC::openWriteMain(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */)
{
  cancel_trigger();
  int called_user = 0;
  ink_assert(!is_io_in_progress());
Lagain:
  if (!vio.buffer.writer()) {
    if (calluser(VC_EVENT_WRITE_READY) == EVENT_DONE) {
      return EVENT_DONE;
    }
    if (!vio.buffer.writer()) {
      return EVENT_CONT;
    }
  }
  if (vio.ntodo() <= 0) {
    called_user = 1;
    if (calluser(VC_EVENT_WRITE_COMPLETE) == EVENT_DONE) {
      return EVENT_DONE;
    }
    ink_assert(!f.close_complete || !"close expected after write COMPLETE");
    if (vio.ntodo() <= 0) {
      return EVENT_CONT;
    }
  }
  int64_t ntodo       = (int64_t)(vio.ntodo() + length);
  int64_t total_avail = vio.buffer.reader()->read_avail();
  int64_t avail       = total_avail;
  int64_t towrite     = avail + length;
  if (towrite > ntodo) {
    avail -= (towrite - ntodo);
    towrite = ntodo;
  }
  if (towrite > static_cast<int>(MAX_FRAG_SIZE)) {
    avail -= (towrite - MAX_FRAG_SIZE);
    towrite = MAX_FRAG_SIZE;
  }
  if (!blocks && towrite) {
    blocks = vio.buffer.reader()->block;
    offset = vio.buffer.reader()->start_offset;
  }
  if (avail > 0) {
    vio.buffer.reader()->consume(avail);
    vio.ndone += avail;
    total_len += avail;
  }
  length = (uint64_t)towrite;
  if (length > target_fragment_size() && (length < target_fragment_size() + target_fragment_size() / 4)) {
    write_len = target_fragment_size();
  } else {
    write_len = length;
  }
  bool not_writing = towrite != ntodo && towrite < target_fragment_size();
  if (!called_user) {
    if (not_writing) {
      called_user = 1;
      if (calluser(VC_EVENT_WRITE_READY) == EVENT_DONE) {
        return EVENT_DONE;
      }
      goto Lagain;
    } else if (vio.ntodo() <= 0) {
      goto Lagain;
    }
  }
  if (not_writing) {
    return EVENT_CONT;
  }
  if (towrite == ntodo && f.close_complete) {
    closed = 1;
    SET_HANDLER(&CacheVC::openWriteClose);
    return openWriteClose(EVENT_NONE, nullptr);
  }
  SET_HANDLER(&CacheVC::openWriteWriteDone);
  return do_write_lock_call();
}

// begin overwrite
int
CacheVC::openWriteOverwrite(int event, Event *e)
{
  cancel_trigger();
  if (event != AIO_EVENT_DONE) {
    if (event == EVENT_IMMEDIATE) {
      last_collision = nullptr;
    }
  } else {
    Doc *doc = nullptr;
    set_io_not_in_progress();
    if (_action.cancelled) {
      return openWriteCloseDir(event, e);
    }
    if (!io.ok()) {
      goto Ldone;
    }
    doc = (Doc *)buf->data();
    if (!(doc->first_key == first_key)) {
      goto Lcollision;
    }
    od->first_dir = dir;
    first_buf     = buf;
    goto Ldone;
  }
Lcollision : {
  CACHE_TRY_LOCK(lock, vol->mutex, this_ethread());
  if (!lock.is_locked()) {
    VC_LOCK_RETRY_EVENT();
  }
  int res = dir_probe(&first_key, vol, &dir, &last_collision);
  if (res > 0) {
    if ((res = do_read_call(&first_key)) == EVENT_RETURN) {
      goto Lcallreturn;
    }
    return res;
  }
}
Ldone:
  SET_HANDLER(&CacheVC::openWriteMain);
  return callcont(CACHE_EVENT_OPEN_WRITE);
Lcallreturn:
  return handleEvent(AIO_EVENT_DONE, nullptr); // hopefully a tail call
}

// openWriteStartDone handles vector read (addition of alternates)
// and lock misses
int
CacheVC::openWriteStartDone(int event, Event *e)
{
  intptr_t err = ECACHE_NO_DOC;
  cancel_trigger();
  if (is_io_in_progress()) {
    if (event != AIO_EVENT_DONE) {
      return EVENT_CONT;
    }
    set_io_not_in_progress();
  }
  {
    CACHE_TRY_LOCK(lock, vol->mutex, mutex->thread_holding);
    if (!lock.is_locked()) {
      VC_LOCK_RETRY_EVENT();
    }

    if (_action.cancelled && (!od || !od->has_multiple_writers())) {
      goto Lcancel;
    }

    if (event == AIO_EVENT_DONE) { // vector read done
      Doc *doc = (Doc *)buf->data();
      if (!io.ok()) {
        err = ECACHE_READ_FAIL;
        goto Lfailure;
      }

      /* INKqa07123.
         A directory entry which is no longer valid may have been overwritten.
         We need to start afresh from the beginning by setting last_collision
         to nullptr.
       */
      if (!dir_valid(vol, &dir)) {
        DDebug("cache_write", "OpenReadStartDone: Dir not valid: Write Head: %" PRId64 ", Dir: %" PRId64,
               (int64_t)vol->offset_to_vol_offset(vol->header->write_pos), dir_offset(&dir));
        last_collision = nullptr;
        goto Lcollision;
      }
      if (!(doc->first_key == first_key)) {
        goto Lcollision;
      }

      if (doc->magic != DOC_MAGIC || !doc->hlen || this->load_http_info(write_vector, doc, buf.object()) != doc->hlen) {
        err = ECACHE_BAD_META_DATA;
        goto Lfailure;
      }
      ink_assert(write_vector->count() > 0);
      od->first_dir = dir;
      first_dir     = dir;
      if (doc->single_fragment()) {
        // fragment is tied to the vector
        od->move_resident_alt = true;
        od->single_doc_key    = doc->key;
        dir_assign(&od->single_doc_dir, &dir);
        dir_set_tag(&od->single_doc_dir, od->single_doc_key.slice32(2));
      }
      first_buf = buf;
      goto Lsuccess;
    }

  Lcollision:
    int if_writers = ((uintptr_t)info == CACHE_ALLOW_MULTIPLE_WRITES);
    if (!od) {
      if ((err = vol->open_write(this, if_writers, cache_config_http_max_alts > 1 ? cache_config_http_max_alts : 0)) > 0) {
        goto Lfailure;
      }
      if (od->has_multiple_writers()) {
        MUTEX_RELEASE(lock);
        SET_HANDLER(&CacheVC::openWriteMain);
        return callcont(CACHE_EVENT_OPEN_WRITE);
      }
    }
    // check for collision
    if (dir_probe(&first_key, vol, &dir, &last_collision)) {
      od->reading_vec = true;
      int ret         = do_read_call(&first_key);
      if (ret == EVENT_RETURN) {
        goto Lcallreturn;
      }
      return ret;
    }
    if (f.update) {
      // fail update because vector has been GC'd
      goto Lfailure;
    }
  }
Lsuccess:
  od->reading_vec = false;
  if (_action.cancelled) {
    goto Lcancel;
  }
  SET_HANDLER(&CacheVC::openWriteMain);
  return callcont(CACHE_EVENT_OPEN_WRITE);

Lfailure:
  CACHE_INCREMENT_DYN_STAT(base_stat + CACHE_STAT_FAILURE);
  _action.continuation->handleEvent(CACHE_EVENT_OPEN_WRITE_FAILED, (void *)-err);
Lcancel:
  if (od) {
    od->reading_vec = false;
    return openWriteCloseDir(event, e);
  } else {
    return free_CacheVC(this);
  }
Lcallreturn:
  return handleEvent(AIO_EVENT_DONE, nullptr); // hopefully a tail call
}

// handle lock failures from main Cache::open_write entry points below
int
CacheVC::openWriteStartBegin(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */)
{
  intptr_t err;
  cancel_trigger();
  if (_action.cancelled) {
    return free_CacheVC(this);
  }
  if (((err = vol->open_write_lock(this, false, 1)) > 0)) {
    CACHE_INCREMENT_DYN_STAT(base_stat + CACHE_STAT_FAILURE);
    free_CacheVC(this);
    _action.continuation->handleEvent(CACHE_EVENT_OPEN_WRITE_FAILED, (void *)-err);
    return EVENT_DONE;
  }
  if (err < 0) {
    VC_SCHED_LOCK_RETRY();
  }
  if (f.overwrite) {
    SET_HANDLER(&CacheVC::openWriteOverwrite);
    return openWriteOverwrite(EVENT_IMMEDIATE, nullptr);
  } else {
    // write by key
    SET_HANDLER(&CacheVC::openWriteMain);
    return callcont(CACHE_EVENT_OPEN_WRITE);
  }
}

// main entry point for writing of of non-http documents
Action *
Cache::open_write(Continuation *cont, const CacheKey *key, CacheFragType frag_type, int options, time_t apin_in_cache,
                  const char *hostname, int host_len)
{
  if (!CacheProcessor::IsCacheReady(frag_type)) {
    cont->handleEvent(CACHE_EVENT_OPEN_WRITE_FAILED, (void *)-ECACHE_NOT_READY);
    return ACTION_RESULT_DONE;
  }

  ink_assert(caches[frag_type] == this);

  intptr_t res      = 0;
  CacheVC *c        = new_CacheVC(cont);
  ProxyMutex *mutex = cont->mutex.get();
  SCOPED_MUTEX_LOCK(lock, c->mutex, this_ethread());
  c->vio.op    = VIO::WRITE;
  c->base_stat = cache_write_active_stat;
  c->vol       = key_to_vol(key, hostname, host_len);
  Vol *vol     = c->vol;
  CACHE_INCREMENT_DYN_STAT(c->base_stat + CACHE_STAT_ACTIVE);
  c->first_key = c->key = *key;
  c->frag_type          = frag_type;
  /*
     The transition from single fragment document to a multi-fragment document
     would cause a problem if the key and the first_key collide. In case of
     a collision, old vector data could be served to HTTP. Need to avoid that.
     Also, when evacuating a fragment, we have to decide if its the first_key
     or the earliest_key based on the dir_tag.
   */
  do {
    rand_CacheKey(&c->key, cont->mutex);
  } while (DIR_MASK_TAG(c->key.slice32(2)) == DIR_MASK_TAG(c->first_key.slice32(2)));
  c->earliest_key     = c->key;
  c->info             = nullptr;
  c->f.overwrite      = (options & CACHE_WRITE_OPT_OVERWRITE) != 0;
  c->f.close_complete = (options & CACHE_WRITE_OPT_CLOSE_COMPLETE) != 0;
  c->f.sync           = (options & CACHE_WRITE_OPT_SYNC) == CACHE_WRITE_OPT_SYNC;
  c->pin_in_cache     = (uint32_t)apin_in_cache;

  if ((res = c->vol->open_write_lock(c, false, 1)) > 0) {
    // document currently being written, abort
    CACHE_INCREMENT_DYN_STAT(c->base_stat + CACHE_STAT_FAILURE);
    cont->handleEvent(CACHE_EVENT_OPEN_WRITE_FAILED, (void *)-res);
    free_CacheVC(c);
    return ACTION_RESULT_DONE;
  }
  if (res < 0) {
    SET_CONTINUATION_HANDLER(c, &CacheVC::openWriteStartBegin);
    c->trigger = CONT_SCHED_LOCK_RETRY(c);
    return &c->_action;
  }
  if (!c->f.overwrite) {
    SET_CONTINUATION_HANDLER(c, &CacheVC::openWriteMain);
    c->callcont(CACHE_EVENT_OPEN_WRITE);
    return ACTION_RESULT_DONE;
  } else {
    SET_CONTINUATION_HANDLER(c, &CacheVC::openWriteOverwrite);
    if (c->openWriteOverwrite(EVENT_IMMEDIATE, nullptr) == EVENT_DONE) {
      return ACTION_RESULT_DONE;
    } else {
      return &c->_action;
    }
  }
}

// main entry point for writing of http documents
Action *
Cache::open_write(Continuation *cont, const CacheKey *key, CacheHTTPInfo *info, time_t apin_in_cache,
                  const CacheKey * /* key1 ATS_UNUSED */, CacheFragType type, const char *hostname, int host_len)
{
  if (!CacheProcessor::IsCacheReady(type)) {
    cont->handleEvent(CACHE_EVENT_OPEN_WRITE_FAILED, (void *)-ECACHE_NOT_READY);
    return ACTION_RESULT_DONE;
  }

  ink_assert(caches[type] == this);
  intptr_t err      = 0;
  int if_writers    = (uintptr_t)info == CACHE_ALLOW_MULTIPLE_WRITES;
  CacheVC *c        = new_CacheVC(cont);
  ProxyMutex *mutex = cont->mutex.get();
  c->vio.op         = VIO::WRITE;
  c->first_key      = *key;
  /*
     The transition from single fragment document to a multi-fragment document
     would cause a problem if the key and the first_key collide. In case of
     a collision, old vector data could be served to HTTP. Need to avoid that.
     Also, when evacuating a fragment, we have to decide if its the first_key
     or the earliest_key based on the dir_tag.
   */
  do {
    rand_CacheKey(&c->key, cont->mutex);
  } while (DIR_MASK_TAG(c->key.slice32(2)) == DIR_MASK_TAG(c->first_key.slice32(2)));
  c->earliest_key = c->key;
  c->frag_type    = CACHE_FRAG_TYPE_HTTP;
  c->vol          = key_to_vol(key, hostname, host_len);
  Vol *vol        = c->vol;
  c->info         = info;
  if (c->info && (uintptr_t)info != CACHE_ALLOW_MULTIPLE_WRITES) {
    /*
       Update has the following code paths :
       a) Update alternate header only :
       In this case the vector has to be rewritten. The content
       length(update_len) and the key for the document are set in the
       new_info in the set_http_info call.
       HTTP OPERATIONS
       open_write with info set
       set_http_info new_info
       (total_len == 0)
       close
       b) Update alternate and data
       In this case both the vector and the data needs to be rewritten.
       This case is similar to the standard write of a document case except
       that the new_info is inserted into the vector at the alternate_index
       (overwriting the old alternate) rather than the end of the vector.
       HTTP OPERATIONS
       open_write with info set
       set_http_info new_info
       do_io_write =>  (total_len > 0)
       close
       c) Delete an alternate
       The vector may need to be deleted (if there was only one alternate) or
       rewritten (if there were more than one alternate). The deletion of the
       vector is done in openWriteRemoveVector.
       HTTP OPERATIONS
       open_write with info set
       close
     */
    c->f.update  = 1;
    c->base_stat = cache_update_active_stat;
    DDebug("cache_update", "Update called");
    info->object_key_get(&c->update_key);
    ink_assert(!(c->update_key == zero_key));
    c->update_len = info->object_size_get();
  } else {
    c->base_stat = cache_write_active_stat;
  }
  CACHE_INCREMENT_DYN_STAT(c->base_stat + CACHE_STAT_ACTIVE);
  c->pin_in_cache = (uint32_t)apin_in_cache;

  {
    CACHE_TRY_LOCK(lock, c->vol->mutex, cont->mutex->thread_holding);
    if (lock.is_locked()) {
      if ((err = c->vol->open_write(c, if_writers, cache_config_http_max_alts > 1 ? cache_config_http_max_alts : 0)) > 0) {
        goto Lfailure;
      }
      // If there are multiple writers, then this one cannot be an update.
      // Only the first writer can do an update. If that's the case, we can
      // return success to the state machine now.;
      if (c->od->has_multiple_writers()) {
        goto Lmiss;
      }
      if (!dir_probe(key, c->vol, &c->dir, &c->last_collision)) {
        if (c->f.update) {
          // fail update because vector has been GC'd
          // This situation can also arise in openWriteStartDone
          err = ECACHE_NO_DOC;
          goto Lfailure;
        }
        // document doesn't exist, begin write
        goto Lmiss;
      } else {
        c->od->reading_vec = true;
        // document exists, read vector
        SET_CONTINUATION_HANDLER(c, &CacheVC::openWriteStartDone);
        switch (c->do_read_call(&c->first_key)) {
        case EVENT_DONE:
          return ACTION_RESULT_DONE;
        case EVENT_RETURN:
          goto Lcallreturn;
        default:
          return &c->_action;
        }
      }
    }
    // missed lock
    SET_CONTINUATION_HANDLER(c, &CacheVC::openWriteStartDone);
    CONT_SCHED_LOCK_RETRY(c);
    return &c->_action;
  }

Lmiss:
  SET_CONTINUATION_HANDLER(c, &CacheVC::openWriteMain);
  c->callcont(CACHE_EVENT_OPEN_WRITE);
  return ACTION_RESULT_DONE;

Lfailure:
  CACHE_INCREMENT_DYN_STAT(c->base_stat + CACHE_STAT_FAILURE);
  cont->handleEvent(CACHE_EVENT_OPEN_WRITE_FAILED, (void *)-err);
  if (c->od) {
    c->openWriteCloseDir(EVENT_IMMEDIATE, nullptr);
    return ACTION_RESULT_DONE;
  }
  free_CacheVC(c);
  return ACTION_RESULT_DONE;

Lcallreturn:
  if (c->handleEvent(AIO_EVENT_DONE, nullptr) == EVENT_DONE) {
    return ACTION_RESULT_DONE;
  }
  return &c->_action;
}

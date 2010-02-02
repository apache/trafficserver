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


#define HDR_PTR_SIZE            (sizeof(inku64))
#define HDR_PTR_ALIGNMENT_MASK  ((HDR_PTR_SIZE) - 1L)

#define CACHE_AGGREGATE_WRITE 1
#define MAX_AGG_LEN (256*1024)

extern int cache_config_max_agg_delay;
extern int cache_config_check_disk_idle;
extern int cache_config_agg_write_backlog;

// Given a key, finds the index of the alternate which matches
// used to get the alternate which is actually present in the document
#ifdef HTTP_CACHE
int
get_alternate_index(CacheHTTPInfoVector * cache_vector, CacheKey key)
{
  int alt_count = cache_vector->count();
  CacheHTTPInfo *obj;
  if (!alt_count)
    return -1;
  for (int i = 0; i < alt_count; i++) {
    obj = cache_vector->get(i);
    if (obj->compare_object_key(&key)) {
      // Debug("cache_key", "Resident alternate key  %X", key.word(0));
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
CacheVC::updateVector(int event, Event * e)
{
  NOWARN_UNUSED(e);
  NOWARN_UNUSED(event);

  cancel_trigger();
  if (od->reading_vec || od->writing_vec)
    VC_SCHED_LOCK_RETRY();

  CACHE_TRY_LOCK(lock, part->mutex, mutex->thread_holding);
  if (!lock || od->writing_vec)
    VC_SCHED_LOCK_RETRY();

  int vec = alternate.valid();
  if (f.update) {
    // all Update cases. Need to get the alternate index.
    alternate_index = get_alternate_index(write_vector, update_key);
    Debug("cache_update", "updating alternate index %d", alternate_index);
    // if its an alternate delete
    if (!vec) {
      ink_assert(!total_len);
      if (alternate_index >= 0) {
        write_vector->remove(alternate_index, true);
        alternate_index = CACHE_ALT_REMOVED;
        if (!write_vector->count())
          dir_delete(&first_key, part, &od->first_dir);
      }
      // the alternate is not there any more. somebody might have
      // deleted it. Just close this writer
      if (alternate_index != CACHE_ALT_REMOVED || !write_vector->count()) {
        SET_HANDLER(&CacheVC::openWriteCloseDir);
        return openWriteCloseDir(EVENT_IMMEDIATE, 0);
      }
    }
    if (update_key == od->single_doc_key && (total_len || !vec))
      od->move_resident_alt = 0;
  }
  if (cache_config_http_max_alts > 1 && write_vector->count() >= cache_config_http_max_alts && alternate_index < 0) {

    if (od->move_resident_alt && get_alternate_index(write_vector, od->single_doc_key) == 0)
      od->move_resident_alt = 0;

    write_vector->remove(0, true);
  }
  if (vec)
    alternate_index = write_vector->insert(&alternate, alternate_index);

  if (od->move_resident_alt && first_buf._ptr() && !od->has_multiple_writers()) {
    Doc *doc = (Doc *) first_buf->data();
    int small_doc = doc->data_len() < cache_config_alt_rewrite_max_size;
    int have_res_alt = doc->key == od->single_doc_key;
    // if the new alternate is not written with the vector
    // then move the old one with the vector
    // if its a header only update move the resident alternate
    // with the vector.
    // We are sure that the body of the resident alternate that we are
    // rewriting has not changed and the alternate is not being deleted,
    // since we set od->move_resident_alt  to 0 in that case
    // (in updateVector)
    if (small_doc && have_res_alt && (segment || (f.update && !total_len))) {
      // for multiple segment document, we must have done
      // CacheVC:openWriteCloseDataDone
      ink_assert(!segment || f.data_done);
      od->move_resident_alt = 0;
      f.rewrite_resident_alt = 1;
      write_len = doc->data_len();
      Debug("cache_update_alt",
            "rewriting resident alt size: %d key: %X, first_key: %X", write_len, doc->key.word(0), first_key.word(0));
    }
  }
  od->writing_vec = 1;
  f.use_first_key = 1;
  SET_HANDLER(&CacheVC::openWriteCloseHeadDone);
  return do_write();
}
#endif
/*
   The following fields of the CacheVC are used when writing down a fragment.
   Make sure that each of the fields is set to a valid value before calling
   this function
   - f.http_request. Checked to see if a vector needs to be marshalled.
   - f.use_first_key. To decide if the vector should be marshalled and to set
     the doc->key to the appropriate key (first_key or earliest_key)
   - f.evac_vector. If set, the writer is pushed in the beginning of the 
     agg queue. And if !f.evac_vector && !f.update the alternate->object_size
     is set to vc->total_len
   - f.readers.  If set, assumes that this is an evacuation, so the write
     is not aborted even if part->agg_todo_size > agg_write_backlog
   - f.evacuator. If this is an evacuation.
   - f.rewrite_resident_alt. The resident alternate is rewritten.
   - f.update. Used only if the write_vector needs to be written to disk. 
     Used to set the length of the alternate to total_len.
   - write_vector. Used only if f.http_request && 
     (f.use_fist_key || f.evac_vector) is set. Write_vector is written to disk
   - alternate_index. Used only if write_vector needs to be written to disk.
     Used to find out the VC's alternate in the write_vector and set its 
     length to tatal_len.
   - write_len. The number of bytes for this fragment.
   - total_len. The total number of bytes for the document so far. 
     Doc->total_len and alternate's total len is set to this value.
   - first_key. Doc's first_key is set to this value.
   - pin_in_cache. Doc's pinned value is set to this + ink_get_hrtime().
   - earliest_key. If f.use_first_key, Doc's key is set to this value. 
   - key. If !f.use_first_key, Doc's key is set to this value.
   - blocks. Used only if write_len is set. Data to be written
   - offset. Used only if write_len is set. offset into the block to copy
     the data from.
   - buf. Used only if f.evacuator is set. Should point to the old document. 
   The functions sets the length, offset, pinned, head and phase of vc->dir.
   */

int
CacheVC::handleWrite(int event, Event * e)
{
  NOWARN_UNUSED(e);
  NOWARN_UNUSED(event);

  // plain write case
  ink_assert(!trigger);
#ifdef HTTP_CACHE
  if (f.http_request && (f.use_first_key || f.evac_vector)) {
    ink_assert(od->writing_vec);
    vec_len = write_vector->marshal_length();
    ink_assert(vec_len > 0);
  } else
#endif
    vec_len = 0;
  set_agg_write_in_progress();
  POP_HANDLER;
  agg_len = round_to_approx_size(write_len + vec_len + sizeofDoc);
  part->agg_todo_size += agg_len;
  ink_assert(agg_len <= AGG_SIZE);
  bool agg_error = (agg_len > AGG_SIZE ||
                    (!f.readers && (part->agg_todo_size > cache_config_agg_write_backlog + AGG_SIZE) && write_len));
#ifdef CACHE_AGG_FAIL_RATE
  agg_error = agg_error || ((inku32) mutex->thread_holding->generator.random() <
                            (inku32) (UINT_MAX * CACHE_AGG_FAIL_RATE));
#endif
  bool max_doc_error = (cache_config_max_doc_size &&
                        (cache_config_max_doc_size < vio.ndone ||
                         (vio.nbytes != MAXINT && (cache_config_max_doc_size < vio.nbytes))));

  if (agg_error || max_doc_error) {
    CACHE_INCREMENT_DYN_STAT(cache_write_backlog_failure_stat);
    CACHE_INCREMENT_DYN_STAT(base_stat + CACHE_STAT_FAILURE);
    part->agg_todo_size -= agg_len;
    io.aio_result = AIO_SOFT_FAILURE;
    return handleEvent(AIO_EVENT_DONE, 0);
  }
  if (f.evac_vector)
    part->agg.push(this);
  else
    part->agg.enqueue(this);

  if (!part->is_io_in_progress()) {
    part->aggWrite(EVENT_IMMEDIATE, 0);
  }
  return EVENT_CONT;
}

static char *
iobufferblock_memcpy(char *p, int len, IOBufferBlock * ab, int offset)
{
  IOBufferBlock *b = ab;
  while (b && len >= 0) {
    char *start = b->_start;
    char *end = b->_end;
    int max_bytes = end - start;
    max_bytes -= offset;
    if (max_bytes <= 0) {
      offset = -max_bytes;
      b = b->next;
      continue;
    }
    int bytes = len;
    if (bytes >= max_bytes)
      bytes = max_bytes;
    ::memcpy(p, start + offset, bytes);
    p += bytes;
    len -= bytes;
    b = b->next;
    offset = 0;
  }
  return p;
}

EvacuationBlock *
Part::force_evacuate_head(Dir * evac_dir, int pinned)
{
  // build an evacuation block for the object
  EvacuationBlock *b = evacuation_block_exists(evac_dir, this);
  // if we have already started evacuating this document, its too late
  // to evacuate the head...bad luck
  if (b && b->f.done)
    return b;

  if (!b) {
    b = new_EvacuationBlock(mutex->thread_holding);
    b->dir = *evac_dir;
    Debug("cache_evac", "force: %d, %d", (int) dir_offset(evac_dir), (int) dir_phase(evac_dir));
    evacuate[dir_evac_bucket(evac_dir)].push(b);
  }
  b->f.pinned = pinned;
  b->f.evacuate_head = 1;
  b->evac_frags.key.set(0, 0);  // ensure that the block gets
  // evacuated no matter what
  b->f.readers = 0;             // ensure that the block does not disappear
  return b;
}

void
Part::scan_for_pinned_documents()
{
  if (cache_config_permit_pinning) {
    // we can't evacuate anything between header->write_pos and
    // header->write_pos + AGG_SIZE.
    int ps = offset_to_part_offset(this, header->write_pos + AGG_SIZE);
    int pe = offset_to_part_offset(this, header->write_pos + 2 * EVAC_SIZE + (len / PIN_SCAN_EVERY));
    int part_end_offset = offset_to_part_offset(this, len + skip);
    int before_end_of_part = pe < part_end_offset;
    Debug("cache_evac", "scan %d %d", ps, pe);
    for (int i = 0; i < part_direntries(this); i++) {
      // is it a valid pinned object?
      if (!dir_is_empty(&dir[i]) && dir_pinned(&dir[i]) && dir_head(&dir[i])) {
        // select objects only within this PIN_SCAN region
        int o = dir_offset(&dir[i]);
        if (dir_phase(&dir[i]) == header->phase) {
          if (before_end_of_part || o >= (pe - part_end_offset))
            continue;
        } else {
          if (o<ps || o>= pe)
            continue;
        }
        force_evacuate_head(&dir[i], 1);
        //      Debug("cache_evac", "scan pinned at offset %d %d %d %d %d %d",
        //            (int)dir_offset(&b->dir), ps, o , pe, i, (int)b->f.done);
      }
    }
  }
}

int
PartCallback::aggWriteDone(int event, Event * e)
{
  NOWARN_UNUSED(e);
  NOWARN_UNUSED(event);

  if (trigger) {
    trigger->cancel_action();
    trigger = NULL;
  }

  CacheVC *c;
  Queue<CacheVC> not_done;
  EThread *t = mutex->thread_holding;
  while ((c = (CacheVC *) write_done.dequeue())) {
    if (!c->mutex->is_thread() || c->mutex->thread_holding == t) {
      CACHE_TRY_LOCK(lock, c->mutex, t);
      if (!lock) {
        not_done.enqueue(c);
        continue;
      }
      c->handleEvent(AIO_EVENT_DONE, 0);
    } else {
      not_done.enqueue(c);
      continue;
    }
  }
  if (not_done.head) {
    write_done = not_done;
    if (write_done.head->mutex->is_thread() && write_done.head->mutex->thread_holding != t)
      trigger = write_done.head->mutex->thread_holding->schedule_imm(this);
    else
      trigger = eventProcessor.schedule_in(this, MUTEX_RETRY_DELAY);
    return EVENT_CONT;
  }
  return EVENT_DONE;
}

/* NOTE:: This state can be called by an AIO thread, so DON'T DON'T
   DON'T schedule any events on this thread using VC_SCHED_XXX or
   mutex->thread_holding->schedule_xxx_local(). ALWAYS use 
   eventProcessor.schedule_xxx(). 
   */
int
Part::aggWriteDone(int event, Event * e)
{
  NOWARN_UNUSED(e);
  NOWARN_UNUSED(event);

  cancel_trigger();
  ProxyMutex *dir_sync_lock;
  if (dir_sync_waiting)
    dir_sync_lock = cacheDirSync->mutex;
  else
    dir_sync_lock = mutex;

  CACHE_TRY_LOCK(lock, dir_sync_lock, mutex->thread_holding);
  if (!lock) {
    // INKqa10347
    // Race condition between cacheDirSync and the part when setting the
    // dir_sync_waiting flag
    eventProcessor.schedule_in(this, MUTEX_RETRY_DELAY);
    return EVENT_CONT;
  }

  if (io.ok()) {
    header->last_write_pos = header->write_pos;
    header->write_pos += io.aiocb.aio_nbytes;
    Debug("cache_agg", "Dir %s, Write: %llu, last Write: %llu\n", hash_id, header->write_pos, header->last_write_pos);
    ink_assert(header->write_pos == header->agg_pos);
    if (header->write_pos + EVAC_SIZE > scan_pos)
      periodic_scan();

    agg_buf_pos = 0;
    header->write_serial++;
  } else {
    // delete all the directory entries that we inserted
    // for fragments is this aggregation buffer
    Debug("cache_disk_error", "Write error on disk %s\n \
              write range : [%llu - %llu bytes]  [%llu - %llu blocks] \n", hash_id, io.aiocb.aio_offset, io.aiocb.aio_offset + io.aiocb.aio_nbytes, io.aiocb.aio_offset / 512, (io.aiocb.aio_offset + io.aiocb.aio_nbytes) / 512);
    Dir del_dir;
    dir_clear(&del_dir);
    for (int done = 0; done < agg_buf_pos;) {
      Doc *doc = (Doc *) (agg_buffer + done);
      dir_set_offset(&del_dir, header->write_pos + done);
      dir_delete(&doc->key, this, &del_dir);
      done += round_to_approx_size(doc->len);
    }
    agg_buf_pos = 0;
  }

  set_io_not_in_progress();
  SET_HANDLER(&Part::aggWrite);

  if (dir_sync_waiting) {
    dir_sync_waiting = 0;
    cacheDirSync->handleEvent(EVENT_IMMEDIATE, 0);
  }

  if (agg.head)
    return aggWrite(EVENT_IMMEDIATE, e);
  return EVENT_CONT;
}

extern volatile int cachewrite_buf_data;

CacheVC *
new_DocEvacuator(int nbytes, Part * part)
{
  CacheVC *c = new_CacheVC(part);
  ProxyMutex *mutex = part->mutex;
  c->base_stat = cache_evacuate_active_stat;

  CACHE_INCREMENT_DYN_STAT(c->base_stat + CACHE_STAT_ACTIVE);
  c->buf = new_IOBufferData(iobuffer_size_to_index(nbytes), MEMALIGNED);
  c->part = part;
  c->f.evacuator = 1;
  c->earliest_key = zero_key;
  SET_CONTINUATION_HANDLER(c, &CacheVC::evacuateDocDone);
  return c;
}

int
CacheVC::evacuateReadHead(int event, Event * e)
{
  NOWARN_UNUSED(e);
  NOWARN_UNUSED(event);

  // The evacuator vc shares the lock with the partition mutex
  ink_debug_assert(part->mutex->thread_holding == this_ethread());
  cancel_trigger();
  Doc *doc = (Doc *) buf->data();
#ifdef HTTP_CACHE
  CacheHTTPInfo *alternate_tmp = 0;
#endif
  if (!io.ok())
    goto Ldone;
  // a directory entry which is nolonger valid may have been overwritten
  if (!dir_valid(part, &dir)) {
    last_collision = NULL;
    goto Lcollision;
  }
  if (doc->magic != DOC_MAGIC || !(doc->first_key == first_key))
    goto Lcollision;
#ifdef HTTP_CACHE
  alternate_tmp = 0;
  if (doc->hlen) {
    //its an http document
    if (vector.get_handles(doc->hdr, doc->hlen) != doc->hlen) {
      Note("bad vector detected during evacuation");
      goto Ldone;
    }
    alternate_index = get_alternate_index(&vector, earliest_key);
    if (alternate_index < 0)
      goto Ldone;
    alternate_tmp = vector.get(alternate_index);
    doc_len = alternate_tmp->object_size_get();
    Debug("cache_evac", "evacuateReadHead http earliest %X first: %X len: %d",
          first_key.word(0), earliest_key.word(0), doc_len);
  } else
#endif
  {
    // non-http document
    CacheKey next_key;
    next_CacheKey(&next_key, &doc->key);
    if (!(next_key == earliest_key))
      goto Ldone;
    doc_len = doc->total_len;
    Debug("cache_evac",
          "evacuateReadHead non-http earliest %X first: %X len: %d", first_key.word(0), earliest_key.word(0), doc_len);
  }
  if (doc_len == total_len) {
    // the whole document has been evacuated. Insert the directory
    // entry in the directory.
    dir_lookaside_fixup(&earliest_key, part);
    return free_CacheVC(this);
  }
  return EVENT_CONT;

Lcollision:
  if (dir_probe(&first_key, part, &dir, &last_collision))
    return do_read(&first_key);

Ldone:
  dir_lookaside_remove(&earliest_key, part);
  return free_CacheVC(this);
}

int
CacheVC::evacuateDocDone(int event, Event * e)
{
  NOWARN_UNUSED(e);
  NOWARN_UNUSED(event);

  ink_debug_assert(part->mutex->thread_holding == this_ethread());
  Doc *doc = (Doc *) buf->data();
  Debug("cache_evac", "evacuateDocDone %X o %d p %d new_o %d new_p %d",
        (int) key.word(0), (int) dir_offset(&overwrite_dir),
        (int) dir_phase(&overwrite_dir), (int) dir_offset(&dir), (int) dir_phase(&dir));
  int i = dir_evac_bucket(&overwrite_dir);
  // nasty beeping race condition, need to have the EvacuationBlock here
  EvacuationBlock *b = part->evacuate[i].head;
  for (; b; b = b->link.next) {
    if (dir_offset(&b->dir) == dir_offset(&overwrite_dir)) {

      // If the document is single segment (although not tied to the vector),
      // then we don't have to put the directory entry in the lookaside
      // buffer. But, we have no way of finding out if the document is
      // single segment. doc->single_segment() can be true for a multiple
      // fragment document since total_len and doc->len could be equal at
      // the time we write the fragment down. To be on the safe side, we
      // only overwrite the entry in the directory if its not a head.
      if (!dir_head(&overwrite_dir)) {
        // find the earliest key
        EvacuationKey *evac = &b->evac_frags;
        for (; evac && !(evac->key == doc->key); evac = evac->link.next);
        ink_assert(evac);
        if (!evac)
          break;
        if (evac->earliest_key.fold()) {
          Debug("cache_evac", "evacdocdone: evacuating key %X earliest %X",
                evac->key.word(0), evac->earliest_key.word(0));
          EvacuationBlock *eblock = 0;
          Dir dir_tmp;
          dir_lookaside_probe(&evac->earliest_key, part, &dir_tmp, &eblock);
          if (eblock) {
            CacheVC *earliest_evac = eblock->earliest_evacuator;
            earliest_evac->total_len += doc->data_len();
            if (earliest_evac->total_len == earliest_evac->doc_len) {
              dir_lookaside_fixup(&evac->earliest_key, part);
              free_CacheVC(earliest_evac);
            }
          }
        }
        dir_overwrite(&doc->key, part, &dir, &overwrite_dir);
      }
      // if the tag in the overwrite_dir matches the first_key in the
      // document, then it has to be the vector. We gaurantee that
      // the first_key and the earliest_key will never collide (see
      // Cache::open_write). Once we know its the vector, we can
      // safely overwrite the first_key in the directory.
      if (dir_head(&overwrite_dir) && b->f.evacuate_head) {
        Debug("cache_evac",
              "evacuateDocDone evacuate_head %X %X hlen %d offset %d",
              (int) key.word(0), (int) doc->key.word(0), doc->hlen, (int) dir_offset(&overwrite_dir));

        if (dir_compare_tag(&overwrite_dir, &doc->first_key)) {
          OpenDirEntry *cod;
          Debug("cache_evac", "evacuating vector: %X %d",
                (int) doc->first_key.word(0), (int) dir_offset(&overwrite_dir));
          if ((cod = part->open_read(&doc->first_key))) {
            // writer  exists
            Debug("cache_evac", "overwriting the open directory %X %d %d",
                  (int) doc->first_key.word(0), (int) dir_offset(&cod->first_dir), (int) dir_offset(&dir));
            ink_assert(dir_pinned(&dir));
            cod->first_dir = dir;

          }
          if (dir_overwrite(&doc->first_key, part, &dir, &overwrite_dir)) {
            part->ram_cache.fixup(&doc->first_key, 0, dir_offset(&overwrite_dir), 0, dir_offset(&dir));
          }
        } else {
          Debug("cache_evac", "evacuating earliest: %X %d", (int) doc->key.word(0), (int) dir_offset(&overwrite_dir));
          ink_debug_assert(dir_compare_tag(&overwrite_dir, &doc->key));
          ink_assert(b->earliest_evacuator == this);
          total_len += doc->data_len();
          first_key = doc->first_key;
          earliest_dir = dir;
          if (dir_probe(&first_key, part, &dir, &last_collision) > 0) {
            dir_lookaside_insert(b, part, &earliest_dir);
            // read the vector
            SET_HANDLER(&CacheVC::evacuateReadHead);
            return do_read(&first_key);
          }
        }
      }
      break;
    }
  }
  return free_CacheVC(this);
}

int
evacuate_segments(CacheKey * key, CacheKey * earliest_key, int force, Part * part)
{
  Dir dir, *last_collision = 0;
  int i = 0;
  while (dir_probe(key, part, &dir, &last_collision)) {
    // next fragment cannot be a head...if it is, it must have been a
    // directory collision.
    if (dir_head(&dir))
      continue;
    EvacuationBlock *b = evacuation_block_exists(&dir, part);
    if (!b) {
      b = new_EvacuationBlock(part->mutex->thread_holding);
      b->dir = dir;
      b->evac_frags.key = *key;
      b->evac_frags.earliest_key = *earliest_key;
      part->evacuate[dir_evac_bucket(&dir)].push(b);
      i++;
    } else {
      ink_assert(dir_offset(&dir) == dir_offset(&b->dir));
      ink_assert(dir_phase(&dir) == dir_phase(&b->dir));
      EvacuationKey *evac_frag = evacuationKeyAllocator.alloc();
      evac_frag->key = *key;
      evac_frag->earliest_key = *earliest_key;
      evac_frag->link.next = b->evac_frags.link.next;
      b->evac_frags.link.next = evac_frag;
    }
    if (force)
      b->f.readers = 0;
    Debug("cache_evac",
          "next segment %X Earliest: %X offset %d phase %d force %d",
          (int) key->word(0), (int) earliest_key->word(0), (int) dir_offset(&dir), (int) dir_phase(&dir), force);
  }
  return i;
}

int
Part::evacuateWrite(CacheVC * evacuator, int event, Event * e)
{
  NOWARN_UNUSED(e);
  NOWARN_UNUSED(event);

  // push to front of aggregation write list, so it is written first

  evacuator->agg_len = round_to_approx_size(((Doc *) evacuator->buf->data())->len);
  agg_todo_size += evacuator->agg_len;
  /* insert the evacuator after all the other evacuators */
  CacheVC *cur = (CacheVC *) agg.head;
  CacheVC *after = NULL;
  for (; cur && cur->f.evacuator; cur = (CacheVC *) cur->link.next)
    after = cur;
  ink_assert(evacuator->agg_len <= AGG_SIZE);
  agg.insert(evacuator, after);
  return aggWrite(event, e);

}

int
Part::evacuateDocReadDone(int event, Event * e)
{
  NOWARN_UNUSED(e);

  cancel_trigger();
  if (event != AIO_EVENT_DONE) {
    return EVENT_DONE;
  }
  ink_assert(is_io_in_progress());
  set_io_not_in_progress();
  ink_debug_assert(mutex->thread_holding == this_ethread());
  Doc *doc = (Doc *) doc_evacuator->buf->data();
  CacheKey next_key;
  EvacuationBlock *b = NULL;
  if (doc->magic != DOC_MAGIC) {
    Debug("cache_evac", "DOC magic: %X %d",
          (int) dir_tag(&doc_evacuator->overwrite_dir), (int) dir_offset(&doc_evacuator->overwrite_dir));
    ink_assert(doc->magic == DOC_MAGIC);
    goto Ldone;
  }
  Debug("cache_evac", "evacuateDocReadDone %X offset %d",
        (int) doc->key.word(0), (int) dir_offset(&doc_evacuator->overwrite_dir));

  b = evacuate[dir_evac_bucket(&doc_evacuator->overwrite_dir)].head;
  while (b) {
    if (dir_offset(&b->dir) == dir_offset(&doc_evacuator->overwrite_dir))
      break;
    b = b->link.next;
  }
  if (!b)
    goto Ldone;
  if ((b->f.pinned && !b->f.readers) && doc->pinned < (inku32) (ink_get_based_hrtime() / HRTIME_SECOND))
    goto Ldone;

  if (dir_head(&b->dir) && b->f.evacuate_head) {
    ink_assert(!b->evac_frags.key.fold());
    // if its a head (vector), evacuation is real simple...we just
    // need to write this vector down and overwrite the directory entry.
    if (dir_compare_tag(&b->dir, &doc->first_key)) {
      doc_evacuator->key = doc->first_key;
      b->evac_frags.key = doc->first_key;
      Debug("cache_evac", "evacuating vector %X offset %d",
            (int) doc->first_key.word(0), (int) dir_offset(&doc_evacuator->overwrite_dir));
    } else {
      // if its an earliest fragment (alternate) evacuation, things get
      // a little tricky. We have to propagate the earliest key to the next
      // fragments for this alternate. The last fragment to be evacuated
      // fixes up the lookaside buffer.
      doc_evacuator->key = doc->key;
      doc_evacuator->earliest_key = doc->key;
      b->evac_frags.key = doc->key;
      b->evac_frags.earliest_key = doc->key;
      b->earliest_evacuator = doc_evacuator;
      Debug("cache_evac", "evacuating earliest %X %X evac: %X offset: %d",
            (int) b->evac_frags.key.word(0), (int) doc->key.word(0),
            doc_evacuator, (int) dir_offset(&doc_evacuator->overwrite_dir));
    }
  } else {
    // find which key matches the document
    EvacuationKey *ek = &b->evac_frags;
    for (; ek && !(ek->key == doc->key); ek = ek->link.next);
    if (!ek)
      goto Ldone;
    doc_evacuator->key = ek->key;
    doc_evacuator->earliest_key = ek->earliest_key;
    Debug("cache_evac", "evacuateDocReadDone key: %X earliest: %X",
          (int) ek->key.word(0), (int) ek->earliest_key.word(0));
  }
  // if the tag in the c->dir does match the first_key in the
  // document, then it has to be the earliest fragment. We gaurantee that
  // the first_key and the earliest_key will never collide (see
  // Cache::open_write).
  if (!dir_head(&b->dir) || !dir_compare_tag(&b->dir, &doc->first_key)) {
    next_CacheKey(&next_key, &doc->key);
    evacuate_segments(&next_key, &doc_evacuator->earliest_key, !b->f.readers, this);
  }
  return evacuateWrite(doc_evacuator, event, e);
Ldone:
  free_CacheVC(doc_evacuator);
  return aggWrite(event, e);
}

int
Part::evac_range(ink_off_t low, ink_off_t high, int evac_phase)
{
  int s = offset_to_part_offset(this, low);
  int e = offset_to_part_offset(this, high);
  int si = dir_offset_evac_bucket(s);
  int ei = dir_offset_evac_bucket(e);

  //  Debug("cache_evac", "evac_range %d %d %d %d", si, ei, s, e);
  for (int i = si; i <= ei; i++) {
    EvacuationBlock *b = evacuate[i].head;
    EvacuationBlock *first = 0;
    int first_offset = INT_MAX;
    for (; b; b = b->link.next) {
      int offset = dir_offset(&b->dir);
      int phase = dir_phase(&b->dir);
      //      Debug("cache_evac", "evac_range test %X %d %d",
      //      b->key.word(0), offset, b->f.done);
      if (offset >= s && offset < e && !b->f.done && phase == evac_phase)
        if (offset < first_offset) {
          first = b;
          first_offset = offset;
        }
    }
    if (first) {
      first->f.done = 1;
      io.aiocb.aio_fildes = fd;
      io.aiocb.aio_nbytes = dir_approx_size(&first->dir);
      io.aiocb.aio_offset = part_offset(this, &first->dir);
      if ((ink_off_t)(io.aiocb.aio_offset + io.aiocb.aio_nbytes) > (ink_off_t)(skip + len))
        io.aiocb.aio_nbytes = skip + len - io.aiocb.aio_offset;
      doc_evacuator = new_DocEvacuator(io.aiocb.aio_nbytes, this);
      doc_evacuator->overwrite_dir = first->dir;

      io.aiocb.aio_buf = doc_evacuator->buf->data();
      io.action = this;
      io.thread = mutex->thread_holding;
      Debug("cache_evac", "evac_range evacuating %X %d", (int) dir_tag(&first->dir), (int) dir_offset(&first->dir));
      SET_HANDLER(&Part::evacuateDocReadDone);
      ink_assert(ink_aio_read(&io) >= 0);
      return -1;
    }
  }
  return 0;
}


static int
agg_copy(char *p, CacheVC * vc)
{
  Part *part = vc->part;
  ink_off_t o = part->header->write_pos + part->agg_buf_pos;

  if (!vc->f.evacuator) {
    Doc *doc = (Doc *) p;
    IOBufferBlock *res_alt_blk = 0;

    int seglen = vc->write_len + vc->vec_len + sizeofDoc;
    ink_assert(!vc->f.http_request || seglen != sizeofDoc);
    int writelen = round_to_approx_size(seglen);
    // update copy of directory entry for this document
    dir_set_approx_size(&vc->dir, writelen);
    dir_set_offset(&vc->dir, offset_to_part_offset(part, o));
    ink_assert(part_offset(part, &vc->dir) < (part->skip + part->len));
    dir_set_phase(&vc->dir, part->header->phase);

    // fill in document header
    doc->magic = DOC_MAGIC;
    doc->len = seglen;
    doc->hlen = vc->vec_len;
    doc->total_len = vc->total_len;
    doc->first_key = vc->first_key;
    doc->sync_serial = part->header->sync_serial;
    doc->write_serial = part->header->write_serial;
    doc->checksum = DOC_NO_CHECKSUM;
    if (vc->pin_in_cache) {
      dir_set_pinned(&vc->dir, 1);
      doc->pinned = (inku32) (ink_get_based_hrtime() / HRTIME_SECOND) + vc->pin_in_cache;
    } else {
      dir_set_pinned(&vc->dir, 0);
      doc->pinned = 0;
    }

    if (vc->f.use_first_key) {
      if (doc->data_len())
        doc->key = vc->earliest_key;
      else {
        // the vector is being written by itself
        prev_CacheKey(&doc->key, &vc->earliest_key);
      }
      dir_set_head(&vc->dir, true);
    } else {
      doc->key = vc->key;
      dir_set_head(&vc->dir, !vc->segment);
    }

#ifdef HTTP_CACHE
    if (vc->f.rewrite_resident_alt) {
      ink_assert(vc->f.use_first_key);
      Doc *res_doc = (Doc *) vc->first_buf->data();
      res_alt_blk = new_IOBufferBlock(vc->first_buf, res_doc->data_len(), sizeofDoc + res_doc->hlen);
      doc->key = res_doc->key;
      doc->total_len = res_doc->data_len();
    }
#endif
    // update the new_info object_key, and total_len and dirinfo
#ifdef HTTP_CACHE
    if (vc->vec_len) {
      ink_debug_assert(vc->f.use_first_key);
      ink_debug_assert(vc->write_vector->count() > 0);
      if (!vc->f.update && !vc->f.evac_vector) {
        ink_debug_assert(!(vc->first_key == zero_key));
        CacheHTTPInfo *http_info = vc->write_vector->get(vc->alternate_index);
        http_info->object_size_set(vc->total_len);
      }
      // update + data_written =>  Update case (b)
      // need to change the old alternate's object length
      if (vc->f.update && vc->total_len) {
        CacheHTTPInfo *http_info = vc->write_vector->get(vc->alternate_index);
        http_info->object_size_set(vc->total_len);
      }
      ink_assert(!(((unsigned long) &doc->hdr[0]) & HDR_PTR_ALIGNMENT_MASK));
      ink_assert(vc->vec_len == vc->write_vector->marshal(&doc->hdr[0], vc->vec_len));
      // the single segment flag is not used in the write call.
      // putting it in for completeness.
      vc->f.single_segment = doc->single_segment();
    }
#endif
    // move data
    if (vc->write_len) {
      {
        ProxyMutex RELEASE_UNUSED *mutex = vc->part->mutex;
        ink_debug_assert(mutex->thread_holding == this_ethread());
        CACHE_DEBUG_SUM_DYN_STAT(cache_write_bytes_stat, vc->write_len);
      }
#ifdef HTTP_CACHE
      if (vc->f.rewrite_resident_alt)
        iobufferblock_memcpy(&doc->hdr[vc->vec_len], vc->write_len, res_alt_blk, 0);
      else
#endif
        iobufferblock_memcpy(&doc->hdr[vc->vec_len], vc->write_len, vc->blocks, vc->offset);
#ifdef VERIFY_JTEST_DATA
      if (f.use_first_key && vec_len) {
        int ib = 0, xd = 0;
        char xx[500];
        new_info.request_get().url_get().print(xx, 500, &ib, &xd);
        char *x = xx;
        for (int q = 0; q < 3; q++)
          x = strchr(x + 1, '/');
        ink_assert(!memcmp(&doc->hdr[vec_len], x, ib - (x - xx)));
      }
#endif

    }
    if (cache_config_enable_checksum) {
      doc->checksum = 0;
      for (char *b = doc->hdr; b < (char *) doc + doc->len; b++) {
        doc->checksum += *b;
      }
    }

    if (vc->f.http_request && vc->f.single_segment)
      ink_assert(doc->hlen);

    if (res_alt_blk)
      res_alt_blk->free();

    return writelen;
  } else {
    // for evacuated documents, copy the data, and update directory
    Doc *doc = (Doc *) vc->buf->data();
    int l = round_to_approx_size(doc->len);
    {
      ProxyMutex RELEASE_UNUSED *mutex = vc->part->mutex;
      ink_debug_assert(mutex->thread_holding == this_ethread());
      CACHE_DEBUG_INCREMENT_DYN_STAT(cache_gc_frags_evacuated_stat);
      CACHE_DEBUG_SUM_DYN_STAT(cache_gc_bytes_evacuated_stat, l);
    }
    doc->sync_serial = vc->part->header->sync_serial;
    doc->write_serial = vc->part->header->write_serial;

    memcpy(p, doc, doc->len);

    vc->dir = vc->overwrite_dir;
    dir_set_offset(&vc->dir, offset_to_part_offset(vc->part, o));
    dir_set_phase(&vc->dir, vc->part->header->phase);

    return l;
  }
}

inline void
Part::evacuate_cleanup_blocks(int i)
{
  EvacuationBlock *b = evacuate[i].head;
  while (b) {
    if (b->f.done &&
        ((header->phase != dir_phase(&b->dir) &&
          header->write_pos > part_offset(this, &b->dir)) ||
         (header->phase == dir_phase(&b->dir) && header->write_pos <= part_offset(this, &b->dir)))) {
      EvacuationBlock *x = b;
      Debug("cache_evac", "evacuate cleanup free %X offset %d",
            (int) b->evac_frags.key.word(0), (int) dir_offset(&b->dir));
      b = b->link.next;
      evacuate[i].remove(x);
      free_EvacuationBlock(x, mutex->thread_holding);
      continue;
    }
    b = b->link.next;
  }
}

void
Part::evacuate_cleanup()
{
  int eo = ((header->write_pos - start) / INK_BLOCK_SIZE) + 1;
  int e = dir_offset_evac_bucket(eo);
  int sx = e - (evacuate_size / PIN_SCAN_EVERY) - 1;
  int s = sx;
  int i;

  if (e > evacuate_size)
    e = evacuate_size;
  if (sx < 0)
    s = 0;
  for (i = s; i < e; i++)
    evacuate_cleanup_blocks(i);

  // if we have wrapped, handle the end bit
  if (sx <= 0) {
    s = evacuate_size + sx - 2;
    if (s < 0)
      s = 0;
    for (i = s; i < evacuate_size; i++)
      evacuate_cleanup_blocks(i);
  }
}

void
Part::periodic_scan()
{
  evacuate_cleanup();
  scan_for_pinned_documents();
  if (header->write_pos == start)
    scan_pos = start;
  scan_pos += len / PIN_SCAN_EVERY;
}

void
Part::agg_wrap()
{
  header->write_pos = start;
  header->phase = !header->phase;

  header->cycle++;
  header->agg_pos = header->write_pos;
  dir_lookaside_cleanup(this);
  dir_clean_part(this);
  periodic_scan();
}

/* NOTE:: This state can be called by an AIO thread, so DON'T DON'T
   DON'T schedule any events on this thread using VC_SCHED_XXX or
   mutex->thread_holding->schedule_xxx_local(). ALWAYS use 
   eventProcessor.schedule_xxx().
   Also, make sure that any functions called by this also use
   the eventProcessor to schedule events
   */
int
Part::aggWrite(int event, Event * e)
{
  NOWARN_UNUSED(e);
  NOWARN_UNUSED(event);

  cancel_trigger();

  ink_assert(!is_io_in_progress());
Lagain:
  // calculate length of aggregated write
  CacheVC * c;

  for (c = (CacheVC *) agg.head; c;) {
    int writelen = c->agg_len;
    ink_assert(writelen < AGG_SIZE);
    if (agg_buf_pos + writelen > AGG_SIZE || header->write_pos + agg_buf_pos + writelen > (skip + len))
      break;
    Debug("agg_read", "copying: %d, %llu, key: %d", agg_buf_pos, header->write_pos + agg_buf_pos, c->first_key.word(0));
    agg_copy(agg_buffer + agg_buf_pos, c);
    agg_todo_size -= writelen;
    agg_buf_pos += writelen;
    CacheVC *n = (CacheVC *) c->link.next;
    agg.dequeue();
    if (c->f.evacuator)
      c->handleEvent(AIO_EVENT_DONE, 0);
    else
      callback_cont->write_done.enqueue(c);
    c = n;
  }

  // if we got nothing...
  if (!agg_buf_pos) {
    if (!agg.head) {
      // nothing to get
      return EVENT_DONE;
    }
    if (header->write_pos == start) {
      // write aggregation too long, bad bad, punt on everything.
      //action_tag_assert("cache", false);
      Note("write aggregation exceeds part size");
      if (is_action_tag_set("cache")) {
        ink_release_assert(false);
      }
      CacheVC *vc;
      while ((vc = agg.dequeue())) {
        agg_todo_size -= vc->agg_len;
        // signal failure?
        callback_cont->write_done.enqueue(vc);
        if (!callback_cont->trigger)
          callback_cont->trigger = eventProcessor.schedule_imm(callback_cont);
      }
      return EVENT_DONE;
    }
    // start back
    agg_wrap();
    goto Lagain;
  }

  if (!callback_cont->trigger) {
    if (callback_cont->write_done.head)
      callback_cont->trigger = eventProcessor.schedule_imm(callback_cont);
  }
  // evacuate space
  ink_off_t end = header->write_pos + agg_buf_pos + EVAC_SIZE;
  if (evac_range(header->write_pos, end, !header->phase) < 0)
    return EVENT_CONT;
  if (end > skip + len)
    if (evac_range(start, start + (end - (skip + len)), header->phase))
      return EVENT_CONT;

  // if agg.head, then we are near the end of the disk, so
  // write down the aggregation in whatever size it is.
  if (agg_buf_pos < MAX_AGG_LEN && !agg.head && !dir_sync_waiting)
    return EVENT_CONT;
  Debug("agg_read", "flushing: %d", agg_buf_pos);

  // set write limit
  header->agg_pos = header->write_pos + agg_buf_pos;

  // do write
  io.aiocb.aio_fildes = fd;
  io.aiocb.aio_offset = header->write_pos;
  io.aiocb.aio_buf = agg_buffer;
  io.aiocb.aio_nbytes = agg_buf_pos;
  io.action = this;
  io.thread = mutex->thread_holding;
  SET_HANDLER(&Part::aggWriteDone);
  ink_aio_write(&io);
  return EVENT_CONT;
}

int
CacheVC::openWriteCloseDir(int event, Event * e)
{
  NOWARN_UNUSED(e);
  NOWARN_UNUSED(event);

  cancel_trigger();
  {
    CACHE_TRY_LOCK(lock, part->mutex, mutex->thread_holding);
    if (!lock) {
      SET_HANDLER(&CacheVC::openWriteCloseDir);
      ink_debug_assert(!is_io_in_progress());
      VC_SCHED_LOCK_RETRY();
    }
    part->close_write(this);
    if (closed < 0 && segment)
      dir_delete(&earliest_key, part, &earliest_dir);
  }
  if (is_debug_tag_set("cache_update")) {
    if (f.update && closed > 0) {
      if (!total_len && alternate_index != CACHE_ALT_REMOVED) {
        Debug("cache_update", "header only %d (%llu, %llu)\n",
              DIR_MASK_TAG(first_key.word(1)), update_key.b[0], update_key.b[1]);

      } else if (total_len && alternate_index != CACHE_ALT_REMOVED) {
        Debug("cache_update", "header body, %d, (%llu, %llu), (%llu, %llu)\n",
              DIR_MASK_TAG(first_key.word(1)), update_key.b[0], update_key.b[1], earliest_key.b[0], earliest_key.b[1]);
      } else if (!total_len && alternate_index == CACHE_ALT_REMOVED) {
        Debug("cache_update", "alt delete, %d, (%llu, %llu)\n",
              DIR_MASK_TAG(first_key.word(1)), update_key.b[0], update_key.b[1]);
      }
    }
  }
  // update the appropriate stat variable
  // These variables may not give the current no of documents with
  // one, two and three or more fragments. This is because for
  // updates we dont decrement the variable corresponding the old
  // size of the document
  /*
     Debug("segment_size","Segment = %d\n",segment);
     Debug("segment_size","total_len = %d\n",total_len);
     Debug("segment_size","closed = %d\n",closed);
     Debug("segment_size","f.update = %d\n",f.update);
   */
  if ((closed == 1) && (total_len > 0)) {
    Debug("cache_stats", "Segment = %d", segment);
    switch (segment) {
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

  return free_CacheVC(this);
}

int
CacheVC::openWriteCloseHeadDone(int event, Event * e)
{
  NOWARN_UNUSED(e);
  ink_assert(event == AIO_EVENT_DONE);
  set_io_not_in_progress();
  od->writing_vec = 0;

  if (!io.ok())
    return openWriteCloseDir(event, e);
  ink_assert(f.use_first_key);
  // lock taken by caller
  ink_debug_assert(part->mutex->thread_holding == this_ethread());
  if (!od->dont_update_directory) {
    if (dir_is_empty(&od->first_dir)) {
      dir_insert(&first_key, part, &dir);
    } else {
      // multiple segment vector write
      dir_overwrite(&first_key, part, &dir, &od->first_dir, false);
      // insert moved resident alternate
      if (od->move_resident_alt) {
        if (dir_valid(part, &od->single_doc_dir))
          dir_insert(&od->single_doc_key, part, &od->single_doc_dir);
        od->move_resident_alt = 0;
      }
    }
    od->first_dir = dir;
    if (f.http_request && f.single_segment) {
      // segment is tied to the vector
      od->move_resident_alt = 1;
      if (!f.rewrite_resident_alt) {
        od->single_doc_key = earliest_key;
      }
      dir_assign(&od->single_doc_dir, &dir);
      dir_set_tag(&od->single_doc_dir, od->single_doc_key.word(1));
    }
  }
  return openWriteCloseDir(event, e);
}


int
CacheVC::openWriteCloseHead(int event, Event * e)
{
  NOWARN_UNUSED(e);

  cancel_trigger();
  f.use_first_key = 1;
  if (io.ok())
    ink_assert(segment || (length == total_len));
  else
    return openWriteCloseDir(event, e);
  if (f.data_done)
    write_len = 0;
  else
    write_len = length;
#ifdef HTTP_CACHE
  if (f.http_request) {
    SET_HANDLER(&CacheVC::updateVector);
    return updateVector(EVENT_IMMEDIATE, 0);
  } else {
#endif
    SET_HANDLER(&CacheVC::openWriteCloseHeadDone);
    return do_write_lock();
#ifdef HTTP_CACHE

  }
#endif
}

int
CacheVC::openWriteCloseDataDone(int event, Event * e)
{
  NOWARN_UNUSED(e);

  if (event == AIO_EVENT_DONE) {
    set_io_not_in_progress();

    if (!io.ok())
      return openWriteCloseDir(event, e);
  }
  // locked taken by caller
  ink_debug_assert(part->mutex->thread_holding == this_ethread());
  dir_insert(&key, part, &dir);

  f.data_done = 1;
  ink_assert(segment);
  return openWriteCloseHead(event, e);
}

int
CacheVC::openWriteClose(int event, Event * e)
{
  NOWARN_UNUSED(e);

  cancel_trigger();
  if (is_io_in_progress()) {
    if (event != AIO_EVENT_DONE)
      return EVENT_CONT;
    set_io_not_in_progress();
    if (!io.ok())
      return openWriteCloseDir(event, e);
  }
  if (closed > 0) {
    if (total_len == 0) {
#ifdef HTTP_CACHE
      if (f.update) {
        return updateVector(event, e);
      } else {
        // If we've been CLOSE'd but nothing has been written then
        // this close is transformed into an abort.
        closed = -1;
        return openWriteCloseDir(event, e);
      }
#else
      return openWriteCloseDir(event, e);
#endif //HTTP_CACHE

    }
    if (length && segment) {
      SET_HANDLER(&CacheVC::openWriteCloseDataDone);
      write_len = length;
      return do_write_lock();
    } else
      return openWriteCloseHead(event, e);
  } else {
    return openWriteCloseDir(event, e);
  }
}

int
CacheVC::openWriteWriteDone(int event, Event * e)
{
  NOWARN_UNUSED(e);

  cancel_trigger();
  ink_assert(is_io_in_progress());
  if (event != AIO_EVENT_DONE)
    return EVENT_CONT;

  set_io_not_in_progress();
  // In the event of VC_EVENT_ERROR, the cont must do an io_close
  if (!io.ok()) {
    if (closed) {
      closed = -1;
      return die();
    }
    SET_HANDLER(&CacheVC::openWriteMain);
    calluser(VC_EVENT_ERROR);
    return EVENT_CONT;
  }
  // store the earliest directory. Need to remove the earliest dir
  // in case the writer aborts.
  if (!segment) {
    ink_assert(key == earliest_key);
    earliest_dir = dir;
  }
  segment++;
  dir_insert(&key, part, &dir);
  Debug("cache_insert", "WriteDone: %X, %X, %d", key.word(0), first_key.word(0), write_len);
  blocks = iobufferblock_skip(blocks, &offset, &length, write_len);
  write_len = length;
  next_CacheKey(&key, &key);
  if (closed)
    return die();
  SET_HANDLER(&CacheVC::openWriteMain);
  return openWriteMain(event, e);
}

int
CacheVC::openWriteMain(int event, Event * e)
{
  NOWARN_UNUSED(e);
  NOWARN_UNUSED(event);
  cancel_trigger();
  int called_user = 0;
  ink_debug_assert(!is_io_in_progress());
Lagain:
  if (!vio.buffer.writer()) {
    if (calluser(VC_EVENT_WRITE_READY))
      return EVENT_DONE;
    if (!vio.buffer.writer())
      return EVENT_CONT;
  }
  if (vio.ntodo() <= 0) {
    called_user = 1;
    if (calluser(VC_EVENT_WRITE_COMPLETE))
      return EVENT_DONE;
    if (vio.ntodo() <= 0)
      return EVENT_CONT;
  }
  ink64 ntodo = (ink64) vio.ntodo() + length;
  int total_avail = vio.buffer.reader()->read_avail();
  int avail = total_avail;
  int towrite = avail + length;
  if (towrite > ntodo) {
    avail -= (towrite - ntodo);
    towrite = ntodo;
  }
  if (towrite > MAX_FRAG_SIZE) {
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
  length = towrite;
  if (length > TARGET_FRAG_SIZE && length < SHRINK_TARGET_FRAG_SIZE)
    write_len = TARGET_FRAG_SIZE;
  else
    write_len = length;
  bool not_writing = towrite != ntodo && towrite < TARGET_FRAG_SIZE;
  if (!called_user) {
    if (not_writing) {
      called_user = 1;
      if (calluser(VC_EVENT_WRITE_READY))
        return EVENT_DONE;
      goto Lagain;
    } else if (vio.ntodo() <= 0)
      goto Lagain;
  }
  if (not_writing)
    return EVENT_CONT;

  SET_HANDLER(&CacheVC::openWriteWriteDone);
  return do_write_lock();
}

// begin overwrite
int
CacheVC::openWriteOverwrite(int event, Event * e)
{
  NOWARN_UNUSED(e);
  int res;

  cancel_trigger();
  if (event != AIO_EVENT_DONE) {
    if (event == EVENT_IMMEDIATE)
      last_collision = 0;
  } else {
    Doc *doc = NULL;
    set_io_not_in_progress();
    if (_action.cancelled) {
      SET_HANDLER(&CacheVC::openWriteCloseDir);
      return openWriteCloseDir(event, e);
    }
    if (!io.ok())
      goto Ldone;
    doc = (Doc *) buf->data();
    if (!(doc->first_key == first_key))
      goto Lcollision;
    od->first_dir = dir;
    goto Ldone;
  }

Lcollision:
  {
    CACHE_TRY_LOCK(lock, part->mutex, this_ethread());
    if (!lock)
      VC_SCHED_LOCK_RETRY();
    res = dir_probe(&first_key, part, &dir, &last_collision);
    if (res > 0)
      return do_read(&first_key);
  }
Ldone:
  SET_HANDLER(&CacheVC::openWriteMain);
  _action.continuation->handleEvent(CACHE_EVENT_OPEN_WRITE, (void *) this);
  return EVENT_DONE;
}

#ifdef HTTP_CACHE
// openWriteStartDone handles vector read (addition of alternates)
// and lock misses
int
CacheVC::openWriteStartDone(int event, Event * e)
{
  NOWARN_UNUSED(e);

  intptr_t err = ECACHE_NO_DOC;
  cancel_trigger();
  if (is_io_in_progress()) {
    if (event != AIO_EVENT_DONE)
      return EVENT_CONT;
    set_io_not_in_progress();
  }
  if (_action.cancelled && (!od || !od->has_multiple_writers()))
    goto Lcancel;

  if (event == AIO_EVENT_DONE) {        // vector read done
    Doc *doc = (Doc *) buf->data();
    if (!io.ok()) {
      err = ECACHE_READ_FAIL;
      goto Lfailure;
    }

    /* INKqa07123.
       A directory entry which is nolonger valid may have been overwritten. 
       We need to start afresh from the beginning by setting last_collision 
       to NULL.
     */
    if (!dir_valid(part, &dir)) {
      Debug("cache_write",
            "OpenReadStartDone: Dir not valid: Write Head: %d, Dir: %d",
            offset_to_part_offset(part, part->header->write_pos), dir.offset);
      last_collision = NULL;
      goto Lcollision;
    }
    if (!(doc->first_key == first_key))
      goto Lcollision;
    if (doc->magic != DOC_MAGIC) {
      err = ECACHE_BAD_META_DATA;
      goto Lfailure;
    }
    if (!doc->hlen) {
      err = ECACHE_BAD_META_DATA;
      goto Lfailure;
    }
    ink_assert((((unsigned long) &doc->hdr[0]) & HDR_PTR_ALIGNMENT_MASK) == 0);

    if (write_vector->get_handles(doc->hdr, doc->hlen, buf) != doc->hlen) {
      err = ECACHE_BAD_META_DATA;
      goto Lfailure;
    }
    ink_debug_assert(write_vector->count() > 0);
    od->first_dir = dir;
    first_dir = dir;
    if (doc->single_segment()) {
      // segment is tied to the vector
      od->move_resident_alt = 1;
      od->single_doc_key = doc->key;
      dir_assign(&od->single_doc_dir, &dir);
      dir_set_tag(&od->single_doc_dir, DIR_MASK_TAG(od->single_doc_key.word(1)));
    }
    first_buf = buf;
    goto Lsuccess;
  }

Lcollision:
  {
    int if_writers = ((uintptr_t) info == CACHE_ALLOW_MULTIPLE_WRITES);
    CACHE_TRY_LOCK(lock, part->mutex, mutex->thread_holding);
    if (!lock)
      VC_SCHED_LOCK_RETRY();
    if (!od) {
      if ((err = part->open_write(this, if_writers,
                                  cache_config_http_max_alts > 1 ? cache_config_http_max_alts : 0)) > 0)
        goto Lfailure;
      if (od->has_multiple_writers()) {
        SET_HANDLER(&CacheVC::openWriteMain);
        _action.continuation->handleEvent(CACHE_EVENT_OPEN_WRITE, (void *) this);
        return EVENT_DONE;
      }
    }
    // check for collision
    if (dir_probe(&first_key, part, &dir, &last_collision)) {
      od->reading_vec = 1;
      return do_read(&first_key);
    }
    if (f.update) {
      // fail update because vector has been GC'd
      goto Lfailure;
    }

  }
Lsuccess:
  od->reading_vec = 0;
  if (_action.cancelled)
    goto Lcancel;
  SET_HANDLER(&CacheVC::openWriteMain);
  _action.continuation->handleEvent(CACHE_EVENT_OPEN_WRITE, (void *) this);
  return EVENT_DONE;

Lfailure:
  CACHE_INCREMENT_DYN_STAT(base_stat + CACHE_STAT_FAILURE);
  _action.continuation->handleEvent(CACHE_EVENT_OPEN_WRITE_FAILED, (void *) -err);
Lcancel:
  if (od) {
    od->reading_vec = 0;
    SET_HANDLER(&CacheVC::openWriteCloseDir);
    openWriteCloseDir(event, e);
    return EVENT_DONE;
  } else
    return free_CacheVC(this);
}
#endif
// handle lock failures from main Cache::open_write entry points below
int
CacheVC::openWriteStartBegin(int event, Event * e)
{
  NOWARN_UNUSED(e);
  NOWARN_UNUSED(event);

  intptr_t err;
  cancel_trigger();
  if (_action.cancelled)
    return free_CacheVC(this);
  CACHE_TRY_LOCK(lock, part->mutex, mutex->thread_holding);
  if (!lock)
    VC_SCHED_LOCK_RETRY();
  if ((err = part->open_write(this, false, 1)) > 0) {
    CACHE_INCREMENT_DYN_STAT(base_stat + CACHE_STAT_FAILURE);
    _action.continuation->handleEvent(CACHE_EVENT_OPEN_WRITE_FAILED, (void *) -err);
    return free_CacheVC(this);
  }
  if (f.overwrite) {
    SET_HANDLER(&CacheVC::openWriteOverwrite);
    return openWriteOverwrite(EVENT_IMMEDIATE, 0);
  } else {
    // write by key
    SET_HANDLER(&CacheVC::openWriteMain);
    _action.continuation->handleEvent(CACHE_EVENT_OPEN_WRITE, (void *) this);
    return EVENT_DONE;
  }
}

// main entry point for writing of of non-http documents
Action *
Cache::open_write(Continuation * cont, CacheKey * key, CacheFragType frag_type,
                  bool overwrite, time_t apin_in_cache, char *hostname, int host_len)
{

  if (!(CacheProcessor::cache_ready & frag_type)) {
    cont->handleEvent(CACHE_EVENT_OPEN_WRITE_FAILED, (void *) -ECACHE_NOT_READY);
    return ACTION_RESULT_DONE;
  }

  ink_assert(caches[frag_type] == this);

  intptr_t res = 0;
  CacheVC *c = new_CacheVC(cont);
  ProxyMutex *mutex = cont->mutex;
  c->vio.op = VIO::WRITE;
  c->base_stat = cache_write_active_stat;
  c->part = key_to_part(key, hostname, host_len);
  Part *part = c->part;
  CACHE_INCREMENT_DYN_STAT(c->base_stat + CACHE_STAT_ACTIVE);
  c->first_key = c->key = *key;
  /*
     The transition from single segment document to a multi-segment document
     would cause a problem if the key and the first_key collide. In case of
     a collision, old vector data could be served to HTTP. Need to avoid that.
     Also, when evacuating a fragment, we have to decide if its the first_key
     or the earliest_key based on the dir_tag. 
   */
  do {
    rand_CacheKey(&c->key, cont->mutex);
  }
  while (DIR_MASK_TAG(c->key.word(1)) == DIR_MASK_TAG(c->first_key.word(1)));
  c->earliest_key = c->key;
#ifdef HTTP_CACHE
  c->info = 0;
#endif
  c->f.overwrite = overwrite;
  c->pin_in_cache = (inku32) apin_in_cache;

  if ((res = c->part->open_write_lock(c, false, 1)) > 0) {
    // document currently being written, abort
    CACHE_INCREMENT_DYN_STAT(c->base_stat + CACHE_STAT_FAILURE);
    cont->handleEvent(CACHE_EVENT_OPEN_WRITE_FAILED, (void *) -res);
    free_CacheVC(c);
    return ACTION_RESULT_DONE;
  }
  if (res < 0) {
    SET_CONTINUATION_HANDLER(c, &CacheVC::openWriteStartBegin);
    c->trigger = CONT_SCHED_LOCK_RETRY(c);
    return &c->_action;
  }
  if (!overwrite) {
    SET_CONTINUATION_HANDLER(c, &CacheVC::openWriteMain);
    cont->handleEvent(CACHE_EVENT_OPEN_WRITE, (void *) c);
    return ACTION_RESULT_DONE;
  } else {
    SET_CONTINUATION_HANDLER(c, &CacheVC::openWriteOverwrite);
    if (c->openWriteOverwrite(EVENT_IMMEDIATE, 0) == EVENT_DONE)
      return ACTION_RESULT_DONE;
    else
      return &c->_action;
  }
}

#ifdef HTTP_CACHE
// main entry point for writing of http documents
Action *
Cache::open_write(Continuation * cont, CacheKey * key, CacheHTTPInfo * info, time_t apin_in_cache,
                  CacheKey * key1, CacheFragType type, char *hostname, int host_len)
{
  NOWARN_UNUSED(key1);

  if (!(CacheProcessor::cache_ready & type)) {
    cont->handleEvent(CACHE_EVENT_OPEN_WRITE_FAILED, (void *) -ECACHE_NOT_READY);
    return ACTION_RESULT_DONE;
  }

  ink_assert(caches[type] == this);
  intptr_t err = 0;
  int if_writers = (uintptr_t) info == CACHE_ALLOW_MULTIPLE_WRITES;
  CacheVC *c = new_CacheVC(cont);
  ProxyMutex *mutex = cont->mutex;
  c->vio.op = VIO::WRITE;
  c->first_key = *key;
  /*
     The transition from single segment document to a multi-segment document
     would cause a problem if the key and the first_key collide. In case of
     a collision, old vector data could be served to HTTP. Need to avoid that.
     Also, when evacuating a fragment, we have to decide if its the first_key
     or the earliest_key based on the dir_tag. 
   */
  do {
    rand_CacheKey(&c->key, cont->mutex);
  }
  while (DIR_MASK_TAG(c->key.word(1)) == DIR_MASK_TAG(c->first_key.word(1)));
  c->earliest_key = c->key;
  c->f.http_request = 1;
  c->part = key_to_part(key, hostname, host_len);
  Part *part = c->part;
  c->info = info;
  if (c->info && (uintptr_t) info != CACHE_ALLOW_MULTIPLE_WRITES) {
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
    c->f.update = 1;
    c->base_stat = cache_update_active_stat;
    Debug("cache_update", "Update called");
    info->object_key_get(&c->update_key);
    ink_debug_assert(!(c->update_key == zero_key));
    c->update_len = info->object_size_get();
  } else
    c->base_stat = cache_write_active_stat;
  CACHE_INCREMENT_DYN_STAT(c->base_stat + CACHE_STAT_ACTIVE);
  c->pin_in_cache = (inku32) apin_in_cache;

  CACHE_TRY_LOCK(lock, c->part->mutex, cont->mutex->thread_holding);
  if (lock) {
    if ((err = c->part->open_write(c, if_writers, cache_config_http_max_alts > 1 ? cache_config_http_max_alts : 0)) > 0) {
      goto Lfailure;
    }
    // If there are multiple writers, then this one cannot be an update.
    // Only the first writer can do an update. If that's the case, we can
    // return success to the state machine now.;
    if (c->od->has_multiple_writers()) {
      SET_CONTINUATION_HANDLER(c, &CacheVC::openWriteMain);
      cont->handleEvent(CACHE_EVENT_OPEN_WRITE, (void *) c);
      return ACTION_RESULT_DONE;
    }
    if (!dir_probe(key, c->part, &c->dir, &c->last_collision)) {
      if (c->f.update) {
        // fail update because vector has been GC'd
        // This situation can also arise in openWriteStartDone
        err = ECACHE_NO_DOC;
        goto Lfailure;
      }
      // document doesn't exist, begin write
      SET_CONTINUATION_HANDLER(c, &CacheVC::openWriteMain);
      cont->handleEvent(CACHE_EVENT_OPEN_WRITE, (void *) c);
      return ACTION_RESULT_DONE;
    } else {
      c->od->reading_vec = 1;
      // document exists, read vector
      SET_CONTINUATION_HANDLER(c, &CacheVC::openWriteStartDone);
      if (c->do_read(&c->first_key) == EVENT_CONT)
        return &c->_action;
      else
        return ACTION_RESULT_DONE;
    }
  }
  // missed lock
  SET_CONTINUATION_HANDLER(c, &CacheVC::openWriteStartDone);
  CONT_SCHED_LOCK_RETRY(c);
  return &c->_action;

Lfailure:
  CACHE_INCREMENT_DYN_STAT(c->base_stat + CACHE_STAT_FAILURE);
  cont->handleEvent(CACHE_EVENT_OPEN_WRITE_FAILED, (void *) -err);
  if (c->od) {
    SET_CONTINUATION_HANDLER(c, &CacheVC::openWriteCloseDir);
    c->openWriteCloseDir(EVENT_IMMEDIATE, 0);
    return ACTION_RESULT_DONE;
  }
  free_CacheVC(c);
  return ACTION_RESULT_DONE;
}
#endif

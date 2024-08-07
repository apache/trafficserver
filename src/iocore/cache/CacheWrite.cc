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
#include "P_CacheDoc.h"

DEF_DBG(cache_update)
DEF_DBG(cache_update_alt)

#ifdef DEBUG
DEF_DBG(cache_stats)
DEF_DBG(cache_write)
DEF_DBG(cache_insert)
#endif

// Given a key, finds the index of the alternate which matches
// used to get the alternate which is actually present in the document
int
get_alternate_index(CacheHTTPInfoVector *cache_vector, CacheKey key)
{
  int            alt_count = cache_vector->count();
  CacheHTTPInfo *obj;
  if (!alt_count) {
    return -1;
  }
  for (int i = 0; i < alt_count; i++) {
    obj = cache_vector->get(i);
    if (obj->compare_object_key(&key)) {
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
    CACHE_TRY_LOCK(lock, stripe->mutex, mutex->thread_holding);
    if (!lock.is_locked() || od->writing_vec) {
      VC_SCHED_LOCK_RETRY();
    }

    int vec = alternate.valid();
    if (f.update) {
      // all Update cases. Need to get the alternate index.
      alternate_index = get_alternate_index(write_vector, update_key);
      Dbg(get_dbg_cache_update(), "updating alternate index %d frags %d", alternate_index,
          alternate_index >= 0 ? write_vector->get(alternate_index)->get_frag_offset_count() : -1);
      // if its an alternate delete
      if (!vec) {
        ink_assert(!total_len);
        if (alternate_index >= 0) {
          write_vector->remove(alternate_index, true);
          alternate_index = CACHE_ALT_REMOVED;
          if (!write_vector->count()) {
            dir_delete(&first_key, stripe, &od->first_dir);
          }
        }
        // the alternate is not there any more. somebody might have
        // deleted it. Just close this writer
        if (alternate_index != CACHE_ALT_REMOVED || !write_vector->count()) {
          SET_HANDLER(&CacheVC::openWriteCloseDir);
          return openWriteCloseDir(EVENT_IMMEDIATE, nullptr);
        }
      }
      if (update_key == od->single_doc_key && (total_len || f.allow_empty_doc || !vec)) {
        od->move_resident_alt = false;
      }
    }
    if (cache_config_http_max_alts > 1 && write_vector->count() >= cache_config_http_max_alts && alternate_index < 0) {
      if (od->move_resident_alt && get_alternate_index(write_vector, od->single_doc_key) == 0) {
        od->move_resident_alt = false;
      }
      if (cache_config_log_alternate_eviction) {
        // Initially there was an attempt to make alternate eviction a log
        // field. However it was discovered this could not work because this
        // code, in which alternates are evicted, happens during the processing
        // of IO which happens after transaction logs are emitted and after the
        // HttpSM is destructed. Instead, therefore, alternate eviction logging
        // was implemented for diags.log with the
        // proxy.config.cache.log.alternate.eviction toggle.
        CacheHTTPInfo *info    = write_vector->get(0);
        HTTPHdr       *request = info->request_get();
        if (request->valid()) {
          // Marking the request's target as dirty will guarantee that the
          // internal members of the request used for printing the URL will be
          // coherent and valid by the time it is printed.
          request->mark_target_dirty();
          // In contrast to url_string_get, this url_print interface doesn't
          // use HTTPHdr's m_heap which is not valid at this point because the
          // HttpSM is most likely gone.
          int                  url_length = request->url_printed_length();
          ats_scoped_mem<char> url_text;
          url_text   = static_cast<char *>(ats_malloc(url_length + 1));
          int index  = 0;
          int offset = 0;
          // url_print does not NULL terminate, so url_length instead of url_length + 1.
          int ret                    = request->url_print(url_text.get(), url_length, &index, &offset);
          url_text.get()[url_length] = '\0';
          if (ret == 0) {
            Note("Could not print URL of evicted alternate.");
          } else {
            Status("The maximum number of alternates was exceeded for a resource. "
                   "An alternate was evicted for URL: %.*s",
                   url_length, url_text.get());
          }
        }
      }
      write_vector->remove(0, true);
    }
    if (vec) {
      /* preserve fragment offset data from old info. This method is
         called iff the update is a header only update so the fragment
         data should remain valid.
      */
      // If we are not in header only updating case. Don't copy fragments.
      if (alternate_index >= 0 &&
          ((total_len == 0 && alternate.get_frag_offset_count() == 0) && !(f.allow_empty_doc && this->vio.nbytes == 0))) {
        alternate.copy_frag_offsets_from(write_vector->get(alternate_index));
      }
      alternate_index = write_vector->insert(&alternate, alternate_index);
    }

    if (od->move_resident_alt && first_buf.get() && !od->has_multiple_writers()) {
      Doc *doc          = reinterpret_cast<Doc *>(first_buf->data());
      int  small_doc    = static_cast<int64_t>(doc->data_len()) < static_cast<int64_t>(cache_config_alt_rewrite_max_size);
      int  have_res_alt = doc->key == od->single_doc_key;
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
        Dbg(get_dbg_cache_update_alt(), "rewriting resident alt size: %d key: %X, first_key: %X", write_len, doc->key.slice32(0),
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
     is not aborted even if
     stripe->_write_buffer.get_bytes_pending_aggregation() > agg_write_backlog
   - f.evacuator. If this is an evacuation.
   - f.rewrite_resident_alt. The resident alternate is rewritten.
   - f.update. Used only if the write_vector needs to be written to disk.
     Used to set the length of the alternate to total_len.
   - write_vector. Used only if frag_type == CACHE_FRAG_TYPE_HTTP &&
     (f.use_first_key || f.evac_vector) is set. Write_vector is written to disk
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
CacheVC::handleWrite(int event, Event * /* e ATS_UNUSED */)
{
  // plain write case
  ink_assert(!trigger);
  frag_len = 0;

  set_agg_write_in_progress();
  POP_HANDLER;

  bool max_doc_error = (cache_config_max_doc_size && (cache_config_max_doc_size < vio.ndone ||
                                                      (vio.nbytes != INT64_MAX && (cache_config_max_doc_size < vio.nbytes))));
  // Make sure the size is correct for checking error conditions before calling add_writer(this).
  agg_len = stripe->round_to_approx_size(write_len + header_len + frag_len + sizeof(Doc));
  if (max_doc_error || !stripe->add_writer(this)) {
    Metrics::Counter::increment(cache_rsb.write_backlog_failure);
    Metrics::Counter::increment(stripe->cache_vol->vol_rsb.write_backlog_failure);
    Metrics::Counter::increment(cache_rsb.status[op_type].failure);
    Metrics::Counter::increment(stripe->cache_vol->vol_rsb.status[op_type].failure);
    io.aio_result = AIO_SOFT_FAILURE;
    if (event == EVENT_CALL) {
      return EVENT_RETURN;
    }
    return handleEvent(AIO_EVENT_DONE, nullptr);
  }
  if (!stripe->is_io_in_progress()) {
    return stripe->aggWrite(event, this);
  }
  return EVENT_CONT;
}

int
CacheVC::openWriteCloseDir(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */)
{
  cancel_trigger();
  {
    CACHE_TRY_LOCK(lock, stripe->mutex, mutex->thread_holding);
    if (!lock.is_locked()) {
      SET_HANDLER(&CacheVC::openWriteCloseDir);
      ink_assert(!is_io_in_progress());
      VC_SCHED_LOCK_RETRY();
    }
    stripe->close_write(this);
    if (closed < 0 && fragment) {
      dir_delete(&earliest_key, stripe, &earliest_dir);
    }
  }
  if (get_dbg_cache_update().on()) {
    if (f.update && closed > 0) {
      if (!total_len && !f.allow_empty_doc && alternate_index != CACHE_ALT_REMOVED) {
        Dbg(get_dbg_cache_update(), "header only %d (%" PRIu64 ", %" PRIu64 ")", DIR_MASK_TAG(first_key.slice32(2)),
            update_key.b[0], update_key.b[1]);

      } else if ((total_len || f.allow_empty_doc) && alternate_index != CACHE_ALT_REMOVED) {
        Dbg(get_dbg_cache_update(), "header body, %d, (%" PRIu64 ", %" PRIu64 "), (%" PRIu64 ", %" PRIu64 ")",
            DIR_MASK_TAG(first_key.slice32(2)), update_key.b[0], update_key.b[1], earliest_key.b[0], earliest_key.b[1]);
      } else if (!total_len && alternate_index == CACHE_ALT_REMOVED) {
        Dbg(get_dbg_cache_update(), "alt delete, %d, (%" PRIu64 ", %" PRIu64 ")", DIR_MASK_TAG(first_key.slice32(2)),
            update_key.b[0], update_key.b[1]);
      }
    }
  }
  // update the appropriate stat variable
  // These variables may not give the current no of documents with
  // one, two and three or more fragments. This is because for
  // updates we dont decrement the variable corresponding the old
  // size of the document
  if ((closed == 1) && (total_len > 0 || f.allow_empty_doc)) {
    DDbg(get_dbg_cache_stats(), "Fragment = %d", fragment);

    Metrics::Counter::increment(cache_rsb.fragment_document_count[std::clamp(fragment, 0, 2)]);
    Metrics::Counter::increment(stripe->cache_vol->vol_rsb.fragment_document_count[std::clamp(fragment, 0, 2)]);
  }
  if (f.close_complete) {
    recursive++;
    ink_assert(!stripe || this_ethread() != stripe->mutex->thread_holding);
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
    CACHE_TRY_LOCK(lock, stripe->mutex, mutex->thread_holding);
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
        dir_insert(&first_key, stripe, &dir);
      } else {
        // multiple fragment vector write
        dir_overwrite(&first_key, stripe, &dir, &od->first_dir, false);
        // insert moved resident alternate
        if (od->move_resident_alt) {
          if (dir_valid(stripe, &od->single_doc_dir)) {
            dir_insert(&od->single_doc_key, stripe, &od->single_doc_dir);
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
    CACHE_TRY_LOCK(lock, stripe->mutex, this_ethread());
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
    dir_insert(&key, stripe, &dir);
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
    CACHE_TRY_LOCK(lock, stripe->mutex, mutex->thread_holding);
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
    dir_insert(&key, stripe, &dir);
    DDbg(get_dbg_cache_insert(), "WriteDone: %X, %X, %d", key.slice32(0), first_key.slice32(0), write_len);
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
  uint64_t value = cache_config_target_fragment_size - sizeof(Doc);
  ink_release_assert(value <= MAX_FRAG_SIZE);
  return value;
}

int
CacheVC::openWriteMain(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */)
{
  cancel_trigger();
  int called_user = 0;
  ink_assert(!is_io_in_progress());
Lagain:
  if (!vio.get_writer()) {
    if (calluser(VC_EVENT_WRITE_READY) == EVENT_DONE) {
      return EVENT_DONE;
    }
    if (!vio.get_writer()) {
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
  int64_t ntodo       = static_cast<int64_t>(vio.ntodo() + length);
  int64_t total_avail = vio.get_reader()->read_avail();
  int64_t avail       = total_avail;
  int64_t towrite     = avail + length;
  if (towrite > ntodo) {
    avail   -= (towrite - ntodo);
    towrite  = ntodo;
  }
  if (towrite > static_cast<int>(MAX_FRAG_SIZE)) {
    avail   -= (towrite - MAX_FRAG_SIZE);
    towrite  = MAX_FRAG_SIZE;
  }
  if (!blocks && towrite) {
    blocks = vio.get_reader()->block;
    offset = vio.get_reader()->start_offset;
  }
  if (avail > 0) {
    vio.get_reader()->consume(avail);
    vio.ndone += avail;
    total_len += avail;
  }
  length = static_cast<uint64_t>(towrite);
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
    doc = reinterpret_cast<Doc *>(buf->data());
    if (!(doc->first_key == first_key)) {
      goto Lcollision;
    }
    od->first_dir = dir;
    first_buf     = buf;
    goto Ldone;
  }
Lcollision: {
  CACHE_TRY_LOCK(lock, stripe->mutex, this_ethread());
  if (!lock.is_locked()) {
    VC_LOCK_RETRY_EVENT();
  }
  int res = dir_probe(&first_key, stripe, &dir, &last_collision);
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
    CACHE_TRY_LOCK(lock, stripe->mutex, mutex->thread_holding);
    if (!lock.is_locked()) {
      VC_LOCK_RETRY_EVENT();
    }

    if (_action.cancelled && (!od || !od->has_multiple_writers())) {
      goto Lcancel;
    }

    if (event == AIO_EVENT_DONE) { // vector read done
      Doc *doc = reinterpret_cast<Doc *>(buf->data());
      if (!io.ok()) {
        err = ECACHE_READ_FAIL;
        goto Lfailure;
      }

      /* INKqa07123.
         A directory entry which is no longer valid may have been overwritten.
         We need to start afresh from the beginning by setting last_collision
         to nullptr.
       */
      if (!dir_valid(stripe, &dir)) {
        DDbg(get_dbg_cache_write(), "OpenReadStartDone: Dir not valid: Write Head: %" PRId64 ", Dir: %" PRId64,
             (int64_t)stripe->offset_to_vol_offset(stripe->header->write_pos), dir_offset(&dir));
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
      if ((err = stripe->open_write(this, if_writers, cache_config_http_max_alts > 1 ? cache_config_http_max_alts : 0)) > 0) {
        goto Lfailure;
      }
      if (od->has_multiple_writers()) {
        MUTEX_RELEASE(lock);
        SET_HANDLER(&CacheVC::openWriteMain);
        return callcont(CACHE_EVENT_OPEN_WRITE);
      }
    }
    // check for collision
    if (dir_probe(&first_key, stripe, &dir, &last_collision)) {
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
  Metrics::Counter::increment(cache_rsb.status[op_type].failure);
  Metrics::Counter::increment(stripe->cache_vol->vol_rsb.status[op_type].failure);
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
  if (((err = stripe->open_write_lock(this, false, 1)) > 0)) {
    Metrics::Counter::increment(cache_rsb.status[op_type].failure);
    Metrics::Counter::increment(stripe->cache_vol->vol_rsb.status[op_type].failure);
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

// main entry point for writing of non-http documents
Action *
Cache::open_write(Continuation *cont, const CacheKey *key, CacheFragType frag_type, int options, time_t apin_in_cache,
                  const char *hostname, int host_len)
{
  if (!CacheProcessor::IsCacheReady(frag_type)) {
    cont->handleEvent(CACHE_EVENT_OPEN_WRITE_FAILED, (void *)-ECACHE_NOT_READY);
    return ACTION_RESULT_DONE;
  }

  ink_assert(caches[frag_type] == this);

  intptr_t res = 0;
  CacheVC *c   = new_CacheVC(cont);
  SCOPED_MUTEX_LOCK(lock, c->mutex, this_ethread());
  c->vio.op        = VIO::WRITE;
  c->op_type       = static_cast<int>(CacheOpType::Write);
  c->stripe        = key_to_stripe(key, hostname, host_len);
  StripeSM *stripe = c->stripe;
  Metrics::Gauge::increment(cache_rsb.status[c->op_type].active);
  Metrics::Gauge::increment(stripe->cache_vol->vol_rsb.status[c->op_type].active);
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
    rand_CacheKey(&c->key);
  } while (DIR_MASK_TAG(c->key.slice32(2)) == DIR_MASK_TAG(c->first_key.slice32(2)));
  c->earliest_key     = c->key;
  c->info             = nullptr;
  c->f.overwrite      = (options & CACHE_WRITE_OPT_OVERWRITE) != 0;
  c->f.close_complete = (options & CACHE_WRITE_OPT_CLOSE_COMPLETE) != 0;
  c->f.sync           = (options & CACHE_WRITE_OPT_SYNC) == CACHE_WRITE_OPT_SYNC;
  // coverity[Y2K38_SAFETY:FALSE]
  c->pin_in_cache = static_cast<uint32_t>(apin_in_cache);

  if ((res = c->stripe->open_write_lock(c, false, 1)) > 0) {
    // document currently being written, abort
    Metrics::Counter::increment(cache_rsb.status[c->op_type].failure);
    Metrics::Counter::increment(stripe->cache_vol->vol_rsb.status[c->op_type].failure);
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
  intptr_t err        = 0;
  int      if_writers = (uintptr_t)info == CACHE_ALLOW_MULTIPLE_WRITES;
  CacheVC *c          = new_CacheVC(cont);
  c->vio.op           = VIO::WRITE;
  c->first_key        = *key;
  /*
     The transition from single fragment document to a multi-fragment document
     would cause a problem if the key and the first_key collide. In case of
     a collision, old vector data could be served to HTTP. Need to avoid that.
     Also, when evacuating a fragment, we have to decide if its the first_key
     or the earliest_key based on the dir_tag.
   */
  do {
    rand_CacheKey(&c->key);
  } while (DIR_MASK_TAG(c->key.slice32(2)) == DIR_MASK_TAG(c->first_key.slice32(2)));
  c->earliest_key  = c->key;
  c->frag_type     = CACHE_FRAG_TYPE_HTTP;
  c->stripe        = key_to_stripe(key, hostname, host_len);
  StripeSM *stripe = c->stripe;
  c->info          = info;
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
       rewritten (if there were more than one alternate).
       HTTP OPERATIONS
       open_write with info set
       close
     */
    c->f.update = 1;
    c->op_type  = static_cast<int>(CacheOpType::Update);
    DDbg(get_dbg_cache_update(), "Update called");
    info->object_key_get(&c->update_key);
    ink_assert(!(c->update_key.is_zero()));
    c->update_len = info->object_size_get();
  } else {
    c->op_type = static_cast<int>(CacheOpType::Write);
  }

  Metrics::Gauge::increment(cache_rsb.status[c->op_type].active);
  Metrics::Gauge::increment(stripe->cache_vol->vol_rsb.status[c->op_type].active);
  // coverity[Y2K38_SAFETY:FALSE]
  c->pin_in_cache = static_cast<uint32_t>(apin_in_cache);

  {
    CACHE_TRY_LOCK(lock, c->stripe->mutex, cont->mutex->thread_holding);
    if (lock.is_locked()) {
      if ((err = c->stripe->open_write(c, if_writers, cache_config_http_max_alts > 1 ? cache_config_http_max_alts : 0)) > 0) {
        goto Lfailure;
      }
      // If there are multiple writers, then this one cannot be an update.
      // Only the first writer can do an update. If that's the case, we can
      // return success to the state machine now.;
      if (c->od->has_multiple_writers()) {
        goto Lmiss;
      }
      if (!dir_probe(key, c->stripe, &c->dir, &c->last_collision)) {
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
  Metrics::Counter::increment(cache_rsb.status[c->op_type].failure);
  Metrics::Counter::increment(stripe->cache_vol->vol_rsb.status[c->op_type].failure);
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

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

#include "HttpCacheSM.h" //Added to get the scope of HttpCacheSM object.

extern int cache_config_compatibility_4_2_0_fixup;

Action *
Cache::open_read(Continuation *cont, const CacheKey *key, CacheFragType type, const char *hostname, int host_len)
{
  if (!CacheProcessor::IsCacheReady(type)) {
    cont->handleEvent(CACHE_EVENT_OPEN_READ_FAILED, (void *)-ECACHE_NOT_READY);
    return ACTION_RESULT_DONE;
  }
  ink_assert(caches[type] == this);

  Vol *vol = key_to_vol(key, hostname, host_len);
  Dir result, *last_collision = nullptr;
  ProxyMutex *mutex = cont->mutex.get();
  OpenDirEntry *od  = nullptr;
  CacheVC *c        = nullptr;
  {
    CACHE_TRY_LOCK(lock, vol->mutex, mutex->thread_holding);
    if (!lock.is_locked() || (od = vol->open_read(key)) || dir_probe(key, vol, &result, &last_collision)) {
      c = new_CacheVC(cont);
      SET_CONTINUATION_HANDLER(c, &CacheVC::openReadStartHead);
      c->vio.op    = VIO::READ;
      c->base_stat = cache_read_active_stat;
      CACHE_INCREMENT_DYN_STAT(c->base_stat + CACHE_STAT_ACTIVE);
      c->first_key = c->key = c->earliest_key = *key;
      c->vol                                  = vol;
      c->frag_type                            = type;
      c->od                                   = od;
    }
    if (!c) {
      goto Lmiss;
    }
    if (!lock.is_locked()) {
      CONT_SCHED_LOCK_RETRY(c);
      return &c->_action;
    }
    if (c->od) {
      goto Lwriter;
    }
    c->dir            = result;
    c->last_collision = last_collision;
    switch (c->do_read_call(&c->key)) {
    case EVENT_DONE:
      return ACTION_RESULT_DONE;
    case EVENT_RETURN:
      goto Lcallreturn;
    default:
      return &c->_action;
    }
  }
Lmiss:
  CACHE_INCREMENT_DYN_STAT(cache_read_failure_stat);
  cont->handleEvent(CACHE_EVENT_OPEN_READ_FAILED, (void *)-ECACHE_NO_DOC);
  return ACTION_RESULT_DONE;
Lwriter:
  SET_CONTINUATION_HANDLER(c, &CacheVC::openReadFromWriter);
  if (c->handleEvent(EVENT_IMMEDIATE, nullptr) == EVENT_DONE) {
    return ACTION_RESULT_DONE;
  }
  return &c->_action;
Lcallreturn:
  if (c->handleEvent(AIO_EVENT_DONE, nullptr) == EVENT_DONE) {
    return ACTION_RESULT_DONE;
  }
  return &c->_action;
}

Action *
Cache::open_read(Continuation *cont, const CacheKey *key, CacheHTTPHdr *request, OverridableHttpConfigParams *params,
                 CacheFragType type, const char *hostname, int host_len)
{
  if (!CacheProcessor::IsCacheReady(type)) {
    cont->handleEvent(CACHE_EVENT_OPEN_READ_FAILED, (void *)-ECACHE_NOT_READY);
    return ACTION_RESULT_DONE;
  }
  ink_assert(caches[type] == this);

  Vol *vol = key_to_vol(key, hostname, host_len);
  Dir result, *last_collision = nullptr;
  ProxyMutex *mutex = cont->mutex.get();
  OpenDirEntry *od  = nullptr;
  CacheVC *c        = nullptr;

  {
    CACHE_TRY_LOCK(lock, vol->mutex, mutex->thread_holding);
    if (!lock.is_locked() || (od = vol->open_read(key)) || dir_probe(key, vol, &result, &last_collision)) {
      c            = new_CacheVC(cont);
      c->first_key = c->key = c->earliest_key = *key;
      c->vol                                  = vol;
      c->vio.op                               = VIO::READ;
      c->base_stat                            = cache_read_active_stat;
      CACHE_INCREMENT_DYN_STAT(c->base_stat + CACHE_STAT_ACTIVE);
      c->request.copy_shallow(request);
      c->frag_type = CACHE_FRAG_TYPE_HTTP;
      c->params    = params;
      c->od        = od;
    }
    if (!lock.is_locked()) {
      SET_CONTINUATION_HANDLER(c, &CacheVC::openReadStartHead);
      CONT_SCHED_LOCK_RETRY(c);
      return &c->_action;
    }
    if (!c) {
      goto Lmiss;
    }
    if (c->od) {
      goto Lwriter;
    }
    // hit
    c->dir = c->first_dir = result;
    c->last_collision     = last_collision;
    SET_CONTINUATION_HANDLER(c, &CacheVC::openReadStartHead);
    switch (c->do_read_call(&c->key)) {
    case EVENT_DONE:
      return ACTION_RESULT_DONE;
    case EVENT_RETURN:
      goto Lcallreturn;
    default:
      return &c->_action;
    }
  }
Lmiss:
  CACHE_INCREMENT_DYN_STAT(cache_read_failure_stat);
  cont->handleEvent(CACHE_EVENT_OPEN_READ_FAILED, (void *)-ECACHE_NO_DOC);
  return ACTION_RESULT_DONE;
Lwriter:
  // this is a horrible violation of the interface and should be fixed (FIXME)
  ((HttpCacheSM *)cont)->set_readwhilewrite_inprogress(true);
  SET_CONTINUATION_HANDLER(c, &CacheVC::openReadFromWriter);
  if (c->handleEvent(EVENT_IMMEDIATE, nullptr) == EVENT_DONE) {
    return ACTION_RESULT_DONE;
  }
  return &c->_action;
Lcallreturn:
  if (c->handleEvent(AIO_EVENT_DONE, nullptr) == EVENT_DONE) {
    return ACTION_RESULT_DONE;
  }
  return &c->_action;
}

uint32_t
CacheVC::load_http_info(CacheHTTPInfoVector *info, Doc *doc, RefCountObj *block_ptr)
{
  uint32_t zret = info->get_handles(doc->hdr(), doc->hlen, block_ptr);
  if (!this->f.doc_from_ram_cache && // ram cache is always already fixed up.
                                     // If this is an old object, the object version will be old or 0, in either case this is
                                     // correct. Forget the 4.2 compatibility, always update older versioned objects.
      VersionNumber(doc->v_major, doc->v_minor) < CACHE_DB_VERSION) {
    for (int i = info->xcount - 1; i >= 0; --i) {
      info->data(i).alternate.m_alt->m_response_hdr.m_mime->recompute_accelerators_and_presence_bits();
      info->data(i).alternate.m_alt->m_request_hdr.m_mime->recompute_accelerators_and_presence_bits();
    }
  }
  return zret;
}

int
CacheVC::openReadFromWriterFailure(int event, Event *e)
{
  od = nullptr;
  vector.clear(false);
  CACHE_INCREMENT_DYN_STAT(cache_read_failure_stat);
  CACHE_INCREMENT_DYN_STAT(cache_read_busy_failure_stat);
  _action.continuation->handleEvent(event, e);
  free_CacheVC(this);
  return EVENT_DONE;
}

int
CacheVC::openReadChooseWriter(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */)
{
  intptr_t err = ECACHE_DOC_BUSY;
  CacheVC *w   = nullptr;

  ink_assert(vol->mutex->thread_holding == mutex->thread_holding && write_vc == nullptr);

  if (!od) {
    return EVENT_RETURN;
  }

  if (frag_type != CACHE_FRAG_TYPE_HTTP) {
    ink_assert(od->num_writers == 1);
    w = od->writers.head;
    if (w->start_time > start_time || w->closed < 0) {
      od = nullptr;
      return EVENT_RETURN;
    }
    if (!w->closed) {
      return -err;
    }
    write_vc = w;
  } else {
    write_vector      = &od->vector;
    int write_vec_cnt = write_vector->count();
    for (int c = 0; c < write_vec_cnt; c++) {
      vector.insert(write_vector->get(c));
    }
    // check if all the writers who came before this reader have
    // set the http_info.
    for (w = (CacheVC *)od->writers.head; w; w = (CacheVC *)w->opendir_link.next) {
      if (w->start_time > start_time || w->closed < 0) {
        continue;
      }
      if (!w->closed && !cache_config_read_while_writer) {
        return -err;
      }
      if (w->alternate_index != CACHE_ALT_INDEX_DEFAULT) {
        continue;
      }

      if (!w->closed && !w->alternate.valid()) {
        od = nullptr;
        ink_assert(!write_vc);
        vector.clear(false);
        return EVENT_CONT;
      }
      // construct the vector from the writers.
      int alt_ndx = CACHE_ALT_INDEX_DEFAULT;
      if (w->f.update) {
        // all Update cases. Need to get the alternate index.
        alt_ndx = get_alternate_index(&vector, w->update_key);
        // if its an alternate delete
        if (!w->alternate.valid()) {
          if (alt_ndx >= 0) {
            vector.remove(alt_ndx, false);
          }
          continue;
        }
      }
      if (w->alternate.valid()) {
        vector.insert(&w->alternate, alt_ndx);
      }
    }

    if (!vector.count()) {
      if (od->reading_vec) {
        // the writer(s) are reading the vector, so there is probably
        // an old vector. Since this reader came before any of the
        // current writers, we should return the old data
        od = nullptr;
        return EVENT_RETURN;
      }
      return -ECACHE_NO_DOC;
    }
    if (cache_config_select_alternate) {
      alternate_index = HttpTransactCache::SelectFromAlternates(&vector, &request, params);
      if (alternate_index < 0) {
        return -ECACHE_ALT_MISS;
      }
    } else {
      alternate_index = 0;
    }
    CacheHTTPInfo *obj = vector.get(alternate_index);
    for (w = (CacheVC *)od->writers.head; w; w = (CacheVC *)w->opendir_link.next) {
      if (obj->m_alt == w->alternate.m_alt) {
        write_vc = w;
        break;
      }
    }
    vector.clear(false);
    if (!write_vc) {
      DDebug("cache_read_agg", "%p: key: %X writer alternate different: %d", this, first_key.slice32(1), alternate_index);
      od = nullptr;
      return EVENT_RETURN;
    }

    DDebug("cache_read_agg", "%p: key: %X eKey: %d # alts: %d, ndx: %d, # writers: %d writer: %p", this, first_key.slice32(1),
           write_vc->earliest_key.slice32(1), vector.count(), alternate_index, od->num_writers, write_vc);
  }
  return EVENT_NONE;
}

int
CacheVC::openReadFromWriter(int event, Event *e)
{
  if (!f.read_from_writer_called) {
    // The assignment to last_collision as nullptr was
    // made conditional after INKqa08411
    last_collision = nullptr;
    // Let's restart the clock from here - the first time this a reader
    // gets in this state. Its possible that the open_read was called
    // before the open_write, but the reader could not get the volume
    // lock. If we don't reset the clock here, we won't choose any writer
    // and hence fail the read request.
    start_time                = Thread::get_hrtime();
    f.read_from_writer_called = 1;
  }
  cancel_trigger();
  intptr_t err = ECACHE_DOC_BUSY;
  DDebug("cache_read_agg", "%p: key: %X In openReadFromWriter", this, first_key.slice32(1));
  if (_action.cancelled) {
    od = nullptr; // only open for read so no need to close
    return free_CacheVC(this);
  }
  CACHE_TRY_LOCK(lock, vol->mutex, mutex->thread_holding);
  if (!lock.is_locked()) {
    VC_SCHED_LOCK_RETRY();
  }
  od = vol->open_read(&first_key); // recheck in case the lock failed
  if (!od) {
    MUTEX_RELEASE(lock);
    write_vc = nullptr;
    SET_HANDLER(&CacheVC::openReadStartHead);
    return openReadStartHead(event, e);
  } else {
    ink_assert(od == vol->open_read(&first_key));
  }
  if (!write_vc) {
    int ret = openReadChooseWriter(event, e);
    if (ret < 0) {
      MUTEX_RELEASE(lock);
      SET_HANDLER(&CacheVC::openReadFromWriterFailure);
      return openReadFromWriterFailure(CACHE_EVENT_OPEN_READ_FAILED, reinterpret_cast<Event *>(ret));
    } else if (ret == EVENT_RETURN) {
      MUTEX_RELEASE(lock);
      SET_HANDLER(&CacheVC::openReadStartHead);
      return openReadStartHead(event, e);
    } else if (ret == EVENT_CONT) {
      ink_assert(!write_vc);
      if (writer_lock_retry < cache_config_read_while_writer_max_retries) {
        VC_SCHED_WRITER_RETRY();
      } else {
        return openReadFromWriterFailure(CACHE_EVENT_OPEN_READ_FAILED, (Event *)-err);
      }
    } else {
      ink_assert(write_vc);
    }
  } else {
    if (writer_done()) {
      MUTEX_RELEASE(lock);
      DDebug("cache_read_agg", "%p: key: %X writer %p has left, continuing as normal read", this, first_key.slice32(1), write_vc);
      od       = nullptr;
      write_vc = nullptr;
      SET_HANDLER(&CacheVC::openReadStartHead);
      return openReadStartHead(event, e);
    }
  }
  OpenDirEntry *cod = od;
  od                = nullptr;
  // someone is currently writing the document
  if (write_vc->closed < 0) {
    MUTEX_RELEASE(lock);
    write_vc = nullptr;
    // writer aborted, continue as if there is no writer
    SET_HANDLER(&CacheVC::openReadStartHead);
    return openReadStartHead(EVENT_IMMEDIATE, nullptr);
  }
  // allow reading from unclosed writer for http requests only.
  ink_assert(frag_type == CACHE_FRAG_TYPE_HTTP || write_vc->closed);
  if (!write_vc->closed && !write_vc->fragment) {
    if (!cache_config_read_while_writer || frag_type != CACHE_FRAG_TYPE_HTTP ||
        writer_lock_retry >= cache_config_read_while_writer_max_retries) {
      MUTEX_RELEASE(lock);
      return openReadFromWriterFailure(CACHE_EVENT_OPEN_READ_FAILED, (Event *)-err);
    }
    DDebug("cache_read_agg", "%p: key: %X writer: closed:%d, fragment:%d, retry: %d", this, first_key.slice32(1), write_vc->closed,
           write_vc->fragment, writer_lock_retry);
    VC_SCHED_WRITER_RETRY();
  }

  CACHE_TRY_LOCK(writer_lock, write_vc->mutex, mutex->thread_holding);
  if (!writer_lock.is_locked()) {
    DDebug("cache_read_agg", "%p: key: %X lock miss", this, first_key.slice32(1));
    VC_SCHED_LOCK_RETRY();
  }
  MUTEX_RELEASE(lock);

  if (!write_vc->io.ok()) {
    return openReadFromWriterFailure(CACHE_EVENT_OPEN_READ_FAILED, (Event *)-err);
  }
  if (frag_type == CACHE_FRAG_TYPE_HTTP) {
    DDebug("cache_read_agg", "%p: key: %X http passed stage 1, closed: %d, frag: %d", this, first_key.slice32(1), write_vc->closed,
           write_vc->fragment);
    if (!write_vc->alternate.valid()) {
      return openReadFromWriterFailure(CACHE_EVENT_OPEN_READ_FAILED, (Event *)-err);
    }
    alternate.copy(&write_vc->alternate);
    vector.insert(&alternate);
    alternate.object_key_get(&key);
    write_vc->f.readers = 1;
    if (!(write_vc->f.update && write_vc->total_len == 0)) {
      key = write_vc->earliest_key;
      if (!write_vc->closed) {
        alternate.object_size_set(write_vc->vio.nbytes);
      } else {
        alternate.object_size_set(write_vc->total_len);
      }
    } else {
      key = write_vc->update_key;
      ink_assert(write_vc->closed);
      DDebug("cache_read_agg", "%p: key: %X writer header update", this, first_key.slice32(1));
      // Update case (b) : grab doc_len from the writer's alternate
      doc_len = alternate.object_size_get();
      if (write_vc->update_key == cod->single_doc_key && (cod->move_resident_alt || write_vc->f.rewrite_resident_alt) &&
          write_vc->first_buf.get()) {
        // the resident alternate is being updated and its a
        // header only update. The first_buf of the writer has the
        // document body.
        Doc *doc   = (Doc *)write_vc->first_buf->data();
        writer_buf = new_IOBufferBlock(write_vc->first_buf, doc->data_len(), doc->prefix_len());
        MUTEX_RELEASE(writer_lock);
        ink_assert(doc_len == doc->data_len());
        length            = doc_len;
        f.single_fragment = 1;
        doc_pos           = 0;
        earliest_key      = key;
        dir_clean(&first_dir);
        dir_clean(&earliest_dir);
        SET_HANDLER(&CacheVC::openReadFromWriterMain);
        CACHE_INCREMENT_DYN_STAT(cache_read_busy_success_stat);
        return callcont(CACHE_EVENT_OPEN_READ);
      }
      // want to snarf the new headers from the writer
      // and then continue as if nothing happened
      last_collision = nullptr;
      MUTEX_RELEASE(writer_lock);
      SET_HANDLER(&CacheVC::openReadStartEarliest);
      return openReadStartEarliest(event, e);
    }
  } else {
    DDebug("cache_read_agg", "%p: key: %X non-http passed stage 1", this, first_key.slice32(1));
    key = write_vc->earliest_key;
  }
  if (write_vc->fragment) {
    doc_len        = write_vc->vio.nbytes;
    last_collision = nullptr;
    DDebug("cache_read_agg", "%p: key: %X closed: %d, fragment: %d, len: %d starting first fragment", this, first_key.slice32(1),
           write_vc->closed, write_vc->fragment, (int)doc_len);
    MUTEX_RELEASE(writer_lock);
    // either a header + body update or a new document
    SET_HANDLER(&CacheVC::openReadStartEarliest);
    return openReadStartEarliest(event, e);
  }
  writer_buf    = write_vc->blocks;
  writer_offset = write_vc->offset;
  length        = write_vc->length;
  // copy the vector
  f.single_fragment = !write_vc->fragment; // single fragment doc
  doc_pos           = 0;
  earliest_key      = write_vc->earliest_key;
  ink_assert(earliest_key == key);
  doc_len = write_vc->total_len;
  dir_clean(&first_dir);
  dir_clean(&earliest_dir);
  DDebug("cache_read_agg", "%p: key: %X %X: single fragment read", this, first_key.slice32(1), key.slice32(0));
  MUTEX_RELEASE(writer_lock);
  SET_HANDLER(&CacheVC::openReadFromWriterMain);
  CACHE_INCREMENT_DYN_STAT(cache_read_busy_success_stat);
  return callcont(CACHE_EVENT_OPEN_READ);
}

int
CacheVC::openReadFromWriterMain(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */)
{
  cancel_trigger();
  if (seek_to) {
    vio.ndone = seek_to;
    seek_to   = 0;
  }
  IOBufferBlock *b = nullptr;
  int64_t ntodo    = vio.ntodo();
  if (ntodo <= 0) {
    return EVENT_CONT;
  }
  if (length < ((int64_t)doc_len) - vio.ndone) {
    DDebug("cache_read_agg", "truncation %X", first_key.slice32(1));
    if (is_action_tag_set("cache")) {
      ink_release_assert(false);
    }
    Warning("Document %X truncated at %d of %d, reading from writer", first_key.slice32(1), (int)vio.ndone, (int)doc_len);
    return calluser(VC_EVENT_ERROR);
  }
  /* its possible that the user did a do_io_close before
     openWriteWriteDone was called. */
  if (length > ((int64_t)doc_len) - vio.ndone) {
    int64_t skip_bytes = length - (doc_len - vio.ndone);
    iobufferblock_skip(writer_buf.get(), &writer_offset, &length, skip_bytes);
  }
  int64_t bytes = length;
  if (bytes > vio.ntodo()) {
    bytes = vio.ntodo();
  }
  if (vio.ndone >= (int64_t)doc_len) {
    ink_assert(bytes <= 0);
    // reached the end of the document and the user still wants more
    return calluser(VC_EVENT_EOS);
  }
  b          = iobufferblock_clone(writer_buf.get(), writer_offset, bytes);
  writer_buf = iobufferblock_skip(writer_buf.get(), &writer_offset, &length, bytes);
  vio.buffer.writer()->append_block(b);
  vio.ndone += bytes;
  if (vio.ntodo() <= 0) {
    return calluser(VC_EVENT_READ_COMPLETE);
  } else {
    return calluser(VC_EVENT_READ_READY);
  }
}

int
CacheVC::openReadClose(int event, Event * /* e ATS_UNUSED */)
{
  cancel_trigger();
  if (is_io_in_progress()) {
    if (event != AIO_EVENT_DONE) {
      return EVENT_CONT;
    }
    set_io_not_in_progress();
  }
  CACHE_TRY_LOCK(lock, vol->mutex, mutex->thread_holding);
  if (!lock.is_locked()) {
    VC_SCHED_LOCK_RETRY();
  }
  if (f.hit_evacuate && dir_valid(vol, &first_dir) && closed > 0) {
    if (f.single_fragment) {
      vol->force_evacuate_head(&first_dir, dir_pinned(&first_dir));
    } else if (dir_valid(vol, &earliest_dir)) {
      vol->force_evacuate_head(&first_dir, dir_pinned(&first_dir));
      vol->force_evacuate_head(&earliest_dir, dir_pinned(&earliest_dir));
    }
  }
  vol->close_read(this);
  return free_CacheVC(this);
}

int
CacheVC::openReadReadDone(int event, Event *e)
{
  Doc *doc = nullptr;

  cancel_trigger();
  if (event == EVENT_IMMEDIATE) {
    return EVENT_CONT;
  }
  set_io_not_in_progress();
  {
    CACHE_TRY_LOCK(lock, vol->mutex, mutex->thread_holding);
    if (!lock.is_locked()) {
      VC_SCHED_LOCK_RETRY();
    }
    if (event == AIO_EVENT_DONE && !io.ok()) {
      dir_delete(&earliest_key, vol, &earliest_dir);
      goto Lerror;
    }
    if (last_collision &&     // no missed lock
        dir_valid(vol, &dir)) // object still valid
    {
      doc = (Doc *)buf->data();
      if (doc->magic != DOC_MAGIC) {
        char tmpstring[CRYPTO_HEX_SIZE];
        if (doc->magic == DOC_CORRUPT) {
          Warning("Middle: Doc checksum does not match for %s", key.toHexStr(tmpstring));
        } else {
          Warning("Middle: Doc magic does not match for %s", key.toHexStr(tmpstring));
        }
        goto Lerror;
      }
      if (doc->key == key) {
        goto LreadMain;
      }
    }
    if (last_collision && dir_offset(&dir) != dir_offset(last_collision)) {
      last_collision = nullptr; // object has been/is being overwritten
    }
    if (dir_probe(&key, vol, &dir, &last_collision)) {
      int ret = do_read_call(&key);
      if (ret == EVENT_RETURN) {
        goto Lcallreturn;
      }
      return EVENT_CONT;
    } else if (write_vc) {
      if (writer_done()) {
        last_collision = nullptr;
        while (dir_probe(&earliest_key, vol, &dir, &last_collision)) {
          if (dir_offset(&dir) == dir_offset(&earliest_dir)) {
            DDebug("cache_read_agg", "%p: key: %X ReadRead complete: %d", this, first_key.slice32(1), (int)vio.ndone);
            doc_len = vio.ndone;
            goto Ldone;
          }
        }
        DDebug("cache_read_agg", "%p: key: %X ReadRead writer aborted: %d", this, first_key.slice32(1), (int)vio.ndone);
        goto Lerror;
      }
      if (writer_lock_retry < cache_config_read_while_writer_max_retries) {
        DDebug("cache_read_agg", "%p: key: %X ReadRead retrying: %d", this, first_key.slice32(1), (int)vio.ndone);
        VC_SCHED_WRITER_RETRY(); // wait for writer
      } else {
        DDebug("cache_read_agg", "%p: key: %X ReadRead retries exhausted, bailing..: %d", this, first_key.slice32(1),
               (int)vio.ndone);
        goto Ldone;
      }
    }
    // fall through for truncated documents
  }
Lerror : {
  char tmpstring[CRYPTO_HEX_SIZE];
  if (request.valid()) {
    int url_length;
    const char *url_text = request.url_get()->string_get_ref(&url_length);
    Warning("Document %s truncated, url[%.*s]", earliest_key.toHexStr(tmpstring), url_length, url_text);
  } else {
    Warning("Document %s truncated", earliest_key.toHexStr(tmpstring));
  }
  return calluser(VC_EVENT_ERROR);
}
Ldone:
  return calluser(VC_EVENT_EOS);
Lcallreturn:
  return handleEvent(AIO_EVENT_DONE, nullptr);
LreadMain:
  fragment++;
  doc_pos = doc->prefix_len();
  next_CacheKey(&key, &key);
  SET_HANDLER(&CacheVC::openReadMain);
  return openReadMain(event, e);
}

int
CacheVC::openReadMain(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */)
{
  cancel_trigger();
  Doc *doc         = (Doc *)buf->data();
  int64_t ntodo    = vio.ntodo();
  int64_t bytes    = doc->len - doc_pos;
  IOBufferBlock *b = nullptr;
  if (seek_to) { // handle do_io_pread
    if (seek_to >= doc_len) {
      vio.ndone = doc_len;
      return calluser(VC_EVENT_EOS);
    }
    HTTPInfo::FragOffset *frags = alternate.get_frag_table();
    if (is_debug_tag_set("cache_seek")) {
      char b[CRYPTO_HEX_SIZE], c[CRYPTO_HEX_SIZE];
      Debug("cache_seek", "Seek @ %" PRId64 " in %s from #%d @ %" PRId64 "/%d:%s", seek_to, first_key.toHexStr(b), fragment,
            doc_pos, doc->len, doc->key.toHexStr(c));
    }
    /* Because single fragment objects can migrate to hang off an alt vector
       they can appear to the VC as multi-fragment when they are not really.
       The essential difference is the existence of a fragment table.
    */
    if (frags) {
      int target                    = 0;
      HTTPInfo::FragOffset next_off = frags[target];
      int lfi                       = static_cast<int>(alternate.get_frag_offset_count()) - 1;
      ink_assert(lfi >= 0); // because it's not a single frag doc.

      /* Note: frag[i].offset is the offset of the first byte past the
         i'th fragment. So frag[0].offset is the offset of the first
         byte of fragment 1. In addition the # of fragments is one
         more than the fragment table length, the start of the last
         fragment being the last offset in the table.
      */
      if (fragment == 0 || seek_to < frags[fragment - 1] || (fragment <= lfi && frags[fragment] <= seek_to)) {
        // search from frag 0 on to find the proper frag
        while (seek_to >= next_off && target < lfi) {
          next_off = frags[++target];
        }
        if (target == lfi && seek_to >= next_off) {
          ++target;
        }
      } else { // shortcut if we are in the fragment already
        target = fragment;
      }
      if (target != fragment) {
        // Lread will read the next fragment always, so if that
        // is the one we want, we don't need to do anything
        int cfi = fragment;
        --target;
        while (target > fragment) {
          next_CacheKey(&key, &key);
          ++fragment;
        }
        while (target < fragment) {
          prev_CacheKey(&key, &key);
          --fragment;
        }

        if (is_debug_tag_set("cache_seek")) {
          char target_key_str[CRYPTO_HEX_SIZE];
          key.toHexStr(target_key_str);
          Debug("cache_seek", "Seek #%d @ %" PRId64 " -> #%d @ %" PRId64 ":%s", cfi, doc_pos, target, seek_to, target_key_str);
        }
        goto Lread;
      }
    }
    doc_pos = doc->prefix_len() + seek_to;
    if (fragment && frags) {
      doc_pos -= static_cast<int64_t>(frags[fragment - 1]);
    }
    vio.ndone = 0;
    seek_to   = 0;
    ntodo     = vio.ntodo();
    bytes     = doc->len - doc_pos;
    if (is_debug_tag_set("cache_seek")) {
      char target_key_str[CRYPTO_HEX_SIZE];
      key.toHexStr(target_key_str);
      Debug("cache_seek", "Read # %d @ %" PRId64 "/%d for %" PRId64, fragment, doc_pos, doc->len, bytes);
    }

    // This shouldn't happen for HTTP assets but it does
    // occasionally in production. This is a temporary fix
    // to clean up broken objects until the root cause can
    // be found. It must be the case that either the fragment
    // offsets are incorrect or a fragment table isn't being
    // created when it should be.
    if (frag_type == CACHE_FRAG_TYPE_HTTP && bytes < 0) {
      char xt[CRYPTO_HEX_SIZE];
      char yt[CRYPTO_HEX_SIZE];

      int url_length       = 0;
      char const *url_text = nullptr;
      if (request.valid()) {
        url_text = request.url_get()->string_get_ref(&url_length);
      }

      int64_t prev_frag_size = 0;
      if (fragment && frags) {
        prev_frag_size = static_cast<int64_t>(frags[fragment - 1]);
      }

      Warning("cache_seek range request bug: read %s targ %s - %s frag # %d (prev_frag %" PRId64 ") @ %" PRId64 "/%d for %" PRId64
              " tot %" PRId64 " url '%.*s'",
              doc->key.toHexStr(xt), key.toHexStr(yt), f.single_fragment ? "single" : "multi", fragment, prev_frag_size, doc_pos,
              doc->len, bytes, doc->total_len, url_length, url_text);

      goto Lerror;
    }
  }
  if (ntodo <= 0) {
    return EVENT_CONT;
  }
  if (vio.buffer.writer()->max_read_avail() > vio.buffer.writer()->water_mark && vio.ndone) { // initiate read of first block
    return EVENT_CONT;
  }
  if ((bytes <= 0) && vio.ntodo() >= 0) {
    goto Lread;
  }
  if (bytes > vio.ntodo()) {
    bytes = vio.ntodo();
  }
  b           = new_IOBufferBlock(buf, bytes, doc_pos);
  b->_buf_end = b->_end;
  vio.buffer.writer()->append_block(b);
  vio.ndone += bytes;
  doc_pos += bytes;
  if (vio.ntodo() <= 0) {
    return calluser(VC_EVENT_READ_COMPLETE);
  } else {
    if (calluser(VC_EVENT_READ_READY) == EVENT_DONE) {
      return EVENT_DONE;
    }
    // we have to keep reading until we give the user all the
    // bytes it wanted or we hit the watermark.
    if (vio.ntodo() > 0 && !vio.buffer.writer()->high_water()) {
      goto Lread;
    }
    return EVENT_CONT;
  }
Lread : {
  if (vio.ndone >= (int64_t)doc_len) {
    // reached the end of the document and the user still wants more
    return calluser(VC_EVENT_EOS);
  }
  last_collision    = nullptr;
  writer_lock_retry = 0;
  // if the state machine calls reenable on the callback from the cache,
  // we set up a schedule_imm event. The openReadReadDone discards
  // EVENT_IMMEDIATE events. So, we have to cancel that trigger and set
  // a new EVENT_INTERVAL event.
  cancel_trigger();
  CACHE_TRY_LOCK(lock, vol->mutex, mutex->thread_holding);
  if (!lock.is_locked()) {
    SET_HANDLER(&CacheVC::openReadMain);
    VC_SCHED_LOCK_RETRY();
  }
  if (dir_probe(&key, vol, &dir, &last_collision)) {
    SET_HANDLER(&CacheVC::openReadReadDone);
    int ret = do_read_call(&key);
    if (ret == EVENT_RETURN) {
      goto Lcallreturn;
    }
    return EVENT_CONT;
  } else if (write_vc) {
    if (writer_done()) {
      last_collision = nullptr;
      while (dir_probe(&earliest_key, vol, &dir, &last_collision)) {
        if (dir_offset(&dir) == dir_offset(&earliest_dir)) {
          DDebug("cache_read_agg", "%p: key: %X ReadMain complete: %d", this, first_key.slice32(1), (int)vio.ndone);
          doc_len = vio.ndone;
          goto Leos;
        }
      }
      DDebug("cache_read_agg", "%p: key: %X ReadMain writer aborted: %d", this, first_key.slice32(1), (int)vio.ndone);
      goto Lerror;
    }
    DDebug("cache_read_agg", "%p: key: %X ReadMain retrying: %d", this, first_key.slice32(1), (int)vio.ndone);
    SET_HANDLER(&CacheVC::openReadMain);
    VC_SCHED_WRITER_RETRY();
  }
  if (is_action_tag_set("cache")) {
    ink_release_assert(false);
  }
  Warning("Document %X truncated at %d of %d, missing fragment %X", first_key.slice32(1), (int)vio.ndone, (int)doc_len,
          key.slice32(1));
  // remove the directory entry
  dir_delete(&earliest_key, vol, &earliest_dir);
}
Lerror:
  return calluser(VC_EVENT_ERROR);
Leos:
  return calluser(VC_EVENT_EOS);
Lcallreturn:
  return handleEvent(AIO_EVENT_DONE, nullptr);
}

/*
  This code follows CacheVC::openReadStartHead closely,
  if you change this you might have to change that.
*/
int
CacheVC::openReadStartEarliest(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */)
{
  int ret  = 0;
  Doc *doc = nullptr;
  cancel_trigger();
  set_io_not_in_progress();
  if (_action.cancelled) {
    return free_CacheVC(this);
  }
  {
    CACHE_TRY_LOCK(lock, vol->mutex, mutex->thread_holding);
    if (!lock.is_locked()) {
      VC_SCHED_LOCK_RETRY();
    }
    if (!buf) {
      goto Lread;
    }
    if (!io.ok()) {
      goto Ldone;
    }
    // an object needs to be outside the aggregation window in order to be
    // be evacuated as it is read
    if (!dir_agg_valid(vol, &dir)) {
      // a directory entry which is nolonger valid may have been overwritten
      if (!dir_valid(vol, &dir)) {
        last_collision = nullptr;
      }
      goto Lread;
    }
    doc = (Doc *)buf->data();
    if (doc->magic != DOC_MAGIC) {
      char tmpstring[CRYPTO_HEX_SIZE];
      if (is_action_tag_set("cache")) {
        ink_release_assert(false);
      }
      if (doc->magic == DOC_CORRUPT) {
        Warning("Earliest: Doc checksum does not match for %s", key.toHexStr(tmpstring));
      } else {
        Warning("Earliest : Doc magic does not match for %s", key.toHexStr(tmpstring));
      }
      // remove the dir entry
      dir_delete(&key, vol, &dir);
      // try going through the directory entries again
      // in case the dir entry we deleted doesnt correspond
      // to the key we are looking for. This is possible
      // because of directory collisions
      last_collision = nullptr;
      goto Lread;
    }
    if (!(doc->key == key)) { // collisiion
      goto Lread;
    }
    // success
    earliest_key = key;
    doc_pos      = doc->prefix_len();
    next_CacheKey(&key, &doc->key);
    vol->begin_read(this);
    if (vol->within_hit_evacuate_window(&earliest_dir) &&
        (!cache_config_hit_evacuate_size_limit || doc_len <= (uint64_t)cache_config_hit_evacuate_size_limit)) {
      DDebug("cache_hit_evac", "dir: %" PRId64 ", write: %" PRId64 ", phase: %d", dir_offset(&earliest_dir),
             vol->offset_to_vol_offset(vol->header->write_pos), vol->header->phase);
      f.hit_evacuate = 1;
    }
    goto Lsuccess;
  Lread:
    if (dir_probe(&key, vol, &earliest_dir, &last_collision) || dir_lookaside_probe(&key, vol, &earliest_dir, nullptr)) {
      dir = earliest_dir;
      if ((ret = do_read_call(&key)) == EVENT_RETURN) {
        goto Lcallreturn;
      }
      return ret;
    }
    // read has detected that alternate does not exist in the cache.
    // rewrite the vector.
    if (!f.read_from_writer_called && frag_type == CACHE_FRAG_TYPE_HTTP) {
      // don't want any writers while we are evacuating the vector
      if (!vol->open_write(this, false, 1)) {
        Doc *doc1    = (Doc *)first_buf->data();
        uint32_t len = this->load_http_info(write_vector, doc1);
        ink_assert(len == doc1->hlen && write_vector->count() > 0);
        write_vector->remove(alternate_index, true);
        // if the vector had one alternate, delete it's directory entry
        if (len != doc1->hlen || !write_vector->count()) {
          // sometimes the delete fails when there is a race and another read
          // finds that the directory entry has been overwritten
          // (cannot assert on the return value)
          dir_delete(&first_key, vol, &first_dir);
        } else {
          buf             = nullptr;
          last_collision  = nullptr;
          write_len       = 0;
          header_len      = write_vector->marshal_length();
          f.evac_vector   = 1;
          f.use_first_key = 1;
          key             = first_key;
          // always use od->first_dir to overwrite a directory.
          // If an evacuation happens while a vector is being updated
          // the evacuator changes the od->first_dir to the new directory
          // that it inserted
          od->first_dir   = first_dir;
          od->writing_vec = true;
          earliest_key    = zero_key;

          // set up this VC as a alternate delete write_vc
          vio.op          = VIO::WRITE;
          total_len       = 0;
          f.update        = 1;
          alternate_index = CACHE_ALT_REMOVED;
          /////////////////////////////////////////////////////////////////
          // change to create a directory entry for a resident alternate //
          // when another alternate does not exist.                      //
          /////////////////////////////////////////////////////////////////
          if (doc1->total_len > 0) {
            od->move_resident_alt = true;
            od->single_doc_key    = doc1->key;
            dir_assign(&od->single_doc_dir, &dir);
            dir_set_tag(&od->single_doc_dir, od->single_doc_key.slice32(2));
          }
          SET_HANDLER(&CacheVC::openReadVecWrite);
          if ((ret = do_write_call()) == EVENT_RETURN) {
            goto Lcallreturn;
          }
          return ret;
        }
      }
    }
  // open write failure - another writer, so don't modify the vector
  Ldone:
    if (od) {
      vol->close_write(this);
    }
  }
  CACHE_INCREMENT_DYN_STAT(cache_read_failure_stat);
  _action.continuation->handleEvent(CACHE_EVENT_OPEN_READ_FAILED, (void *)-ECACHE_NO_DOC);
  return free_CacheVC(this);
Lcallreturn:
  return handleEvent(AIO_EVENT_DONE, nullptr); // hopefully a tail call
Lsuccess:
  if (write_vc) {
    CACHE_INCREMENT_DYN_STAT(cache_read_busy_success_stat);
  }
  SET_HANDLER(&CacheVC::openReadMain);
  return callcont(CACHE_EVENT_OPEN_READ);
}

// create the directory entry after the vector has been evacuated
// the volume lock has been taken when this function is called
int
CacheVC::openReadVecWrite(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */)
{
  cancel_trigger();
  set_io_not_in_progress();
  ink_assert(od);
  od->writing_vec = false;
  if (_action.cancelled) {
    return openWriteCloseDir(EVENT_IMMEDIATE, nullptr);
  }
  {
    CACHE_TRY_LOCK(lock, vol->mutex, mutex->thread_holding);
    if (!lock.is_locked()) {
      VC_SCHED_LOCK_RETRY();
    }
    if (io.ok()) {
      ink_assert(f.evac_vector);
      ink_assert(frag_type == CACHE_FRAG_TYPE_HTTP);
      ink_assert(!buf);
      f.evac_vector   = false;
      last_collision  = nullptr;
      f.update        = 0;
      alternate_index = CACHE_ALT_INDEX_DEFAULT;
      f.use_first_key = 0;
      vio.op          = VIO::READ;
      dir_overwrite(&first_key, vol, &dir, &od->first_dir);
      if (od->move_resident_alt) {
        dir_insert(&od->single_doc_key, vol, &od->single_doc_dir);
      }
      int alt_ndx = HttpTransactCache::SelectFromAlternates(write_vector, &request, params);
      vol->close_write(this);
      if (alt_ndx >= 0) {
        vector.clear();
        // we don't need to start all over again, since we already
        // have the vector in memory. But this is simpler and this
        // case is rare.
        goto Lrestart;
      }
    } else {
      vol->close_write(this);
    }
  }

  CACHE_INCREMENT_DYN_STAT(cache_read_failure_stat);
  _action.continuation->handleEvent(CACHE_EVENT_OPEN_READ_FAILED, (void *)-ECACHE_ALT_MISS);
  return free_CacheVC(this);
Lrestart:
  SET_HANDLER(&CacheVC::openReadStartHead);
  return openReadStartHead(EVENT_IMMEDIATE, nullptr);
}

/*
  This code follows CacheVC::openReadStartEarliest closely,
  if you change this you might have to change that.
*/
int
CacheVC::openReadStartHead(int event, Event *e)
{
  intptr_t err = ECACHE_NO_DOC;
  Doc *doc     = nullptr;
  cancel_trigger();
  set_io_not_in_progress();
  if (_action.cancelled) {
    return free_CacheVC(this);
  }
  {
    CACHE_TRY_LOCK(lock, vol->mutex, mutex->thread_holding);
    if (!lock.is_locked()) {
      VC_SCHED_LOCK_RETRY();
    }
    if (!buf) {
      goto Lread;
    }
    if (!io.ok()) {
      goto Ldone;
    }
    // an object needs to be outside the aggregation window in order to be
    // be evacuated as it is read
    if (!dir_agg_valid(vol, &dir)) {
      // a directory entry which is nolonger valid may have been overwritten
      if (!dir_valid(vol, &dir)) {
        last_collision = nullptr;
      }
      goto Lread;
    }
    doc = (Doc *)buf->data();
    if (doc->magic != DOC_MAGIC) {
      char tmpstring[CRYPTO_HEX_SIZE];
      if (is_action_tag_set("cache")) {
        ink_release_assert(false);
      }
      if (doc->magic == DOC_CORRUPT) {
        Warning("Head: Doc checksum does not match for %s", key.toHexStr(tmpstring));
      } else {
        Warning("Head : Doc magic does not match for %s", key.toHexStr(tmpstring));
      }
      // remove the dir entry
      dir_delete(&key, vol, &dir);
      // try going through the directory entries again
      // in case the dir entry we deleted doesnt correspond
      // to the key we are looking for. This is possible
      // because of directory collisions
      last_collision = nullptr;
      goto Lread;
    }
    if (!(doc->first_key == key)) {
      goto Lread;
    }
    if (f.lookup) {
      goto Lookup;
    }
    earliest_dir = dir;
    CacheHTTPInfo *alternate_tmp;
    if (frag_type == CACHE_FRAG_TYPE_HTTP) {
      uint32_t uml;
      ink_assert(doc->hlen);
      if (!doc->hlen) {
        goto Ldone;
      }
      if ((uml = this->load_http_info(&vector, doc)) != doc->hlen) {
        if (buf) {
          HTTPCacheAlt *alt  = reinterpret_cast<HTTPCacheAlt *>(doc->hdr());
          int32_t alt_length = 0;
          // count should be reasonable, as vector is initialized and unlikly to be too corrupted
          // by bad disk data - count should be the number of successfully unmarshalled alts.
          for (int32_t i = 0; i < vector.count(); ++i) {
            CacheHTTPInfo *info = vector.get(i);
            if (info && info->m_alt) {
              alt_length += info->m_alt->m_unmarshal_len;
            }
          }
          Note("OpenReadHead failed for cachekey %X : vector inconsistency - "
               "unmarshalled %d expecting %d in %d (base=%zu, ver=%d:%d) "
               "- vector n=%d size=%d"
               "first alt=%d[%s]",
               key.slice32(0), uml, doc->hlen, doc->len, sizeof(Doc), doc->v_major, doc->v_minor, vector.count(), alt_length,
               alt->m_magic,
               (CACHE_ALT_MAGIC_ALIVE == alt->m_magic ?
                  "alive" :
                  CACHE_ALT_MAGIC_MARSHALED == alt->m_magic ? "serial" : CACHE_ALT_MAGIC_DEAD == alt->m_magic ? "dead" : "bogus"));
          dir_delete(&key, vol, &dir);
        }
        err = ECACHE_BAD_META_DATA;
        goto Ldone;
      }
      if (cache_config_select_alternate) {
        alternate_index = HttpTransactCache::SelectFromAlternates(&vector, &request, params);
        if (alternate_index < 0) {
          err = ECACHE_ALT_MISS;
          goto Ldone;
        }
      } else {
        alternate_index = 0;
      }
      alternate_tmp = vector.get(alternate_index);
      if (!alternate_tmp->valid()) {
        if (buf) {
          Note("OpenReadHead failed for cachekey %X : alternate inconsistency", key.slice32(0));
          dir_delete(&key, vol, &dir);
        }
        goto Ldone;
      }

      alternate.copy_shallow(alternate_tmp);
      alternate.object_key_get(&key);
      doc_len = alternate.object_size_get();
      if (key == doc->key) { // is this my data?
        f.single_fragment = doc->single_fragment();
        ink_assert(f.single_fragment); // otherwise need to read earliest
        ink_assert(doc->hlen);
        doc_pos = doc->prefix_len();
        next_CacheKey(&key, &doc->key);
      } else {
        f.single_fragment = false;
      }
    } else {
      next_CacheKey(&key, &doc->key);
      f.single_fragment = doc->single_fragment();
      doc_pos           = doc->prefix_len();
      doc_len           = doc->total_len;
    }

    if (is_debug_tag_set("cache_read")) { // amc debug
      char xt[CRYPTO_HEX_SIZE], yt[CRYPTO_HEX_SIZE];
      Debug("cache_read", "CacheReadStartHead - read %s target %s - %s %d of %" PRId64 " bytes, %d fragments",
            doc->key.toHexStr(xt), key.toHexStr(yt), f.single_fragment ? "single" : "multi", doc->len, doc->total_len,
            alternate.get_frag_offset_count());
    }
    // the first fragment might have been gc'ed. Make sure the first
    // fragment is there before returning CACHE_EVENT_OPEN_READ
    if (!f.single_fragment) {
      goto Learliest;
    }

    if (vol->within_hit_evacuate_window(&dir) &&
        (!cache_config_hit_evacuate_size_limit || doc_len <= (uint64_t)cache_config_hit_evacuate_size_limit)) {
      DDebug("cache_hit_evac", "dir: %" PRId64 ", write: %" PRId64 ", phase: %d", dir_offset(&dir),
             vol->offset_to_vol_offset(vol->header->write_pos), vol->header->phase);
      f.hit_evacuate = 1;
    }

    first_buf = buf;
    vol->begin_read(this);

    goto Lsuccess;

  Lread:
    // check for collision
    // INKqa07684 - Cache::lookup returns CACHE_EVENT_OPEN_READ_FAILED.
    // don't want to go through this BS of reading from a writer if
    // its a lookup. In this case lookup will fail while the document is
    // being written to the cache.
    OpenDirEntry *cod = vol->open_read(&key);
    if (cod && !f.read_from_writer_called) {
      if (f.lookup) {
        err = ECACHE_DOC_BUSY;
        goto Ldone;
      }
      od = cod;
      MUTEX_RELEASE(lock);
      SET_HANDLER(&CacheVC::openReadFromWriter);
      return handleEvent(EVENT_IMMEDIATE, nullptr);
    }
    if (dir_probe(&key, vol, &dir, &last_collision)) {
      first_dir = dir;
      int ret   = do_read_call(&key);
      if (ret == EVENT_RETURN) {
        goto Lcallreturn;
      }
      return ret;
    }
  }
Ldone:
  if (!f.lookup) {
    CACHE_INCREMENT_DYN_STAT(cache_read_failure_stat);
    _action.continuation->handleEvent(CACHE_EVENT_OPEN_READ_FAILED, (void *)-err);
  } else {
    CACHE_INCREMENT_DYN_STAT(cache_lookup_failure_stat);
    _action.continuation->handleEvent(CACHE_EVENT_LOOKUP_FAILED, (void *)-err);
  }
  return free_CacheVC(this);
Lcallreturn:
  return handleEvent(AIO_EVENT_DONE, nullptr); // hopefully a tail call
Lsuccess:
  SET_HANDLER(&CacheVC::openReadMain);
  return callcont(CACHE_EVENT_OPEN_READ);
Lookup:
  CACHE_INCREMENT_DYN_STAT(cache_lookup_success_stat);
  _action.continuation->handleEvent(CACHE_EVENT_LOOKUP, nullptr);
  return free_CacheVC(this);
Learliest:
  first_buf      = buf;
  buf            = nullptr;
  earliest_key   = key;
  last_collision = nullptr;
  SET_HANDLER(&CacheVC::openReadStartEarliest);
  return openReadStartEarliest(event, e);
}

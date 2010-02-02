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
#include "../../proxy/http2/HttpCacheSM.h"      //Added to get the scope of HttpCacheSM object.

#define READ_WHILE_WRITER 1

Action *
Cache::open_read(Continuation * cont, CacheKey * key, CacheFragType type, char *hostname, int host_len)
{

  if (!(CacheProcessor::cache_ready & type)) {
    cont->handleEvent(CACHE_EVENT_OPEN_READ_FAILED, (void *) -ECACHE_NOT_READY);
    return ACTION_RESULT_DONE;
  }

  ink_assert(caches[type] == this);

  Part *part = key_to_part(key, hostname, host_len);
  Dir result, *last_collision = NULL;
  ProxyMutex *mutex = cont->mutex;

  CacheVC *c = new_CacheVC(cont);
  SET_CONTINUATION_HANDLER(c, &CacheVC::openReadStartHead);
  c->vio.op = VIO::READ;
  c->base_stat = cache_read_active_stat;
  CACHE_INCREMENT_DYN_STAT(c->base_stat + CACHE_STAT_ACTIVE);
  c->first_key = c->key = c->earliest_key = *key;
  c->part = part;
  if (type == CACHE_FRAG_TYPE_HTTP) {
    c->f.http_request = 1;
  }

  CACHE_TRY_LOCK(lock, part->mutex, mutex->thread_holding);
  if (lock) {
    c->od = part->open_read(key);
    if (c->od) {
      // someone is currently writing the document 
      SET_CONTINUATION_HANDLER(c, &CacheVC::openReadFromWriter);
      if (c->handleEvent(EVENT_IMMEDIATE, 0) == EVENT_CONT)
        return &c->_action;
      else
        return ACTION_RESULT_DONE;
    }
    // no writer
    if (!dir_probe(key, part, &result, &last_collision)) {
      //release the lock before calling handleEvent??
      CACHE_INCREMENT_DYN_STAT(cache_read_failure_stat);
      cont->handleEvent(CACHE_EVENT_OPEN_READ_FAILED, (void *) -ECACHE_NO_DOC);
      free_CacheVC(c);
      return ACTION_RESULT_DONE;
    }

    c->dir = result;
    c->last_collision = last_collision;
  }
  if (!lock) {
    CONT_SCHED_LOCK_RETRY(c);
    return &c->_action;
  }
  if (c->do_read(&c->key) == EVENT_CONT)
    return &c->_action;
  else
    return ACTION_RESULT_DONE;  // ram cache hit
}

#ifdef HTTP_CACHE
Action *
Cache::open_read(Continuation * cont, CacheKey * key, CacheHTTPHdr * request,
                 CacheLookupHttpConfig * params, CacheFragType type, char *hostname, int host_len)
{

  if (!(CacheProcessor::cache_ready & type)) {
    cont->handleEvent(CACHE_EVENT_OPEN_READ_FAILED, (void *) -ECACHE_NOT_READY);
    return ACTION_RESULT_DONE;
  }

  ink_assert(caches[type] == this);

  Part *part = key_to_part(key, hostname, host_len);
  Dir result, *last_collision = NULL;
  ProxyMutex *mutex = cont->mutex;

  CacheVC *c = new_CacheVC(cont);
  c->first_key = c->key = c->earliest_key = *key;
  c->part = part;
  c->vio.op = VIO::READ;
  c->base_stat = cache_read_active_stat;
  CACHE_INCREMENT_DYN_STAT(c->base_stat + CACHE_STAT_ACTIVE);
  c->request.copy_shallow(request);
  c->f.http_request = 1;
  c->params = params;

  CACHE_TRY_LOCK(lock, part->mutex, mutex->thread_holding);
  if (lock) {
    c->od = part->open_read(key);
    if (c->od) {
      //Type-casting the continuation to HttpCacheSM object
      HttpCacheSM *cachesm = (HttpCacheSM *) cont;
      //Setting the read_while_write_inprogress flag 
      cachesm->set_readwhilewrite_inprogress(true);

      // someone is currently writing the document 
      SET_CONTINUATION_HANDLER(c, &CacheVC::openReadFromWriter);
      if (c->handleEvent(EVENT_IMMEDIATE, 0) == EVENT_CONT)
        return &c->_action;
      else
        return ACTION_RESULT_DONE;
    }
    if (!dir_probe(key, part, &result, &last_collision)) {
      //release the lock before calling handleEvent???
      CACHE_INCREMENT_DYN_STAT(cache_read_failure_stat);
      cont->handleEvent(CACHE_EVENT_OPEN_READ_FAILED, (void *) -ECACHE_NO_DOC);
      free_CacheVC(c);
      return ACTION_RESULT_DONE;
    }
    c->dir = c->first_dir = result;
    c->last_collision = last_collision;

  }
  SET_CONTINUATION_HANDLER(c, &CacheVC::openReadStartHead);
  if (!lock) {
    CONT_SCHED_LOCK_RETRY(c);
    return &c->_action;
  }
  if (c->do_read(&c->key) == EVENT_CONT)
    return &c->_action;
  else
    return ACTION_RESULT_DONE;  // ram cache hit
}
#endif

int
CacheVC::openReadFromWriterFailure(int event, Event * e)
{

  od = NULL;
  vector.clear(false);
  CACHE_INCREMENT_DYN_STAT(cache_read_failure_stat);
  CACHE_INCREMENT_DYN_STAT(cache_read_busy_failure_stat);
  _action.continuation->handleEvent(event, e);
  free_CacheVC(this);
  return EVENT_DONE;
}

int
CacheVC::openReadChooseWriter(int event, Event * e)
{
  NOWARN_UNUSED(e);
  NOWARN_UNUSED(event);

  intptr_t err = ECACHE_DOC_BUSY;
  CacheVC *w = NULL;

  if (!f.http_request) {
    ink_assert(od->num_writers == 1);
    w = od->writers.head;
    if (w->start_time > start_time || w->closed < 0) {
      od = NULL;
      SET_HANDLER(&CacheVC::openReadStartHead);
      return openReadStartHead(EVENT_IMMEDIATE, 0);
    }
    if (!w->closed) {
      return openReadFromWriterFailure(CACHE_EVENT_OPEN_READ_FAILED, (Event *) -err);
    }
    write_vc = w;
  }
#ifdef HTTP_CACHE
  else {
    write_vector = &od->vector;
    int write_vec_cnt = write_vector->count();
    for (int c = 0; c < write_vec_cnt; c++)
      vector.insert(write_vector->get(c));
    // check if all the writers who came before this reader have
    // set the http_info.
    for (w = (CacheVC *) od->writers.head; w; w = (CacheVC *) w->opendir_link.next) {
      if (w->start_time > start_time || w->closed < 0)
        continue;
      if (!w->closed && !cache_config_read_while_writer) {
        return openReadFromWriterFailure(CACHE_EVENT_OPEN_READ_FAILED, (Event *) - err);
      }
      if (w->alternate_index != CACHE_ALT_INDEX_DEFAULT)
        continue;

      if (!w->closed && !w->alternate.valid()) {
        od = NULL;
        vector.clear(false);
        VC_SCHED_LOCK_RETRY();
      }
      // construct the vector from the writers.
      int alt_ndx = CACHE_ALT_INDEX_DEFAULT;
      if (w->f.update) {
        // all Update cases. Need to get the alternate index.
        alt_ndx = get_alternate_index(&vector, w->update_key);
        // if its an alternate delete
        if (!w->alternate.valid()) {
          if (alt_ndx >= 0)
            vector.remove(alt_ndx, false);
          continue;
        }
      }
      ink_assert(w->alternate.valid());
      if (w->alternate.valid())
        vector.insert(&w->alternate, alt_ndx);
    }

    if (!vector.count()) {
      if (od->reading_vec) {
        // the writer(s) are reading the vector, so there is probably
        // an old vector. Since this reader came before any of the
        // current writers, we should return the old data
        od = NULL;
        SET_HANDLER(&CacheVC::openReadStartHead);
        return openReadStartHead(EVENT_IMMEDIATE, 0);
      } else {
        return openReadFromWriterFailure(CACHE_EVENT_OPEN_READ_FAILED, (Event *) - ECACHE_NO_DOC);
      }
    }
#ifdef FIXME_NONMODULAR
    if (cache_config_select_alternate) {
      alternate_index = HttpTransactCache::SelectFromAlternates(&vector, &request, params);
      if (alternate_index < 0)
        return openReadFromWriterFailure(CACHE_EVENT_OPEN_READ_FAILED, (Event *) - ECACHE_ALT_MISS);
    } else
#endif
      alternate_index = 0;
    CacheHTTPInfo *obj = vector.get(alternate_index);
    for (w = (CacheVC *) od->writers.head; w; w = (CacheVC *) w->opendir_link.next) {
      if (obj->m_alt == w->alternate.m_alt) {
        write_vc = w;
        break;
      }
    }
    vector.clear(false);
    if (!write_vc) {
      Debug("cache_read_agg", "%x: key: %X writer alternate different: %d", this, first_key.word(1), alternate_index);
      od = NULL;
      SET_HANDLER(&CacheVC::openReadStartHead);
      return openReadStartHead(EVENT_IMMEDIATE, 0);
    }

    Debug("cache_read_agg",
          "%x: key: %X eKey: %d # alts: %d, ndx: %d, # writers: %d writer: %x",
          this, first_key.word(1), write_vc->earliest_key.word(1),
          vector.count(), alternate_index, od->num_writers, write_vc);
  }
#endif //HTTP_CACHE
  return EVENT_CONT;
}

int
CacheVC::openReadFromWriter(int event, Event * e)
{
  if (!f.read_from_writer_called) {
    // The assignment to last_collision as NULL was 
    // made conditional after INKqa08411
    last_collision = NULL;
    // Let's restart the clock from here - the first time this a reader
    // gets in this state. Its possible that the open_read was called
    // before the open_write, but the reader could not get the partition
    // lock. If we don't reset the clock here, we won't choose any writer
    // and hence fail the read request.
    start_time = ink_get_hrtime();
    f.read_from_writer_called = 1;
  }
  cancel_trigger();
  intptr_t err = ECACHE_DOC_BUSY;
  Debug("cache_read_agg", "%x: key: %X In openReadFromWriter", this, first_key.word(1));
#ifndef READ_WHILE_WRITER
  return openReadFromWriterFailure(CACHE_EVENT_OPEN_READ_FAILED, (Event *) -err);
#else
  if (_action.cancelled)
    return free_CacheVC(this);
  CACHE_TRY_LOCK(lock, part->mutex, mutex->thread_holding);
  if (!lock) {
    ink_assert(!od);
    VC_SCHED_LOCK_RETRY();
  }
  if (!od) {
    od = part->open_read(&first_key);
    if (!od) {
      write_vc = NULL;
      SET_HANDLER(&CacheVC::openReadStartHead);
      return openReadStartHead(event, e);
    }
  } else
    ink_debug_assert(od == part->open_read(&first_key));

  if (!write_vc) {
    int ret = openReadChooseWriter(event, e);
    if (ret == EVENT_DONE || !write_vc)
      return ret;
  } else {
    if (writer_done()) {
      Debug("cache_read_agg",
            "%x: key: %X writer %x has left, continuing as normal read", this, first_key.word(1), write_vc);
      od = NULL;
      write_vc = NULL;
      SET_HANDLER(&CacheVC::openReadStartHead);
      return openReadStartHead(event, e);
    }
  }
  OpenDirEntry *cod = od;
  od = NULL;
  // someone is currently writing the document
  if (write_vc->closed < 0) {
    write_vc = NULL;
    //writer aborted, continue as if there is no writer
    SET_HANDLER(&CacheVC::openReadStartHead);
    return openReadStartHead(EVENT_IMMEDIATE, 0);
  }
  // allow reading from unclosed writer for http requests only. 
  ink_assert(f.http_request || write_vc->closed);
  if (!write_vc->closed && !write_vc->segment) {
    if (!cache_config_read_while_writer || !f.http_request)
      return openReadFromWriterFailure(CACHE_EVENT_OPEN_READ_FAILED, (Event *) - err);
    Debug("cache_read_agg",
          "%x: key: %X writer: closed:%d, segment:%d, retry: %d",
          this, first_key.word(1), write_vc->closed, write_vc->segment, writer_lock_retry);
    VC_SCHED_WRITER_RETRY();
  }

  CACHE_TRY_LOCK(writer_lock, write_vc->mutex, mutex->thread_holding);
  if (!writer_lock) {
    Debug("cache_read_agg", "%x: key: %X lock miss", this, first_key.word(1));
    VC_SCHED_LOCK_RETRY();
  }

  if (!write_vc->io.ok())
    return openReadFromWriterFailure(CACHE_EVENT_OPEN_READ_FAILED, (Event *) - err);
#ifdef HTTP_CACHE
  if (f.http_request) {
    Debug("cache_read_agg",
          "%x: key: %X http passed stage 1, closed: %d, seg: %d",
          this, first_key.word(1), write_vc->closed, write_vc->segment);
    if (!write_vc->alternate.valid())
      return openReadFromWriterFailure(CACHE_EVENT_OPEN_READ_FAILED, (Event *) - err);
    alternate.copy(&write_vc->alternate);
    vector.insert(&alternate);
    alternate.object_key_get(&key);
    write_vc->f.readers = 1;
    if (!(write_vc->f.update && write_vc->total_len == 0)) {
      key = write_vc->earliest_key;
      if (!write_vc->closed)
        alternate.object_size_set(write_vc->vio.nbytes);
      else
        alternate.object_size_set(write_vc->total_len);
    } else {
      key = write_vc->update_key;
      ink_assert(write_vc->closed);
      Debug("cache_read_agg", "%x: key: %X writer header update", this, first_key.word(1));
      // Update case (b) : grab doc_len from the writer's alternate
      doc_len = alternate.object_size_get();
      if (write_vc->update_key == cod->single_doc_key &&
          (cod->move_resident_alt || write_vc->f.rewrite_resident_alt) && write_vc->first_buf._ptr()) {
        // the resident alternate is being updated and its a 
        // header only update. The first_buf of the writer has the
        // document body. 
        Doc *doc = (Doc *) write_vc->first_buf->data();
        writer_buf = new_IOBufferBlock(write_vc->first_buf, doc->data_len(), sizeofDoc + doc->hlen);
        MUTEX_RELEASE(writer_lock);
        ink_assert(doc_len == doc->data_len());
        length = doc_len;
        f.single_segment = 1;
        docpos = 0;
        earliest_key = key;
        dir_clean(&first_dir);
        dir_clean(&earliest_dir);
        SET_HANDLER(&CacheVC::openReadFromWriterMain);
        CACHE_INCREMENT_DYN_STAT(cache_read_busy_success_stat);
        _action.continuation->handleEvent(CACHE_EVENT_OPEN_READ, (void *) this);
        return EVENT_DONE;
      }
      // want to snarf the new headers from the writer 
      // and then continue as if nothing happened
      last_collision = NULL;
      MUTEX_RELEASE(writer_lock);
      SET_HANDLER(&CacheVC::openReadStartEarliest);
      return openReadStartEarliest(event, e);
    }
  } else {
#endif //HTTP_CACHE
    Debug("cache_read_agg", "%x: key: %X non-http passed stage 1", this, first_key.word(1));
    key = write_vc->earliest_key;
#ifdef HTTP_CACHE
  }
#endif
  if (write_vc->segment) {
    doc_len = write_vc->vio.nbytes;
    last_collision = NULL;
    Debug("cache_read_agg",
          "%x: key: %X closed: %d, segment: %d, len: %d starting first segment",
          this, first_key.word(1), write_vc->closed, write_vc->segment, doc_len);
    MUTEX_RELEASE(writer_lock);
    // either a header + body update or a new document
    SET_HANDLER(&CacheVC::openReadStartEarliest);
    return openReadStartEarliest(event, e);
  }
  writer_buf = write_vc->blocks;
  writer_offset = write_vc->offset;
  length = write_vc->length;
  //copy the vector
  f.single_segment = !write_vc->segment;        //single segment doc
  docpos = 0;
  earliest_key = write_vc->earliest_key;
  ink_assert(earliest_key == key);
  doc_len = write_vc->total_len;
  dir_clean(&first_dir);
  dir_clean(&earliest_dir);
  Debug("cache_read_agg", "%x key: %X %X: single segment read", first_key.word(1), key.word(0));

  MUTEX_RELEASE(writer_lock);
  //we've got everything....ready to roll!!
  SET_HANDLER(&CacheVC::openReadFromWriterMain);
  CACHE_INCREMENT_DYN_STAT(cache_read_busy_success_stat);
  _action.continuation->handleEvent(CACHE_EVENT_OPEN_READ, (void *) this);
  return EVENT_DONE;

#endif //READ_WHILE_WRITER
}

int
CacheVC::openReadFromWriterMain(int event, Event * e)
{
  NOWARN_UNUSED(e);
  NOWARN_UNUSED(event);

  IOBufferBlock *b = NULL;
  int ntodo = vio.ntodo();
  cancel_trigger();
  if (ntodo <= 0)
    return EVENT_CONT;
  if (vio.buffer.mbuf->max_read_avail() > vio.buffer.writer()->water_mark && vio.ndone) // initiate read of first block
    return EVENT_CONT;

  if (length < (doc_len - vio.ndone)) {
    Debug("cache_read_agg", "truncation %X", earliest_key.word(0));
    if (is_action_tag_set("cache")) {
      ink_release_assert(false);
    }
//    action_tag_assert("cache", false);
    Warning("Document for %X truncated", earliest_key.word(0));
    calluser(VC_EVENT_ERROR);
    return EVENT_CONT;
  }
  /* its possible that the user did a do_io_close before 
     openWriteWriteDone was called. */
  if (length > (doc_len - vio.ndone)) {
    int skip_bytes = length - (doc_len - vio.ndone);
    iobufferblock_skip(writer_buf, &writer_offset, &length, skip_bytes);
  }
  int bytes = length;
  if (length > vio.ntodo())
    bytes = vio.ntodo();

  if (vio.ndone >= doc_len) {
    ink_assert(bytes <= 0);
    // reached the end of the document and the user still wants more 
    calluser(VC_EVENT_EOS);
    return EVENT_DONE;
  }
  b = iobufferblock_clone(writer_buf, writer_offset, bytes);
  writer_buf = iobufferblock_skip(writer_buf, &writer_offset, &length, bytes);

  vio.buffer.mbuf->append_block(b);
  vio.ndone += bytes;
  if (vio.ntodo() <= 0) {
    calluser(VC_EVENT_READ_COMPLETE);
    return EVENT_DONE;
  } else {
    if (calluser(VC_EVENT_READ_READY))
      return EVENT_DONE;
    else
      return EVENT_CONT;
  }
}


int
CacheVC::openReadClose(int event, Event * e)
{
  NOWARN_UNUSED(e);
  NOWARN_UNUSED(event);

  cancel_trigger();
  if (is_io_in_progress()) {
    if (event != AIO_EVENT_DONE)
      return EVENT_CONT;
    set_io_not_in_progress();
  }
  MUTEX_TRY_LOCK(lock, part->mutex, mutex->thread_holding);
  if (!lock)
    VC_SCHED_LOCK_RETRY();
#ifdef HIT_EVACUATE
  if (f.hit_evacuate && dir_valid(part, &first_dir) && closed > 0) {
    if (f.single_segment)
      part->force_evacuate_head(&first_dir, dir_pinned(&first_dir));
    else if (dir_valid(part, &earliest_dir)) {
      part->force_evacuate_head(&first_dir, dir_pinned(&first_dir));
      part->force_evacuate_head(&earliest_dir, dir_pinned(&earliest_dir));
    }
  }
#endif
  part->close_read(this);
  return free_CacheVC(this);
}

int
CacheVC::openReadReadDone(int event, Event * e)
{
  Doc *doc = NULL;

  cancel_trigger();
  if (event == EVENT_IMMEDIATE)
    return EVENT_CONT;
  set_io_not_in_progress();
  if (event == AIO_EVENT_DONE && !io.ok())
    goto Ldone;
  if (last_collision &&         // no missed lock
      dir_valid(part, &dir))    // object still valid
  {
    doc = (Doc *) buf->data();
    if (doc->magic != DOC_MAGIC) {
      char tmpstring[100];
      if (doc->magic == DOC_CORRUPT)
        Warning("Middle: Doc checksum does not match for %s", key.string(tmpstring));
      else
        Warning("Middle: Doc magic does not match for %s", key.string(tmpstring));
      goto Ldone;
    }

    if (doc->key == key) {
      docpos = sizeofDoc + doc->hlen;
      next_CacheKey(&key, &key);
      SET_HANDLER(&CacheVC::openReadMain);
      return openReadMain(event, e);
    }
  }
  {
    CACHE_TRY_LOCK(lock, part->mutex, mutex->thread_holding);
    if (!lock)
      VC_SCHED_LOCK_RETRY();

    if (last_collision && dir_offset(&dir) != dir_offset(last_collision))
      last_collision = 0;       // object has been/is being overwritten
    if (dir_probe(&key, part, &dir, &last_collision)) {
      do_read(&key);
      return EVENT_CONT;
    } else if (write_vc) {
      if (writer_done()) {
        last_collision = NULL;
        while (dir_probe(&earliest_key, part, &dir, &last_collision)) {
          if (dir_offset(&dir) == dir_offset(&earliest_dir)) {
            Debug("cache_read_agg", "%x: key: %X ReadRead complete: %d", this, first_key.word(1), vio.ndone);
            doc_len = vio.ndone;
            calluser(VC_EVENT_EOS);
            return EVENT_DONE;
          }
        }
        Debug("cache_read_agg", "%x: key: %X ReadRead writer aborted: %d", this, first_key.word(1), vio.ndone);
        calluser(VC_EVENT_ERROR);
        return EVENT_DONE;
      }
      Debug("cache_read_agg", "%x: key: %X ReadRead retrying: %d", this, first_key.word(1), vio.ndone);
      VC_SCHED_WRITER_RETRY();
    }
  }
Ldone:
//  action_tag_assert("cache", false);
  if (is_action_tag_set("cache")) {
    ink_release_assert(false);
  }
  // remove the directory entry
  dir_delete_lock(&earliest_key, part, mutex, &earliest_dir);
  char tmpstring[100];
  Warning("Document truncated for %s", earliest_key.string(tmpstring));
  calluser(VC_EVENT_ERROR);
  return EVENT_CONT;
}

int
CacheVC::openReadMain(int event, Event * e)
{
  NOWARN_UNUSED(e);
  NOWARN_UNUSED(event);

  IOBufferBlock *b = NULL;
  int ntodo = vio.ntodo();
  cancel_trigger();
  if (ntodo <= 0)
    return EVENT_CONT;
  if (vio.buffer.mbuf->max_read_avail() > vio.buffer.writer()->water_mark && vio.ndone) // initiate read of first block
    return EVENT_CONT;
  Doc *doc = (Doc *) buf->data();
  int bytes = doc->len - docpos;
  if ((bytes <= 0) && vio.ntodo() >= 0) {
    goto Lread;
  }
  if (bytes > vio.ntodo())
    bytes = vio.ntodo();
  b = new_IOBufferBlock(buf, bytes, docpos);
  b->_buf_end = b->_end;
  vio.buffer.mbuf->append_block(b);
  vio.ndone += bytes;
  docpos += bytes;
  if (vio.ntodo() <= 0) {
    calluser(VC_EVENT_READ_COMPLETE);
    return EVENT_DONE;
  } else {
    if (calluser(VC_EVENT_READ_READY))
      return EVENT_DONE;
    // we have to keep reading until we give the user all the 
    // bytes it wanted or we hit the watermark. 
    if (vio.ntodo() > 0 && !vio.buffer.writer()->high_water())
      goto Lread;
    return EVENT_CONT;
  }
Lread:{
    if (vio.ndone >= doc_len) {
      // reached the end of the document and the user still wants more 
      calluser(VC_EVENT_EOS);
      return EVENT_DONE;
    }
    last_collision = 0;
    writer_lock_retry = 0;
    // if the state machine calls reenable on the callback from the cache,
    // we set up a schedule_imm event. The openReadReadDone discards 
    // EVENT_IMMEDIATE events. So, we have to cancel that trigger and set
    // a new EVENT_INTERVAL event.
    cancel_trigger();
    CACHE_TRY_LOCK(lock, part->mutex, mutex->thread_holding);
    if (!lock) {
      SET_HANDLER(&CacheVC::openReadReadDone);
      VC_SCHED_LOCK_RETRY();
    }
    if (dir_probe(&key, part, &dir, &last_collision)) {
      SET_HANDLER(&CacheVC::openReadReadDone);
      do_read(&key);
      return EVENT_CONT;
    } else if (write_vc) {
      if (writer_done()) {
        last_collision = NULL;
        while (dir_probe(&earliest_key, part, &dir, &last_collision)) {
          if (dir_offset(&dir) == dir_offset(&earliest_dir)) {
            Debug("cache_read_agg", "%x: key: %X ReadMain complete: %d", this, first_key.word(1), vio.ndone);
            doc_len = vio.ndone;
            calluser(VC_EVENT_EOS);
            return EVENT_DONE;
          }
        }
        Debug("cache_read_agg", "%x: key: %X ReadMain writer aborted: %d", this, first_key.word(1), vio.ndone);
        calluser(VC_EVENT_ERROR);
        return EVENT_DONE;
      }
      Debug("cache_read_agg", "%x: key: %X ReadMain retrying: %d", this, first_key.word(1), vio.ndone);
      SET_HANDLER(&CacheVC::openReadReadDone);
      VC_SCHED_WRITER_RETRY();
    }
  }
  Debug("cache_evac", "truncation %X", key.word(0));
//  action_tag_assert("cache", false);
  if (is_action_tag_set("cache")) {
    ink_release_assert(false);
  }
  Warning("Document for %X truncated", earliest_key.word(0));
  // remove the directory entry
  dir_delete_lock(&earliest_key, part, mutex, &earliest_dir);
  calluser(VC_EVENT_ERROR);
  return EVENT_CONT;
}


int
CacheVC::openReadStartEarliest(int event, Event * e)
{
  NOWARN_UNUSED(e);
  NOWARN_UNUSED(event);

  intptr_t err = ECACHE_NO_DOC;
  Doc *doc = NULL;

  cancel_trigger();
  set_io_not_in_progress();
  if (_action.cancelled)
    return free_CacheVC(this);
  if (!buf)
    goto Lcollision;
  if (!io.ok())
    goto Ldone;
  // an object needs to be outside the aggregation window in order to be
  // be evacuated as it is read
  if (!dir_agg_valid(part, &dir)) {
    // a directory entry which is nolonger valid may have been overwritten
    if (!dir_valid(part, &dir))
      last_collision = NULL;
    goto Lcollision;
  }
  doc = (Doc *) buf->data();
  if (doc->magic != DOC_MAGIC) {
    char tmpstring[100];
    //action_tag_assert("cache", false);
    if (is_action_tag_set("cache")) {
      ink_release_assert(false);
    }

    if (doc->magic == DOC_CORRUPT)
      Warning("Earliest: Doc checksum does not match for %s", key.string(tmpstring));
    else
      Warning("Earliest : Doc magic does not match for %s", key.string(tmpstring));
    // remove the dir entry
    dir_delete(&key, part, &dir);
    // try going through the directory entries again
    // in case the dir entry we deleted doesnt correspond
    // to the key we are looking for. This is possible 
    // because of directory collisions
    last_collision = NULL;
    goto Lcollision;
  }
  if (!(doc->key == key))
    goto Lcollision;
  // success
  earliest_key = key;
  docpos = sizeofDoc + doc->hlen;
  next_CacheKey(&key, &doc->key);
  if (part->begin_read_lock(this) < 0)
    VC_SCHED_LOCK_RETRY();

#ifdef HIT_EVACUATE
  if (part->within_hit_evacuate_window(&earliest_dir) &&
      (!cache_config_hit_evacuate_size_limit || doc_len <= cache_config_hit_evacuate_size_limit)) {
    Debug("cache_hit_evac", "dir: %d, write: %d, phase: %d",
          dir_offset(&earliest_dir), offset_to_part_offset(part, part->header->write_pos), part->header->phase);
    f.hit_evacuate = 1;
  }
#endif
  if (write_vc)
    CACHE_INCREMENT_DYN_STAT(cache_read_busy_success_stat);

  SET_HANDLER(&CacheVC::openReadMain);
  _action.continuation->handleEvent(CACHE_EVENT_OPEN_READ, (void *) this);
  return EVENT_DONE;

Lcollision:{
    CACHE_TRY_LOCK(lock, part->mutex, mutex->thread_holding);
    if (!lock)
      VC_SCHED_LOCK_RETRY();
    if (dir_probe(&key, part, &earliest_dir, &last_collision) || dir_lookaside_probe(&key, part, &earliest_dir, NULL)) {
      dir = earliest_dir;
      return do_read(&key);
    }
    // read has detected that alternate does not exist in the cache. 
    // rewrite the vector.
#ifdef HTTP_CACHE
    if (!f.read_from_writer_called && f.http_request) {
      // don't want any writers while we are evacuating the vector
      if (!part->open_write(this, false, 1)) {
        Doc *doc1 = (Doc *) first_buf->data();
        int len = write_vector->get_handles(doc1->hdr, doc1->hlen);
        ink_assert(len == doc1->hlen && write_vector->count() > 0);
        write_vector->remove(alternate_index, true);
        // if the vector had one alternate, delete it's directory entry
        if (len != doc1->hlen || !write_vector->count()) {
          // sometimes the delete fails when there is a race and another read
          // finds that the directory entry has been overwritten 
          // (cannot assert on the return value)
          dir_delete(&first_key, part, &first_dir);
        } else {
          buf = NULL;
          last_collision = NULL;
          write_len = 0;
          f.evac_vector = 1;
          f.use_first_key = 1;
          key = first_key;
          // always use od->first_dir to overwrite a directory.
          // If an evacuation happens while a vector is being updated
          // the evacuator changes the od->first_dir to the new directory 
          // that it inserted
          od->first_dir = first_dir;
          od->writing_vec = 1;
          earliest_key = zero_key;

          // set up this VC as a alternate delete write_vc
          vio.op = VIO::WRITE;
          total_len = 0;
          f.update = 1;
          alternate_index = CACHE_ALT_REMOVED;
          /////////////////////////////////////////////////////////////////
          // change to create a directory entry for a resident alternate //
          // when another alternate does not exist.                      //
          /////////////////////////////////////////////////////////////////
          if (doc1->total_len > 0) {
            od->move_resident_alt = 1;
            od->single_doc_key = doc1->key;
            dir_assign(&od->single_doc_dir, &dir);
            dir_set_tag(&od->single_doc_dir, od->single_doc_key.word(1));
          }
          SET_HANDLER(&CacheVC::openReadVecWrite);
          return do_write();
        }
      }
    }
#endif
    // open write failure - another writer, so don't modify the vector
  }
Ldone:
  if (od)
    part->close_write_lock(this);
  CACHE_INCREMENT_DYN_STAT(cache_read_failure_stat);
  _action.continuation->handleEvent(CACHE_EVENT_OPEN_READ_FAILED, (void *) -err);
  return free_CacheVC(this);
}

// create the directory entry after the vector has been evacuated
// the partition lock has been taken when this function is called
#ifdef HTTP_CACHE
int
CacheVC::openReadVecWrite(int event, Event * e)
{
  NOWARN_UNUSED(e);
  NOWARN_UNUSED(event);

  ink_debug_assert(part->mutex->thread_holding == this_ethread());
  intptr_t err = ECACHE_ALT_MISS;
  ink_assert(event == AIO_EVENT_DONE);
  set_io_not_in_progress();
  ink_assert(od);
  od->writing_vec = 0;
  if (_action.cancelled)
    return openWriteCloseDir(EVENT_IMMEDIATE, 0);

  if (io.ok()) {
    ink_assert(f.evac_vector);
    ink_assert(f.http_request);
    ink_assert(!buf.m_ptr);
    f.evac_vector = false;
    last_collision = NULL;
    f.update = 0;
    alternate_index = CACHE_ALT_INDEX_DEFAULT;
    f.use_first_key = 0;
    vio.op = VIO::READ;
    dir_overwrite(&first_key, part, &dir, &od->first_dir);
    if (od->move_resident_alt) {
      dir_insert(&od->single_doc_key, part, &od->single_doc_dir);
    }
#ifdef FIXME_NONMODULAR
    int alt_ndx = HttpTransactCache::SelectFromAlternates(write_vector,
                                                          &request, params);
#else
    int alt_ndx = 0;
#endif
    part->close_write(this);
    if (alt_ndx >= 0) {
      vector.clear();
      // we don't need to start all over again, since we already
      // have the vector in memory. But this is simpler and this
      // case is rare.
      SET_HANDLER(&CacheVC::openReadStartHead);
      return openReadStartHead(EVENT_IMMEDIATE, 0);
    }
  } else
    part->close_write(this);

  CACHE_INCREMENT_DYN_STAT(cache_read_failure_stat);
  _action.continuation->handleEvent(CACHE_EVENT_OPEN_READ_FAILED, (void *) -err);
  return free_CacheVC(this);
}
#endif

int
CacheVC::openReadStartHead(int event, Event * e)
{
  intptr_t err = ECACHE_NO_DOC;
  Doc *doc = NULL;
  cancel_trigger();
  set_io_not_in_progress();
  if (_action.cancelled)
    return free_CacheVC(this);
  if (!buf)
    goto Lcollision;
  if (!io.ok())
    goto Ldone;
  // an object needs to be outside the aggregation window in order to be
  // be evacuated as it is read
  if (!dir_agg_valid(part, &dir)) {
    // a directory entry which is nolonger valid may have been overwritten
    if (!dir_valid(part, &dir))
      last_collision = NULL;
    goto Lcollision;
  }
  doc = (Doc *) buf->data();
  if (doc->magic != DOC_MAGIC) {
    char tmpstring[100];
    //action_tag_assert("cache", false);
    if (is_action_tag_set("cache")) {
      ink_release_assert(false);
    }
    if (doc->magic == DOC_CORRUPT)
      Warning("Head: Doc checksum does not match for %s", key.string(tmpstring));
    else
      Warning("Head : Doc magic does not match for %s", key.string(tmpstring));
    //remove the directory entry
    dir_delete(&key, part, &dir);
    // try going through the directory entries again
    // in case the dir entry we deleted doesnt correspond
    // to the key we are looking for. This is possible 
    // because of directory collisions
    last_collision = NULL;
    goto Lcollision;
  }
  if (!(doc->first_key == key))
    goto Lcollision;


  if (f.lookup) {
    CACHE_INCREMENT_DYN_STAT(cache_lookup_success_stat);
    _action.continuation->handleEvent(CACHE_EVENT_LOOKUP, 0);
    return free_CacheVC(this);
  }
  earliest_dir = dir;
#ifdef HTTP_CACHE
  CacheHTTPInfo *alternate_tmp;
  if (f.http_request) {
    ink_assert(doc->hlen);
    if (!doc->hlen)
      goto Ldone;
    if (vector.get_handles(doc->hdr, doc->hlen) != doc->hlen) {
      if (buf) {
        Note("OpenReadHead failed for cachekey %X : vector inconsistency with %d", key.word(0), doc->hlen);
        dir_delete(&key, part, &dir);
      }
      err = ECACHE_BAD_META_DATA;
      goto Ldone;
    }
    if (cache_config_select_alternate) {
#ifdef FIXME_NONMODULAR
      alternate_index = HttpTransactCache::SelectFromAlternates(&vector, &request, params);
#else
      alternate_index = 0;
#endif
      if (alternate_index < 0) {
        err = ECACHE_ALT_MISS;
        goto Ldone;
      }
    } else
      alternate_index = 0;
    alternate_tmp = vector.get(alternate_index);
    if (!alternate_tmp->valid()) {
      if (buf) {
        Note("OpenReadHead failed for cachekey %X : alternate inconsistency", key.word(0));
        dir_delete(&key, part, &dir);
      }
      goto Ldone;
    }

    alternate.copy_shallow(alternate_tmp);
    alternate.object_key_get(&key);
    doc_len = alternate.object_size_get();
    if (key == doc->key) {      // is this my data?
      f.single_segment = doc->single_segment();
      ink_assert(f.single_segment);     // otherwise need to read earliest
      ink_assert(doc->hlen);
      docpos = sizeofDoc + doc->hlen;
      next_CacheKey(&key, &doc->key);
    } else {
      f.single_segment = false;
    }
  } else
#endif
  {
    // non-http docs have the total len set in the first segment
    if (doc->hlen) {
      ink_debug_assert(!"Cache::openReadStartHead non-http request" " for http doc");
      err = -ECACHE_BAD_READ_REQUEST;
      goto Ldone;
    }
    next_CacheKey(&key, &doc->key);
    f.single_segment = doc->single_segment();
    docpos = sizeofDoc + doc->hlen;
    doc_len = doc->total_len;
  }
  // the first fragment might have been gc'ed. Make sure the first
  // fragment is there before returning CACHE_EVENT_OPEN_READ
  if (!f.single_segment) {
    first_buf = buf;
    buf = NULL;
    earliest_key = key;
    last_collision = NULL;
    SET_HANDLER(&CacheVC::openReadStartEarliest);
    return openReadStartEarliest(event, e);
  }
#ifdef HIT_EVACUATE
  if (part->within_hit_evacuate_window(&dir) &&
      (!cache_config_hit_evacuate_size_limit || doc_len <= cache_config_hit_evacuate_size_limit)) {
    Debug("cache_hit_evac", "dir: %d, write: %d, phase: %d",
          dir_offset(&dir), offset_to_part_offset(part, part->header->write_pos), part->header->phase);
    f.hit_evacuate = 1;
  }
#endif

  first_buf = buf;
  if (part->begin_read_lock(this) < 0)
    VC_SCHED_LOCK_RETRY();
  // success
  SET_HANDLER(&CacheVC::openReadMain);
  _action.continuation->handleEvent(CACHE_EVENT_OPEN_READ, (void *) this);
  return EVENT_DONE;

Lcollision:{
    CACHE_TRY_LOCK(lock, part->mutex, mutex->thread_holding);
    if (!lock)
      VC_SCHED_LOCK_RETRY();
    // check for collision
    // INKqa07684 - Cache::lookup returns CACHE_EVENT_OPEN_READ_FAILED.
    // don't want to go through this BS of reading from a writer if
    // its a lookup. In this case lookup will fail while the document is
    // being written to the cache.
    OpenDirEntry *cod = part->open_read(&key);
    if (cod && !f.read_from_writer_called) {
      if (f.lookup) {
        err = ECACHE_DOC_BUSY;
        goto Ldone;
      }
      od = cod;
      SET_HANDLER(&CacheVC::openReadFromWriter);
      return handleEvent(EVENT_IMMEDIATE, 0);
    }
    if (dir_probe(&key, part, &dir, &last_collision)) {
      first_dir = dir;
      return do_read(&key);
    }
  }
Ldone:
  if (!f.lookup) {
    CACHE_INCREMENT_DYN_STAT(cache_read_failure_stat);
    _action.continuation->handleEvent(CACHE_EVENT_OPEN_READ_FAILED, (void *) -err);
  } else {
    CACHE_INCREMENT_DYN_STAT(cache_lookup_failure_stat);
    _action.continuation->handleEvent(CACHE_EVENT_LOOKUP_FAILED, (void *) -err);
  }
  return free_CacheVC(this);
}

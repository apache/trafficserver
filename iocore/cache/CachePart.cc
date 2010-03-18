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

#define SCAN_BUF_SIZE      (512 * 1024)
#define SCAN_WRITER_LOCK_MAX_RETRY 5

Action *
Cache::scan(Continuation * cont, char *hostname, int host_len, int KB_per_second)
{
  Debug("cache_scan_truss", "inside scan");
  if (!(CacheProcessor::cache_ready & CACHE_FRAG_TYPE_HTTP)) {
    cont->handleEvent(CACHE_EVENT_SCAN_FAILED, 0);
    return ACTION_RESULT_DONE;
  }

  CacheVC *c = new_CacheVC(cont);
  c->part = NULL;
  /* do we need to make a copy */
  c->hostname = hostname;
  c->host_len = host_len;
  c->base_stat = cache_scan_active_stat;
  c->buf = new_IOBufferData(BUFFER_SIZE_FOR_XMALLOC(SCAN_BUF_SIZE), MEMALIGNED);
  c->scan_msec_delay = (512000 / KB_per_second);
  c->offset = 0;
  SET_CONTINUATION_HANDLER(c, &CacheVC::scanPart);
  eventProcessor.schedule_in(c, HRTIME_MSECONDS(c->scan_msec_delay));
  cont->handleEvent(CACHE_EVENT_SCAN, c);
  return ACTION_RESULT_DONE;
}

int
CacheVC::scanPart(int event, Event * e)
{
  NOWARN_UNUSED(e);
  NOWARN_UNUSED(event);

  Debug("cache_scan_truss", "inside %p:scanPart", this);
  if (_action.cancelled)
    return free_CacheVC(this);
  CacheHostRecord *rec = &theCache->hosttable->gen_host_rec;
  if (host_len) {
    CacheHostResult res;
    theCache->hosttable->Match(hostname, host_len, &res);
    if (res.record)
      rec = res.record;
  }
  if (!part) {
    if (!rec->num_part)
      goto Ldone;
    part = rec->parts[0];
  } else {
    for (int i = 0; i < rec->num_part - 1; i++)
      if (part == rec->parts[i]) {
        part = rec->parts[i + 1];
        goto Lcont;
      }
    goto Ldone;
  }
Lcont:
  fragment = 0;
  SET_HANDLER(&CacheVC::scanObject);
  eventProcessor.schedule_in(this, HRTIME_MSECONDS(scan_msec_delay));
  return EVENT_CONT;
Ldone:
  _action.continuation->handleEvent(CACHE_EVENT_SCAN_DONE, NULL);
  return free_CacheVC(this);
}

int
CacheVC::scanObject(int event, Event * e)
{
  NOWARN_UNUSED(e);
  NOWARN_UNUSED(event);

  Debug("cache_scan_truss", "inside %p:scanObject", this);
  if (_action.cancelled)
    return free_CacheVC(this);

  Doc *doc = NULL;
  void *result = NULL;
#ifdef HTTP_CACHE
  int hlen = 0;
  char hname[500];
  bool hostinfo_copied = false;
#endif

  cancel_trigger();
  set_io_not_in_progress();
  if (_action.cancelled)
    return free_CacheVC(this);

  CACHE_TRY_LOCK(lock, part->mutex, mutex->thread_holding);
  if (!lock) {
    mutex->thread_holding->schedule_in_local(this, MUTEX_RETRY_DELAY);
    return EVENT_CONT;
  }

  if (!fragment) {               // initialize for first read
    fragment = 1;
    io.aiocb.aio_offset = part_offset_to_offset(part, 0);
    io.aiocb.aio_nbytes = SCAN_BUF_SIZE;
    io.aiocb.aio_buf = buf->data();
    io.action = this;
    io.thread = AIO_CALLBACK_THREAD_ANY;
    goto Lread;
  }

  if ((int) io.aio_result != (int) io.aiocb.aio_nbytes) {
    result = (void *) -ECACHE_READ_FAIL;
    goto Ldone;
  }

  doc = (Doc *) (buf->data() + offset);
  while ((char *) doc < buf->data() + io.aiocb.aio_nbytes) {
#ifdef HTTP_CACHE
    int i;
    bool changed;

    if (doc->magic != DOC_MAGIC || doc->ftype != CACHE_FRAG_TYPE_HTTP || !doc->hlen)
      goto Lskip;

    last_collision = NULL;
    while (1) {
      if (!dir_probe(&doc->first_key, part, &dir, &last_collision))
        goto Lskip;
      if (!dir_agg_valid(part, &dir) || !dir_head(&dir) ||
          (part_offset(part, &dir) != io.aiocb.aio_offset + ((char *) doc - buf->data())))
        continue;
      break;
    }
    if (doc->data() - buf->data() > (int) io.aiocb.aio_nbytes)
      goto Lskip;
    {
      char *tmp = doc->hdr();
      int len = doc->hlen;
      while (len > 0) {
        int r = HTTPInfo::unmarshal(tmp, len, buf._ptr());
        if (r < 0) {
          ink_assert(!"CacheVC::scanObject unmarshal failed");
          goto Lskip;
        }
        len -= r;
        tmp += r;
      }
    }
    if (vector.get_handles(doc->hdr(), doc->hlen) != doc->hlen)
      goto Lskip;
    changed = false;
    hostinfo_copied = 0;
    for (i = 0; i < vector.count(); i++) {
      if (!vector.get(i)->valid())
        continue;
      if (!hostinfo_copied) {
        memccpy(hname, vector.get(i)->request_get()->url_get()->host_get(&hlen), 0, 500);
        Debug("cache_scan", "hostname = '%s', hostlen = %d", hname, hlen);
        hostinfo_copied = 1;
      }
      vector.get(i)->object_key_get(&key);
      alternate_index = i;
      // verify that the earliest block exists, reducing 'false hit' callbacks
      if (!(key == doc->key)) {
        last_collision = NULL;
        if (!dir_probe(&key, part, &earliest_dir, &last_collision))
          continue;
      }
      earliest_key = key;
      int result1 = _action.continuation->handleEvent(CACHE_EVENT_SCAN_OBJECT, vector.get(i));
      switch (result1) {
      case CACHE_SCAN_RESULT_CONTINUE:
        continue;
      case CACHE_SCAN_RESULT_DELETE:
        changed = true;
        vector.remove(i, true);
        i--;
        continue;
      case CACHE_SCAN_RESULT_DELETE_ALL_ALTERNATES:
        changed = true;
        vector.clear();
        i = 0;
        break;
      case CACHE_SCAN_RESULT_UPDATE:
        ink_debug_assert(alternate_index >= 0);
        vector.insert(&alternate, alternate_index);
        if (!vector.get(alternate_index)->valid())
          continue;
        changed = true;
        continue;
      case EVENT_DONE:
        goto Lcancel;
      default:
        ink_assert(!"unexpected CACHE_SCAN_RESULT");
        continue;
      }
    }
    if (changed) {
      if (!vector.count()) {
        ink_debug_assert(hostinfo_copied);
        SET_HANDLER(&CacheVC::scanRemoveDone);
        // force remove even if there is a writer
        cacheProcessor.remove(this, &doc->first_key, CACHE_FRAG_TYPE_HTTP, true, false, (char *) hname, hlen);
        return EVENT_CONT;
      } else {
        offset = (char *) doc - buf->data();
        write_len = 0;
        frag_type = CACHE_FRAG_TYPE_HTTP;
        f.use_first_key = 1;
        f.evac_vector = 1;
        first_key = key = doc->first_key;
        alternate_index = CACHE_ALT_REMOVED;
        earliest_key = zero_key;
        writer_lock_retry = 0;
        SET_HANDLER(&CacheVC::scanOpenWrite);
        return scanOpenWrite(EVENT_NONE, 0);
      }
    }
    doc = (Doc *) ((char *) doc + round_to_approx_size(doc->len));
    continue;
  Lskip:
#endif
    doc = (Doc *) ((char *) doc + INK_BLOCK_SIZE);
  }
#ifdef HTTP_CACHE
  vector.clear();
#endif
  io.aiocb.aio_offset += (char *) doc - buf->data();
  if (io.aiocb.aio_offset >= part->skip + part->len) {
    SET_HANDLER(&CacheVC::scanPart);
    eventProcessor.schedule_in(this, HRTIME_MSECONDS(scan_msec_delay));
    return EVENT_CONT;
  }

Lread:
  io.aiocb.aio_fildes = part->fd;
  if ((ink_off_t)(io.aiocb.aio_offset + io.aiocb.aio_nbytes) > (ink_off_t)(part->skip + part->len))
    io.aiocb.aio_nbytes = part->skip + part->len - io.aiocb.aio_offset;
  offset = 0;
  ink_assert(ink_aio_read(&io) >= 0);
  return EVENT_CONT;

Ldone:
  _action.continuation->handleEvent(CACHE_EVENT_SCAN_DONE, result);
#ifdef HTTP_CACHE
Lcancel:
#endif
  return free_CacheVC(this);
}

int
CacheVC::scanRemoveDone(int event, Event * e)
{
  NOWARN_UNUSED(e);
  NOWARN_UNUSED(event);

  Debug("cache_scan_truss", "inside %p:scanRemoveDone", this);
  Debug("cache_scan", "remove done.");
#ifdef HTTP_CACHE
  alternate.destroy();
#endif
  SET_HANDLER(&CacheVC::scanObject);
  return handleEvent(EVENT_IMMEDIATE, 0);
}

int
CacheVC::scanOpenWrite(int event, Event * e)
{
  NOWARN_UNUSED(e);
  NOWARN_UNUSED(event);
  Debug("cache_scan_truss", "inside %p:scanOpenWrite", this);
  cancel_trigger();
  // get partition lock
  if (writer_lock_retry > SCAN_WRITER_LOCK_MAX_RETRY) {
    int r = _action.continuation->handleEvent(CACHE_EVENT_SCAN_OPERATION_BLOCKED, 0);
    Debug("cache_scan", "still havent got the writer lock, asking user..");
    switch (r) {
    case CACHE_SCAN_RESULT_RETRY:
      writer_lock_retry = 0;
      break;
    case CACHE_SCAN_RESULT_CONTINUE:
      SET_HANDLER(&CacheVC::scanObject);
      return scanObject(EVENT_IMMEDIATE, 0);
    }
  }
  int ret = 0;
  {
    CACHE_TRY_LOCK(lock, part->mutex, mutex->thread_holding);
    if (!lock)
      VC_SCHED_LOCK_RETRY();

    Debug("cache_scan", "trying for writer lock");
    if (part->open_write(this, false, 1)) {
      writer_lock_retry++;
      SET_HANDLER(&CacheVC::scanOpenWrite);
      mutex->thread_holding->schedule_in_local(this, scan_msec_delay);
      return EVENT_CONT;
    }

    ink_debug_assert(this->od);
    // put all the alternates in the open directory vector
    int alt_count = vector.count();
    for (int i = 0; i < alt_count; i++) {
      write_vector->insert(vector.get(i));
    }
    od->writing_vec = 1;
    vector.clear(false);
    // check that the directory entry was not overwritten
    // if so return failure
    Debug("cache_scan", "got writer lock");
    Dir *l = NULL;
    Dir d;
    Doc *doc = (Doc *) (buf->data() + offset);
    offset = (char *) doc - buf->data() + round_to_approx_size(doc->len);
    // if the doc contains some data, then we need to create
    // a new directory entry for this fragment. Remember the
    // offset and the key in earliest_key
    dir_assign(&od->first_dir, &dir);
    if (doc->total_len) {
      dir_assign(&od->single_doc_dir, &dir);
      dir_set_tag(&od->single_doc_dir, doc->key.word(2));
      od->single_doc_key = doc->key;
      od->move_resident_alt = 1;
    }

    while (1) {
      if (!dir_probe(&first_key, part, &d, &l)) {
        part->close_write(this);
        _action.continuation->handleEvent(CACHE_EVENT_SCAN_OPERATION_FAILED, 0);
        SET_HANDLER(&CacheVC::scanObject);
        return handleEvent(EVENT_IMMEDIATE, 0);
      }
      if (memcmp(&dir, &d, SIZEOF_DIR)) {
        Debug("cache_scan", "dir entry has changed");
        continue;
      }
      break;
    }

    // the document was not modified 
    // we are safe from now on as we hold the 
    // writer lock on the doc
    SET_HANDLER(&CacheVC::scanUpdateDone);
    ret = do_write_call();
  }
  if (ret == EVENT_RETURN)
    return handleEvent(AIO_EVENT_DONE, 0);
  return ret;
}

int
CacheVC::scanUpdateDone(int event, Event * e)
{
  NOWARN_UNUSED(e);
  NOWARN_UNUSED(event);
  Debug("cache_scan_truss", "inside %p:scanUpdateDone", this);
  cancel_trigger();
  // get partition lock
  CACHE_TRY_LOCK(lock, part->mutex, mutex->thread_holding);
  if (lock) {
    // insert a directory entry for the previous fragment 
    dir_overwrite(&first_key, part, &dir, &od->first_dir, false);
    if (od->move_resident_alt) {
      dir_insert(&od->single_doc_key, part, &od->single_doc_dir);
    }
    ink_debug_assert(part->open_read(&first_key));
    ink_debug_assert(this->od);
    part->close_write(this);
    SET_HANDLER(&CacheVC::scanObject);
    return handleEvent(EVENT_IMMEDIATE, 0);
  } else {
    mutex->thread_holding->schedule_in_local(this, MUTEX_RETRY_DELAY);
    return EVENT_CONT;
  }
}

void
Cache::print_stats(FILE * fp, int verbose)
{
  for (int i = 0; i < gnpart; i++)
    gpart[i]->ram_cache.print_stats(fp, verbose);
}

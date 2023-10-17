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

#define SCAN_BUF_SIZE RECOVERY_SIZE
#define SCAN_WRITER_LOCK_MAX_RETRY 5

Action *
Cache::scan(Continuation *cont, const char *hostname, int host_len, int KB_per_second)
{
  Debug("cache_scan_truss", "inside scan");
  if (!CacheProcessor::IsCacheReady(CACHE_FRAG_TYPE_HTTP)) {
    cont->handleEvent(CACHE_EVENT_SCAN_FAILED, nullptr);
    return ACTION_RESULT_DONE;
  }

  CacheVC *c = new_CacheVC(cont);
  c->vol     = nullptr;
  /* do we need to make a copy */
  c->hostname        = const_cast<char *>(hostname);
  c->host_len        = host_len;
  c->base_stat       = cache_scan_active_stat;
  c->buf             = new_IOBufferData(BUFFER_SIZE_FOR_XMALLOC(SCAN_BUF_SIZE), MEMALIGNED);
  c->scan_msec_delay = (SCAN_BUF_SIZE / KB_per_second);
  c->offset          = 0;
  SET_CONTINUATION_HANDLER(c, &CacheVC::scanVol);
  eventProcessor.schedule_in(c, HRTIME_MSECONDS(c->scan_msec_delay));
  cont->handleEvent(CACHE_EVENT_SCAN, c);
  return &c->_action;
}

int
CacheVC::scanVol(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */)
{
  Debug("cache_scan_truss", "inside %p:scanVol", this);
  if (_action.cancelled) {
    return free_CacheVC(this);
  }

  ReplaceablePtr<CacheHostTable>::ScopedReader hosttable(&theCache->hosttable);

  const CacheHostRecord *rec = &hosttable->gen_host_rec;
  if (host_len) {
    CacheHostResult res;
    hosttable->Match(hostname, host_len, &res);
    if (res.record) {
      rec = res.record;
    }
  }

  if (!vol) {
    if (!rec->num_vols) {
      goto Ldone;
    }
    vol = rec->vols[0];
  } else {
    for (int i = 0; i < rec->num_vols - 1; i++) {
      if (vol == rec->vols[i]) {
        vol = rec->vols[i + 1];
        goto Lcont;
      }
    }
    goto Ldone;
  }
Lcont:
  fragment = 0;
  SET_HANDLER(&CacheVC::scanObject);
  eventProcessor.schedule_in(this, HRTIME_MSECONDS(scan_msec_delay));
  return EVENT_CONT;
Ldone:
  _action.continuation->handleEvent(CACHE_EVENT_SCAN_DONE, nullptr);
  return free_CacheVC(this);
}

/* Next block with some data in it in this partition.  Returns end of partition if no more
 * locations.
 *
 * d - Vol
 * vol_map - precalculated map
 * offset - offset to start looking at (and data at this location has not been read yet). */
static off_t
next_in_map(Vol *d, char *vol_map, off_t offset)
{
  off_t start_offset = d->vol_offset_to_offset(0);
  off_t new_off      = (offset - start_offset);
  off_t vol_len      = d->vol_relative_length(start_offset);

  while (new_off < vol_len && !vol_map[new_off / SCAN_BUF_SIZE]) {
    new_off += SCAN_BUF_SIZE;
  }
  if (new_off >= vol_len) {
    return vol_len + start_offset;
  }
  return new_off + start_offset;
}

// Function in CacheDir.cc that we need for make_vol_map().
int dir_bucket_loop_fix(Dir *start_dir, int s, Vol *d);

// TODO: If we used a bit vector, we could make a smaller map structure.
// TODO: If we saved a high water mark we could have a smaller buf, and avoid searching it
// when we are asked about the highest interesting offset.
/* Make map of what blocks in partition are used.
 *
 * d - Vol to make a map of. */
static char *
make_vol_map(Vol *d)
{
  // Map will be one byte for each SCAN_BUF_SIZE bytes.
  off_t start_offset = d->vol_offset_to_offset(0);
  off_t vol_len      = d->vol_relative_length(start_offset);
  size_t map_len     = (vol_len + (SCAN_BUF_SIZE - 1)) / SCAN_BUF_SIZE;
  char *vol_map      = static_cast<char *>(ats_malloc(map_len));

  memset(vol_map, 0, map_len);

  // Scan directories.
  // Copied from dir_entries_used() and modified to fill in the map instead.
  for (int s = 0; s < d->segments; s++) {
    Dir *seg = d->dir_segment(s);
    for (int b = 0; b < d->buckets; b++) {
      Dir *e = dir_bucket(b, seg);
      if (dir_bucket_loop_fix(e, s, d)) {
        break;
      }
      while (e) {
        if (dir_offset(e) && dir_valid(d, e) && dir_agg_valid(d, e) && dir_head(e)) {
          off_t offset = d->vol_offset(e) - start_offset;
          if (offset <= vol_len) {
            vol_map[offset / SCAN_BUF_SIZE] = 1;
          }
        }
        e = next_dir(e, seg);
        if (!e) {
          break;
        }
      }
    }
  }
  return vol_map;
}

int
CacheVC::scanObject(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */)
{
  Debug("cache_scan_truss", "inside %p:scanObject", this);

  Doc *doc     = nullptr;
  void *result = nullptr;
  int hlen     = 0;
  char hname[500];
  bool hostinfo_copied         = false;
  off_t next_object_len        = 0;
  bool might_need_overlap_read = false;

  cancel_trigger();
  set_io_not_in_progress();
  if (_action.cancelled) {
    return free_CacheVC(this);
  }

  CACHE_TRY_LOCK(lock, vol->mutex, mutex->thread_holding);
  if (!lock.is_locked()) {
    Debug("cache_scan_truss", "delay %p:scanObject", this);
    mutex->thread_holding->schedule_in_local(this, HRTIME_MSECONDS(cache_config_mutex_retry_delay));
    return EVENT_CONT;
  }

  if (!fragment) { // initialize for first read
    fragment            = 1;
    scan_vol_map        = make_vol_map(vol);
    io.aiocb.aio_offset = next_in_map(vol, scan_vol_map, vol->vol_offset_to_offset(0));
    if (io.aiocb.aio_offset >= static_cast<off_t>(vol->skip + vol->len)) {
      goto Lnext_vol;
    }
    io.aiocb.aio_nbytes = SCAN_BUF_SIZE;
    io.aiocb.aio_buf    = buf->data();
    io.action           = this;
    io.thread           = AIO_CALLBACK_THREAD_ANY;
    Debug("cache_scan_truss", "read %p:scanObject", this);
    goto Lread;
  }

  if (!io.ok()) {
    result = (void *)-ECACHE_READ_FAIL;
    goto Ldone;
  }

  doc = reinterpret_cast<Doc *>(buf->data() + offset);
  // If there is data in the buffer before the start that is from a partial object read previously
  // Fix things as if we read it this time.
  if (scan_fix_buffer_offset) {
    io.aio_result += scan_fix_buffer_offset;
    io.aiocb.aio_nbytes += scan_fix_buffer_offset;
    io.aiocb.aio_offset -= scan_fix_buffer_offset;
    io.aiocb.aio_buf       = static_cast<char *>(io.aiocb.aio_buf) - scan_fix_buffer_offset;
    scan_fix_buffer_offset = 0;
  }
  while (static_cast<off_t>(reinterpret_cast<char *>(doc) - buf->data()) + next_object_len <
         static_cast<off_t>(io.aiocb.aio_nbytes)) {
    might_need_overlap_read = false;
    doc                     = reinterpret_cast<Doc *>(reinterpret_cast<char *>(doc) + next_object_len);
    next_object_len         = vol->round_to_approx_size(doc->len);
    int i;
    bool changed;

    if (doc->magic != DOC_MAGIC) {
      next_object_len = CACHE_BLOCK_SIZE;
      Debug("cache_scan_truss", "blockskip %p:scanObject", this);
      continue;
    }

    if (doc->doc_type != CACHE_FRAG_TYPE_HTTP || !doc->hlen) {
      goto Lskip;
    }

    last_collision = nullptr;
    while (true) {
      if (!dir_probe(&doc->first_key, vol, &dir, &last_collision)) {
        goto Lskip;
      }
      if (!dir_agg_valid(vol, &dir) || !dir_head(&dir) ||
          (vol->vol_offset(&dir) != io.aiocb.aio_offset + (reinterpret_cast<char *>(doc) - buf->data()))) {
        continue;
      }
      break;
    }
    if (doc->data() - buf->data() > static_cast<int>(io.aiocb.aio_nbytes)) {
      might_need_overlap_read = true;
      goto Lskip;
    }
    {
      char *tmp = doc->hdr();
      int len   = doc->hlen;
      while (len > 0) {
        int r = HTTPInfo::unmarshal(tmp, len, buf.get());
        if (r < 0) {
          ink_assert(!"CacheVC::scanObject unmarshal failed");
          goto Lskip;
        }
        len -= r;
        tmp += r;
      }
    }
    if (this->load_http_info(&vector, doc) != doc->hlen) {
      goto Lskip;
    }
    changed         = false;
    hostinfo_copied = false;
    for (i = 0; i < vector.count(); i++) {
      if (!vector.get(i)->valid()) {
        goto Lskip;
      }
      if (!hostinfo_copied) {
        memccpy(hname, vector.get(i)->request_get()->host_get(&hlen), 0, 500);
        hname[hlen] = 0;
        Debug("cache_scan", "hostname = '%s', hostlen = %d", hname, hlen);
        hostinfo_copied = true;
      }
      vector.get(i)->object_key_get(&key);
      alternate_index = i;
      // verify that the earliest block exists, reducing 'false hit' callbacks
      if (!(key == doc->key)) {
        last_collision = nullptr;
        if (!dir_probe(&key, vol, &earliest_dir, &last_collision)) {
          continue;
        }
      }
      earliest_key = key;
      int result1  = _action.continuation->handleEvent(CACHE_EVENT_SCAN_OBJECT, vector.get(i));
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
        ink_assert(alternate_index >= 0);
        vector.insert(&alternate, alternate_index);
        if (!vector.get(alternate_index)->valid()) {
          continue;
        }
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
        ink_assert(hostinfo_copied);
        SET_HANDLER(&CacheVC::scanRemoveDone);
        // force remove even if there is a writer
        cacheProcessor.remove(this, &doc->first_key, CACHE_FRAG_TYPE_HTTP, hname, hlen);
        return EVENT_CONT;
      } else {
        offset          = reinterpret_cast<char *>(doc) - buf->data();
        write_len       = 0;
        frag_type       = CACHE_FRAG_TYPE_HTTP;
        f.use_first_key = 1;
        f.evac_vector   = 1;
        first_key = key   = doc->first_key;
        alternate_index   = CACHE_ALT_REMOVED;
        earliest_key      = zero_key;
        writer_lock_retry = 0;
        SET_HANDLER(&CacheVC::scanOpenWrite);
        return scanOpenWrite(EVENT_NONE, nullptr);
      }
    }
    continue;
  Lskip:;
  }
  vector.clear();
  // If we had an object that went past the end of the buffer, and it is small enough to fix,
  // fix it.
  if (might_need_overlap_read &&
      (static_cast<off_t>(reinterpret_cast<char *>(doc) - buf->data()) + next_object_len >
       static_cast<off_t>(io.aiocb.aio_nbytes)) &&
      next_object_len > 0) {
    off_t partial_object_len = io.aiocb.aio_nbytes - (reinterpret_cast<char *>(doc) - buf->data());
    // Copy partial object to beginning of the buffer.
    memmove(buf->data(), reinterpret_cast<char *>(doc), partial_object_len);
    io.aiocb.aio_offset += io.aiocb.aio_nbytes;
    io.aiocb.aio_nbytes    = SCAN_BUF_SIZE - partial_object_len;
    io.aiocb.aio_buf       = buf->data() + partial_object_len;
    scan_fix_buffer_offset = partial_object_len;
  } else { // Normal case, where we ended on a object boundary.
    io.aiocb.aio_offset += (reinterpret_cast<char *>(doc) - buf->data()) + next_object_len;
    Debug("cache_scan_truss", "next %p:scanObject %" PRId64, this, (int64_t)io.aiocb.aio_offset);
    io.aiocb.aio_offset = next_in_map(vol, scan_vol_map, io.aiocb.aio_offset);
    Debug("cache_scan_truss", "next_in_map %p:scanObject %" PRId64, this, (int64_t)io.aiocb.aio_offset);
    io.aiocb.aio_nbytes    = SCAN_BUF_SIZE;
    io.aiocb.aio_buf       = buf->data();
    scan_fix_buffer_offset = 0;
  }

  if (io.aiocb.aio_offset >= vol->skip + vol->len) {
  Lnext_vol:
    SET_HANDLER(&CacheVC::scanVol);
    eventProcessor.schedule_in(this, HRTIME_MSECONDS(scan_msec_delay));
    return EVENT_CONT;
  }

Lread:
  io.aiocb.aio_fildes = vol->fd;
  if (static_cast<off_t>(io.aiocb.aio_offset + io.aiocb.aio_nbytes) > static_cast<off_t>(vol->skip + vol->len)) {
    io.aiocb.aio_nbytes = vol->skip + vol->len - io.aiocb.aio_offset;
  }
  offset = 0;
  ink_assert(ink_aio_read(&io) >= 0);
  Debug("cache_scan_truss", "read %p:scanObject %" PRId64 " %zu", this, (int64_t)io.aiocb.aio_offset, (size_t)io.aiocb.aio_nbytes);
  return EVENT_CONT;

Ldone:
  Debug("cache_scan_truss", "done %p:scanObject", this);
  _action.continuation->handleEvent(CACHE_EVENT_SCAN_DONE, result);
Lcancel:
  return free_CacheVC(this);
}

int
CacheVC::scanRemoveDone(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */)
{
  Debug("cache_scan_truss", "inside %p:scanRemoveDone", this);
  Debug("cache_scan", "remove done.");
  alternate.destroy();
  SET_HANDLER(&CacheVC::scanObject);
  return handleEvent(EVENT_IMMEDIATE, nullptr);
}

int
CacheVC::scanOpenWrite(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */)
{
  Debug("cache_scan_truss", "inside %p:scanOpenWrite", this);
  cancel_trigger();
  // get volume lock
  if (writer_lock_retry > SCAN_WRITER_LOCK_MAX_RETRY) {
    int r = _action.continuation->handleEvent(CACHE_EVENT_SCAN_OPERATION_BLOCKED, nullptr);
    Debug("cache_scan", "still haven't got the writer lock, asking user..");
    switch (r) {
    case CACHE_SCAN_RESULT_RETRY:
      writer_lock_retry = 0;
      break;
    case CACHE_SCAN_RESULT_CONTINUE:
      SET_HANDLER(&CacheVC::scanObject);
      return scanObject(EVENT_IMMEDIATE, nullptr);
    }
  }
  int ret = 0;
  {
    CACHE_TRY_LOCK(lock, vol->mutex, mutex->thread_holding);
    if (!lock.is_locked()) {
      Debug("cache_scan", "vol->mutex %p:scanOpenWrite", this);
      VC_SCHED_LOCK_RETRY();
    }

    Debug("cache_scan", "trying for writer lock");
    if (vol->open_write(this, false, 1)) {
      writer_lock_retry++;
      SET_HANDLER(&CacheVC::scanOpenWrite);
      mutex->thread_holding->schedule_in_local(this, scan_msec_delay);
      return EVENT_CONT;
    }

    ink_assert(this->od);
    // put all the alternates in the open directory vector
    int alt_count = vector.count();
    for (int i = 0; i < alt_count; i++) {
      write_vector->insert(vector.get(i));
    }
    od->writing_vec = true;
    vector.clear(false);
    // check that the directory entry was not overwritten
    // if so return failure
    Debug("cache_scan", "got writer lock");
    Dir *l = nullptr;
    Dir d;
    Doc *doc = reinterpret_cast<Doc *>(buf->data() + offset);
    offset   = reinterpret_cast<char *>(doc) - buf->data() + vol->round_to_approx_size(doc->len);
    // if the doc contains some data, then we need to create
    // a new directory entry for this fragment. Remember the
    // offset and the key in earliest_key
    dir_assign(&od->first_dir, &dir);
    if (doc->total_len) {
      dir_assign(&od->single_doc_dir, &dir);
      dir_set_tag(&od->single_doc_dir, doc->key.slice32(2));
      od->single_doc_key    = doc->key;
      od->move_resident_alt = true;
    }

    while (true) {
      if (!dir_probe(&first_key, vol, &d, &l)) {
        vol->close_write(this);
        _action.continuation->handleEvent(CACHE_EVENT_SCAN_OPERATION_FAILED, nullptr);
        SET_HANDLER(&CacheVC::scanObject);
        return handleEvent(EVENT_IMMEDIATE, nullptr);
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
    if (f.evac_vector) {
      header_len = write_vector->marshal_length();
    }
    SET_HANDLER(&CacheVC::scanUpdateDone);
    ret = do_write_call();
  }
  if (ret == EVENT_RETURN) {
    return handleEvent(AIO_EVENT_DONE, nullptr);
  }
  return ret;
}

int
CacheVC::scanUpdateDone(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */)
{
  Debug("cache_scan_truss", "inside %p:scanUpdateDone", this);
  cancel_trigger();
  // get volume lock
  CACHE_TRY_LOCK(lock, vol->mutex, mutex->thread_holding);
  if (lock.is_locked()) {
    // insert a directory entry for the previous fragment
    dir_overwrite(&first_key, vol, &dir, &od->first_dir, false);
    if (od->move_resident_alt) {
      dir_insert(&od->single_doc_key, vol, &od->single_doc_dir);
    }
    ink_assert(vol->open_read(&first_key));
    ink_assert(this->od);
    vol->close_write(this);
    SET_HANDLER(&CacheVC::scanObject);
    return handleEvent(EVENT_IMMEDIATE, nullptr);
  } else {
    mutex->thread_holding->schedule_in_local(this, HRTIME_MSECONDS(cache_config_mutex_retry_delay));
    return EVENT_CONT;
  }
}

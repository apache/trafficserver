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

#include "I_CacheDefs.h"
#include "P_CacheDisk.h"
#include "P_CacheHttp.h"
#include "P_CacheInternal.h"
#include "P_CacheVol.h"

// hdrs
#include "HTTP.h"
#include "MIME.h"

// aio
#include "I_AIO.h"

// tsapi
#if DEBUG
#include "api/Metrics.h"
#endif

// inkevent
#include "I_Continuation.h"
#include "I_EThread.h"
#include "I_Event.h"
#include "I_IOBuffer.h"
#include "I_Lock.h"
#include "I_VIO.h"

// tscppapi
#include "tscpp/api/HttpStatus.h"

// tscore
#include "tscore/I_Version.h"
#include "tscore/ink_assert.h"
#include "tscore/Ptr.h"

// ts
#include "ts/DbgCtl.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ctime>

DbgCtl dbg_ctl_cache_bc{"cache_bc"};
DbgCtl dbg_ctl_cache_disk_error{"cache_disk_error"};
DbgCtl dbg_ctl_cache_read{"cache_read"};
#ifdef DEBUG
DbgCtl dbg_ctl_cache_close{"cache_close"};
DbgCtl dbg_ctl_cache_reenable{"cache_reenable"};
#endif

// Compilation Options
#define STORE_COLLISION   1
#define USELESS_REENABLES // allow them for now
// #define VERIFY_JTEST_DATA

extern int cache_config_ram_cache_cutoff;

int CacheVC::size_to_init = -1;

CacheVC::CacheVC()
{
  size_to_init = sizeof(CacheVC) - (size_t) & ((CacheVC *)nullptr)->vio;
  memset((void *)&vio, 0, size_to_init);
}

HTTPInfo::FragOffset *
CacheVC::get_frag_table()
{
  ink_assert(alternate.valid());
  return alternate.valid() ? alternate.get_frag_table() : nullptr;
}

VIO *
CacheVC::do_io_read(Continuation *c, int64_t nbytes, MIOBuffer *abuf)
{
  ink_assert(vio.op == VIO::READ);
  vio.buffer.writer_for(abuf);
  vio.set_continuation(c);
  vio.ndone     = 0;
  vio.nbytes    = nbytes;
  vio.vc_server = this;
#ifdef DEBUG
  ink_assert(!c || c->mutex->thread_holding);
#endif
  if (c && !trigger && !recursive) {
    trigger = c->mutex->thread_holding->schedule_imm_local(this);
  }
  return &vio;
}

VIO *
CacheVC::do_io_pread(Continuation *c, int64_t nbytes, MIOBuffer *abuf, int64_t offset)
{
  ink_assert(vio.op == VIO::READ);
  vio.buffer.writer_for(abuf);
  vio.set_continuation(c);
  vio.ndone     = 0;
  vio.nbytes    = nbytes;
  vio.vc_server = this;
  seek_to       = offset;
#ifdef DEBUG
  ink_assert(c->mutex->thread_holding);
#endif
  if (!trigger && !recursive) {
    trigger = c->mutex->thread_holding->schedule_imm_local(this);
  }
  return &vio;
}

VIO *
CacheVC::do_io_write(Continuation *c, int64_t nbytes, IOBufferReader *abuf, bool owner)
{
  ink_assert(vio.op == VIO::WRITE);
  ink_assert(!owner);
  vio.buffer.reader_for(abuf);
  vio.set_continuation(c);
  vio.ndone     = 0;
  vio.nbytes    = nbytes;
  vio.vc_server = this;
#ifdef DEBUG
  ink_assert(!c || c->mutex->thread_holding);
#endif
  if (c && !trigger && !recursive) {
    trigger = c->mutex->thread_holding->schedule_imm_local(this);
  }
  return &vio;
}

void
CacheVC::do_io_close(int alerrno)
{
  ink_assert(mutex->thread_holding == this_ethread());
  int previous_closed = closed;
  closed              = (alerrno == -1) ? 1 : -1; // Stupid default arguments
  DDbg(dbg_ctl_cache_close, "do_io_close %p %d %d", this, alerrno, closed);
  if (!previous_closed && !recursive) {
    die();
  }
}

void
CacheVC::reenable(VIO *avio)
{
  DDbg(dbg_ctl_cache_reenable, "reenable %p", this);
  (void)avio;
#ifdef DEBUG
  ink_assert(avio->mutex->thread_holding);
#endif
  if (!trigger) {
#ifndef USELESS_REENABLES
    if (vio.op == VIO::READ) {
      if (vio.buffer.mbuf->max_read_avail() > vio.buffer.writer()->water_mark)
        ink_assert(!"useless reenable of cache read");
    } else if (!vio.buffer.reader()->read_avail())
      ink_assert(!"useless reenable of cache write");
#endif
    trigger = avio->mutex->thread_holding->schedule_imm_local(this);
  }
}

void
CacheVC::reenable_re(VIO *avio)
{
  DDbg(dbg_ctl_cache_reenable, "reenable_re %p", this);
  (void)avio;
#ifdef DEBUG
  ink_assert(avio->mutex->thread_holding);
#endif
  if (!trigger) {
    if (!is_io_in_progress() && !recursive) {
      handleEvent(EVENT_NONE, (void *)nullptr);
    } else {
      trigger = avio->mutex->thread_holding->schedule_imm_local(this);
    }
  }
}

bool
CacheVC::get_data(int i, void *data)
{
  switch (i) {
  case CACHE_DATA_HTTP_INFO:
    *(static_cast<CacheHTTPInfo **>(data)) = &alternate;
    return true;
  case CACHE_DATA_RAM_CACHE_HIT_FLAG:
    *(static_cast<int *>(data)) = !f.not_from_ram_cache;
    return true;
  default:
    break;
  }
  return false;
}

int64_t
CacheVC::get_object_size()
{
  return (this)->doc_len;
}

bool
CacheVC::set_data(int /* i ATS_UNUSED */, void * /* data */)
{
  ink_assert(!"CacheVC::set_data should not be called!");
  return true;
}

void
CacheVC::get_http_info(CacheHTTPInfo **ainfo)
{
  *ainfo = &(this)->alternate;
}

// set_http_info must be called before do_io_write
// cluster vc does an optimization where it calls do_io_write() before
// calling set_http_info(), but it guarantees that the info will
// be set before transferring any bytes
void
CacheVC::set_http_info(CacheHTTPInfo *ainfo)
{
  ink_assert(!total_len);
  if (f.update) {
    ainfo->object_key_set(update_key);
    ainfo->object_size_set(update_len);
  } else {
    ainfo->object_key_set(earliest_key);
    // don't know the total len yet
  }

  MIMEField *field = ainfo->m_alt->m_response_hdr.field_find(MIME_FIELD_CONTENT_LENGTH, MIME_LEN_CONTENT_LENGTH);
  if ((field && !field->value_get_int64()) || ainfo->m_alt->m_response_hdr.status_get() == HTTP_STATUS_NO_CONTENT) {
    f.allow_empty_doc = 1;
    // Set the object size here to zero in case this is a cache replace where the new object
    // length is zero but the old object was not.
    ainfo->object_size_set(0);
  } else {
    f.allow_empty_doc = 0;
  }

  alternate.copy_shallow(ainfo);
  ainfo->clear();
}

bool
CacheVC::set_pin_in_cache(time_t time_pin)
{
  if (total_len) {
    ink_assert(!"should Pin the document before writing");
    return false;
  }
  if (vio.op != VIO::WRITE) {
    ink_assert(!"Pinning only allowed while writing objects to the cache");
    return false;
  }
  pin_in_cache = time_pin;
  return true;
}

time_t
CacheVC::get_pin_in_cache()
{
  return pin_in_cache;
}

int
CacheVC::dead(int /* event ATS_UNUSED */, Event * /*e ATS_UNUSED */)
{
  ink_assert(0);
  return EVENT_DONE;
}

bool
CacheVC::is_pread_capable()
{
  return !f.read_from_writer_called;
}

static void
unmarshal_helper(Doc *doc, Ptr<IOBufferData> &buf, int &okay)
{
  using UnmarshalFunc           = int(char *buf, int len, RefCountObj *block_ref);
  UnmarshalFunc *unmarshal_func = &HTTPInfo::unmarshal;
  ts::VersionNumber version(doc->v_major, doc->v_minor);

  // introduced by https://github.com/apache/trafficserver/pull/4874, this is used to distinguish the doc version
  // before and after #4847
  if (version < CACHE_DB_VERSION) {
    unmarshal_func = &HTTPInfo::unmarshal_v24_1;
  }

  char *tmp = doc->hdr();
  int len   = doc->hlen;
  while (len > 0) {
    int r = unmarshal_func(tmp, len, buf.get());
    if (r < 0) {
      ink_assert(!"CacheVC::handleReadDone unmarshal failed");
      okay = 0;
      break;
    }
    len -= r;
    tmp += r;
  }
}

// [amc] I think this is where all disk reads from cache funnel through here.
int
CacheVC::handleReadDone(int event, Event *e)
{
  cancel_trigger();
  ink_assert(this_ethread() == mutex->thread_holding);

  Doc *doc = nullptr;
  if (event == AIO_EVENT_DONE) {
    set_io_not_in_progress();
  } else if (is_io_in_progress()) {
    return EVENT_CONT;
  }
  if (DISK_BAD(vol->disk)) {
    io.aio_result = -1;
    Warning("Canceling cache read: disk %s is bad.", vol->hash_text.get());
    goto Ldone;
  }
  {
    MUTEX_TRY_LOCK(lock, vol->mutex, mutex->thread_holding);
    if (!lock.is_locked()) {
      VC_SCHED_LOCK_RETRY();
    }
    if ((!dir_valid(vol, &dir)) || (!io.ok())) {
      if (!io.ok()) {
        Dbg(dbg_ctl_cache_disk_error, "Read error on disk %s\n \
	    read range : [%" PRIu64 " - %" PRIu64 " bytes]  [%" PRIu64 " - %" PRIu64 " blocks] \n",
            vol->hash_text.get(), (uint64_t)io.aiocb.aio_offset, (uint64_t)io.aiocb.aio_offset + io.aiocb.aio_nbytes,
            (uint64_t)io.aiocb.aio_offset / 512, (uint64_t)(io.aiocb.aio_offset + io.aiocb.aio_nbytes) / 512);
      }
      goto Ldone;
    }

    doc = reinterpret_cast<Doc *>(buf->data());
    ink_assert(vol->mutex->nthread_holding < 1000);
    ink_assert(doc->magic == DOC_MAGIC);

    if (ts::VersionNumber(doc->v_major, doc->v_minor) > CACHE_DB_VERSION) {
      // future version, count as corrupted
      doc->magic = DOC_CORRUPT;
      Dbg(dbg_ctl_cache_bc, "Object is future version %d:%d - disk %s - doc id = %" PRIx64 ":%" PRIx64 "", doc->v_major,
          doc->v_minor, vol->hash_text.get(), read_key->slice64(0), read_key->slice64(1));
      goto Ldone;
    }

#ifdef VERIFY_JTEST_DATA
    char xx[500];
    if (read_key && *read_key == doc->key && request.valid() && !dir_head(&dir) && !vio.ndone) {
      int ib = 0, xd = 0;
      request.url_get()->print(xx, 500, &ib, &xd);
      char *x = xx;
      for (int q = 0; q < 3; q++)
        x = strchr(x + 1, '/');
      ink_assert(!memcmp(doc->data(), x, ib - (x - xx)));
    }
#endif

    if (dbg_ctl_cache_read.on()) {
      char xt[CRYPTO_HEX_SIZE];
      Dbg(dbg_ctl_cache_read,
          "Read complete on fragment %s. Length: data payload=%d this fragment=%d total doc=%" PRId64 " prefix=%d",
          doc->key.toHexStr(xt), doc->data_len(), doc->len, doc->total_len, doc->prefix_len());
    }

    // put into ram cache?
    if (io.ok() && ((doc->first_key == *read_key) || (doc->key == *read_key) || STORE_COLLISION) && doc->magic == DOC_MAGIC) {
      int okay = 1;
      if (!f.doc_from_ram_cache) {
        f.not_from_ram_cache = 1;
      }
      if (cache_config_enable_checksum && doc->checksum != DOC_NO_CHECKSUM) {
        // verify that the checksum matches
        uint32_t checksum = 0;
        for (char *b = doc->hdr(); b < reinterpret_cast<char *>(doc) + doc->len; b++) {
          checksum += *b;
        }
        ink_assert(checksum == doc->checksum);
        if (checksum != doc->checksum) {
          Note("cache: checksum error for [%" PRIu64 " %" PRIu64 "] len %d, hlen %d, disk %s, offset %" PRIu64 " size %zu",
               doc->first_key.b[0], doc->first_key.b[1], doc->len, doc->hlen, vol->path, (uint64_t)io.aiocb.aio_offset,
               (size_t)io.aiocb.aio_nbytes);
          doc->magic = DOC_CORRUPT;
          okay       = 0;
        }
      }
      (void)e; // Avoid compiler warnings
      bool http_copy_hdr = false;
      http_copy_hdr =
        cache_config_ram_cache_compress && !f.doc_from_ram_cache && doc->doc_type == CACHE_FRAG_TYPE_HTTP && doc->hlen;
      // If http doc we need to unmarshal the headers before putting in the ram cache
      // unless it could be compressed
      if (!http_copy_hdr && doc->doc_type == CACHE_FRAG_TYPE_HTTP && doc->hlen && okay) {
        unmarshal_helper(doc, buf, okay);
      }
      // Put the request in the ram cache only if its a open_read or lookup
      if (vio.op == VIO::READ && okay) {
        bool cutoff_check;
        // cutoff_check :
        // doc_len == 0 for the first fragment (it is set from the vector)
        //                The decision on the first fragment is based on
        //                doc->total_len
        // After that, the decision is based of doc_len (doc_len != 0)
        // (cache_config_ram_cache_cutoff == 0) : no cutoffs
        cutoff_check =
          ((!doc_len && static_cast<int64_t>(doc->total_len) < cache_config_ram_cache_cutoff) ||
           (doc_len && static_cast<int64_t>(doc_len) < cache_config_ram_cache_cutoff) || !cache_config_ram_cache_cutoff);
        if (cutoff_check && !f.doc_from_ram_cache) {
          uint64_t o = dir_offset(&dir);
          vol->ram_cache->put(read_key, buf.get(), doc->len, http_copy_hdr, o);
        }
        if (!doc_len) {
          // keep a pointer to it. In case the state machine decides to
          // update this document, we don't have to read it back in memory
          // again
          vol->first_fragment_key    = *read_key;
          vol->first_fragment_offset = dir_offset(&dir);
          vol->first_fragment_data   = buf;
        }
      } // end VIO::READ check
      // If it could be compressed, unmarshal after
      if (http_copy_hdr && doc->doc_type == CACHE_FRAG_TYPE_HTTP && doc->hlen && okay) {
        unmarshal_helper(doc, buf, okay);
      }
    } // end io.ok() check
  }
Ldone:
  POP_HANDLER;
  return handleEvent(AIO_EVENT_DONE, nullptr);
}

int
CacheVC::handleRead(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */)

{
  cancel_trigger();

  f.doc_from_ram_cache = false;

  // check ram cache
  ink_assert(vol->mutex->thread_holding == this_ethread());
  int64_t o           = dir_offset(&dir);
  int ram_hit_state   = vol->ram_cache->get(read_key, &buf, static_cast<uint64_t>(o));
  f.compressed_in_ram = (ram_hit_state > RAM_HIT_COMPRESS_NONE) ? 1 : 0;
  if (ram_hit_state >= RAM_HIT_COMPRESS_NONE) {
    goto LramHit;
  }

  // check if it was read in the last open_read call
  if (*read_key == vol->first_fragment_key && dir_offset(&dir) == vol->first_fragment_offset) {
    buf = vol->first_fragment_data;
    goto LmemHit;
  }
  // see if its in the aggregation buffer
  if (dir_agg_buf_valid(vol, &dir)) {
    int agg_offset = vol->vol_offset(&dir) - vol->header->write_pos;
    buf            = new_IOBufferData(iobuffer_size_to_index(io.aiocb.aio_nbytes, MAX_BUFFER_SIZE_INDEX), MEMALIGNED);
    ink_assert((agg_offset + io.aiocb.aio_nbytes) <= (unsigned)vol->agg_buf_pos);
    char *doc = buf->data();
    char *agg = vol->agg_buffer + agg_offset;
    memcpy(doc, agg, io.aiocb.aio_nbytes);
    io.aio_result = io.aiocb.aio_nbytes;
    SET_HANDLER(&CacheVC::handleReadDone);
    return EVENT_RETURN;
  }

  io.aiocb.aio_fildes = vol->fd;
  io.aiocb.aio_offset = vol->vol_offset(&dir);
  if (static_cast<off_t>(io.aiocb.aio_offset + io.aiocb.aio_nbytes) > static_cast<off_t>(vol->skip + vol->len)) {
    io.aiocb.aio_nbytes = vol->skip + vol->len - io.aiocb.aio_offset;
  }
  buf              = new_IOBufferData(iobuffer_size_to_index(io.aiocb.aio_nbytes, MAX_BUFFER_SIZE_INDEX), MEMALIGNED);
  io.aiocb.aio_buf = buf->data();
  io.action        = this;
  io.thread        = mutex->thread_holding->tt == DEDICATED ? AIO_CALLBACK_THREAD_ANY : mutex->thread_holding;
  SET_HANDLER(&CacheVC::handleReadDone);
  ink_assert(ink_aio_read(&io) >= 0);

// ToDo: Why are these for debug only ??
#if DEBUG
  Metrics::increment(cache_rsb.pread_count);
  Metrics::increment(vol->cache_vol->vol_rsb.pread_count);
#endif

  return EVENT_CONT;

LramHit: {
  f.doc_from_ram_cache = true;
  io.aio_result        = io.aiocb.aio_nbytes;
  Doc *doc             = reinterpret_cast<Doc *>(buf->data());
  if (cache_config_ram_cache_compress && doc->doc_type == CACHE_FRAG_TYPE_HTTP && doc->hlen) {
    SET_HANDLER(&CacheVC::handleReadDone);
    return EVENT_RETURN;
  }
}
LmemHit:
  f.doc_from_ram_cache = true;
  io.aio_result        = io.aiocb.aio_nbytes;
  POP_HANDLER;
  return EVENT_RETURN; // allow the caller to release the volume lock
}

int
CacheVC::removeEvent(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */)
{
  cancel_trigger();
  set_io_not_in_progress();
  {
    MUTEX_TRY_LOCK(lock, vol->mutex, mutex->thread_holding);
    if (!lock.is_locked()) {
      VC_SCHED_LOCK_RETRY();
    }
    if (_action.cancelled) {
      if (od) {
        vol->close_write(this);
        od = nullptr;
      }
      goto Lfree;
    }
    if (!f.remove_aborted_writers) {
      if (vol->open_write(this, true, 1)) {
        // writer  exists
        od = vol->open_read(&key);
        ink_release_assert(od);
        od->dont_update_directory = true;
        od                        = nullptr;
      } else {
        od->dont_update_directory = true;
      }
      f.remove_aborted_writers = 1;
    }
  Lread:
    SET_HANDLER(&CacheVC::removeEvent);
    if (!buf) {
      goto Lcollision;
    }
    if (!dir_valid(vol, &dir)) {
      last_collision = nullptr;
      goto Lcollision;
    }
    // check read completed correct FIXME: remove bad vols
    if (!io.ok()) {
      goto Ldone;
    }
    {
      // verify that this is our document
      Doc *doc = reinterpret_cast<Doc *>(buf->data());
      /* should be first_key not key..right?? */
      if (doc->first_key == key) {
        ink_assert(doc->magic == DOC_MAGIC);
        if (dir_delete(&key, vol, &dir) > 0) {
          if (od) {
            vol->close_write(this);
          }
          od = nullptr;
          goto Lremoved;
        }
        goto Ldone;
      }
    }
  Lcollision:
    // check for collision
    if (dir_probe(&key, vol, &dir, &last_collision) > 0) {
      int ret = do_read_call(&key);
      if (ret == EVENT_RETURN) {
        goto Lread;
      }
      return ret;
    }
  Ldone:
    Metrics::increment(cache_rsb.status[static_cast<int>(CacheOpType::Remove)].failure);
    Metrics::increment(vol->cache_vol->vol_rsb.status[static_cast<int>(CacheOpType::Remove)].failure);
    if (od) {
      vol->close_write(this);
    }
  }
  ink_assert(!vol || this_ethread() != vol->mutex->thread_holding);
  _action.continuation->handleEvent(CACHE_EVENT_REMOVE_FAILED, (void *)-ECACHE_NO_DOC);
  goto Lfree;
Lremoved:
  _action.continuation->handleEvent(CACHE_EVENT_REMOVE, nullptr);
Lfree:
  return free_CacheVC(this);
}

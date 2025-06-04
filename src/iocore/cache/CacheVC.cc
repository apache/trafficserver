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

#include "iocore/cache/Cache.h"
#include "iocore/cache/CacheDefs.h"
#include "P_CacheDisk.h"
#include "P_CacheDoc.h"
#include "P_CacheHttp.h"
#include "P_CacheInternal.h"
#include "Stripe.h"

// must be included after the others
#include "CacheVC.h"

// hdrs
#include "proxy/hdrs/HTTP.h"
#include "proxy/hdrs/MIME.h"

// aio
#include "iocore/aio/AIO.h"
#include "tscore/InkErrno.h"

// tsapi
#if DEBUG
#include "tsutil/Metrics.h"
#endif
#include "tscore/Version.h"

// inkevent
#include "iocore/eventsystem/Continuation.h"
#include "iocore/eventsystem/EThread.h"
#include "iocore/eventsystem/Event.h"
#include "iocore/eventsystem/IOBuffer.h"
#include "iocore/eventsystem/Lock.h"
#include "iocore/eventsystem/VIO.h"

// tscore
#include "tscore/ink_assert.h"
#include "tscore/ink_hrtime.h"
#include "tscore/Ptr.h"

// ts
#include "tsutil/DbgCtl.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ctime>

namespace
{
DbgCtl dbg_ctl_cache_bc{"cache_bc"};
DbgCtl dbg_ctl_cache_disk_error{"cache_disk_error"};
DbgCtl dbg_ctl_cache_read{"cache_read"};
DbgCtl dbg_ctl_cache_scan{"cache_scan"};
DbgCtl dbg_ctl_cache_scan_truss{"cache_scan_truss"};
#ifdef DEBUG
DbgCtl dbg_ctl_cache_close{"cache_close"};
DbgCtl dbg_ctl_cache_reenable{"cache_reenable"};
#endif
} // end anonymous namespace

// Compilation Options
#define SCAN_BUF_SIZE              RECOVERY_SIZE
#define SCAN_WRITER_LOCK_MAX_RETRY 5
#define STORE_COLLISION            1
#define USELESS_REENABLES          // allow them for now

extern int64_t cache_config_ram_cache_cutoff;

/* Next block with some data in it in this partition.  Returns end of partition if no more
 * locations.
 *
 * d - Stripe
 * vol_map - precalculated map
 * offset - offset to start looking at (and data at this location has not been read yet). */
static off_t
next_in_map(Stripe *stripe, char *vol_map, off_t offset)
{
  off_t start_offset = stripe->vol_offset_to_offset(0);
  off_t new_off      = (offset - start_offset);
  off_t vol_len      = stripe->vol_relative_length(start_offset);

  while (new_off < vol_len && !vol_map[new_off / SCAN_BUF_SIZE]) {
    new_off += SCAN_BUF_SIZE;
  }
  if (new_off >= vol_len) {
    return vol_len + start_offset;
  }
  return new_off + start_offset;
}

// Function in CacheDir.cc that we need for make_vol_map().
int dir_bucket_loop_fix(Dir *start_dir, int s, Stripe *stripe);

// TODO: If we used a bit vector, we could make a smaller map structure.
// TODO: If we saved a high water mark we could have a smaller buf, and avoid searching it
// when we are asked about the highest interesting offset.
/* Make map of what blocks in partition are used.
 *
 * d - Stripe to make a map of. */
static char *
make_vol_map(Stripe *stripe)
{
  // Map will be one byte for each SCAN_BUF_SIZE bytes.
  off_t  start_offset = stripe->vol_offset_to_offset(0);
  off_t  vol_len      = stripe->vol_relative_length(start_offset);
  size_t map_len      = (vol_len + (SCAN_BUF_SIZE - 1)) / SCAN_BUF_SIZE;
  char  *vol_map      = static_cast<char *>(ats_malloc(map_len));

  memset(vol_map, 0, map_len);

  // Scan directories.
  // Copied from dir_entries_used() and modified to fill in the map instead.
  for (int s = 0; s < stripe->directory.segments; s++) {
    Dir *seg = stripe->directory.get_segment(s);
    for (int b = 0; b < stripe->directory.buckets; b++) {
      Dir *e = dir_bucket(b, seg);
      if (dir_bucket_loop_fix(e, s, stripe)) {
        break;
      }
      while (e) {
        if (dir_offset(e) && stripe->dir_valid(e) && stripe->dir_agg_valid(e) && dir_head(e)) {
          off_t offset = stripe->vol_offset(e) - start_offset;
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

int CacheVC::size_to_init = -1;

CacheVC::CacheVC()
{
  // Initialize Region C
  size_to_init = sizeof(CacheVC) - reinterpret_cast<size_t>(&(static_cast<CacheVC *>(nullptr))->vio);
  memset(reinterpret_cast<void *>(&vio), 0, size_to_init);
}

VIO *
CacheVC::do_io_read(Continuation *c, int64_t nbytes, MIOBuffer *abuf)
{
  ink_assert(vio.op == VIO::READ);
  vio.set_writer(abuf);
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
  vio.set_writer(abuf);
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
  vio.set_reader(abuf);
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
#ifdef DEBUG
  ink_assert(avio->mutex->thread_holding);
#endif
  if (!trigger) {
#ifndef USELESS_REENABLES
    if (vio.op == VIO::READ) {
      if (vio.buffer.mbuf->max_read_avail() > vio.get_writer->water_mark)
        ink_assert(!"useless reenable of cache read");
    } else if (!vio.get_reader()->read_avail())
      ink_assert(!"useless reenable of cache write");
#endif
    trigger = avio->mutex->thread_holding->schedule_imm_local(this);
  }
}

void
CacheVC::reenable_re(VIO *avio)
{
  DDbg(dbg_ctl_cache_reenable, "reenable_re %p", this);
#ifdef DEBUG
  ink_assert(avio->mutex->thread_holding);
#endif
  if (!trigger) {
    if (!is_io_in_progress() && !recursive) {
      handleEvent(EVENT_NONE);
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

int
CacheVC::dead(int /* event ATS_UNUSED */, Event * /*e ATS_UNUSED */)
{
  ink_assert(0);
  return EVENT_DONE;
}

static void
unmarshal_helper(Doc *doc, Ptr<IOBufferData> &buf, int &okay)
{
  using UnmarshalFunc              = int(char *buf, int len, RefCountObj *block_ref);
  UnmarshalFunc    *unmarshal_func = &HTTPInfo::unmarshal;
  ts::VersionNumber version(doc->v_major, doc->v_minor);

  // introduced by https://github.com/apache/trafficserver/pull/4874, this is used to distinguish the doc version
  // before and after #4847
  if (version < CACHE_DB_VERSION) {
    unmarshal_func = &HTTPInfo::unmarshal_v24_1;
  }

  char *tmp = doc->hdr();
  int   len = doc->hlen;
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
CacheVC::handleReadDone(int event, Event * /* e ATS_UNUSED */)
{
  cancel_trigger();
  ink_assert(this_ethread() == mutex->thread_holding);

  Doc *doc = nullptr;
  if (event == AIO_EVENT_DONE) {
    set_io_not_in_progress();
  } else if (is_io_in_progress()) {
    return EVENT_CONT;
  }
  if (DISK_BAD(stripe->disk)) {
    io.aio_result = -1;
    Error("Canceling cache read: disk %s is bad.", stripe->hash_text.get());
    goto Ldone;
  }
  {
    MUTEX_TRY_LOCK(lock, stripe->mutex, mutex->thread_holding);
    if (!lock.is_locked()) {
      VC_SCHED_LOCK_RETRY();
    }
    if ((!stripe->dir_valid(&dir)) || (!io.ok())) {
      if (!io.ok()) {
        Dbg(dbg_ctl_cache_disk_error, "Read error on disk %s\n \
	    read range : [%" PRIu64 " - %" PRIu64 " bytes]  [%" PRIu64 " - %" PRIu64 " blocks] \n",
            stripe->hash_text.get(), (uint64_t)io.aiocb.aio_offset, (uint64_t)io.aiocb.aio_offset + io.aiocb.aio_nbytes,
            (uint64_t)io.aiocb.aio_offset / 512, (uint64_t)(io.aiocb.aio_offset + io.aiocb.aio_nbytes) / 512);
      }
      goto Ldone;
    }

    doc = reinterpret_cast<Doc *>(buf->data());
    ink_assert(stripe->mutex->nthread_holding < 1000);
    ink_assert(doc->magic == DOC_MAGIC);

    if (ts::VersionNumber(doc->v_major, doc->v_minor) > CACHE_DB_VERSION) {
      // future version, count as corrupted
      doc->magic = DOC_CORRUPT;
      Dbg(dbg_ctl_cache_bc, "Object is future version %d:%d - disk %s - doc id = %" PRIx64 ":%" PRIx64 "", doc->v_major,
          doc->v_minor, stripe->hash_text.get(), read_key->slice64(0), read_key->slice64(1));
      goto Ldone;
    }

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
               doc->first_key.b[0], doc->first_key.b[1], doc->len, doc->hlen, stripe->disk->path, (uint64_t)io.aiocb.aio_offset,
               (size_t)io.aiocb.aio_nbytes);
          doc->magic = DOC_CORRUPT;
          okay       = 0;
        }
      }
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
          stripe->ram_cache->put(read_key, buf.get(), doc->len, http_copy_hdr, o);
        }
        if (!doc_len) {
          // keep a pointer to it. In case the state machine decides to
          // update this document, we don't have to read it back in memory
          // again
          stripe->first_fragment_key    = *read_key;
          stripe->first_fragment_offset = dir_offset(&dir);
          stripe->first_fragment_data   = buf;
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

  ink_assert(stripe->mutex->thread_holding == this_ethread());
  if (load_from_ram_cache()) {
    goto LramHit;
  } else if (load_from_last_open_read_call()) {
    goto LmemHit;
  } else if (load_from_aggregation_buffer()) {
    io.aio_result = io.aiocb.aio_nbytes;
    SET_HANDLER(&CacheVC::handleReadDone);
    return EVENT_RETURN;
  }

  io.aiocb.aio_fildes = stripe->fd;
  io.aiocb.aio_offset = stripe->vol_offset(&dir);
  if (static_cast<off_t>(io.aiocb.aio_offset + io.aiocb.aio_nbytes) > static_cast<off_t>(stripe->skip + stripe->len)) {
    io.aiocb.aio_nbytes = stripe->skip + stripe->len - io.aiocb.aio_offset;
  }
  buf              = new_IOBufferData(iobuffer_size_to_index(io.aiocb.aio_nbytes, MAX_BUFFER_SIZE_INDEX), MEMALIGNED);
  io.aiocb.aio_buf = buf->data();
  io.action        = this;
  io.thread        = mutex->thread_holding->tt == DEDICATED ? AIO_CALLBACK_THREAD_ANY : mutex->thread_holding;
  SET_HANDLER(&CacheVC::handleReadDone);
  ink_assert(ink_aio_read(&io) >= 0);

// ToDo: Why are these for debug only ??
#if DEBUG
  ts::Metrics::Counter::increment(cache_rsb.pread_count);
  ts::Metrics::Counter::increment(stripe->cache_vol->vol_rsb.pread_count);
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

bool
CacheVC::load_from_ram_cache()
{
  int64_t o             = dir_offset(&this->dir);
  int     ram_hit_state = this->stripe->ram_cache->get(read_key, &this->buf, static_cast<uint64_t>(o));
  f.compressed_in_ram   = (ram_hit_state > RAM_HIT_COMPRESS_NONE) ? 1 : 0;
  return ram_hit_state >= RAM_HIT_COMPRESS_NONE;
}

bool
CacheVC::load_from_last_open_read_call()
{
  if (*this->read_key == this->stripe->first_fragment_key && dir_offset(&this->dir) == this->stripe->first_fragment_offset) {
    this->buf = this->stripe->first_fragment_data;
    return true;
  }
  return false;
}

bool
CacheVC::load_from_aggregation_buffer()
{
  if (!this->stripe->dir_agg_buf_valid(&this->dir)) {
    return false;
  }

  this->buf = new_IOBufferData(iobuffer_size_to_index(this->io.aiocb.aio_nbytes, MAX_BUFFER_SIZE_INDEX), MEMALIGNED);
  char                 *doc     = this->buf->data();
  [[maybe_unused]] bool success = this->stripe->copy_from_aggregate_write_buffer(doc, dir, this->io.aiocb.aio_nbytes);
  // We already confirmed that the copy was valid, so it should not fail.
  ink_assert(success);
  return true;
}

int
CacheVC::removeEvent(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */)
{
  cancel_trigger();
  set_io_not_in_progress();
  {
    MUTEX_TRY_LOCK(lock, stripe->mutex, mutex->thread_holding);
    if (!lock.is_locked()) {
      VC_SCHED_LOCK_RETRY();
    }
    if (_action.cancelled) {
      if (od) {
        stripe->close_write(this);
        od = nullptr;
      }
      goto Lfree;
    }
    if (!f.remove_aborted_writers) {
      if (stripe->open_write(this, true, 1)) {
        // writer  exists
        od = stripe->open_read(&key);
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
    if (!stripe->dir_valid(&dir)) {
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
        if (dir_delete(&key, stripe, &dir) > 0) {
          if (od) {
            stripe->close_write(this);
          }
          od = nullptr;
          goto Lremoved;
        }
        goto Ldone;
      }
    }
  Lcollision:
    // check for collision
    if (dir_probe(&key, stripe, &dir, &last_collision) > 0) {
      int ret = do_read_call(&key);
      if (ret == EVENT_RETURN) {
        goto Lread;
      }
      return ret;
    }
  Ldone:
    ts::Metrics::Counter::increment(cache_rsb.status[static_cast<int>(CacheOpType::Remove)].failure);
    ts::Metrics::Counter::increment(stripe->cache_vol->vol_rsb.status[static_cast<int>(CacheOpType::Remove)].failure);
    if (od) {
      stripe->close_write(this);
    }
  }
  ink_assert(!stripe || this_ethread() != stripe->mutex->thread_holding);
  _action.continuation->handleEvent(CACHE_EVENT_REMOVE_FAILED, reinterpret_cast<void *>(-ECACHE_NO_DOC));
  goto Lfree;
Lremoved:
  _action.continuation->handleEvent(CACHE_EVENT_REMOVE, nullptr);
Lfree:
  return free_CacheVC(this);
}

int
CacheVC::scanStripe(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */)
{
  Dbg(dbg_ctl_cache_scan_truss, "%p", this);
  if (_action.cancelled) {
    return free_CacheVC(this);
  }

  ReplaceablePtr<CacheHostTable>::ScopedReader hosttable(&theCache->hosttable);

  const CacheHostRecord *rec = &hosttable->gen_host_rec;
  if (!hostname.empty()) {
    CacheHostResult res;
    hosttable->Match(hostname, &res);
    if (res.record) {
      rec = res.record;
    }
  }

  if (!stripe) {
    if (!rec->num_vols) {
      goto Ldone;
    }
    stripe = rec->stripes[0];
  } else {
    for (int i = 0; i < rec->num_vols - 1; i++) {
      if (stripe == rec->stripes[i]) {
        stripe = rec->stripes[i + 1];
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

int
CacheVC::scanObject(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */)
{
  Dbg(dbg_ctl_cache_scan_truss, "inside %p:scanObject", this);

  Doc  *doc    = nullptr;
  void *result = nullptr;
  int   hlen   = 0;
  char  hname[500];
  bool  hostinfo_copied         = false;
  off_t next_object_len         = 0;
  bool  might_need_overlap_read = false;

  cancel_trigger();
  set_io_not_in_progress();
  if (_action.cancelled) {
    return free_CacheVC(this);
  }

  CACHE_TRY_LOCK(lock, stripe->mutex, mutex->thread_holding);
  if (!lock.is_locked()) {
    Dbg(dbg_ctl_cache_scan_truss, "delay %p:scanObject", this);
    mutex->thread_holding->schedule_in_local(this, HRTIME_MSECONDS(cache_config_mutex_retry_delay));
    return EVENT_CONT;
  }

  if (!fragment) { // initialize for first read
    fragment            = 1;
    scan_stripe_map     = make_vol_map(stripe);
    io.aiocb.aio_offset = next_in_map(stripe, scan_stripe_map, stripe->vol_offset_to_offset(0));
    if (io.aiocb.aio_offset >= static_cast<off_t>(stripe->skip + stripe->len)) {
      goto Lnext_vol;
    }
    io.aiocb.aio_nbytes = SCAN_BUF_SIZE;
    io.aiocb.aio_buf    = buf->data();
    io.action           = this;
    io.thread           = AIO_CALLBACK_THREAD_ANY;
    Dbg(dbg_ctl_cache_scan_truss, "read %p:scanObject", this);
    goto Lread;
  }

  if (!io.ok()) {
    result = reinterpret_cast<void *>(-ECACHE_READ_FAIL);
    goto Ldone;
  }

  doc = reinterpret_cast<Doc *>(buf->data() + offset);
  // If there is data in the buffer before the start that is from a partial object read previously
  // Fix things as if we read it this time.
  if (scan_fix_buffer_offset) {
    io.aio_result          += scan_fix_buffer_offset;
    io.aiocb.aio_nbytes    += scan_fix_buffer_offset;
    io.aiocb.aio_offset    -= scan_fix_buffer_offset;
    io.aiocb.aio_buf        = static_cast<char *>(io.aiocb.aio_buf) - scan_fix_buffer_offset;
    scan_fix_buffer_offset  = 0;
  }
  while (static_cast<off_t>(reinterpret_cast<char *>(doc) - buf->data()) + next_object_len <
         static_cast<off_t>(io.aiocb.aio_nbytes)) {
    might_need_overlap_read = false;
    doc                     = reinterpret_cast<Doc *>(reinterpret_cast<char *>(doc) + next_object_len);
    next_object_len         = stripe->round_to_approx_size(doc->len);
    int  i;
    bool changed;

    if (doc->magic != DOC_MAGIC) {
      next_object_len = CACHE_BLOCK_SIZE;
      Dbg(dbg_ctl_cache_scan_truss, "blockskip %p:scanObject", this);
      continue;
    }

    if (doc->doc_type != CACHE_FRAG_TYPE_HTTP || !doc->hlen) {
      goto Lskip;
    }

    last_collision = nullptr;
    while (true) {
      if (!dir_probe(&doc->first_key, stripe, &dir, &last_collision)) {
        goto Lskip;
      }
      if (!stripe->dir_agg_valid(&dir) || !dir_head(&dir) ||
          (stripe->vol_offset(&dir) != io.aiocb.aio_offset + (reinterpret_cast<char *>(doc) - buf->data()))) {
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
      int   len = doc->hlen;
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
        auto host{vector.get(i)->request_get()->host_get()};
        hlen = static_cast<int>(host.length());
        memccpy(hname, host.data(), 0, 500);
        hname[hlen] = 0;
        Dbg(dbg_ctl_cache_scan, "hostname = '%s', hostlen = %d", hname, hlen);
        hostinfo_copied = true;
      }
      vector.get(i)->object_key_get(&key);
      alternate_index = i;
      // verify that the earliest block exists, reducing 'false hit' callbacks
      if (!(key == doc->key)) {
        last_collision = nullptr;
        if (!dir_probe(&key, stripe, &earliest_dir, &last_collision)) {
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
        cacheProcessor.remove(this, &doc->first_key, CACHE_FRAG_TYPE_HTTP,
                              std::string_view{hname, static_cast<std::string_view::size_type>(hlen)});
        return EVENT_CONT;
      } else {
        offset            = reinterpret_cast<char *>(doc) - buf->data();
        write_len         = 0;
        frag_type         = CACHE_FRAG_TYPE_HTTP;
        f.use_first_key   = 1;
        f.evac_vector     = 1;
        alternate_index   = CACHE_ALT_REMOVED;
        writer_lock_retry = 0;

        first_key = key = doc->first_key;
        earliest_key.clear();

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
    io.aiocb.aio_offset    += io.aiocb.aio_nbytes;
    io.aiocb.aio_nbytes     = SCAN_BUF_SIZE - partial_object_len;
    io.aiocb.aio_buf        = buf->data() + partial_object_len;
    scan_fix_buffer_offset  = partial_object_len;
  } else { // Normal case, where we ended on a object boundary.
    io.aiocb.aio_offset += (reinterpret_cast<char *>(doc) - buf->data()) + next_object_len;
    Dbg(dbg_ctl_cache_scan_truss, "next %p:scanObject %" PRId64, this, (int64_t)io.aiocb.aio_offset);
    io.aiocb.aio_offset = next_in_map(stripe, scan_stripe_map, io.aiocb.aio_offset);
    Dbg(dbg_ctl_cache_scan_truss, "next_in_map %p:scanObject %" PRId64, this, (int64_t)io.aiocb.aio_offset);
    io.aiocb.aio_nbytes    = SCAN_BUF_SIZE;
    io.aiocb.aio_buf       = buf->data();
    scan_fix_buffer_offset = 0;
  }

  if (io.aiocb.aio_offset >= stripe->skip + stripe->len) {
  Lnext_vol:
    SET_HANDLER(&CacheVC::scanStripe);
    eventProcessor.schedule_in(this, HRTIME_MSECONDS(scan_msec_delay));
    return EVENT_CONT;
  }

Lread:
  io.aiocb.aio_fildes = stripe->fd;
  if (static_cast<off_t>(io.aiocb.aio_offset + io.aiocb.aio_nbytes) > static_cast<off_t>(stripe->skip + stripe->len)) {
    io.aiocb.aio_nbytes = stripe->skip + stripe->len - io.aiocb.aio_offset;
  }
  offset = 0;
  ink_assert(ink_aio_read(&io) >= 0);
  Dbg(dbg_ctl_cache_scan_truss, "read %p:scanObject %" PRId64 " %zu", this, (int64_t)io.aiocb.aio_offset,
      (size_t)io.aiocb.aio_nbytes);
  return EVENT_CONT;

Ldone:
  Dbg(dbg_ctl_cache_scan_truss, "done %p:scanObject", this);
  _action.continuation->handleEvent(CACHE_EVENT_SCAN_DONE, result);
Lcancel:
  return free_CacheVC(this);
}

int
CacheVC::scanRemoveDone(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */)
{
  Dbg(dbg_ctl_cache_scan_truss, "inside %p:scanRemoveDone", this);
  Dbg(dbg_ctl_cache_scan, "remove done.");
  alternate.destroy();
  SET_HANDLER(&CacheVC::scanObject);
  return handleEvent(EVENT_IMMEDIATE, nullptr);
}

int
CacheVC::scanOpenWrite(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */)
{
  Dbg(dbg_ctl_cache_scan_truss, "inside %p:scanOpenWrite", this);
  cancel_trigger();
  // get volume lock
  if (writer_lock_retry > SCAN_WRITER_LOCK_MAX_RETRY) {
    int r = _action.continuation->handleEvent(CACHE_EVENT_SCAN_OPERATION_BLOCKED, nullptr);
    Dbg(dbg_ctl_cache_scan, "still haven't got the writer lock, asking user..");
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
    CACHE_TRY_LOCK(lock, stripe->mutex, mutex->thread_holding);
    if (!lock.is_locked()) {
      Dbg(dbg_ctl_cache_scan, "stripe->mutex %p:scanOpenWrite", this);
      VC_SCHED_LOCK_RETRY();
    }

    Dbg(dbg_ctl_cache_scan, "trying for writer lock");
    if (stripe->open_write(this, false, 1)) {
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
    Dbg(dbg_ctl_cache_scan, "got writer lock");
    Dir *l = nullptr;
    Dir  d;
    Doc *doc = reinterpret_cast<Doc *>(buf->data() + offset);
    offset   = reinterpret_cast<char *>(doc) - buf->data() + stripe->round_to_approx_size(doc->len);
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
      if (!dir_probe(&first_key, stripe, &d, &l)) {
        stripe->close_write(this);
        _action.continuation->handleEvent(CACHE_EVENT_SCAN_OPERATION_FAILED, nullptr);
        SET_HANDLER(&CacheVC::scanObject);
        return handleEvent(EVENT_IMMEDIATE, nullptr);
      }
      if (memcmp(&dir, &d, SIZEOF_DIR)) {
        Dbg(dbg_ctl_cache_scan, "dir entry has changed");
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
  Dbg(dbg_ctl_cache_scan_truss, "inside %p:scanUpdateDone", this);
  cancel_trigger();
  // get volume lock
  CACHE_TRY_LOCK(lock, stripe->mutex, mutex->thread_holding);
  if (lock.is_locked()) {
    // insert a directory entry for the previous fragment
    dir_overwrite(&first_key, stripe, &dir, &od->first_dir, false);
    if (od->move_resident_alt) {
      dir_insert(&od->single_doc_key, stripe, &od->single_doc_dir);
    }
    ink_assert(stripe->open_read(&first_key));
    ink_assert(this->od);
    stripe->close_write(this);
    SET_HANDLER(&CacheVC::scanObject);
    return handleEvent(EVENT_IMMEDIATE, nullptr);
  } else {
    mutex->thread_holding->schedule_in_local(this, HRTIME_MSECONDS(cache_config_mutex_retry_delay));
    return EVENT_CONT;
  }
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

  MIMEField *field = ainfo->m_alt->m_response_hdr.field_find(static_cast<std::string_view>(MIME_FIELD_CONTENT_LENGTH));
  if ((field && !field->value_get_int64()) || ainfo->m_alt->m_response_hdr.status_get() == HTTPStatus::NO_CONTENT) {
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

void
CacheVC::get_http_info(CacheHTTPInfo **ainfo)
{
  *ainfo = &(this)->alternate;
}

HTTPInfo::FragOffset *
CacheVC::get_frag_table()
{
  ink_assert(alternate.valid());
  return alternate.valid() ? alternate.get_frag_table() : nullptr;
}

bool
CacheVC::is_pread_capable()
{
  return !f.read_from_writer_called;
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

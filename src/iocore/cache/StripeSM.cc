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

#include "P_CacheDisk.h"
#include "P_CacheDoc.h"
#include "P_CacheInternal.h"
#include "P_CacheStats.h"
#include "StripeSM.h"
#include "P_CacheDir.h"

#include "CacheEvacuateDocVC.h"
#include "PreservationTable.h"
#include "Stripe.h"

#include "iocore/cache/CacheDefs.h"
#include "iocore/cache/CacheVC.h"

#include "iocore/aio/AIO.h"

#include "iocore/eventsystem/Continuation.h"
#include "iocore/eventsystem/EThread.h"
#include "iocore/eventsystem/Event.h"
#include "iocore/eventsystem/EventProcessor.h"
#include "iocore/eventsystem/IOBuffer.h"

#include "tsutil/DbgCtl.h"
#include "tsutil/Metrics.h"

#include "tscore/InkErrno.h"
#include "tscore/Diags.h"
#include "tscore/ink_assert.h"
#include "tscore/ink_hrtime.h"
#include "tscore/List.h"

#include <cinttypes>
#include <cstddef>
#include <cstdlib>
#include <cstring>

// These macros allow two incrementing unsigned values x and y to maintain
// their ordering when one of them overflows, given that the values stay close to each other.
#define UINT_WRAP_LTE(_x, _y) (((_y) - (_x)) < INT_MAX)  // exploit overflow
#define UINT_WRAP_GTE(_x, _y) (((_x) - (_y)) < INT_MAX)  // exploit overflow
#define UINT_WRAP_LT(_x, _y)  (((_x) - (_y)) >= INT_MAX) // exploit overflow

namespace
{

short int const CACHE_DB_MAJOR_VERSION_COMPATIBLE = 21;

DbgCtl dbg_ctl_cache_dir_sync{"dir_sync"};
DbgCtl dbg_ctl_cache_disk_error{"cache_disk_error"};
DbgCtl dbg_ctl_cache_evac{"cache_evac"};
DbgCtl dbg_ctl_cache_init{"cache_init"};

#ifdef DEBUG

DbgCtl dbg_ctl_agg_read{"agg_read"};
DbgCtl dbg_ctl_cache_agg{"cache_agg"};

#endif

} // namespace

static void init_document(CacheVC const *vc, Doc *doc, int const len);
static void update_document_key(CacheVC *vc, Doc *doc);
static void update_header_info(CacheVC *vc, Doc *doc);
static int  evacuate_fragments(CacheKey *key, CacheKey *earliest_key, int force, StripeSM *stripe);

struct StripeInitInfo {
  off_t       recover_pos;
  AIOCallback vol_aio[4];
  char       *vol_h_f;

  StripeInitInfo()
  {
    recover_pos = 0;
    vol_h_f     = static_cast<char *>(ats_memalign(ats_pagesize(), 4 * STORE_BLOCK_SIZE));
    memset(vol_h_f, 0, 4 * STORE_BLOCK_SIZE);
  }

  ~StripeInitInfo()
  {
    for (auto &i : vol_aio) {
      i.action = nullptr;
      i.mutex.clear();
    }
    free(vol_h_f);
  }
};

// This is weird: the len passed to the constructor for _preserved_dirs is
// initialized in the superclasse's constructor. This is safe because the
// superclass should always be initialized first.
StripeSM::StripeSM(CacheDisk *disk, off_t blocks, off_t dir_skip, int avg_obj_size, int fragment_size)
  : Continuation(new_ProxyMutex()),
    Stripe{disk, blocks, dir_skip, avg_obj_size, fragment_size},
    fd{disk->fd},
    disk{disk},
    _preserved_dirs{static_cast<int>(len)}
{
  open_dir.mutex = this->mutex;
  SET_HANDLER(&StripeSM::aggWrite);
}

int
StripeSM::begin_read(CacheVC *cont) const
{
  ink_assert(cont->mutex->thread_holding == this_ethread());
  ink_assert(mutex->thread_holding == this_ethread());
  // no need for evacuation as the entire document is already in memory
  if (cont->f.single_fragment) {
    return 0;
  }
  return this->_preserved_dirs.acquire(cont->earliest_dir, cont->earliest_key);
}

int
StripeSM::close_read(CacheVC *cont) const
{
  EThread *t = cont->mutex->thread_holding;
  ink_assert(t == this_ethread());
  ink_assert(t == mutex->thread_holding);
  if (!dir_is_empty(&cont->earliest_dir)) {
    this->_preserved_dirs.release(cont->earliest_dir);
  }
  return 1;
}

/**
  Clear Dir directly. This is mainly used by unit tests. The clear_dir_aio() is the suitable function in most cases.
 */
int
StripeSM::clear_dir()
{
  size_t dir_len = this->dirlen();
  this->_clear_init(this->disk->hw_sector_size);

  if (pwrite(this->fd, this->raw_dir, dir_len, this->skip) < 0) {
    Warning("unable to clear cache directory '%s'", this->hash_text.get());
    return -1;
  }

  return 0;
}

int
StripeSM::init(bool clear)
{
  CryptoContext().hash_immediate(hash_id, hash_text, strlen(hash_text));

  // Evacuation
  this->hit_evacuate_window = (this->data_blocks * cache_config_hit_evacuate_percent) / 100;

  // AIO
  if (clear) {
    Note("clearing cache directory '%s'", hash_text.get());
    return clear_dir_aio();
  }

  init_info           = new StripeInitInfo();
  int   footerlen     = ROUND_TO_STORE_BLOCK(sizeof(StripteHeaderFooter));
  off_t footer_offset = this->dirlen() - footerlen;
  // try A
  off_t as = skip;

  Dbg(dbg_ctl_cache_init, "reading directory '%s'", hash_text.get());
  SET_HANDLER(&StripeSM::handle_header_read);
  init_info->vol_aio[0].aiocb.aio_offset = as;
  init_info->vol_aio[1].aiocb.aio_offset = as + footer_offset;
  off_t bs                               = skip + this->dirlen();
  init_info->vol_aio[2].aiocb.aio_offset = bs;
  init_info->vol_aio[3].aiocb.aio_offset = bs + footer_offset;

  for (unsigned i = 0; i < countof(init_info->vol_aio); i++) {
    AIOCallback *aio      = &(init_info->vol_aio[i]);
    aio->aiocb.aio_fildes = fd;
    aio->aiocb.aio_buf    = &(init_info->vol_h_f[i * STORE_BLOCK_SIZE]);
    aio->aiocb.aio_nbytes = footerlen;
    aio->action           = this;
    aio->thread           = AIO_CALLBACK_THREAD_ANY;
    aio->then             = (i < 3) ? &(init_info->vol_aio[i + 1]) : nullptr;
  }
  ink_assert(ink_aio_read(init_info->vol_aio));
  return 0;
}

int
StripeSM::handle_dir_clear(int event, void *data)
{
  size_t       dir_len = this->dirlen();
  AIOCallback *op;

  if (event == AIO_EVENT_DONE) {
    op = static_cast<AIOCallback *>(data);
    if (!op->ok()) {
      Warning("unable to clear cache directory '%s'", hash_text.get());
      disk->incrErrors(op);
    }

    if (op->aiocb.aio_nbytes == dir_len) {
      /* clear the header for directory B. We don't need to clear the
         whole of directory B. The header for directory B starts at
         skip + len */
      op->aiocb.aio_nbytes = ROUND_TO_STORE_BLOCK(sizeof(StripteHeaderFooter));
      op->aiocb.aio_offset = skip + dir_len;
      ink_assert(ink_aio_write(op));
      return EVENT_DONE;
    }
    set_io_not_in_progress();
    SET_HANDLER(&StripeSM::dir_init_done);
    dir_init_done(EVENT_IMMEDIATE, nullptr);
    /* mark the volume as bad */
  }
  return EVENT_DONE;
}

int
StripeSM::handle_dir_read(int event, void *data)
{
  AIOCallback *op = static_cast<AIOCallback *>(data);

  if (event == AIO_EVENT_DONE) {
    if (!op->ok()) {
      Note("Directory read failed: clearing cache directory %s", this->hash_text.get());
      clear_dir_aio();
      return EVENT_DONE;
    }
  }

  if (!(header->magic == STRIPE_MAGIC && footer->magic == STRIPE_MAGIC &&
        CACHE_DB_MAJOR_VERSION_COMPATIBLE <= header->version._major && header->version._major <= CACHE_DB_MAJOR_VERSION)) {
    Warning("bad footer in cache directory for '%s', clearing", hash_text.get());
    Note("STRIPE_MAGIC %d\n header magic: %d\n footer_magic %d\n CACHE_DB_MAJOR_VERSION_COMPATIBLE %d\n major version %d\n"
         "CACHE_DB_MAJOR_VERSION %d\n",
         STRIPE_MAGIC, header->magic, footer->magic, CACHE_DB_MAJOR_VERSION_COMPATIBLE, header->version._major,
         CACHE_DB_MAJOR_VERSION);
    Note("clearing cache directory '%s'", hash_text.get());
    clear_dir_aio();
    return EVENT_DONE;
  }
  CHECK_DIR(this);

  sector_size = header->sector_size;

  return this->recover_data();
}

/**
  Add AIO task to clear Dir.
 */
int
StripeSM::clear_dir_aio()
{
  size_t dir_len = this->dirlen();
  this->_clear_init(this->disk->hw_sector_size);

  SET_HANDLER(&StripeSM::handle_dir_clear);

  io.aiocb.aio_fildes = fd;
  io.aiocb.aio_buf    = raw_dir;
  io.aiocb.aio_nbytes = dir_len;
  io.aiocb.aio_offset = skip;
  io.action           = this;
  io.thread           = AIO_CALLBACK_THREAD_ANY;
  io.then             = nullptr;
  ink_assert(ink_aio_write(&io));

  return 0;
}

int
StripeSM::recover_data()
{
  SET_HANDLER(&StripeSM::handle_recover_from_data);
  return handle_recover_from_data(EVENT_IMMEDIATE, nullptr);
}

/*
   Philosophy:  The idea is to find the region of disk that could be
   inconsistent and remove all directory entries pointing to that potentially
   inconsistent region.
   Start from a consistent position (the write_pos of the last directory
   synced to disk) and scan forward. Two invariants for docs that were
   written to the disk after the directory was synced:

   1. doc->magic == DOC_MAGIC

   The following two cases happen only when the previous generation
   documents are aligned with the current ones.

   2. All the docs written to the disk
   after the directory was synced will have their sync_serial <=
   header->sync_serial + 1,  because the write aggregation can take
   indeterminate amount of time to sync. The doc->sync_serial can be
   equal to header->sync_serial + 1, because we increment the sync_serial
   before we sync the directory to disk.

   3. The doc->sync_serial will always increase. If doc->sync_serial
   decreases, the document was written in the previous phase

   If either of these conditions fail and we are not too close to the end
   (see the next comment ) then we're done

   We actually start from header->last_write_pos instead of header->write_pos
   to make sure that we haven't wrapped around the whole disk without
   syncing the directory.  Since the sync serial is 60 seconds, it is
   entirely possible to write through the whole cache without
   once syncing the directory. In this case, we need to clear the
   cache.The documents written right before we synced the
   directory to disk should have the write_serial <= header->sync_serial.

      */
int
StripeSM::handle_recover_from_data(int event, void * /* data ATS_UNUSED */)
{
  uint32_t got_len         = 0;
  uint32_t max_sync_serial = header->sync_serial;
  char    *s, *e = nullptr;
  if (event == EVENT_IMMEDIATE) {
    if (header->sync_serial == 0) {
      io.aiocb.aio_buf = nullptr;
      SET_HANDLER(&StripeSM::handle_recover_write_dir);
      return handle_recover_write_dir(EVENT_IMMEDIATE, nullptr);
    }
    // initialize
    recover_wrapped   = false;
    last_sync_serial  = 0;
    last_write_serial = 0;
    recover_pos       = header->last_write_pos;
    if (recover_pos >= skip + len) {
      recover_wrapped = true;
      recover_pos     = start;
    }
    io.aiocb.aio_buf    = static_cast<char *>(ats_memalign(ats_pagesize(), RECOVERY_SIZE));
    io.aiocb.aio_nbytes = RECOVERY_SIZE;
    if (static_cast<off_t>(recover_pos + io.aiocb.aio_nbytes) > static_cast<off_t>(skip + len)) {
      io.aiocb.aio_nbytes = (skip + len) - recover_pos;
    }
  } else if (event == AIO_EVENT_DONE) {
    if (!io.ok()) {
      Warning("disk read error on recover '%s', clearing", hash_text.get());
      disk->incrErrors(&io);
      goto Lclear;
    }
    if (io.aiocb.aio_offset == header->last_write_pos) {
      /* check that we haven't wrapped around without syncing
         the directory. Start from last_write_serial (write pos the documents
         were written to just before syncing the directory) and make sure
         that all documents have write_serial <= header->write_serial.
       */
      uint32_t to_check = header->write_pos - header->last_write_pos;
      ink_assert(to_check && to_check < (uint32_t)io.aiocb.aio_nbytes);
      uint32_t done = 0;
      s             = static_cast<char *>(io.aiocb.aio_buf);
      while (done < to_check) {
        Doc *doc = reinterpret_cast<Doc *>(s + done);
        if (doc->magic != DOC_MAGIC || doc->write_serial > header->write_serial) {
          Warning("no valid directory found while recovering '%s', clearing", hash_text.get());
          goto Lclear;
        }
        done += round_to_approx_size(doc->len);
        if (doc->sync_serial > last_write_serial) {
          last_sync_serial = doc->sync_serial;
        }
      }
      ink_assert(done == to_check);

      got_len      = io.aiocb.aio_nbytes - done;
      recover_pos += io.aiocb.aio_nbytes;
      s            = static_cast<char *>(io.aiocb.aio_buf) + done;
      e            = s + got_len;
    } else {
      got_len      = io.aiocb.aio_nbytes;
      recover_pos += io.aiocb.aio_nbytes;
      s            = static_cast<char *>(io.aiocb.aio_buf);
      e            = s + got_len;
    }
  }
  // examine what we got
  if (got_len) {
    Doc *doc = nullptr;

    if (recover_wrapped && start == io.aiocb.aio_offset) {
      doc = reinterpret_cast<Doc *>(s);
      if (doc->magic != DOC_MAGIC || doc->write_serial < last_write_serial) {
        recover_pos = skip + len - EVACUATION_SIZE;
        goto Ldone;
      }
    }

    // If execution reaches here, then @c got_len > 0 and e == s + got_len therefore s < e
    // clang analyzer can't figure this out, so be explicit.
    ink_assert(s < e);
    while (s < e) {
      doc = reinterpret_cast<Doc *>(s);

      if (doc->magic != DOC_MAGIC || doc->sync_serial != last_sync_serial) {
        if (doc->magic == DOC_MAGIC) {
          if (doc->sync_serial > header->sync_serial) {
            max_sync_serial = doc->sync_serial;
          }

          /*
             doc->magic == DOC_MAGIC, but doc->sync_serial != last_sync_serial
             This might happen in the following situations
             1. We are starting off recovery. In this case the
             last_sync_serial == header->sync_serial, but the doc->sync_serial
             can be anywhere in the range (0, header->sync_serial + 1]
             If this is the case, update last_sync_serial and continue;

             2. A dir sync started between writing documents to the
             aggregation buffer and hence the doc->sync_serial went up.
             If the doc->sync_serial is greater than the last
             sync serial and less than (header->sync_serial + 2) then
             continue;

             3. If the position we are recovering from is within AGG_SIZE
             from the disk end, then we can't trust this document. The
             aggregation buffer might have been larger than the remaining space
             at the end and we decided to wrap around instead of writing
             anything at that point. In this case, wrap around and start
             from the beginning.

             If neither of these 3 cases happen, then we are indeed done.

           */

          // case 1
          // case 2
          if (doc->sync_serial > last_sync_serial && doc->sync_serial <= header->sync_serial + 1) {
            last_sync_serial  = doc->sync_serial;
            s                += round_to_approx_size(doc->len);
            continue;
          }
          // case 3 - we have already recovered some data and
          // (doc->sync_serial < last_sync_serial) ||
          // (doc->sync_serial > header->sync_serial + 1).
          // if we are too close to the end, wrap around
          else if (recover_pos - (e - s) > (skip + len) - AGG_SIZE) {
            recover_wrapped     = true;
            recover_pos         = start;
            io.aiocb.aio_nbytes = RECOVERY_SIZE;

            break;
          }
          // we are done. This doc was written in the earlier phase
          recover_pos -= e - s;
          goto Ldone;
        } else {
          // doc->magic != DOC_MAGIC
          // If we are in the danger zone - recover_pos is within AGG_SIZE
          // from the end, then wrap around
          recover_pos -= e - s;
          if (recover_pos > (skip + len) - AGG_SIZE) {
            recover_wrapped     = true;
            recover_pos         = start;
            io.aiocb.aio_nbytes = RECOVERY_SIZE;

            break;
          }
          // we ar not in the danger zone
          goto Ldone;
        }
      }
      // doc->magic == DOC_MAGIC && doc->sync_serial == last_sync_serial
      last_write_serial  = doc->write_serial;
      s                 += round_to_approx_size(doc->len);
    }

    /* if (s > e) then we gone through RECOVERY_SIZE; we need to
       read more data off disk and continue recovering */
    if (s >= e) {
      /* In the last iteration, we increment s by doc->len...need to undo
         that change */
      if (s > e) {
        s -= round_to_approx_size(doc->len);
      }
      recover_pos -= e - s;
      if (recover_pos >= skip + len) {
        recover_wrapped = true;
        recover_pos     = start;
      }
      io.aiocb.aio_nbytes = RECOVERY_SIZE;
      if (static_cast<off_t>(recover_pos + io.aiocb.aio_nbytes) > static_cast<off_t>(skip + len)) {
        io.aiocb.aio_nbytes = (skip + len) - recover_pos;
      }
    }
  }
  if (recover_pos == prev_recover_pos) { // this should never happen, but if it does break the loop
    goto Lclear;
  }
  prev_recover_pos    = recover_pos;
  io.aiocb.aio_offset = recover_pos;
  ink_assert(ink_aio_read(&io));
  return EVENT_CONT;

Ldone: {
  /* if we come back to the starting position, then we don't have to recover anything */
  if (recover_pos == header->write_pos && recover_wrapped) {
    SET_HANDLER(&StripeSM::handle_recover_write_dir);
    if (dbg_ctl_cache_init.on()) {
      Note("recovery wrapped around. nothing to clear\n");
    }
    return handle_recover_write_dir(EVENT_IMMEDIATE, nullptr);
  }

  recover_pos += EVACUATION_SIZE; // safely cover the max write size
  if (recover_pos < header->write_pos && (recover_pos + EVACUATION_SIZE >= header->write_pos)) {
    Dbg(dbg_ctl_cache_init, "Head Pos: %" PRIu64 ", Rec Pos: %" PRIu64 ", Wrapped:%d", header->write_pos, recover_pos,
        recover_wrapped);
    Warning("no valid directory found while recovering '%s', clearing", hash_text.get());
    goto Lclear;
  }

  if (recover_pos > skip + len) {
    recover_pos -= skip + len;
  }
  // bump sync number so it is different from that in the Doc structs
  uint32_t next_sync_serial = max_sync_serial + 1;
  // make that the next sync does not overwrite our good copy!
  if (!(header->sync_serial & 1) == !(next_sync_serial & 1)) {
    next_sync_serial++;
  }
  // clear effected portion of the cache
  off_t clear_start = this->offset_to_vol_offset(header->write_pos);
  off_t clear_end   = this->offset_to_vol_offset(recover_pos);
  if (clear_start <= clear_end) {
    dir_clear_range(clear_start, clear_end, this);
  } else {
    dir_clear_range(clear_start, DIR_OFFSET_MAX, this);
    dir_clear_range(1, clear_end, this);
  }

  Note("recovery clearing offsets of Stripe %s : [%" PRIu64 ", %" PRIu64 "] sync_serial %d next %d\n", hash_text.get(),
       header->write_pos, recover_pos, header->sync_serial, next_sync_serial);

  footer->sync_serial = header->sync_serial = next_sync_serial;

  for (int i = 0; i < 3; i++) {
    AIOCallback *aio      = &(init_info->vol_aio[i]);
    aio->aiocb.aio_fildes = fd;
    aio->action           = this;
    aio->thread           = AIO_CALLBACK_THREAD_ANY;
    aio->then             = (i < 2) ? &(init_info->vol_aio[i + 1]) : nullptr;
  }
  int    footerlen = ROUND_TO_STORE_BLOCK(sizeof(StripteHeaderFooter));
  size_t dirlen    = this->dirlen();
  int    B         = header->sync_serial & 1;
  off_t  ss        = skip + (B ? dirlen : 0);

  init_info->vol_aio[0].aiocb.aio_buf    = raw_dir;
  init_info->vol_aio[0].aiocb.aio_nbytes = footerlen;
  init_info->vol_aio[0].aiocb.aio_offset = ss;
  init_info->vol_aio[1].aiocb.aio_buf    = raw_dir + footerlen;
  init_info->vol_aio[1].aiocb.aio_nbytes = dirlen - 2 * footerlen;
  init_info->vol_aio[1].aiocb.aio_offset = ss + footerlen;
  init_info->vol_aio[2].aiocb.aio_buf    = raw_dir + dirlen - footerlen;
  init_info->vol_aio[2].aiocb.aio_nbytes = footerlen;
  init_info->vol_aio[2].aiocb.aio_offset = ss + dirlen - footerlen;

  SET_HANDLER(&StripeSM::handle_recover_write_dir);
  ink_assert(ink_aio_write(init_info->vol_aio));
  return EVENT_CONT;
}

Lclear:
  free(static_cast<char *>(io.aiocb.aio_buf));
  delete init_info;
  init_info = nullptr;
  clear_dir_aio();
  return EVENT_CONT;
}

int
StripeSM::handle_recover_write_dir(int /* event ATS_UNUSED */, void * /* data ATS_UNUSED */)
{
  if (io.aiocb.aio_buf) {
    free(static_cast<char *>(io.aiocb.aio_buf));
  }
  delete init_info;
  init_info = nullptr;
  set_io_not_in_progress();
  scan_pos = header->write_pos;
  ink_assert(this->mutex->thread_holding == this_ethread());
  this->_preserved_dirs.periodic_scan(this);
  SET_HANDLER(&StripeSM::dir_init_done);
  return dir_init_done(EVENT_IMMEDIATE, nullptr);
}

CacheEvacuateDocVC *
new_DocEvacuator(int nbytes, StripeSM *stripe)
{
  CacheEvacuateDocVC *c = new_CacheEvacuateDocVC(stripe);
  c->op_type            = static_cast<int>(CacheOpType::Evacuate);
  Metrics::Gauge::increment(cache_rsb.status[c->op_type].active);
  Metrics::Gauge::increment(stripe->cache_vol->vol_rsb.status[c->op_type].active);
  c->buf         = new_IOBufferData(iobuffer_size_to_index(nbytes, MAX_BUFFER_SIZE_INDEX), MEMALIGNED);
  c->stripe      = stripe;
  c->f.evacuator = 1;
  c->earliest_key.clear();
  SET_CONTINUATION_HANDLER(c, &CacheEvacuateDocVC::evacuateDocDone);
  return c;
}

int
StripeSM::handle_header_read(int event, void *data)
{
  AIOCallback         *op;
  StripteHeaderFooter *hf[4];
  switch (event) {
  case AIO_EVENT_DONE:
    op = static_cast<AIOCallback *>(data);
    for (auto &i : hf) {
      ink_assert(op != nullptr);
      i = static_cast<StripteHeaderFooter *>(op->aiocb.aio_buf);
      if (!op->ok()) {
        Note("Header read failed: clearing cache directory %s", this->hash_text.get());
        clear_dir_aio();
        return EVENT_DONE;
      }
      op = op->then;
    }

    io.aiocb.aio_fildes = fd;
    io.aiocb.aio_nbytes = this->dirlen();
    io.aiocb.aio_buf    = raw_dir;
    io.action           = this;
    io.thread           = AIO_CALLBACK_THREAD_ANY;
    io.then             = nullptr;

    if (hf[0]->sync_serial == hf[1]->sync_serial &&
        (hf[0]->sync_serial >= hf[2]->sync_serial || hf[2]->sync_serial != hf[3]->sync_serial)) {
      SET_HANDLER(&StripeSM::handle_dir_read);
      if (dbg_ctl_cache_init.on()) {
        Note("using directory A for '%s'", hash_text.get());
      }
      io.aiocb.aio_offset = skip;
      ink_assert(ink_aio_read(&io));
    }
    // try B
    else if (hf[2]->sync_serial == hf[3]->sync_serial) {
      SET_HANDLER(&StripeSM::handle_dir_read);
      if (dbg_ctl_cache_init.on()) {
        Note("using directory B for '%s'", hash_text.get());
      }
      io.aiocb.aio_offset = skip + this->dirlen();
      ink_assert(ink_aio_read(&io));
    } else {
      Note("no good directory, clearing '%s' since sync_serials on both A and B copies are invalid", hash_text.get());
      Note("Header A: %d\nFooter A: %d\n Header B: %d\n Footer B %d\n", hf[0]->sync_serial, hf[1]->sync_serial, hf[2]->sync_serial,
           hf[3]->sync_serial);
      clear_dir_aio();
      delete init_info;
      init_info = nullptr;
    }
    return EVENT_DONE;
  default:
    ink_assert(!"not reach here");
  }
  return EVENT_DONE;
}

int
StripeSM::dir_init_done(int /* event ATS_UNUSED */, void * /* data ATS_UNUSED */)
{
  if (!cache->cache_read_done) {
    eventProcessor.schedule_in(this, HRTIME_MSECONDS(5), ET_CALL);
    return EVENT_CONT;
  } else {
    int i = gnstripes++;
    ink_assert(!gstripes[i]);
    gstripes[i] = this;
    SET_HANDLER(&StripeSM::aggWrite);
    cache->vol_initialized(fd != -1);
    return EVENT_DONE;
  }
}

/* NOTE:: This state can be called by an AIO thread, so DON'T DON'T
   DON'T schedule any events on this thread using VC_SCHED_XXX or
   mutex->thread_holding->schedule_xxx_local(). ALWAYS use
   eventProcessor.schedule_xxx().
   */
int
StripeSM::aggWriteDone(int event, Event *e)
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
    header->last_write_pos  = header->write_pos;
    header->write_pos      += io.aiocb.aio_nbytes;
    ink_assert(header->write_pos >= start);
    DDbg(dbg_ctl_cache_agg, "Dir %s, Write: %" PRIu64 ", last Write: %" PRIu64 "", hash_text.get(), header->write_pos,
         header->last_write_pos);
    ink_assert(header->write_pos == header->agg_pos);
    if (header->write_pos + EVACUATION_SIZE > scan_pos) {
      ink_assert(this->mutex->thread_holding == this_ethread());
      this->_preserved_dirs.periodic_scan(this);
    }
    this->_write_buffer.reset_buffer_pos();
    header->write_serial++;
  } else {
    // delete all the directory entries that we inserted
    // for fragments is this aggregation buffer
    Dbg(dbg_ctl_cache_disk_error, "Write error on disk %s\n \
            write range : [%" PRIu64 " - %" PRIu64 " bytes]  [%" PRIu64 " - %" PRIu64 " blocks] \n",
        hash_text.get(), (uint64_t)io.aiocb.aio_offset, (uint64_t)io.aiocb.aio_offset + io.aiocb.aio_nbytes,
        (uint64_t)io.aiocb.aio_offset / CACHE_BLOCK_SIZE, (uint64_t)(io.aiocb.aio_offset + io.aiocb.aio_nbytes) / CACHE_BLOCK_SIZE);
    Dir del_dir;
    dir_clear(&del_dir);
    for (int done = 0; done < this->_write_buffer.get_buffer_pos();) {
      Doc *doc = reinterpret_cast<Doc *>(this->_write_buffer.get_buffer() + done);
      dir_set_offset(&del_dir, header->write_pos + done);
      dir_delete(&doc->key, this, &del_dir);
      done += round_to_approx_size(doc->len);
    }
    this->_write_buffer.reset_buffer_pos();
  }
  set_io_not_in_progress();
  // callback ready sync CacheVCs
  CacheVC *c = nullptr;
  while ((c = sync.dequeue())) {
    if (UINT_WRAP_LTE(c->write_serial + 2, header->write_serial)) {
      eventProcessor.schedule_imm(c, ET_CALL, AIO_EVENT_DONE);
    } else {
      sync.push(c); // put it back on the front
      break;
    }
  }
  if (dir_sync_waiting) {
    dir_sync_waiting = false;
    cacheDirSync->handleEvent(EVENT_IMMEDIATE, nullptr);
  }
  if (this->_write_buffer.get_pending_writers().head || sync.head) {
    return aggWrite(event, e);
  }
  return EVENT_CONT;
}

/* NOTE: This state can be called by an AIO thread, so DON'T DON'T
   DON'T schedule any events on this thread using VC_SCHED_XXX or
   mutex->thread_holding->schedule_xxx_local(). ALWAYS use
   eventProcessor.schedule_xxx().
   Also, make sure that any functions called by this also use
   the eventProcessor to schedule events
*/
int
StripeSM::aggWrite(int event, void * /* e ATS_UNUSED */)
{
  ink_assert(!is_io_in_progress());

  Que(CacheVC, link) tocall;
  CacheVC *c;

  cancel_trigger();

Lagain:
  this->aggregate_pending_writes(tocall);

  // if we got nothing...
  if (this->_write_buffer.is_empty()) {
    if (!this->_write_buffer.get_pending_writers().head && !sync.head) { // nothing to get
      return EVENT_CONT;
    }
    if (header->write_pos == start) {
      // write aggregation too long, bad bad, punt on everything.
      Note("write aggregation exceeds vol size");
      ink_assert(!tocall.head);
      ink_assert(false);
      while ((c = this->get_pending_writers().dequeue())) {
        this->_write_buffer.add_bytes_pending_aggregation(-c->agg_len);
        eventProcessor.schedule_imm(c, ET_CALL, AIO_EVENT_DONE);
      }
      return EVENT_CONT;
    }
    // start back
    if (this->get_pending_writers().head) {
      agg_wrap();
      goto Lagain;
    }
  }

  // evacuate space
  off_t end = header->write_pos + this->_write_buffer.get_buffer_pos() + EVACUATION_SIZE;
  if (evac_range(header->write_pos, end, !header->phase) < 0) {
    goto Lwait;
  }
  if (end > skip + len) {
    if (evac_range(start, start + (end - (skip + len)), header->phase) < 0) {
      goto Lwait;
    }
  }

  // if write_buffer.get_pending_writers.head, then we are near the end of the disk, so
  // write down the aggregation in whatever size it is.
  if (this->_write_buffer.get_buffer_pos() < AGG_HIGH_WATER && !this->_write_buffer.get_pending_writers().head && !sync.head &&
      !dir_sync_waiting) {
    goto Lwait;
  }

  // write sync marker
  if (this->_write_buffer.is_empty()) {
    ink_assert(sync.head);
    int l = round_to_approx_size(sizeof(Doc));
    this->_write_buffer.seek(l);
    Doc *d = reinterpret_cast<Doc *>(this->_write_buffer.get_buffer());
    memset(static_cast<void *>(d), 0, sizeof(Doc));
    d->magic        = DOC_MAGIC;
    d->len          = l;
    d->sync_serial  = header->sync_serial;
    d->write_serial = header->write_serial;
  }

  // set write limit
  header->agg_pos = header->write_pos + this->_write_buffer.get_buffer_pos();

  io.aiocb.aio_fildes = fd;
  io.aiocb.aio_offset = header->write_pos;
  io.aiocb.aio_buf    = this->_write_buffer.get_buffer();
  io.aiocb.aio_nbytes = this->_write_buffer.get_buffer_pos();
  io.action           = this;
  /*
    Callback on AIO thread so that we can issue a new write ASAP
    as all writes are serialized in the volume.  This is not necessary
    for reads proceed independently.
   */
  io.thread = AIO_CALLBACK_THREAD_AIO;
  SET_HANDLER(&StripeSM::aggWriteDone);
  ink_aio_write(&io);

Lwait:
  int ret = EVENT_CONT;
  while ((c = tocall.dequeue())) {
    if (event == EVENT_CALL && c->mutex->thread_holding == mutex->thread_holding) {
      ret = EVENT_RETURN;
    } else {
      eventProcessor.schedule_imm(c, ET_CALL, AIO_EVENT_DONE);
    }
  }
  return ret;
}

void
StripeSM::aggregate_pending_writes(Queue<CacheVC, Continuation::Link_link> &tocall)
{
  for (auto *c = static_cast<CacheVC *>(this->_write_buffer.get_pending_writers().head); c;) {
    int writelen = c->agg_len;
    // [amc] this is checked multiple places, on here was it strictly less.
    ink_assert(writelen <= AGG_SIZE);
    if (this->_write_buffer.get_buffer_pos() + writelen > AGG_SIZE ||
        this->header->write_pos + this->_write_buffer.get_buffer_pos() + writelen > (this->skip + this->len)) {
      break;
    }
    DDbg(dbg_ctl_agg_read, "copying: %d, %" PRIu64 ", key: %d", this->_write_buffer.get_buffer_pos(),
         this->header->write_pos + this->_write_buffer.get_buffer_pos(), c->first_key.slice32(0));
    [[maybe_unused]] int wrotelen = this->_agg_copy(c);
    ink_assert(writelen == wrotelen);
    CacheVC *n = static_cast<CacheVC *>(c->link.next);
    this->_write_buffer.get_pending_writers().dequeue();
    if (c->f.sync && c->f.use_first_key) {
      CacheVC *last = this->sync.tail;
      while (last && UINT_WRAP_LT(c->write_serial, last->write_serial)) {
        last = static_cast<CacheVC *>(last->link.prev);
      }
      this->sync.insert(c, last);
    } else if (c->f.evacuator) {
      c->handleEvent(AIO_EVENT_DONE, nullptr);
    } else {
      tocall.enqueue(c);
    }
    c = n;
  }
}

int
StripeSM::_agg_copy(CacheVC *vc)
{
  if (vc->f.evacuator) {
    return this->_copy_evacuator_to_aggregation(vc);
  } else {
    return this->_copy_writer_to_aggregation(vc);
  }
}

int
StripeSM::_copy_evacuator_to_aggregation(CacheVC *vc)
{
  Doc *doc         = reinterpret_cast<Doc *>(vc->buf->data());
  int  approx_size = this->round_to_approx_size(doc->len);

  Metrics::Counter::increment(cache_rsb.gc_frags_evacuated);
  Metrics::Counter::increment(this->cache_vol->vol_rsb.gc_frags_evacuated);

  doc->sync_serial  = this->header->sync_serial;
  doc->write_serial = this->header->write_serial;

  off_t doc_offset{this->header->write_pos + this->_write_buffer.get_buffer_pos()};
  this->_write_buffer.add(doc, approx_size);

  vc->dir = vc->overwrite_dir;
  dir_set_offset(&vc->dir, this->offset_to_vol_offset(doc_offset));
  dir_set_phase(&vc->dir, this->header->phase);
  return approx_size;
}

int
StripeSM::_copy_writer_to_aggregation(CacheVC *vc)
{
  off_t          doc_offset{this->header->write_pos + this->get_agg_buf_pos()};
  uint32_t       len         = vc->write_len + vc->header_len + vc->frag_len + sizeof(Doc);
  Doc           *doc         = this->_write_buffer.emplace(this->round_to_approx_size(len));
  IOBufferBlock *res_alt_blk = nullptr;

  ink_assert(vc->frag_type != CACHE_FRAG_TYPE_HTTP || len != sizeof(Doc));
  ink_assert(this->round_to_approx_size(len) == vc->agg_len);
  // update copy of directory entry for this document
  dir_set_approx_size(&vc->dir, vc->agg_len);
  dir_set_offset(&vc->dir, this->offset_to_vol_offset(doc_offset));
  ink_assert(this->vol_offset(&vc->dir) < (this->skip + this->len));
  dir_set_phase(&vc->dir, this->header->phase);

  // fill in document header
  init_document(vc, doc, len);
  doc->sync_serial = this->header->sync_serial;
  vc->write_serial = doc->write_serial = this->header->write_serial;
  if (vc->get_pin_in_cache()) {
    dir_set_pinned(&vc->dir, 1);
    doc->pin(vc->get_pin_in_cache());
  } else {
    dir_set_pinned(&vc->dir, 0);
    doc->unpin();
  }

  update_document_key(vc, doc);

  if (vc->f.rewrite_resident_alt) {
    ink_assert(vc->f.use_first_key);
    Doc *res_doc   = reinterpret_cast<Doc *>(vc->first_buf->data());
    res_alt_blk    = new_IOBufferBlock(vc->first_buf, res_doc->data_len(), sizeof(Doc) + res_doc->hlen);
    doc->key       = res_doc->key;
    doc->total_len = res_doc->data_len();
  }
  // update the new_info object_key, and total_len and dirinfo
  if (vc->header_len) {
    ink_assert(vc->f.use_first_key);
    update_header_info(vc, doc);
    // the single fragment flag is not used in the write call.
    // putting it in for completeness.
    vc->f.single_fragment = doc->single_fragment();
  }
  // move data
  if (vc->write_len) {
    ink_assert(this->mutex.get()->thread_holding == this_ethread());

    Metrics::Counter::increment(cache_rsb.write_bytes);
    Metrics::Counter::increment(this->cache_vol->vol_rsb.write_bytes);

    if (vc->f.rewrite_resident_alt) {
      doc->set_data(vc->write_len, res_alt_blk, 0);
    } else {
      doc->set_data(vc->write_len, vc->blocks.get(), vc->offset);
    }
  }
  if (cache_config_enable_checksum) {
    doc->calculate_checksum();
  }
  if (vc->frag_type == CACHE_FRAG_TYPE_HTTP && vc->f.single_fragment) {
    ink_assert(doc->hlen);
  }

  if (res_alt_blk) {
    res_alt_blk->free();
  }

  return vc->agg_len;
}

static void
init_document(CacheVC const *vc, Doc *doc, int const len)
{
  doc->magic     = DOC_MAGIC;
  doc->len       = len;
  doc->hlen      = vc->header_len;
  doc->doc_type  = vc->frag_type;
  doc->v_major   = CACHE_DB_MAJOR_VERSION;
  doc->v_minor   = CACHE_DB_MINOR_VERSION;
  doc->unused    = 0; // force this for forward compatibility.
  doc->total_len = vc->total_len;
  doc->first_key = vc->first_key;
  doc->checksum  = DOC_NO_CHECKSUM;
}

static void
update_document_key(CacheVC *vc, Doc *doc)
{
  if (vc->f.use_first_key) {
    if (doc->data_len() || vc->f.allow_empty_doc) {
      doc->key = vc->earliest_key;
    } else { // the vector is being written by itself
      if (vc->earliest_key.is_zero()) {
        do {
          rand_CacheKey(&doc->key);
        } while (DIR_MASK_TAG(doc->key.slice32(2)) == DIR_MASK_TAG(vc->first_key.slice32(2)));
      } else {
        prev_CacheKey(&doc->key, &vc->earliest_key);
      }
    }
    dir_set_head(&vc->dir, true);
  } else {
    doc->key = vc->key;
    dir_set_head(&vc->dir, !vc->fragment);
  }
}

static void
update_header_info(CacheVC *vc, Doc *doc)
{
  if (vc->frag_type == CACHE_FRAG_TYPE_HTTP) {
    ink_assert(vc->write_vector->count() > 0);
    if (!vc->f.update && !vc->f.evac_vector) {
      ink_assert(!(vc->first_key.is_zero()));
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
}

void
StripeSM::agg_wrap()
{
  header->write_pos = start;
  header->phase     = !header->phase;

  header->cycle++;
  header->agg_pos = header->write_pos;
  dir_lookaside_cleanup(this);
  dir_clean_vol(this);
  {
    StripeSM *stripe = this;
    Metrics::Counter::increment(cache_rsb.directory_wrap);
    Metrics::Counter::increment(stripe->cache_vol->vol_rsb.directory_wrap);
    Note("Cache volume %d on disk '%s' wraps around", stripe->cache_vol->vol_number, stripe->hash_text.get());
  }
  ink_assert(this->mutex->thread_holding == this_ethread());
  this->_preserved_dirs.periodic_scan(this);
}

int
StripeSM::evac_range(off_t low, off_t high, int evac_phase)
{
  off_t s  = this->offset_to_vol_offset(low);
  off_t e  = this->offset_to_vol_offset(high);
  int   si = dir_offset_evac_bucket(s);
  int   ei = dir_offset_evac_bucket(e);

  for (int i = si; i <= ei; i++) {
    EvacuationBlock *b            = this->_preserved_dirs.evacuate[i].head;
    EvacuationBlock *first        = nullptr;
    int64_t          first_offset = INT64_MAX;
    for (; b; b = b->link.next) {
      int64_t offset = dir_offset(&b->dir);
      int     phase  = dir_phase(&b->dir);
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
      if (static_cast<off_t>(io.aiocb.aio_offset + io.aiocb.aio_nbytes) > static_cast<off_t>(skip + len)) {
        io.aiocb.aio_nbytes = skip + len - io.aiocb.aio_offset;
      }
      doc_evacuator                = new_DocEvacuator(io.aiocb.aio_nbytes, this);
      doc_evacuator->overwrite_dir = first->dir;

      io.aiocb.aio_buf = doc_evacuator->buf->data();
      io.action        = this;
      io.thread        = AIO_CALLBACK_THREAD_ANY;
      DDbg(dbg_ctl_cache_evac, "evac_range evacuating %X %" PRId64, dir_tag(&first->dir), dir_offset(&first->dir));
      SET_HANDLER(&StripeSM::evacuateDocReadDone);
      ink_assert(ink_aio_read(&io) >= 0);
      return -1;
    }
  }
  return 0;
}

int
StripeSM::evacuateDocReadDone(int event, Event *e)
{
  cancel_trigger();
  if (event != AIO_EVENT_DONE) {
    return EVENT_DONE;
  }
  ink_assert(is_io_in_progress());
  set_io_not_in_progress();
  ink_assert(mutex->thread_holding == this_ethread());
  Doc             *doc = reinterpret_cast<Doc *>(doc_evacuator->buf->data());
  CacheKey         next_key;
  EvacuationBlock *b      = nullptr;
  auto             bucket = dir_evac_bucket(&doc_evacuator->overwrite_dir);
  if (doc->magic != DOC_MAGIC) {
    Dbg(dbg_ctl_cache_evac, "DOC magic: %X %" PRId64, dir_tag(&doc_evacuator->overwrite_dir),
        dir_offset(&doc_evacuator->overwrite_dir));
    ink_assert(doc->magic == DOC_MAGIC);
    goto Ldone;
  }
  DDbg(dbg_ctl_cache_evac, "evacuateDocReadDone %X offset %" PRId64, doc->key.slice32(0),
       dir_offset(&doc_evacuator->overwrite_dir));

  if (evac_bucket_valid(bucket)) {
    b = this->_preserved_dirs.evacuate[bucket].head;
  }
  while (b) {
    if (dir_offset(&b->dir) == dir_offset(&doc_evacuator->overwrite_dir)) {
      break;
    }
    b = b->link.next;
  }
  if (!b) {
    goto Ldone;
  }
  // coverity[Y2K38_SAFETY:FALSE]
  if ((b->f.pinned && !b->readers) && doc->pinned < static_cast<uint32_t>(ink_get_hrtime() / HRTIME_SECOND)) {
    goto Ldone;
  }

  if (dir_head(&b->dir) && b->f.evacuate_head) {
    ink_assert(!b->evac_frags.key.fold());
    // if its a head (vector), evacuation is real simple...we just
    // need to write this vector down and overwrite the directory entry.
    if (dir_compare_tag(&b->dir, &doc->first_key)) {
      doc_evacuator->key = doc->first_key;
      b->evac_frags.key  = doc->first_key;
      DDbg(dbg_ctl_cache_evac, "evacuating vector %X offset %" PRId64, doc->first_key.slice32(0),
           dir_offset(&doc_evacuator->overwrite_dir));
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
      DDbg(dbg_ctl_cache_evac, "evacuating earliest %X %X evac: %p offset: %" PRId64, b->evac_frags.key.slice32(0),
           doc->key.slice32(0), doc_evacuator, dir_offset(&doc_evacuator->overwrite_dir));
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
    DDbg(dbg_ctl_cache_evac, "evacuateDocReadDone key: %X earliest: %X", ek->key.slice32(0), ek->earliest_key.slice32(0));
    b->f.unused = 87;
  }
  // if the tag in the c->dir does match the first_key in the
  // document, then it has to be the earliest fragment. We guarantee that
  // the first_key and the earliest_key will never collide (see
  // Cache::open_write).
  if (!dir_head(&b->dir) || !dir_compare_tag(&b->dir, &doc->first_key)) {
    next_CacheKey(&next_key, &doc->key);
    ink_assert(this->mutex->thread_holding == this_ethread());
    evacuate_fragments(&next_key, &doc_evacuator->earliest_key, !b->readers, this);
  }
  return evacuateWrite(doc_evacuator, event, e);
Ldone:
  free_CacheEvacuateDocVC(doc_evacuator);
  doc_evacuator = nullptr;
  return aggWrite(event, e);
}

static int
evacuate_fragments(CacheKey *key, CacheKey *earliest_key, int force, StripeSM *stripe)
{
  Dir dir, *last_collision = nullptr;
  int i = 0;
  while (dir_probe(key, stripe, &dir, &last_collision)) {
    // next fragment cannot be a head...if it is, it must have been a
    // directory collision.
    if (dir_head(&dir)) {
      continue;
    }
    EvacuationBlock *b = stripe->get_preserved_dirs().find(dir);
    if (!b) {
      b                          = new_EvacuationBlock();
      b->dir                     = dir;
      b->evac_frags.key          = *key;
      b->evac_frags.earliest_key = *earliest_key;
      stripe->get_evac_bucket(dir_evac_bucket(&dir)).push(b);
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
    DDbg(dbg_ctl_cache_evac, "next fragment %X Earliest: %X offset %" PRId64 " phase %d force %d", key->slice32(0),
         earliest_key->slice32(0), dir_offset(&dir), dir_phase(&dir), force);
  }
  return i;
}

int
StripeSM::evacuateWrite(CacheEvacuateDocVC *evacuator, int event, Event *e)
{
  // push to front of aggregation write list, so it is written first

  evacuator->agg_len = round_to_approx_size((reinterpret_cast<Doc *>(evacuator->buf->data()))->len);
  this->_write_buffer.add_bytes_pending_aggregation(evacuator->agg_len);
  /* insert the evacuator after all the other evacuators */
  CacheVC *cur   = static_cast<CacheVC *>(this->_write_buffer.get_pending_writers().head);
  CacheVC *after = nullptr;
  for (; cur && cur->f.evacuator; cur = static_cast<CacheVC *>(cur->link.next)) {
    after = cur;
  }
  ink_assert(evacuator->agg_len <= AGG_SIZE);
  this->_write_buffer.get_pending_writers().insert(evacuator, after);
  return aggWrite(event, e);
}

bool
StripeSM::add_writer(CacheVC *vc)
{
  ink_assert(vc);
  this->_write_buffer.add_bytes_pending_aggregation(vc->agg_len);
  // An extra AGG_SIZE is added to the backlog here, but not in
  // open_write, at the time I'm writing this comment. I venture to
  // guess that because the stripe lock may be released between
  // open_write and add_writer (I have checked this), the number of
  // bytes pending aggregation lags and is inaccurate. Therefore the
  // check in open_write is too permissive, and once we get to add_writer
  // and update our bytes pending, we may discover we have more backlog
  // than we thought we did. The solution to the problem was to permit
  // an aggregation buffer extra of backlog here. That's my analysis.
  bool agg_error =
    (vc->agg_len > AGG_SIZE || vc->header_len + sizeof(Doc) > MAX_FRAG_SIZE ||
     (!vc->f.readers && (this->_write_buffer.get_bytes_pending_aggregation() > cache_config_agg_write_backlog + AGG_SIZE) &&
      vc->write_len));
#ifdef CACHE_AGG_FAIL_RATE
  agg_error = agg_error || ((uint32_t)vc->mutex->thread_holding->generator.random() < (uint32_t)(UINT_MAX * CACHE_AGG_FAIL_RATE));
#endif

  if (agg_error) {
    this->_write_buffer.add_bytes_pending_aggregation(-vc->agg_len);
  } else {
    ink_assert(vc->agg_len <= AGG_SIZE);
    if (vc->f.evac_vector) {
      this->get_pending_writers().push(vc);
    } else {
      this->get_pending_writers().enqueue(vc);
    }
  }

  return !agg_error;
}

void
StripeSM::shutdown(EThread *shutdown_thread)
{
  // the process is going down, do a blocking call
  // dont release the volume's lock, there could
  // be another aggWrite in progress
  MUTEX_TAKE_LOCK(this->mutex, shutdown_thread);

  if (DISK_BAD(this->disk)) {
    Dbg(dbg_ctl_cache_dir_sync, "Dir %s: ignoring -- bad disk", this->hash_text.get());
    return;
  }
  size_t dirlen = this->dirlen();
  ink_assert(dirlen > 0); // make clang happy - if not > 0 the vol is seriously messed up
  if (!this->header->dirty && !this->dir_sync_in_progress) {
    Dbg(dbg_ctl_cache_dir_sync, "Dir %s: ignoring -- not dirty", this->hash_text.get());
    return;
  }
  // recompute hit_evacuate_window
  this->hit_evacuate_window = (this->data_blocks * cache_config_hit_evacuate_percent) / 100;

  // check if we have data in the agg buffer
  // dont worry about the cachevc s in the agg queue
  // directories have not been inserted for these writes
  if (!this->_write_buffer.is_empty()) {
    Dbg(dbg_ctl_cache_dir_sync, "Dir %s: flushing agg buffer first", this->hash_text.get());
    this->flush_aggregate_write_buffer(this->fd);
  }

  // We already asserted that dirlen > 0.
  if (!this->dir_sync_in_progress) {
    this->header->sync_serial++;
  } else {
    Dbg(dbg_ctl_cache_dir_sync, "Periodic dir sync in progress -- overwriting");
  }
  this->footer->sync_serial = this->header->sync_serial;

  CHECK_DIR(d);
  size_t B     = this->header->sync_serial & 1;
  off_t  start = this->skip + (B ? dirlen : 0);
  B            = pwrite(this->fd, this->raw_dir, dirlen, start);
  ink_assert(B == dirlen);
  Dbg(dbg_ctl_cache_dir_sync, "done syncing dir for vol %s", this->hash_text.get());
}

// Returns 0 on success or a positive error code on failure
int
StripeSM::open_write(CacheVC *cont, int allow_if_writers, int max_writers)
{
  StripeSM *stripe    = this;
  bool      agg_error = false;
  if (!cont->f.remove) {
    agg_error = (!cont->f.update && this->_write_buffer.get_bytes_pending_aggregation() > cache_config_agg_write_backlog);
#ifdef CACHE_AGG_FAIL_RATE
    agg_error = agg_error || ((uint32_t)mutex->thread_holding->generator.random() < (uint32_t)(UINT_MAX * CACHE_AGG_FAIL_RATE));
#endif
  }

  if (agg_error) {
    Metrics::Counter::increment(cache_rsb.write_backlog_failure);
    Metrics::Counter::increment(stripe->cache_vol->vol_rsb.write_backlog_failure);

    return ECACHE_WRITE_FAIL;
  }

  if (open_dir.open_write(cont, allow_if_writers, max_writers)) {
    return 0;
  }
  return ECACHE_DOC_BUSY;
}

int
StripeSM::open_write_lock(CacheVC *cont, int allow_if_writers, int max_writers)
{
  EThread *t = cont->mutex->thread_holding;
  CACHE_TRY_LOCK(lock, mutex, t);
  if (!lock.is_locked()) {
    return -1;
  }
  return open_write(cont, allow_if_writers, max_writers);
}

int
StripeSM::close_write(CacheVC *cont)
{
  return open_dir.close_write(cont);
}

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
#include "P_CacheDisk.h"
#include "P_CacheInternal.h"
#include "P_CacheVol.h"

#include "tscore/hugepages.h"

namespace
{

DbgCtl dbg_ctl_cache_init{"cache_init"};

// This is the oldest version number that is still usable.
short int const CACHE_DB_MAJOR_VERSION_COMPATIBLE = 21;

void
vol_init_data_internal(Stripe *vol)
{
  // step1: calculate the number of entries.
  off_t total_entries = (vol->len - (vol->start - vol->skip)) / cache_config_min_average_object_size;
  // step2: calculate the number of buckets
  off_t total_buckets = total_entries / DIR_DEPTH;
  // step3: calculate the number of segments, no segment has more than 16384 buckets
  vol->segments = (total_buckets + (((1 << 16) - 1) / DIR_DEPTH)) / ((1 << 16) / DIR_DEPTH);
  // step4: divide total_buckets into segments on average.
  vol->buckets = (total_buckets + vol->segments - 1) / vol->segments;
  // step5: set the start pointer.
  vol->start = vol->skip + 2 * vol->dirlen();
}

void
vol_init_data(Stripe *vol)
{
  // iteratively calculate start + buckets
  vol_init_data_internal(vol);
  vol_init_data_internal(vol);
  vol_init_data_internal(vol);
}

void
vol_init_dir(Stripe *vol)
{
  int b, s, l;

  for (s = 0; s < vol->segments; s++) {
    vol->header->freelist[s] = 0;
    Dir *seg                 = vol->dir_segment(s);
    for (l = 1; l < DIR_DEPTH; l++) {
      for (b = 0; b < vol->buckets; b++) {
        Dir *bucket = dir_bucket(b, seg);
        dir_free_entry(dir_bucket_row(bucket, l), s, vol);
      }
    }
  }
}

void
vol_clear_init(Stripe *vol)
{
  size_t dir_len = vol->dirlen();
  memset(vol->raw_dir, 0, dir_len);
  vol_init_dir(vol);
  vol->header->magic          = VOL_MAGIC;
  vol->header->version._major = CACHE_DB_MAJOR_VERSION;
  vol->header->version._minor = CACHE_DB_MINOR_VERSION;
  vol->scan_pos = vol->header->agg_pos = vol->header->write_pos = vol->start;
  vol->header->last_write_pos                                   = vol->header->write_pos;
  vol->header->phase                                            = 0;
  vol->header->cycle                                            = 0;
  vol->header->create_time                                      = time(nullptr);
  vol->header->dirty                                            = 0;
  vol->sector_size = vol->header->sector_size = vol->disk->hw_sector_size;
  *vol->footer                                = *vol->header;
}

int
compare_ushort(void const *a, void const *b)
{
  return *static_cast<unsigned short const *>(a) - *static_cast<unsigned short const *>(b);
}

} // namespace

int
vol_dir_clear(Stripe *d)
{
  size_t dir_len = d->dirlen();
  vol_clear_init(d);

  if (pwrite(d->fd, d->raw_dir, dir_len, d->skip) < 0) {
    Warning("unable to clear cache directory '%s'", d->hash_text.get());
    return -1;
  }
  return 0;
}

struct StripeInitInfo {
  off_t recover_pos;
  AIOCallbackInternal vol_aio[4];
  char *vol_h_f;

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

////
// Vol
//

int
Stripe::begin_read(CacheVC *cont) const
{
  ink_assert(cont->mutex->thread_holding == this_ethread());
  ink_assert(mutex->thread_holding == this_ethread());
#ifdef CACHE_STAT_PAGES
  ink_assert(!cont->stat_link.next && !cont->stat_link.prev);
  stat_cache_vcs.enqueue(cont, cont->stat_link);
#endif
  // no need for evacuation as the entire document is already in memory
  if (cont->f.single_fragment) {
    return 0;
  }
  int i = dir_evac_bucket(&cont->earliest_dir);
  EvacuationBlock *b;
  for (b = evacuate[i].head; b; b = b->link.next) {
    if (dir_offset(&b->dir) != dir_offset(&cont->earliest_dir)) {
      continue;
    }
    if (b->readers) {
      b->readers = b->readers + 1;
    }
    return 0;
  }
  // we don't actually need to preserve this block as it is already in
  // memory, but this is easier, and evacuations are rare
  EThread *t        = cont->mutex->thread_holding;
  b                 = new_EvacuationBlock(t);
  b->readers        = 1;
  b->dir            = cont->earliest_dir;
  b->evac_frags.key = cont->earliest_key;
  evacuate[i].push(b);
  return 1;
}

int
Stripe::close_read(CacheVC *cont) const
{
  EThread *t = cont->mutex->thread_holding;
  ink_assert(t == this_ethread());
  ink_assert(t == mutex->thread_holding);
  if (dir_is_empty(&cont->earliest_dir)) {
    return 1;
  }
  int i = dir_evac_bucket(&cont->earliest_dir);
  EvacuationBlock *b;
  for (b = evacuate[i].head; b;) {
    EvacuationBlock *next = b->link.next;
    if (dir_offset(&b->dir) != dir_offset(&cont->earliest_dir)) {
      b = next;
      continue;
    }
    if (b->readers && !--b->readers) {
      evacuate[i].remove(b);
      free_EvacuationBlock(b, t);
      break;
    }
    b = next;
  }
#ifdef CACHE_STAT_PAGES
  stat_cache_vcs.remove(cont, cont->stat_link);
  ink_assert(!cont->stat_link.next && !cont->stat_link.prev);
#endif
  return 1;
}

int
Stripe::clear_dir()
{
  size_t dir_len = this->dirlen();
  vol_clear_init(this);

  SET_HANDLER(&Stripe::handle_dir_clear);

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
Stripe::init(char *s, off_t blocks, off_t dir_skip, bool clear)
{
  char *seed_str              = disk->hash_base_string ? disk->hash_base_string : s;
  const size_t hash_seed_size = strlen(seed_str);
  const size_t hash_text_size = hash_seed_size + 32;

  hash_text = static_cast<char *>(ats_malloc(hash_text_size));
  ink_strlcpy(hash_text, seed_str, hash_text_size);
  snprintf(hash_text + hash_seed_size, (hash_text_size - hash_seed_size), " %" PRIu64 ":%" PRIu64 "",
           static_cast<uint64_t>(dir_skip), static_cast<uint64_t>(blocks));
  CryptoContext().hash_immediate(hash_id, hash_text, strlen(hash_text));

  dir_skip = ROUND_TO_STORE_BLOCK((dir_skip < START_POS ? START_POS : dir_skip));
  path     = ats_strdup(s);
  len      = blocks * STORE_BLOCK_SIZE;
  ink_assert(len <= MAX_VOL_SIZE);
  skip             = dir_skip;
  prev_recover_pos = 0;

  // successive approximation, directory/meta data eats up some storage
  start = dir_skip;
  vol_init_data(this);
  data_blocks         = (len - (start - skip)) / STORE_BLOCK_SIZE;
  hit_evacuate_window = (data_blocks * cache_config_hit_evacuate_percent) / 100;

  evacuate_size = static_cast<int>(len / EVACUATION_BUCKET_SIZE) + 2;
  int evac_len  = evacuate_size * sizeof(DLL<EvacuationBlock>);
  evacuate      = static_cast<DLL<EvacuationBlock> *>(ats_malloc(evac_len));
  memset(static_cast<void *>(evacuate), 0, evac_len);

  Dbg(dbg_ctl_cache_init, "Vol %s: allocating %zu directory bytes for a %lld byte volume (%lf%%)", hash_text.get(), dirlen(),
      (long long)this->len, (double)dirlen() / (double)this->len * 100.0);

  raw_dir = nullptr;
  if (ats_hugepage_enabled()) {
    raw_dir = static_cast<char *>(ats_alloc_hugepage(this->dirlen()));
  }
  if (raw_dir == nullptr) {
    raw_dir = static_cast<char *>(ats_memalign(ats_pagesize(), this->dirlen()));
  }

  dir    = reinterpret_cast<Dir *>(raw_dir + this->headerlen());
  header = reinterpret_cast<StripteHeaderFooter *>(raw_dir);
  footer = reinterpret_cast<StripteHeaderFooter *>(raw_dir + this->dirlen() - ROUND_TO_STORE_BLOCK(sizeof(StripteHeaderFooter)));

  if (clear) {
    Note("clearing cache directory '%s'", hash_text.get());
    return clear_dir();
  }

  init_info           = new StripeInitInfo();
  int footerlen       = ROUND_TO_STORE_BLOCK(sizeof(StripteHeaderFooter));
  off_t footer_offset = this->dirlen() - footerlen;
  // try A
  off_t as = skip;

  Dbg(dbg_ctl_cache_init, "reading directory '%s'", hash_text.get());
  SET_HANDLER(&Stripe::handle_header_read);
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
Stripe::handle_dir_clear(int event, void *data)
{
  size_t dir_len = this->dirlen();
  AIOCallback *op;

  if (event == AIO_EVENT_DONE) {
    op = static_cast<AIOCallback *>(data);
    if (!op->ok()) {
      Warning("unable to clear cache directory '%s'", hash_text.get());
      disk->incrErrors(op);
      fd = -1;
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
    SET_HANDLER(&Stripe::dir_init_done);
    dir_init_done(EVENT_IMMEDIATE, nullptr);
    /* mark the volume as bad */
  }
  return EVENT_DONE;
}

int
Stripe::handle_dir_read(int event, void *data)
{
  AIOCallback *op = static_cast<AIOCallback *>(data);

  if (event == AIO_EVENT_DONE) {
    if (!op->ok()) {
      Note("Directory read failed: clearing cache directory %s", this->hash_text.get());
      clear_dir();
      return EVENT_DONE;
    }
  }

  if (!(header->magic == VOL_MAGIC && footer->magic == VOL_MAGIC && CACHE_DB_MAJOR_VERSION_COMPATIBLE <= header->version._major &&
        header->version._major <= CACHE_DB_MAJOR_VERSION)) {
    Warning("bad footer in cache directory for '%s', clearing", hash_text.get());
    Note("VOL_MAGIC %d\n header magic: %d\n footer_magic %d\n CACHE_DB_MAJOR_VERSION_COMPATIBLE %d\n major version %d\n"
         "CACHE_DB_MAJOR_VERSION %d\n",
         VOL_MAGIC, header->magic, footer->magic, CACHE_DB_MAJOR_VERSION_COMPATIBLE, header->version._major,
         CACHE_DB_MAJOR_VERSION);
    Note("clearing cache directory '%s'", hash_text.get());
    clear_dir();
    return EVENT_DONE;
  }
  CHECK_DIR(this);

  sector_size = header->sector_size;

  return this->recover_data();
}

int
Stripe::recover_data()
{
  SET_HANDLER(&Stripe::handle_recover_from_data);
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
Stripe::handle_recover_from_data(int event, void * /* data ATS_UNUSED */)
{
  uint32_t got_len         = 0;
  uint32_t max_sync_serial = header->sync_serial;
  char *s, *e = nullptr;
  if (event == EVENT_IMMEDIATE) {
    if (header->sync_serial == 0) {
      io.aiocb.aio_buf = nullptr;
      SET_HANDLER(&Stripe::handle_recover_write_dir);
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
    SET_HANDLER(&Stripe::handle_recover_write_dir);
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
  int footerlen = ROUND_TO_STORE_BLOCK(sizeof(StripteHeaderFooter));
  size_t dirlen = this->dirlen();
  int B         = header->sync_serial & 1;
  off_t ss      = skip + (B ? dirlen : 0);

  init_info->vol_aio[0].aiocb.aio_buf    = raw_dir;
  init_info->vol_aio[0].aiocb.aio_nbytes = footerlen;
  init_info->vol_aio[0].aiocb.aio_offset = ss;
  init_info->vol_aio[1].aiocb.aio_buf    = raw_dir + footerlen;
  init_info->vol_aio[1].aiocb.aio_nbytes = dirlen - 2 * footerlen;
  init_info->vol_aio[1].aiocb.aio_offset = ss + footerlen;
  init_info->vol_aio[2].aiocb.aio_buf    = raw_dir + dirlen - footerlen;
  init_info->vol_aio[2].aiocb.aio_nbytes = footerlen;
  init_info->vol_aio[2].aiocb.aio_offset = ss + dirlen - footerlen;

  SET_HANDLER(&Stripe::handle_recover_write_dir);
  ink_assert(ink_aio_write(init_info->vol_aio));
  return EVENT_CONT;
}

Lclear:
  free(static_cast<char *>(io.aiocb.aio_buf));
  delete init_info;
  init_info = nullptr;
  clear_dir();
  return EVENT_CONT;
}

int
Stripe::handle_recover_write_dir(int /* event ATS_UNUSED */, void * /* data ATS_UNUSED */)
{
  if (io.aiocb.aio_buf) {
    free(static_cast<char *>(io.aiocb.aio_buf));
  }
  delete init_info;
  init_info = nullptr;
  set_io_not_in_progress();
  scan_pos = header->write_pos;
  periodic_scan();
  SET_HANDLER(&Stripe::dir_init_done);
  return dir_init_done(EVENT_IMMEDIATE, nullptr);
}

int
Stripe::handle_header_read(int event, void *data)
{
  AIOCallback *op;
  StripteHeaderFooter *hf[4];
  switch (event) {
  case AIO_EVENT_DONE:
    op = static_cast<AIOCallback *>(data);
    for (auto &i : hf) {
      ink_assert(op != nullptr);
      i = static_cast<StripteHeaderFooter *>(op->aiocb.aio_buf);
      if (!op->ok()) {
        Note("Header read failed: clearing cache directory %s", this->hash_text.get());
        clear_dir();
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
      SET_HANDLER(&Stripe::handle_dir_read);
      if (dbg_ctl_cache_init.on()) {
        Note("using directory A for '%s'", hash_text.get());
      }
      io.aiocb.aio_offset = skip;
      ink_assert(ink_aio_read(&io));
    }
    // try B
    else if (hf[2]->sync_serial == hf[3]->sync_serial) {
      SET_HANDLER(&Stripe::handle_dir_read);
      if (dbg_ctl_cache_init.on()) {
        Note("using directory B for '%s'", hash_text.get());
      }
      io.aiocb.aio_offset = skip + this->dirlen();
      ink_assert(ink_aio_read(&io));
    } else {
      Note("no good directory, clearing '%s' since sync_serials on both A and B copies are invalid", hash_text.get());
      Note("Header A: %d\nFooter A: %d\n Header B: %d\n Footer B %d\n", hf[0]->sync_serial, hf[1]->sync_serial, hf[2]->sync_serial,
           hf[3]->sync_serial);
      clear_dir();
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
Stripe::dir_init_done(int /* event ATS_UNUSED */, void * /* data ATS_UNUSED */)
{
  if (!cache->cache_read_done) {
    eventProcessor.schedule_in(this, HRTIME_MSECONDS(5), ET_CALL);
    return EVENT_CONT;
  } else {
    int vol_no = gnvol++;
    ink_assert(!gvol[vol_no]);
    gvol[vol_no] = this;
    SET_HANDLER(&Stripe::aggWrite);
    cache->vol_initialized(fd != -1);
    return EVENT_DONE;
  }
}

int
Stripe::dir_check(bool /* fix ATS_UNUSED */) // TODO: we should eliminate this parameter ?
{
  static int const SEGMENT_HISTOGRAM_WIDTH = 16;
  int hist[SEGMENT_HISTOGRAM_WIDTH + 1]    = {0};
  unsigned short chain_tag[MAX_ENTRIES_PER_SEGMENT];
  int32_t chain_mark[MAX_ENTRIES_PER_SEGMENT];
  uint64_t total_buckets = buckets * segments;
  uint64_t total_entries = total_buckets * DIR_DEPTH;
  int frag_demographics[1 << DIR_SIZE_WIDTH][DIR_BLOCK_SIZES];

  int j;
  int stale = 0, in_use = 0, empty = 0;
  int free = 0, head = 0, buckets_in_use = 0;

  int max_chain_length = 0;
  int64_t bytes_in_use = 0;

  ink_zero(frag_demographics);

  printf("Stripe '[%s]'\n", hash_text.get());
  printf("  Directory Bytes: %" PRIu64 "\n", total_buckets * SIZEOF_DIR);
  printf("  Segments:  %d\n", segments);
  printf("  Buckets per segment:   %" PRIu64 "\n", buckets);
  printf("  Entries:   %" PRIu64 "\n", total_entries);

  for (int s = 0; s < segments; s++) {
    Dir *seg               = this->dir_segment(s);
    int seg_chain_max      = 0;
    int seg_empty          = 0;
    int seg_in_use         = 0;
    int seg_stale          = 0;
    int seg_bytes_in_use   = 0;
    int seg_dups           = 0;
    int seg_buckets_in_use = 0;

    ink_zero(chain_tag);
    memset(chain_mark, -1, sizeof(chain_mark));

    for (int b = 0; b < buckets; b++) {
      Dir *root = dir_bucket(b, seg);
      int h     = 0; // chain length starting in this bucket

      // Walk the chain starting in this bucket
      int chain_idx = 0;
      int mark      = 0;
      ++seg_buckets_in_use;
      for (Dir *e = root; e; e = next_dir(e, seg)) {
        if (!dir_offset(e)) {
          ++seg_empty;
          --seg_buckets_in_use;
          // this should only happen on the first dir in a bucket
          ink_assert(nullptr == next_dir(e, seg));
          break;
        } else {
          int e_idx = e - seg;
          ++h;
          chain_tag[chain_idx++] = dir_tag(e);
          if (chain_mark[e_idx] == mark) {
            printf("    - Cycle of length %d detected for bucket %d\n", h, b);
          } else if (chain_mark[e_idx] >= 0) {
            printf("    - Entry %d is in chain %d and %d", e_idx, chain_mark[e_idx], mark);
          } else {
            chain_mark[e_idx] = mark;
          }

          if (!dir_valid(this, e)) {
            ++seg_stale;
          } else {
            uint64_t size = dir_approx_size(e);
            if (dir_head(e)) {
              ++head;
            }
            ++seg_in_use;
            seg_bytes_in_use += size;
            ++frag_demographics[dir_size(e)][dir_big(e)];
          }
        }
      }

      // Check for duplicates (identical tags in the same bucket).
      if (h > 1) {
        unsigned short last;
        qsort(chain_tag, h, sizeof(chain_tag[0]), &compare_ushort);
        last = chain_tag[0];
        for (int k = 1; k < h; ++k) {
          if (last == chain_tag[k]) {
            ++seg_dups;
          }
          last = chain_tag[k];
        }
      }

      ++hist[std::min(h, SEGMENT_HISTOGRAM_WIDTH)];
      seg_chain_max = std::max(seg_chain_max, h);
    }
    int fl_size       = dir_freelist_length(this, s);
    in_use           += seg_in_use;
    empty            += seg_empty;
    stale            += seg_stale;
    free             += fl_size;
    buckets_in_use   += seg_buckets_in_use;
    max_chain_length  = std::max(max_chain_length, seg_chain_max);
    bytes_in_use     += seg_bytes_in_use;

    printf("  - Segment-%d | Entries: used=%d stale=%d free=%d disk-bytes=%d Buckets: used=%d empty=%d max=%d avg=%.2f dups=%d\n",
           s, seg_in_use, seg_stale, fl_size, seg_bytes_in_use, seg_buckets_in_use, seg_empty, seg_chain_max,
           seg_buckets_in_use ? static_cast<float>(seg_in_use + seg_stale) / seg_buckets_in_use : 0.0, seg_dups);
  }

  printf("  - Stripe | Entries: in-use=%d stale=%d free=%d Buckets: empty=%d max=%d avg=%.2f\n", in_use, stale, free, empty,
         max_chain_length, buckets_in_use ? static_cast<float>(in_use + stale) / buckets_in_use : 0);

  printf("    Chain lengths:  ");
  for (j = 0; j < SEGMENT_HISTOGRAM_WIDTH; ++j) {
    printf(" %d=%d ", j, hist[j]);
  }
  printf(" %d>=%d\n", SEGMENT_HISTOGRAM_WIDTH, hist[SEGMENT_HISTOGRAM_WIDTH]);

  char tt[256];
  printf("    Total Size:      %" PRIu64 "\n", static_cast<uint64_t>(len));
  printf("    Bytes in Use:    %" PRIu64 " [%0.2f%%]\n", bytes_in_use, 100.0 * (static_cast<float>(bytes_in_use) / len));
  printf("    Objects:         %d\n", head);
  printf("    Average Size:    %" PRIu64 "\n", head ? (bytes_in_use / head) : 0);
  printf("    Average Frags:   %.2f\n", head ? static_cast<float>(in_use) / head : 0);
  printf("    Write Position:  %" PRIu64 "\n", header->write_pos - start);
  printf("    Wrap Count:      %d\n", header->cycle);
  printf("    Phase:           %s\n", header->phase ? "true" : "false");
  ink_ctime_r(&header->create_time, tt);
  tt[strlen(tt) - 1] = 0;
  printf("    Sync Serial:     %u\n", header->sync_serial);
  printf("    Write Serial:    %u\n", header->write_serial);
  printf("    Create Time:     %s\n", tt);
  printf("\n");
  printf("  Fragment size demographics\n");
  for (int b = 0; b < DIR_BLOCK_SIZES; ++b) {
    int block_size = DIR_BLOCK_SIZE(b);
    int s          = 0;
    while (s < 1 << DIR_SIZE_WIDTH) {
      for (int j = 0; j < 8; ++j, ++s) {
        // The size markings are redundant. Low values (less than DIR_SHIFT_WIDTH) for larger
        // base block sizes should never be used. Such entries should use the next smaller base block size.
        if (b > 0 && s < 1 << DIR_BLOCK_SHIFT(1)) {
          ink_assert(frag_demographics[s][b] == 0);
          continue;
        }
        printf(" %8d[%2d:%1d]:%06d", (s + 1) * block_size, s, b, frag_demographics[s][b]);
      }
      printf("\n");
    }
  }
  printf("\n");

  return 0;
}

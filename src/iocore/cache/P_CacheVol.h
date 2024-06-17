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

#pragma once

#include "P_CacheDir.h"
#include "P_CacheDoc.h"
#include "P_CacheStats.h"
#include "P_RamCache.h"
#include "AggregateWriteBuffer.h"

#include "iocore/eventsystem/EThread.h"

#include "tscore/CryptoHash.h"

#include <atomic>

#define CACHE_BLOCK_SHIFT        9
#define CACHE_BLOCK_SIZE         (1 << CACHE_BLOCK_SHIFT) // 512, smallest sector size
#define ROUND_TO_STORE_BLOCK(_x) INK_ALIGN((_x), STORE_BLOCK_SIZE)
#define ROUND_TO_CACHE_BLOCK(_x) INK_ALIGN((_x), CACHE_BLOCK_SIZE)
#define ROUND_TO_SECTOR(_p, _x)  INK_ALIGN((_x), _p->sector_size)
#define ROUND_TO(_x, _y)         INK_ALIGN((_x), (_y))

// Stripe
#define STRIPE_MAGIC                 0xF1D0F00D
#define START_BLOCKS                 16 // 8k, STORE_BLOCK_SIZE
#define START_POS                    ((off_t)START_BLOCKS * CACHE_BLOCK_SIZE)
#define EVACUATION_SIZE              (2 * AGG_SIZE)      // 8MB
#define STRIPE_BLOCK_SIZE            (1024 * 1024 * 128) // 128MB
#define MIN_STRIPE_SIZE              STRIPE_BLOCK_SIZE
#define MAX_STRIPE_SIZE              ((off_t)512 * 1024 * 1024 * 1024 * 1024) // 512TB
#define MAX_FRAG_SIZE                (AGG_SIZE - sizeof(Doc))                 // true max
#define LEAVE_FREE                   DEFAULT_MAX_BUFFER_SIZE
#define PIN_SCAN_EVERY               16 // scan every 1/16 of disk
#define STRIPE_HASH_TABLE_SIZE       32707
#define STRIPE_HASH_EMPTY            0xFFFF
#define STRIPE_HASH_ALLOC_SIZE       (8 * 1024 * 1024) // one chance per this unit
#define LOOKASIDE_SIZE               256
#define EVACUATION_BUCKET_SIZE       (2 * EVACUATION_SIZE) // 16MB
#define RECOVERY_SIZE                EVACUATION_SIZE       // 8MB
#define AIO_NOT_IN_PROGRESS          -1
#define AIO_AGG_WRITE_IN_PROGRESS    -2
#define AUTO_SIZE_RAM_CACHE          -1                      // 1-1 with directory size
#define DEFAULT_TARGET_FRAGMENT_SIZE (1048576 - sizeof(Doc)) // 1MB
#define STORE_BLOCKS_PER_STRIPE      (STRIPE_BLOCK_SIZE / STORE_BLOCK_SIZE)

#define dir_offset_evac_bucket(_o) (_o / (EVACUATION_BUCKET_SIZE / CACHE_BLOCK_SIZE))
#define dir_evac_bucket(_e)        dir_offset_evac_bucket(dir_offset(_e))
#define offset_evac_bucket(_d, _o) \
  dir_offset_evac_bucket((_d->offset_to_vol_offset(_o)

// Documents

struct Cache;
class Stripe;
struct CacheDisk;
struct StripeInitInfo;
struct DiskStripe;
struct CacheVol;
class CacheEvacuateDocVC;

struct StripteHeaderFooter {
  unsigned int      magic;
  ts::VersionNumber version;
  time_t            create_time;
  off_t             write_pos;
  off_t             last_write_pos;
  off_t             agg_pos;
  uint32_t          generation; // token generation (vary), this cannot be 0
  uint32_t          phase;
  uint32_t          cycle;
  uint32_t          sync_serial;
  uint32_t          write_serial;
  uint32_t          dirty;
  uint32_t          sector_size;
  uint32_t          unused; // pad out to 8 byte boundary
  uint16_t          freelist[1];
};

// Key and Earliest key for each fragment that needs to be evacuated
struct EvacuationKey {
  SLink<EvacuationKey> link;
  CryptoHash           key;
  CryptoHash           earliest_key;
};

struct EvacuationBlock {
  union {
    unsigned int init;
    struct {
      unsigned int done          : 1; // has been evacuated
      unsigned int pinned        : 1; // check pinning timeout
      unsigned int evacuate_head : 1; // check pinning timeout
      unsigned int unused        : 29;
    } f;
  };

  int readers;
  Dir dir;
  Dir new_dir;
  // we need to have a list of evacuationkeys because of collision.
  EvacuationKey       evac_frags;
  CacheEvacuateDocVC *earliest_evacuator;
  LINK(EvacuationBlock, link);
};

class Stripe : public Continuation
{
public:
  char          *path = nullptr;
  ats_scoped_str hash_text;
  CryptoHash     hash_id;
  int            fd = -1;

  char                *raw_dir             = nullptr;
  Dir                 *dir                 = nullptr;
  StripteHeaderFooter *header              = nullptr;
  StripteHeaderFooter *footer              = nullptr;
  int                  segments            = 0;
  off_t                buckets             = 0;
  off_t                recover_pos         = 0;
  off_t                prev_recover_pos    = 0;
  off_t                scan_pos            = 0;
  off_t                skip                = 0; // start of headers
  off_t                start               = 0; // start of data
  off_t                len                 = 0;
  off_t                data_blocks         = 0;
  int                  hit_evacuate_window = 0;
  AIOCallbackInternal  io;

  Queue<CacheVC, Continuation::Link_link> sync;

  Event *trigger = nullptr;

  OpenDir               open_dir;
  RamCache             *ram_cache     = nullptr;
  int                   evacuate_size = 0;
  DLL<EvacuationBlock> *evacuate      = nullptr;
  DLL<EvacuationBlock>  lookaside[LOOKASIDE_SIZE];
  CacheEvacuateDocVC   *doc_evacuator = nullptr;

  StripeInitInfo *init_info = nullptr;

  CacheDisk *disk                 = nullptr;
  Cache     *cache                = nullptr;
  CacheVol  *cache_vol            = nullptr;
  uint32_t   last_sync_serial     = 0;
  uint32_t   last_write_serial    = 0;
  uint32_t   sector_size          = 0;
  bool       recover_wrapped      = false;
  bool       dir_sync_waiting     = false;
  bool       dir_sync_in_progress = false;
  bool       writing_end_marker   = false;

  CacheKey          first_fragment_key;
  int64_t           first_fragment_offset = 0;
  Ptr<IOBufferData> first_fragment_data;

  void cancel_trigger();

  int recover_data();

  int open_write(CacheVC *cont, int allow_if_writers, int max_writers);
  int open_write_lock(CacheVC *cont, int allow_if_writers, int max_writers);
  int close_write(CacheVC *cont);
  int close_write_lock(CacheVC *cont);
  int begin_read(CacheVC *cont) const;
  int begin_read_lock(CacheVC *cont);
  // unused read-write interlock code
  // currently http handles a write-lock failure by retrying the read
  OpenDirEntry *open_read(const CryptoHash *key) const;
  OpenDirEntry *open_read_lock(CryptoHash *key, EThread *t);
  int           close_read(CacheVC *cont) const;
  int           close_read_lock(CacheVC *cont);

  int clear_dir_aio();
  int clear_dir();

  int init(char *s, off_t blocks, off_t dir_skip, bool clear);

  int handle_dir_clear(int event, void *data);
  int handle_dir_read(int event, void *data);
  int handle_recover_from_data(int event, void *data);
  int handle_recover_write_dir(int event, void *data);
  int handle_header_read(int event, void *data);

  int dir_init_done(int event, void *data);

  int dir_check(bool fix);

  bool evac_bucket_valid(off_t bucket) const;

  int  is_io_in_progress() const;
  void set_io_not_in_progress();

  int aggWriteDone(int event, Event *e);
  int aggWrite(int event, void *e);

  /**
   * Copies virtual connection buffers into the aggregate write buffer.
   *
   * Pending write data will only be copied while space remains in the aggregate
   * write buffer. The copy will stop at the first pending write that does
   * not fit in the remaining space. Note that the total size of each pending
   * write must not be greater than the total aggregate write buffer size.
   *
   * After each virtual connection's buffer is successfully copied, it will
   * receive mutually-exclusive post-handling based on the connection type:
   *
   *     - sync (only if CacheVC::f.use_first_key): inserted into sync queue
   *     - evacuator: handler invoked - probably evacuateDocDone
   *     - otherwise: inserted into tocall for handler to be scheduled later
   *
   * @param tocall Out parameter; a queue of virtual connections with handlers that need to
   *     invoked at the end of aggWrite.
   * @see aggWrite
   */
  void aggregate_pending_writes(Queue<CacheVC, Continuation::Link_link> &tocall);
  void agg_wrap();

  int evacuateWrite(CacheEvacuateDocVC *evacuator, int event, Event *e);
  int evacuateDocReadDone(int event, Event *e);
  int evacuateDoc(int event, Event *e);

  int              evac_range(off_t start, off_t end, int evac_phase);
  void             periodic_scan();
  void             scan_for_pinned_documents();
  void             evacuate_cleanup_blocks(int i);
  void             evacuate_cleanup();
  EvacuationBlock *force_evacuate_head(Dir const *dir, int pinned);
  int              within_hit_evacuate_window(Dir const *dir) const;
  uint32_t         round_to_approx_size(uint32_t l) const;

  // inline functions
  int    headerlen() const;        // calculates the total length of the vol header and the freelist
  int    direntries() const;       // total number of dir entries
  Dir   *dir_segment(int s) const; // returns the first dir in the segment s
  size_t dirlen() const;           // calculates the total length of header, directories and footer
  int    vol_out_of_phase_valid(Dir const *e) const;

  int vol_out_of_phase_agg_valid(Dir const *e) const;
  int vol_out_of_phase_write_valid(Dir const *e) const;
  int vol_in_phase_valid(Dir const *e) const;
  int vol_in_phase_agg_buf_valid(Dir const *e) const;

  off_t vol_offset(Dir const *e) const;
  off_t offset_to_vol_offset(off_t pos) const;
  off_t vol_offset_to_offset(off_t pos) const;
  off_t vol_relative_length(off_t start_offset) const;

  Stripe() : Continuation(new_ProxyMutex())
  {
    open_dir.mutex = mutex;
    SET_HANDLER(&Stripe::aggWrite);
  }

  Queue<CacheVC, Continuation::Link_link> &get_pending_writers();
  int                                      get_agg_buf_pos() const;

  /**
   * Add a virtual connection waiting to write to this stripe.
   *
   * If vc->f.evac_vector is set, it will be queued before any regular writes.
   *
   * This operation may fail for any one of the following reasons:
   *   - The write would overflow the internal aggregation buffer.
   *   - Adding a Doc to the virtual connection header would exceed the
   *       maximum fragment size.
   *   - vc->f.readers is not set (this virtual connection is not an evacuator),
   *       is full, the writes waiting to be aggregated exceed the maximum
   *       backlog plus the space in the aggregatation buffer, and the virtual
   *       connection has a non-zero write length.
   *
   * @param vc: The virtual connection.
   * @return: Returns true if the operation was successfull, otherwise false.
   */
  bool add_writer(CacheVC *vc);

  /**
   * Sync the stripe meta data to memory for shutdown.
   *
   * This method MUST NOT be called during regular operation. The stripe
   * will be locked for this operation, and will not be unlocked afterwards.
   *
   * The aggregate write buffer will be flushed before copying the stripe to
   * disk. Pending writes will be ignored.
   *
   * @param shutdown_thread The EThread to lock the stripe on.
   */
  void shutdown(EThread *shutdown_thread);

  /**
   * Retrieve a document from the aggregate write buffer.
   *
   * This is used to speed up reads by copying from the in-memory write buffer
   * instead of reading from disk. If the document is not in the write buffer,
   * nothing will be copied.
   *
   * @param dir: The directory entry for the desired document.
   * @param dest: The destination buffer where the document will be copied to.
   * @param nbytes: The size of the document (number of bytes to copy).
   * @return Returns true if the document was copied, false otherwise.
   */
  bool copy_from_aggregate_write_buffer(char *dest, Dir const &dir, size_t nbytes) const;

private:
  void _clear_init();
  void _init_dir();
  void _init_data_internal();
  void _init_data();
  int  _agg_copy(CacheVC *vc);
  int  _copy_writer_to_aggregation(CacheVC *vc);
  int  _copy_evacuator_to_aggregation(CacheVC *vc);
  bool flush_aggregate_write_buffer();

  AggregateWriteBuffer _write_buffer;
};

struct AIO_failure_handler : public Continuation {
  int handle_disk_failure(int event, void *data);

  AIO_failure_handler() : Continuation(new_ProxyMutex()) { SET_HANDLER(&AIO_failure_handler::handle_disk_failure); }
};

struct CacheVol {
  int          vol_number       = -1;
  int          scheme           = 0;
  off_t        size             = 0;
  int          num_vols         = 0;
  bool         ramcache_enabled = true;
  Stripe     **stripes          = nullptr;
  DiskStripe **disk_stripes     = nullptr;
  LINK(CacheVol, link);
  // per volume stats
  CacheStatsBlock vol_rsb;

  CacheVol() {}
};

// Global Data

extern Stripe                        **gstripes;
extern std::atomic<int>                gnstripes;
extern ClassAllocator<OpenDirEntry>    openDirEntryAllocator;
extern ClassAllocator<EvacuationBlock> evacuationBlockAllocator;
extern ClassAllocator<EvacuationKey>   evacuationKeyAllocator;
extern unsigned short                 *vol_hash_table;

// inline Functions

inline int
Stripe::headerlen() const
{
  return ROUND_TO_STORE_BLOCK(sizeof(StripteHeaderFooter) + sizeof(uint16_t) * (this->segments - 1));
}

inline Dir *
Stripe::dir_segment(int s) const
{
  return (Dir *)(((char *)this->dir) + (s * this->buckets) * DIR_DEPTH * SIZEOF_DIR);
}

inline size_t
Stripe::dirlen() const
{
  return this->headerlen() + ROUND_TO_STORE_BLOCK(((size_t)this->buckets) * DIR_DEPTH * this->segments * SIZEOF_DIR) +
         ROUND_TO_STORE_BLOCK(sizeof(StripteHeaderFooter));
}

inline int
Stripe::direntries() const
{
  return this->buckets * DIR_DEPTH * this->segments;
}

inline int
Stripe::vol_out_of_phase_valid(Dir const *e) const
{
  return (dir_offset(e) - 1 >= ((this->header->agg_pos - this->start) / CACHE_BLOCK_SIZE));
}

inline int
Stripe::vol_out_of_phase_agg_valid(Dir const *e) const
{
  return (dir_offset(e) - 1 >= ((this->header->agg_pos - this->start + AGG_SIZE) / CACHE_BLOCK_SIZE));
}

inline int
Stripe::vol_out_of_phase_write_valid(Dir const *e) const
{
  return (dir_offset(e) - 1 >= ((this->header->write_pos - this->start) / CACHE_BLOCK_SIZE));
}

inline int
Stripe::vol_in_phase_valid(Dir const *e) const
{
  return (dir_offset(e) - 1 < ((this->header->write_pos + this->_write_buffer.get_buffer_pos() - this->start) / CACHE_BLOCK_SIZE));
}

inline off_t
Stripe::vol_offset(Dir const *e) const
{
  return this->start + (off_t)dir_offset(e) * CACHE_BLOCK_SIZE - CACHE_BLOCK_SIZE;
}

inline off_t
Stripe::offset_to_vol_offset(off_t pos) const
{
  return ((pos - this->start + CACHE_BLOCK_SIZE) / CACHE_BLOCK_SIZE);
}

inline off_t
Stripe::vol_offset_to_offset(off_t pos) const
{
  return this->start + pos * CACHE_BLOCK_SIZE - CACHE_BLOCK_SIZE;
}

inline int
Stripe::vol_in_phase_agg_buf_valid(Dir const *e) const
{
  return (this->vol_offset(e) >= this->header->write_pos &&
          this->vol_offset(e) < (this->header->write_pos + this->_write_buffer.get_buffer_pos()));
}

// length of the partition not including the offset of location 0.
inline off_t
Stripe::vol_relative_length(off_t start_offset) const
{
  return (this->len + this->skip) - start_offset;
}

// inline Functions

inline EvacuationBlock *
evacuation_block_exists(Dir const *dir, Stripe *stripe)
{
  auto bucket = dir_evac_bucket(dir);
  if (stripe->evac_bucket_valid(bucket)) {
    EvacuationBlock *b = stripe->evacuate[bucket].head;
    for (; b; b = b->link.next) {
      if (dir_offset(&b->dir) == dir_offset(dir)) {
        return b;
      }
    }
  }
  return nullptr;
}

inline void
Stripe::cancel_trigger()
{
  if (trigger) {
    trigger->cancel_action();
    trigger = nullptr;
  }
}

inline EvacuationBlock *
new_EvacuationBlock(EThread *t)
{
  EvacuationBlock *b      = THREAD_ALLOC(evacuationBlockAllocator, t);
  b->init                 = 0;
  b->readers              = 0;
  b->earliest_evacuator   = nullptr;
  b->evac_frags.link.next = nullptr;
  return b;
}

inline void
free_EvacuationBlock(EvacuationBlock *b, EThread *t)
{
  EvacuationKey *e = b->evac_frags.link.next;
  while (e) {
    EvacuationKey *n = e->link.next;
    evacuationKeyAllocator.free(e);
    e = n;
  }
  THREAD_FREE(b, evacuationBlockAllocator, t);
}

inline OpenDirEntry *
Stripe::open_read(const CryptoHash *key) const
{
  return open_dir.open_read(key);
}

inline int
Stripe::within_hit_evacuate_window(Dir const *xdir) const
{
  off_t oft       = dir_offset(xdir) - 1;
  off_t write_off = (header->write_pos + AGG_SIZE - start) / CACHE_BLOCK_SIZE;
  off_t delta     = oft - write_off;
  if (delta >= 0)
    return delta < hit_evacuate_window;
  else
    return -delta > (data_blocks - hit_evacuate_window) && -delta < data_blocks;
}

inline uint32_t
Stripe::round_to_approx_size(uint32_t l) const
{
  uint32_t ll = round_to_approx_dir_size(l);
  return ROUND_TO_SECTOR(this, ll);
}

inline bool
Stripe::evac_bucket_valid(off_t bucket) const
{
  return (bucket >= 0 && bucket < evacuate_size);
}

inline int
Stripe::is_io_in_progress() const
{
  return io.aiocb.aio_fildes != AIO_NOT_IN_PROGRESS;
}

inline void
Stripe::set_io_not_in_progress()
{
  io.aiocb.aio_fildes = AIO_NOT_IN_PROGRESS;
}

inline Queue<CacheVC, Continuation::Link_link> &
Stripe::get_pending_writers()
{
  return this->_write_buffer.get_pending_writers();
}

inline int
Stripe::get_agg_buf_pos() const
{
  return this->_write_buffer.get_buffer_pos();
}

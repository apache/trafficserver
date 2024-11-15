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
#include "P_CacheDisk.h"
#include "P_RamCache.h"
#include "AggregateWriteBuffer.h"
#include "PreservationTable.h"
#include "Stripe.h"

#include "iocore/eventsystem/EThread.h"

#include "tscore/CryptoHash.h"
#include "tscore/List.h"

#include <atomic>

// Stripe
#define STRIPE_MAGIC                 0xF1D0F00D
#define START_BLOCKS                 16 // 8k, STORE_BLOCK_SIZE
#define START_POS                    ((off_t)START_BLOCKS * CACHE_BLOCK_SIZE)
#define STRIPE_BLOCK_SIZE            (1024 * 1024 * 128) // 128MB
#define MIN_STRIPE_SIZE              STRIPE_BLOCK_SIZE
#define MAX_STRIPE_SIZE              ((off_t)512 * 1024 * 1024 * 1024 * 1024) // 512TB
#define MAX_FRAG_SIZE                (AGG_SIZE - sizeof(Doc))                 // true max
#define LEAVE_FREE                   DEFAULT_MAX_BUFFER_SIZE
#define STRIPE_HASH_TABLE_SIZE       32707
#define STRIPE_HASH_EMPTY            0xFFFF
#define STRIPE_HASH_ALLOC_SIZE       (8 * 1024 * 1024) // one chance per this unit
#define LOOKASIDE_SIZE               256
#define AIO_NOT_IN_PROGRESS          -1
#define AIO_AGG_WRITE_IN_PROGRESS    -2
#define AUTO_SIZE_RAM_CACHE          -1       // 1-1 with directory size
#define DEFAULT_TARGET_FRAGMENT_SIZE 1048576; // 1MB. Note: Should not exclude sizeof(Doc)
#define STORE_BLOCKS_PER_STRIPE      (STRIPE_BLOCK_SIZE / STORE_BLOCK_SIZE)

// Documents

struct Cache;
struct StripeInitInfo;
class CacheEvacuateDocVC;

class StripeSM : public Continuation, public Stripe
{
public:
  CryptoHash hash_id;

  int hit_evacuate_window{};

  off_t       recover_pos      = 0;
  off_t       prev_recover_pos = 0;
  AIOCallback io;

  Queue<CacheVC, Continuation::Link_link> sync;

  Event *trigger = nullptr;

  OpenDir              open_dir;
  RamCache            *ram_cache = nullptr;
  DLL<EvacuationBlock> lookaside[LOOKASIDE_SIZE];
  CacheEvacuateDocVC  *doc_evacuator = nullptr;

  StripeInitInfo *init_info = nullptr;

  Cache   *cache                = nullptr;
  uint32_t last_sync_serial     = 0;
  uint32_t last_write_serial    = 0;
  bool     recover_wrapped      = false;
  bool     dir_sync_waiting     = false;
  bool     dir_sync_in_progress = false;
  bool     writing_end_marker   = false;

  CacheKey          first_fragment_key;
  int64_t           first_fragment_offset = 0;
  Ptr<IOBufferData> first_fragment_data;

  void cancel_trigger();

  int recover_data();

  int open_write(CacheVC *cont, int allow_if_writers, int max_writers);
  int open_write_lock(CacheVC *cont, int allow_if_writers, int max_writers);
  int close_write(CacheVC *cont);
  int begin_read(CacheVC *cont) const;
  // unused read-write interlock code
  // currently http handles a write-lock failure by retrying the read
  OpenDirEntry *open_read(const CryptoHash *key) const;
  int           close_read(CacheVC *cont) const;

  int clear_dir_aio();
  int clear_dir();

  int init(bool clear);

  int handle_dir_clear(int event, void *data);
  int handle_dir_read(int event, void *data);
  int handle_recover_from_data(int event, void *data);
  int handle_recover_write_dir(int event, void *data);
  int handle_header_read(int event, void *data);

  int dir_init_done(int event, void *data);

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

  int evac_range(off_t start, off_t end, int evac_phase);

  int within_hit_evacuate_window(Dir const *dir) const;

  /**
   * StripeSM constructor.
   *
   * @param disk: The disk object to associate with this stripe.
   * The disk path must be non-null.
   * @param blocks: Number of blocks. Must be at least 10.
   * @param dir_skip: Offset into the disk at which to start the stripe.
   * If this value is less than START_POS, START_POS will be used instead.
   *
   * @see START_POS
   */
  StripeSM(CacheDisk *disk, off_t blocks, off_t dir_skip, int avg_obj_size = -1);

  Queue<CacheVC, Continuation::Link_link> &get_pending_writers();

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

  bool
  evac_bucket_valid(off_t bucket) const
  {
    return this->_preserved_dirs.evac_bucket_valid(bucket);
  }

  DLL<EvacuationBlock>
  get_evac_bucket(off_t bucket) const
  {
    return this->_preserved_dirs.evacuate[bucket];
  }

  void
  force_evacuate_head(Dir const *evac_dir, int pinned)
  {
    return this->_preserved_dirs.force_evacuate_head(evac_dir, pinned);
  }

  PreservationTable &
  get_preserved_dirs()
  {
    return this->_preserved_dirs;
  }

private:
  mutable PreservationTable _preserved_dirs;

  int _agg_copy(CacheVC *vc);
  int _copy_writer_to_aggregation(CacheVC *vc);
  int _copy_evacuator_to_aggregation(CacheVC *vc);
};

// Global Data

extern StripeSM                   **gstripes;
extern std::atomic<int>             gnstripes;
extern ClassAllocator<OpenDirEntry> openDirEntryAllocator;
extern unsigned short              *vol_hash_table;

// inline Functions

inline void
StripeSM::cancel_trigger()
{
  if (trigger) {
    trigger->cancel_action();
    trigger = nullptr;
  }
}

inline OpenDirEntry *
StripeSM::open_read(const CryptoHash *key) const
{
  return open_dir.open_read(key);
}

inline int
StripeSM::is_io_in_progress() const
{
  return io.aiocb.aio_fildes != AIO_NOT_IN_PROGRESS;
}

inline void
StripeSM::set_io_not_in_progress()
{
  io.aiocb.aio_fildes = AIO_NOT_IN_PROGRESS;
}

inline Queue<CacheVC, Continuation::Link_link> &
StripeSM::get_pending_writers()
{
  return this->_write_buffer.get_pending_writers();
}

inline int
StripeSM::within_hit_evacuate_window(Dir const *xdir) const
{
  off_t oft       = dir_offset(xdir) - 1;
  off_t write_off = (header->write_pos + AGG_SIZE - start) / CACHE_BLOCK_SIZE;
  off_t delta     = oft - write_off;
  if (delta >= 0) {
    return delta < hit_evacuate_window;
  } else {
    return -delta > (data_blocks - hit_evacuate_window) && -delta < data_blocks;
  }
}

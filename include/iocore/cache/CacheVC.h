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

/*
// inkcache
#include "iocore/cache/Cache.h"
#include "../../../src/iocore/cache/P_CacheDir.h"
#include "../../../src/iocore/cache/P_CacheDoc.h"
#include "../../../src/iocore/cache/P_CacheVol.h"
*/
#include "../../../src/iocore/cache/P_CacheHttp.h"

// aio
#include "iocore/aio/AIO.h"

// inkevent
#include "iocore/eventsystem/Action.h"
#include "iocore/eventsystem/Continuation.h"
#include "iocore/eventsystem/Event.h"
#include "iocore/eventsystem/IOBuffer.h"
#include "iocore/eventsystem/VIO.h"

// tscore
#include "tscore/ink_hrtime.h"
#include "tscore/List.h"
#include "tscore/Ptr.h"

#include <cstdint>

class Stripe;
class HttpConfigAccessor;

struct CacheVC : public CacheVConnection {
  CacheVC();

  VIO *do_io_read(Continuation *c, int64_t nbytes, MIOBuffer *buf) override;
  VIO *do_io_pread(Continuation *c, int64_t nbytes, MIOBuffer *buf, int64_t offset) override;
  VIO *do_io_write(Continuation *c, int64_t nbytes, IOBufferReader *buf, bool owner = false) override;
  void do_io_close(int lerrno = -1) override;
  void reenable(VIO *avio) override;
  void reenable_re(VIO *avio) override;
  bool get_data(int i, void *data) override;
  bool set_data(int i, void *data) override;

  bool
  is_ram_cache_hit() const override
  {
    ink_assert(vio.op == VIO::READ);
    return !f.not_from_ram_cache;
  }

  int
  get_header(void **ptr, int *len) override
  {
    if (first_buf) {
      Doc *doc = (Doc *)first_buf->data();
      *ptr     = doc->hdr();
      *len     = doc->hlen;
      return 0;
    }

    return -1;
  }

  int
  set_header(void *ptr, int len) override
  {
    header_to_write     = ptr;
    header_to_write_len = len;
    return 0;
  }

  int
  get_single_data(void **ptr, int *len) override
  {
    if (first_buf) {
      Doc *doc = (Doc *)first_buf->data();
      if (doc->data_len() == doc->total_len) {
        *ptr = doc->data();
        *len = doc->data_len();
        return 0;
      }
    }

    return -1;
  }

  int
  get_volume_number() const override
  {
    if (stripe && stripe->cache_vol) {
      return stripe->cache_vol->vol_number;
    }

    return -1;
  }

  const char *
  get_disk_path() const override
  {
    if (stripe && stripe->disk) {
      return stripe->disk->path;
    }

    return nullptr;
  }

  bool
  is_compressed_in_ram() const override
  {
    ink_assert(vio.op == VIO::READ);
    return f.compressed_in_ram;
  }

  bool writer_done();
  int calluser(int event);
  int callcont(int event);
  int die();
  int dead(int event, Event *e);

  int handleReadDone(int event, Event *e);
  int handleRead(int event, Event *e);
  bool load_from_ram_cache();
  bool load_from_last_open_read_call();
  bool load_from_aggregation_buffer();
  int do_read_call(CacheKey *akey);
  int handleWrite(int event, Event *e);
  int handleWriteLock(int event, Event *e);
  int do_write_call();
  int do_write_lock();
  int do_write_lock_call();
  int do_sync(uint32_t target_write_serial);

  int openReadClose(int event, Event *e);
  int openReadReadDone(int event, Event *e);
  int openReadMain(int event, Event *e);
  int openReadStartEarliest(int event, Event *e);
  int openReadVecWrite(int event, Event *e);
  int openReadStartHead(int event, Event *e);
  int openReadFromWriter(int event, Event *e);
  int openReadFromWriterMain(int event, Event *e);
  int openReadFromWriterFailure(int event, Event *);
  int openReadChooseWriter(int event, Event *e);
  int openReadDirDelete(int event, Event *e);

  int openWriteCloseDir(int event, Event *e);
  int openWriteCloseHeadDone(int event, Event *e);
  int openWriteCloseHead(int event, Event *e);
  int openWriteCloseDataDone(int event, Event *e);
  int openWriteClose(int event, Event *e);
  int openWriteWriteDone(int event, Event *e);
  int openWriteOverwrite(int event, Event *e);
  int openWriteMain(int event, Event *e);
  int openWriteStartDone(int event, Event *e);
  int openWriteStartBegin(int event, Event *e);

  int updateVector(int event, Event *e);

  int removeEvent(int event, Event *e);

  int scanStripe(int event, Event *e);
  int scanObject(int event, Event *e);
  int scanUpdateDone(int event, Event *e);
  int scanOpenWrite(int event, Event *e);
  int scanRemoveDone(int event, Event *e);

  int
  is_io_in_progress()
  {
    return io.aiocb.aio_fildes != AIO_NOT_IN_PROGRESS;
  }
  void
  set_io_not_in_progress()
  {
    io.aiocb.aio_fildes = AIO_NOT_IN_PROGRESS;
  }
  void
  set_agg_write_in_progress()
  {
    io.aiocb.aio_fildes = AIO_AGG_WRITE_IN_PROGRESS;
  }

  void cancel_trigger();
  int64_t get_object_size() override;
  void set_http_info(CacheHTTPInfo *info) override;
  void get_http_info(CacheHTTPInfo **info) override;
  /** Get the fragment table.
      @return The address of the start of the fragment table,
      or @c nullptr if there is no fragment table.
  */
  virtual HTTPInfo::FragOffset *get_frag_table();
  /** Load alt pointers and do fixups if needed.
      @return Length of header data used for alternates.
   */
  virtual uint32_t load_http_info(CacheHTTPInfoVector *info, struct Doc *doc, RefCountObj *block_ptr = nullptr);
  bool is_pread_capable() override;
  bool set_pin_in_cache(time_t time_pin) override;
  time_t get_pin_in_cache() override;

  // number of bytes to memset to 0 in the CacheVC when we free
  // it. All member variables starting from vio are memset to 0.
  // This variable is initialized in CacheVC constructor.
  static int size_to_init;

  // Start Region A
  // This set of variables are not reset when the cacheVC is freed.
  // A CacheVC must set these to the correct values whenever needed
  // These are variables that are always set to the correct values
  // before being used by the CacheVC
  CacheKey key, first_key, earliest_key, update_key;
  Dir dir, earliest_dir, overwrite_dir, first_dir;
  // end Region A

  // Start Region B
  // These variables are individually cleared or reset when the
  // CacheVC is freed. All these variables must be reset/cleared
  // in free_CacheVC.
  Action _action;
  CacheHTTPHdr request;
  CacheHTTPInfoVector vector;
  CacheHTTPInfo alternate;
  Ptr<IOBufferData> buf;
  Ptr<IOBufferData> first_buf;
  Ptr<IOBufferBlock> blocks; // data available to write
  Ptr<IOBufferBlock> writer_buf;

  OpenDirEntry *od = nullptr;
  AIOCallbackInternal io;
  int alternate_index = CACHE_ALT_INDEX_DEFAULT; // preferred position in vector
  LINK(CacheVC, opendir_link);
  // end Region B

  // Start Region C
  // These variables are memset to 0 when the structure is freed.
  // The size of this region is size_to_init which is initialized
  // in the CacheVC constructor. It assumes that vio is the start
  // of this region.
  // NOTE: NOTE: NOTE: If vio is NOT the start, then CHANGE the
  // size_to_init initialization
  VIO vio;
  CacheFragType frag_type;
  CacheHTTPInfo *info;
  CacheHTTPInfoVector *write_vector;
  const HttpConfigAccessor *params;
  int header_len;        // for communicating with agg_copy
  int frag_len;          // for communicating with agg_copy
  uint32_t write_len;    // for communicating with agg_copy
  uint32_t agg_len;      // for communicating with aggWrite
  uint32_t write_serial; // serial of the final write for SYNC
  Stripe *stripe;
  Dir *last_collision;
  Event *trigger;
  CacheKey *read_key;
  ContinuationHandler save_handler;
  time_t pin_in_cache;
  ink_hrtime start_time;
  int op_type; // Index into the metrics array for this operation, rather than a CacheOpType (fewer casts)
  int recursive;
  int closed;
  uint64_t seek_to;      // pread offset
  int64_t offset;        // offset into 'blocks' of data to write
  int64_t writer_offset; // offset of the writer for reading from a writer
  int64_t length;        // length of data available to write
  int64_t doc_pos;       // read position in 'buf'
  uint64_t write_pos;    // length written
  uint64_t total_len;    // total length written and available to write
  uint64_t doc_len;      // total_length (of the selected alternate for HTTP)
  uint64_t update_len;
  int fragment;
  int scan_msec_delay;
  CacheVC *write_vc;
  char *hostname;
  int host_len;
  int header_to_write_len;
  void *header_to_write;
  short writer_lock_retry;
  union {
    uint32_t flags;
    struct {
      unsigned int use_first_key           : 1;
      unsigned int overwrite               : 1; // overwrite first_key Dir if it exists
      unsigned int close_complete          : 1; // WRITE_COMPLETE is final
      unsigned int sync                    : 1; // write to be committed to durable storage before WRITE_COMPLETE
      unsigned int evacuator               : 1;
      unsigned int single_fragment         : 1;
      unsigned int evac_vector             : 1;
      unsigned int lookup                  : 1;
      unsigned int update                  : 1;
      unsigned int remove                  : 1;
      unsigned int remove_aborted_writers  : 1;
      unsigned int open_read_timeout       : 1; // UNUSED
      unsigned int data_done               : 1;
      unsigned int read_from_writer_called : 1;
      unsigned int not_from_ram_cache      : 1; // entire object was from ram cache
      unsigned int rewrite_resident_alt    : 1;
      unsigned int readers                 : 1;
      unsigned int doc_from_ram_cache      : 1;
      unsigned int hit_evacuate            : 1;
      unsigned int compressed_in_ram       : 1; // compressed state in ram cache
      unsigned int allow_empty_doc         : 1; // used for cache empty http document
    } f;
  };
  // BTF optimization used to skip reading stuff in cache partition that doesn't contain any
  // dir entries.
  char *scan_stripe_map;
  // BTF fix to handle objects that overlapped over two different reads,
  // this is how much we need to back up the buffer to get the start of the overlapping object.
  off_t scan_fix_buffer_offset;
  // end region C
};

LINK_DEFINITION(CacheVC, opendir_link)

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

// Cache Inspector and State Pages

#include "CacheEvacuateDocVC.h"
#include "CacheVC.h"
#include "P_CacheDoc.h"
#include "P_CacheInternal.h"
#include "P_CacheTest.h"
#include "Stripe.h"
#include "StripeSM.h"
#include "iocore/cache/Cache.h"
#include "tscore/Filenames.h"
#include "tscore/InkErrno.h"
#include "tscore/Layout.h"

#ifdef AIO_FAULT_INJECTION
#include "iocore/aio/AIO_fault_injection.h"
#endif

#include <atomic>
#include <unordered_set>
#include <fstream>
#include <string>
#include <filesystem>

#define SCAN_BUF_SIZE              RECOVERY_SIZE
#define SCAN_WRITER_LOCK_MAX_RETRY 5

extern void register_cache_stats(CacheStatsBlock *rsb, const std::string &prefix);

constexpr ts::VersionNumber CACHE_DB_VERSION(CACHE_DB_MAJOR_VERSION, CACHE_DB_MINOR_VERSION);

// Configuration

int64_t cache_config_ram_cache_size                = AUTO_SIZE_RAM_CACHE;
int     cache_config_ram_cache_algorithm           = 1;
int     cache_config_ram_cache_compress            = 0;
int     cache_config_ram_cache_compress_percent    = 90;
int     cache_config_ram_cache_use_seen_filter     = 1;
int     cache_config_http_max_alts                 = 3;
int     cache_config_log_alternate_eviction        = 0;
int     cache_config_dir_sync_frequency            = 60;
int     cache_config_dir_sync_delay                = 500;
int     cache_config_dir_sync_max_write            = (2 * 1024 * 1024);
int     cache_config_permit_pinning                = 0;
int     cache_config_select_alternate              = 1;
int     cache_config_max_doc_size                  = 0;
int     cache_config_min_average_object_size       = ESTIMATED_OBJECT_SIZE;
int64_t cache_config_ram_cache_cutoff              = AGG_SIZE;
int     cache_config_max_disk_errors               = 5;
int     cache_config_hit_evacuate_percent          = 10;
int     cache_config_hit_evacuate_size_limit       = 0;
int     cache_config_force_sector_size             = 0;
int     cache_config_target_fragment_size          = DEFAULT_TARGET_FRAGMENT_SIZE;
int     cache_config_agg_write_backlog             = AGG_SIZE * 2;
int     cache_config_enable_checksum               = 0;
int     cache_config_alt_rewrite_max_size          = 4096;
int     cache_config_read_while_writer             = 0;
int     cache_config_mutex_retry_delay             = 2;
int     cache_read_while_writer_retry_delay        = 50;
int     cache_config_read_while_writer_max_retries = 10;
int     cache_config_persist_bad_disks             = false;

// Globals

CacheStatsBlock                    cache_rsb;
Cache                             *theCache                     = nullptr;
CacheDisk                        **gdisks                       = nullptr;
int                                gndisks                      = 0;
Cache                             *caches[NUM_CACHE_FRAG_TYPES] = {nullptr};
CacheSync                         *cacheDirSync                 = nullptr;
Store                              theCacheStore;
StripeSM                         **gstripes  = nullptr;
std::atomic<int>                   gnstripes = 0;
ClassAllocator<CacheVC>            cacheVConnectionAllocator("cacheVConnection");
ClassAllocator<CacheEvacuateDocVC> cacheEvacuateDocVConnectionAllocator("cacheEvacuateDocVC");
ClassAllocator<EvacuationBlock>    evacuationBlockAllocator("evacuationBlock");
ClassAllocator<CacheRemoveCont>    cacheRemoveContAllocator("cacheRemoveCont");
ClassAllocator<EvacuationKey>      evacuationKeyAllocator("evacuationKey");
std::unordered_set<std::string>    known_bad_disks;

namespace
{

DbgCtl dbg_ctl_cache_scan_truss{"cache_scan_truss"};
DbgCtl dbg_ctl_cache_init{"cache_init"};
DbgCtl dbg_ctl_cache_hosting{"cache_hosting"};
DbgCtl dbg_ctl_cache_update{"cache_update"};

} // end anonymous namespace

// Global list of the volumes created
Queue<CacheVol> cp_list;
int             cp_list_len = 0;
ConfigVolumes   config_volumes;

#if TS_HAS_TESTS
void
force_link_CacheTestCaller()
{
  force_link_CacheTest();
}
#endif

static int
validate_rww(int new_value)
{
  if (new_value) {
    auto http_bg_fill{RecGetRecordFloat("proxy.config.http.background_fill_completed_threshold").value_or(0)};
    if (http_bg_fill > 0.0) {
      Note("to enable reading while writing a document, %s should be 0.0: read while writing disabled",
           "proxy.config.http.background_fill_completed_threshold");
      return 0;
    }
    if (cache_config_max_doc_size > 0) {
      Note("to enable reading while writing a document, %s should be 0: read while writing disabled",
           "proxy.config.cache.max_doc_size");
      return 0;
    }
    return new_value;
  }
  return 0;
}

static int
update_cache_config(const char * /* name ATS_UNUSED */, RecDataT /* data_type ATS_UNUSED */, RecData data,
                    void * /* cookie ATS_UNUSED */)
{
  int new_value                  = validate_rww(data.rec_int);
  cache_config_read_while_writer = new_value;

  return 0;
}

static_assert(static_cast<int>(TS_EVENT_CACHE_OPEN_READ) == static_cast<int>(CACHE_EVENT_OPEN_READ));
static_assert(static_cast<int>(TS_EVENT_CACHE_OPEN_READ_FAILED) == static_cast<int>(CACHE_EVENT_OPEN_READ_FAILED));
static_assert(static_cast<int>(TS_EVENT_CACHE_OPEN_WRITE) == static_cast<int>(CACHE_EVENT_OPEN_WRITE));
static_assert(static_cast<int>(TS_EVENT_CACHE_OPEN_WRITE_FAILED) == static_cast<int>(CACHE_EVENT_OPEN_WRITE_FAILED));
static_assert(static_cast<int>(TS_EVENT_CACHE_REMOVE) == static_cast<int>(CACHE_EVENT_REMOVE));
static_assert(static_cast<int>(TS_EVENT_CACHE_REMOVE_FAILED) == static_cast<int>(CACHE_EVENT_REMOVE_FAILED));
static_assert(static_cast<int>(TS_EVENT_CACHE_SCAN) == static_cast<int>(CACHE_EVENT_SCAN));
static_assert(static_cast<int>(TS_EVENT_CACHE_SCAN_FAILED) == static_cast<int>(CACHE_EVENT_SCAN_FAILED));
static_assert(static_cast<int>(TS_EVENT_CACHE_SCAN_OBJECT) == static_cast<int>(CACHE_EVENT_SCAN_OBJECT));
static_assert(static_cast<int>(TS_EVENT_CACHE_SCAN_OPERATION_BLOCKED) == static_cast<int>(CACHE_EVENT_SCAN_OPERATION_BLOCKED));
static_assert(static_cast<int>(TS_EVENT_CACHE_SCAN_OPERATION_FAILED) == static_cast<int>(CACHE_EVENT_SCAN_OPERATION_FAILED));
static_assert(static_cast<int>(TS_EVENT_CACHE_SCAN_DONE) == static_cast<int>(CACHE_EVENT_SCAN_DONE));

void
Cache::vol_initialized(bool result)
{
  if (result) {
    ink_atomic_increment(&total_good_nvol, 1);
  }
  if (total_nvol == ink_atomic_increment(&total_initialized_vol, 1) + 1) {
    open_done();
  }
}

int
AIO_failure_handler::handle_disk_failure(int /* event ATS_UNUSED */, void *data)
{
  /* search for the matching file descriptor */
  if (!CacheProcessor::cache_ready) {
    return EVENT_DONE;
  }
  int          disk_no = 0;
  AIOCallback *cb      = static_cast<AIOCallback *>(data);

  for (; disk_no < gndisks; disk_no++) {
    CacheDisk *d = gdisks[disk_no];

    if (d->fd == cb->aiocb.aio_fildes) {
      char message[256];
      d->incrErrors(cb);

      if (!DISK_BAD(d)) {
        snprintf(message, sizeof(message), "Error accessing Disk %s [%d/%d]", d->path, d->num_errors, cache_config_max_disk_errors);
        Warning("%s", message);
      } else if (!DISK_BAD_SIGNALLED(d)) {
        snprintf(message, sizeof(message), "too many errors accessing disk %s [%d/%d]: declaring disk bad", d->path, d->num_errors,
                 cache_config_max_disk_errors);
        Warning("%s", message);
        cacheProcessor.mark_storage_offline(d); // take it out of service
      }
      break;
    }
  }

  delete cb;
  return EVENT_DONE;
}

int
Cache::open_done()
{
  Action *register_ShowCache(Continuation * c, HTTPHdr * h);
  Action *register_ShowCacheInternal(Continuation * c, HTTPHdr * h);

  if (total_good_nvol == 0) {
    ready = CacheInitState::FAILED;
    cacheProcessor.cacheInitialized();
    return 0;
  }

  {
    CacheHostTable *hosttable_raw = new CacheHostTable(this, scheme);
    hosttable.reset(hosttable_raw);
    hosttable_raw->register_config_callback(&hosttable);
  }

  ReplaceablePtr<CacheHostTable>::ScopedReader hosttable(&this->hosttable);
  if (hosttable->gen_host_rec.num_cachevols == 0) {
    ready = CacheInitState::FAILED;
  } else {
    ready = CacheInitState::INITIALIZED;
  }

  // TS-3848
  if (ready == CacheInitState::FAILED && cacheProcessor.waitForCache() >= 2) {
    Emergency("Failed to initialize cache host table");
  }

  cacheProcessor.cacheInitialized();

  return 0;
}

int
Cache::open(bool clear, bool /* fix ATS_UNUSED */)
{
  int   i;
  off_t blocks          = 0;
  cache_read_done       = 0;
  total_initialized_vol = 0;
  total_nvol            = 0;
  total_good_nvol       = 0;

  RecEstablishStaticConfigInt32(cache_config_min_average_object_size, "proxy.config.cache.min_average_object_size");
  Dbg(dbg_ctl_cache_init, "Cache::open - proxy.config.cache.min_average_object_size = %d", cache_config_min_average_object_size);

  CacheVol *cp = cp_list.head;
  for (; cp; cp = cp->link.next) {
    if (cp->scheme == scheme) {
      cp->stripes = static_cast<StripeSM **>(ats_malloc(cp->num_vols * sizeof(StripeSM *)));
      int vol_no  = 0;
      for (i = 0; i < gndisks; i++) {
        if (cp->disk_stripes[i] && !DISK_BAD(cp->disk_stripes[i]->disk)) {
          DiskStripeBlockQueue *q = cp->disk_stripes[i]->dpb_queue.head;
          for (; q; q = q->link.next) {
            blocks                         = q->b->len;
            CacheDisk *d                   = cp->disk_stripes[i]->disk;
            cp->stripes[vol_no]            = new StripeSM(d, blocks, q->b->offset, cp->avg_obj_size, cp->fragment_size);
            cp->stripes[vol_no]->cache     = this;
            cp->stripes[vol_no]->cache_vol = cp;

            bool vol_clear = clear || d->cleared || q->new_block;
            cp->stripes[vol_no]->init(vol_clear);
            vol_no++;
            cache_size += blocks;
          }
        }
      }
      total_nvol += vol_no;
    }
  }
  if (total_nvol == 0) {
    return open_done();
  }
  cache_read_done = 1;
  return 0;
}

int
Cache::close()
{
  return -1;
}

Action *
Cache::lookup(Continuation *cont, const CacheKey *key, CacheFragType type, std::string_view hostname) const
{
  if (!CacheProcessor::IsCacheReady(type)) {
    cont->handleEvent(CACHE_EVENT_LOOKUP_FAILED, nullptr);
    return ACTION_RESULT_DONE;
  }

  StripeSM *stripe = key_to_stripe(key, hostname);
  CacheVC  *c      = new_CacheVC(cont);
  SET_CONTINUATION_HANDLER(c, &CacheVC::openReadStartHead);
  c->vio.op  = VIO::READ;
  c->op_type = static_cast<int>(CacheOpType::Lookup);
  ts::Metrics::Gauge::increment(cache_rsb.status[c->op_type].active);
  ts::Metrics::Gauge::increment(stripe->cache_vol->vol_rsb.status[c->op_type].active);
  c->first_key = c->key = *key;
  c->frag_type          = type;
  c->f.lookup           = 1;
  c->stripe             = stripe;
  c->last_collision     = nullptr;

  if (c->handleEvent(EVENT_INTERVAL, nullptr) == EVENT_CONT) {
    return &c->_action;
  } else {
    return ACTION_RESULT_DONE;
  }
}

Action *
Cache::open_read(Continuation *cont, const CacheKey *key, CacheFragType type, std::string_view hostname) const
{
  if (!CacheProcessor::IsCacheReady(type)) {
    cont->handleEvent(CACHE_EVENT_OPEN_READ_FAILED, reinterpret_cast<void *>(-ECACHE_NOT_READY));
    return ACTION_RESULT_DONE;
  }
  ink_assert(caches[type] == this);

  StripeSM     *stripe = key_to_stripe(key, hostname);
  Dir           result, *last_collision = nullptr;
  ProxyMutex   *mutex = cont->mutex.get();
  OpenDirEntry *od    = nullptr;
  CacheVC      *c     = nullptr;
  {
    CACHE_TRY_LOCK(lock, stripe->mutex, mutex->thread_holding);
    if (!lock.is_locked() || (od = stripe->open_read(key)) || dir_probe(key, stripe, &result, &last_collision)) {
      c = new_CacheVC(cont);
      SET_CONTINUATION_HANDLER(c, &CacheVC::openReadStartHead);
      c->vio.op  = VIO::READ;
      c->op_type = static_cast<int>(CacheOpType::Read);
      ts::Metrics::Gauge::increment(cache_rsb.status[c->op_type].active);
      ts::Metrics::Gauge::increment(stripe->cache_vol->vol_rsb.status[c->op_type].active);
      c->first_key = c->key = c->earliest_key = *key;
      c->stripe                               = stripe;
      c->frag_type                            = type;
      c->od                                   = od;
    }
    if (!c) {
      goto Lmiss;
    }
    if (!lock.is_locked()) {
      CONT_SCHED_LOCK_RETRY(c);
      return &c->_action;
    }
    if (c->od) {
      goto Lwriter;
    }
    c->dir            = result;
    c->last_collision = last_collision;
    switch (c->do_read_call(&c->key)) {
    case EVENT_DONE:
      return ACTION_RESULT_DONE;
    case EVENT_RETURN:
      goto Lcallreturn;
    default:
      return &c->_action;
    }
  }
Lmiss:
  ts::Metrics::Counter::increment(cache_rsb.status[static_cast<int>(CacheOpType::Read)].failure);
  ts::Metrics::Counter::increment(stripe->cache_vol->vol_rsb.status[static_cast<int>(CacheOpType::Read)].failure);
  cont->handleEvent(CACHE_EVENT_OPEN_READ_FAILED, reinterpret_cast<void *>(-ECACHE_NO_DOC));
  return ACTION_RESULT_DONE;
Lwriter:
  SET_CONTINUATION_HANDLER(c, &CacheVC::openReadFromWriter);
  if (c->handleEvent(EVENT_IMMEDIATE, nullptr) == EVENT_DONE) {
    return ACTION_RESULT_DONE;
  }
  return &c->_action;
Lcallreturn:
  if (c->handleEvent(AIO_EVENT_DONE, nullptr) == EVENT_DONE) {
    return ACTION_RESULT_DONE;
  }
  return &c->_action;
}

// main entry point for writing of non-http documents
Action *
Cache::open_write(Continuation *cont, const CacheKey *key, CacheFragType frag_type, int options, time_t apin_in_cache,
                  std::string_view hostname) const
{
  if (!CacheProcessor::IsCacheReady(frag_type)) {
    cont->handleEvent(CACHE_EVENT_OPEN_WRITE_FAILED, reinterpret_cast<void *>(-ECACHE_NOT_READY));
    return ACTION_RESULT_DONE;
  }

  ink_assert(caches[frag_type] == this);

  intptr_t res = 0;
  CacheVC *c   = new_CacheVC(cont);
  SCOPED_MUTEX_LOCK(lock, c->mutex, this_ethread());
  c->vio.op        = VIO::WRITE;
  c->op_type       = static_cast<int>(CacheOpType::Write);
  c->stripe        = key_to_stripe(key, hostname);
  StripeSM *stripe = c->stripe;
  ts::Metrics::Gauge::increment(cache_rsb.status[c->op_type].active);
  ts::Metrics::Gauge::increment(stripe->cache_vol->vol_rsb.status[c->op_type].active);
  c->first_key = c->key = *key;
  c->frag_type          = frag_type;
  /*
     The transition from single fragment document to a multi-fragment document
     would cause a problem if the key and the first_key collide. In case of
     a collision, old vector data could be served to HTTP. Need to avoid that.
     Also, when evacuating a fragment, we have to decide if its the first_key
     or the earliest_key based on the dir_tag.
   */
  do {
    rand_CacheKey(&c->key);
  } while (DIR_MASK_TAG(c->key.slice32(2)) == DIR_MASK_TAG(c->first_key.slice32(2)));
  c->earliest_key     = c->key;
  c->info             = nullptr;
  c->f.overwrite      = (options & CACHE_WRITE_OPT_OVERWRITE) != 0;
  c->f.close_complete = (options & CACHE_WRITE_OPT_CLOSE_COMPLETE) != 0;
  c->f.sync           = (options & CACHE_WRITE_OPT_SYNC) == CACHE_WRITE_OPT_SYNC;
  // coverity[Y2K38_SAFETY:FALSE]
  c->pin_in_cache = static_cast<uint32_t>(apin_in_cache);

  if ((res = c->stripe->open_write_lock(c, false, 1)) > 0) {
    // document currently being written, abort
    ts::Metrics::Counter::increment(cache_rsb.status[c->op_type].failure);
    ts::Metrics::Counter::increment(stripe->cache_vol->vol_rsb.status[c->op_type].failure);
    cont->handleEvent(CACHE_EVENT_OPEN_WRITE_FAILED, reinterpret_cast<void *>(-res));
    free_CacheVC(c);
    return ACTION_RESULT_DONE;
  }
  if (res < 0) {
    SET_CONTINUATION_HANDLER(c, &CacheVC::openWriteStartBegin);
    c->trigger = CONT_SCHED_LOCK_RETRY(c);
    return &c->_action;
  }
  if (!c->f.overwrite) {
    SET_CONTINUATION_HANDLER(c, &CacheVC::openWriteMain);
    c->callcont(CACHE_EVENT_OPEN_WRITE);
    return ACTION_RESULT_DONE;
  } else {
    SET_CONTINUATION_HANDLER(c, &CacheVC::openWriteOverwrite);
    if (c->openWriteOverwrite(EVENT_IMMEDIATE, nullptr) == EVENT_DONE) {
      return ACTION_RESULT_DONE;
    } else {
      return &c->_action;
    }
  }
}

Action *
Cache::remove(Continuation *cont, const CacheKey *key, CacheFragType type, std::string_view hostname) const
{
  if (!CacheProcessor::IsCacheReady(type)) {
    if (cont) {
      cont->handleEvent(CACHE_EVENT_REMOVE_FAILED, nullptr);
    }
    return ACTION_RESULT_DONE;
  }

  Ptr<ProxyMutex> mutex;
  if (!cont) {
    cont = new_CacheRemoveCont();
  }

  CACHE_TRY_LOCK(lock, cont->mutex, this_ethread());
  ink_assert(lock.is_locked());
  StripeSM *stripe = key_to_stripe(key, hostname);
  // coverity[var_decl]
  Dir result;
  dir_clear(&result); // initialized here, set result empty so we can recognize missed lock
  mutex = cont->mutex;

  CacheVC *c   = new_CacheVC(cont);
  c->vio.op    = VIO::NONE;
  c->frag_type = type;
  c->op_type   = static_cast<int>(CacheOpType::Remove);
  ts::Metrics::Gauge::increment(cache_rsb.status[c->op_type].active);
  ts::Metrics::Gauge::increment(stripe->cache_vol->vol_rsb.status[c->op_type].active);
  c->first_key = c->key = *key;
  c->stripe             = stripe;
  c->dir                = result;
  c->f.remove           = 1;

  SET_CONTINUATION_HANDLER(c, &CacheVC::removeEvent);
  int ret = c->removeEvent(EVENT_IMMEDIATE, nullptr);
  if (ret == EVENT_DONE) {
    return ACTION_RESULT_DONE;
  } else {
    return &c->_action;
  }
}

Action *
Cache::scan(Continuation *cont, std::string_view hostname, int KB_per_second) const
{
  Dbg(dbg_ctl_cache_scan_truss, "inside scan");
  if (!CacheProcessor::IsCacheReady(CACHE_FRAG_TYPE_HTTP)) {
    cont->handleEvent(CACHE_EVENT_SCAN_FAILED, nullptr);
    return ACTION_RESULT_DONE;
  }

  CacheVC *c = new_CacheVC(cont);
  c->stripe  = nullptr;
  /* do we need to make a copy */
  c->hostname        = hostname;
  c->op_type         = static_cast<int>(CacheOpType::Scan);
  c->buf             = new_IOBufferData(BUFFER_SIZE_FOR_XMALLOC(SCAN_BUF_SIZE), MEMALIGNED);
  c->scan_msec_delay = (SCAN_BUF_SIZE / KB_per_second);
  c->offset          = 0;
  SET_CONTINUATION_HANDLER(c, &CacheVC::scanStripe);
  eventProcessor.schedule_in(c, HRTIME_MSECONDS(c->scan_msec_delay));
  cont->handleEvent(CACHE_EVENT_SCAN, c);
  return &c->_action;
}

Action *
Cache::open_read(Continuation *cont, const CacheKey *key, CacheHTTPHdr *request, const HttpConfigAccessor *params,
                 CacheFragType type, std::string_view hostname) const
{
  if (!CacheProcessor::IsCacheReady(type)) {
    cont->handleEvent(CACHE_EVENT_OPEN_READ_FAILED, reinterpret_cast<void *>(-ECACHE_NOT_READY));
    return ACTION_RESULT_DONE;
  }
  ink_assert(caches[type] == this);

  StripeSM     *stripe = key_to_stripe(key, hostname);
  Dir           result, *last_collision = nullptr;
  ProxyMutex   *mutex = cont->mutex.get();
  OpenDirEntry *od    = nullptr;
  CacheVC      *c     = nullptr;

  {
    CACHE_TRY_LOCK(lock, stripe->mutex, mutex->thread_holding);
    if (!lock.is_locked() || (od = stripe->open_read(key)) || dir_probe(key, stripe, &result, &last_collision)) {
      c            = new_CacheVC(cont);
      c->first_key = c->key = c->earliest_key = *key;
      c->stripe                               = stripe;
      c->vio.op                               = VIO::READ;
      c->op_type                              = static_cast<int>(CacheOpType::Read);
      ts::Metrics::Gauge::increment(cache_rsb.status[c->op_type].active);
      ts::Metrics::Gauge::increment(stripe->cache_vol->vol_rsb.status[c->op_type].active);
      c->request.copy_shallow(request);
      c->frag_type = CACHE_FRAG_TYPE_HTTP;
      c->params    = params;
      c->od        = od;
    }
    if (!lock.is_locked()) {
      SET_CONTINUATION_HANDLER(c, &CacheVC::openReadStartHead);
      CONT_SCHED_LOCK_RETRY(c);
      return &c->_action;
    }
    if (!c) {
      goto Lmiss;
    }
    if (c->od) {
      goto Lwriter;
    }
    // hit
    c->dir = c->first_dir = result;
    c->last_collision     = last_collision;
    SET_CONTINUATION_HANDLER(c, &CacheVC::openReadStartHead);
    switch (c->do_read_call(&c->key)) {
    case EVENT_DONE:
      return ACTION_RESULT_DONE;
    case EVENT_RETURN:
      goto Lcallreturn;
    default:
      return &c->_action;
    }
  }
Lmiss:
  ts::Metrics::Counter::increment(cache_rsb.status[static_cast<int>(CacheOpType::Read)].failure);
  ts::Metrics::Counter::increment(stripe->cache_vol->vol_rsb.status[static_cast<int>(CacheOpType::Read)].failure);
  cont->handleEvent(CACHE_EVENT_OPEN_READ_FAILED, reinterpret_cast<void *>(-ECACHE_NO_DOC));
  return ACTION_RESULT_DONE;
Lwriter:
  cont->handleEvent(CACHE_EVENT_OPEN_READ_RWW, nullptr);
  SET_CONTINUATION_HANDLER(c, &CacheVC::openReadFromWriter);
  if (c->handleEvent(EVENT_IMMEDIATE, nullptr) == EVENT_DONE) {
    return ACTION_RESULT_DONE;
  }
  return &c->_action;
Lcallreturn:
  if (c->handleEvent(AIO_EVENT_DONE, nullptr) == EVENT_DONE) {
    return ACTION_RESULT_DONE;
  }
  return &c->_action;
}

// main entry point for writing of http documents
Action *
Cache::open_write(Continuation *cont, const CacheKey *key, CacheHTTPInfo *info, time_t apin_in_cache, CacheFragType type,
                  std::string_view hostname) const
{
  if (!CacheProcessor::IsCacheReady(type)) {
    cont->handleEvent(CACHE_EVENT_OPEN_WRITE_FAILED, reinterpret_cast<void *>(-ECACHE_NOT_READY));
    return ACTION_RESULT_DONE;
  }

  ink_assert(caches[type] == this);
  intptr_t err        = 0;
  int      if_writers = reinterpret_cast<uintptr_t>(info) == CACHE_ALLOW_MULTIPLE_WRITES;
  CacheVC *c          = new_CacheVC(cont);
  c->vio.op           = VIO::WRITE;
  c->first_key        = *key;
  /*
     The transition from single fragment document to a multi-fragment document
     would cause a problem if the key and the first_key collide. In case of
     a collision, old vector data could be served to HTTP. Need to avoid that.
     Also, when evacuating a fragment, we have to decide if its the first_key
     or the earliest_key based on the dir_tag.
   */
  do {
    rand_CacheKey(&c->key);
  } while (DIR_MASK_TAG(c->key.slice32(2)) == DIR_MASK_TAG(c->first_key.slice32(2)));
  c->earliest_key  = c->key;
  c->frag_type     = CACHE_FRAG_TYPE_HTTP;
  c->stripe        = key_to_stripe(key, hostname);
  StripeSM *stripe = c->stripe;
  c->info          = info;
  if (c->info && reinterpret_cast<uintptr_t>(info) != CACHE_ALLOW_MULTIPLE_WRITES) {
    /*
       Update has the following code paths :
       a) Update alternate header only :
       In this case the vector has to be rewritten. The content
       length(update_len) and the key for the document are set in the
       new_info in the set_http_info call.
       HTTP OPERATIONS
       open_write with info set
       set_http_info new_info
       (total_len == 0)
       close
       b) Update alternate and data
       In this case both the vector and the data needs to be rewritten.
       This case is similar to the standard write of a document case except
       that the new_info is inserted into the vector at the alternate_index
       (overwriting the old alternate) rather than the end of the vector.
       HTTP OPERATIONS
       open_write with info set
       set_http_info new_info
       do_io_write =>  (total_len > 0)
       close
       c) Delete an alternate
       The vector may need to be deleted (if there was only one alternate) or
       rewritten (if there were more than one alternate).
       HTTP OPERATIONS
       open_write with info set
       close
     */
    c->f.update = 1;
    c->op_type  = static_cast<int>(CacheOpType::Update);
    DDbg(dbg_ctl_cache_update, "Update called");
    info->object_key_get(&c->update_key);
    ink_assert(!(c->update_key.is_zero()));
    c->update_len = info->object_size_get();
  } else {
    c->op_type = static_cast<int>(CacheOpType::Write);
  }

  ts::Metrics::Gauge::increment(cache_rsb.status[c->op_type].active);
  ts::Metrics::Gauge::increment(stripe->cache_vol->vol_rsb.status[c->op_type].active);
  // coverity[Y2K38_SAFETY:FALSE]
  c->pin_in_cache = static_cast<uint32_t>(apin_in_cache);

  {
    CACHE_TRY_LOCK(lock, c->stripe->mutex, cont->mutex->thread_holding);
    if (lock.is_locked()) {
      if ((err = c->stripe->open_write(c, if_writers, cache_config_http_max_alts > 1 ? cache_config_http_max_alts : 0)) > 0) {
        goto Lfailure;
      }
      // If there are multiple writers, then this one cannot be an update.
      // Only the first writer can do an update. If that's the case, we can
      // return success to the state machine now.;
      if (c->od->has_multiple_writers()) {
        goto Lmiss;
      }
      if (!dir_probe(key, c->stripe, &c->dir, &c->last_collision)) {
        if (c->f.update) {
          // fail update because vector has been GC'd
          // This situation can also arise in openWriteStartDone
          err = ECACHE_NO_DOC;
          goto Lfailure;
        }
        // document doesn't exist, begin write
        goto Lmiss;
      } else {
        c->od->reading_vec = true;
        // document exists, read vector
        SET_CONTINUATION_HANDLER(c, &CacheVC::openWriteStartDone);
        switch (c->do_read_call(&c->first_key)) {
        case EVENT_DONE:
          return ACTION_RESULT_DONE;
        case EVENT_RETURN:
          goto Lcallreturn;
        default:
          return &c->_action;
        }
      }
    }
    // missed lock
    SET_CONTINUATION_HANDLER(c, &CacheVC::openWriteStartDone);
    CONT_SCHED_LOCK_RETRY(c);
    return &c->_action;
  }

Lmiss:
  SET_CONTINUATION_HANDLER(c, &CacheVC::openWriteMain);
  c->callcont(CACHE_EVENT_OPEN_WRITE);
  return ACTION_RESULT_DONE;

Lfailure:
  ts::Metrics::Counter::increment(cache_rsb.status[c->op_type].failure);
  ts::Metrics::Counter::increment(stripe->cache_vol->vol_rsb.status[c->op_type].failure);
  cont->handleEvent(CACHE_EVENT_OPEN_WRITE_FAILED, reinterpret_cast<void *>(-err));
  if (c->od) {
    c->openWriteCloseDir(EVENT_IMMEDIATE, nullptr);
    return ACTION_RESULT_DONE;
  }
  free_CacheVC(c);
  return ACTION_RESULT_DONE;

Lcallreturn:
  if (c->handleEvent(AIO_EVENT_DONE, nullptr) == EVENT_DONE) {
    return ACTION_RESULT_DONE;
  }
  return &c->_action;
}

// CacheVConnection
CacheVConnection::CacheVConnection() : VConnection(nullptr) {}

// if generic_host_rec.stripes == nullptr, what do we do???
StripeSM *
Cache::key_to_stripe(const CacheKey *key, std::string_view hostname) const
{
  ReplaceablePtr<CacheHostTable>::ScopedReader hosttable(&this->hosttable);

  uint32_t               h          = (key->slice32(2) >> DIR_TAG_WIDTH) % STRIPE_HASH_TABLE_SIZE;
  unsigned short        *hash_table = hosttable->gen_host_rec.vol_hash_table;
  const CacheHostRecord *host_rec   = &hosttable->gen_host_rec;

  if (hosttable->m_numEntries > 0 && !hostname.empty()) {
    CacheHostResult res;
    hosttable->Match(hostname, &res);
    if (res.record) {
      unsigned short *host_hash_table = res.record->vol_hash_table;
      if (host_hash_table) {
        if (dbg_ctl_cache_hosting.on()) {
          char format_str[50];
          snprintf(format_str, sizeof(format_str), "Volume: %%xd for host: %%.%ds", static_cast<int>(hostname.length()));
          Dbg(dbg_ctl_cache_hosting, format_str, res.record, hostname.data());
        }
        return res.record->stripes[host_hash_table[h]];
      }
    }
  }
  if (hash_table) {
    if (dbg_ctl_cache_hosting.on()) {
      char format_str[50];
      snprintf(format_str, sizeof(format_str), "Generic volume: %%xd for host: %%.%ds", static_cast<int>(hostname.length()));
      Dbg(dbg_ctl_cache_hosting, format_str, host_rec, hostname.data());
    }
    return host_rec->stripes[hash_table[h]];
  } else {
    return host_rec->stripes[0];
  }
}

int
FragmentSizeUpdateCb(const char * /* name ATS_UNUSED */, RecDataT /* data_type ATS_UNUSED */, RecData data,
                     void * /* cookie ATS_UNUSED */)
{
  if (sizeof(Doc) >= static_cast<size_t>(data.rec_int) || static_cast<size_t>(data.rec_int) - sizeof(Doc) > MAX_FRAG_SIZE) {
    Warning("The fragments size exceed the limitation, ignore: %" PRId64 ", %d", data.rec_int, cache_config_target_fragment_size);
    return 0;
  }

  cache_config_target_fragment_size = data.rec_int;
  return 0;
}

void
ink_cache_init(ts::ModuleVersion v)
{
  ink_release_assert(v.check(CACHE_MODULE_VERSION));

  RecEstablishStaticConfigInt(cache_config_ram_cache_size, "proxy.config.cache.ram_cache.size");
  Dbg(dbg_ctl_cache_init, "proxy.config.cache.ram_cache.size = %" PRId64 " = %" PRId64 "Mb", cache_config_ram_cache_size,
      cache_config_ram_cache_size / (1024 * 1024));

  RecEstablishStaticConfigInt32(cache_config_ram_cache_algorithm, "proxy.config.cache.ram_cache.algorithm");
  RecEstablishStaticConfigInt32(cache_config_ram_cache_compress, "proxy.config.cache.ram_cache.compress");
  RecEstablishStaticConfigInt32(cache_config_ram_cache_compress_percent, "proxy.config.cache.ram_cache.compress_percent");
  cache_config_ram_cache_use_seen_filter = RecGetRecordInt("proxy.config.cache.ram_cache.use_seen_filter").value_or(0);

  RecEstablishStaticConfigInt32(cache_config_http_max_alts, "proxy.config.cache.limits.http.max_alts");
  Dbg(dbg_ctl_cache_init, "proxy.config.cache.limits.http.max_alts = %d", cache_config_http_max_alts);

  RecEstablishStaticConfigInt32(cache_config_log_alternate_eviction, "proxy.config.cache.log.alternate.eviction");
  Dbg(dbg_ctl_cache_init, "proxy.config.cache.log.alternate.eviction = %d", cache_config_log_alternate_eviction);

  RecEstablishStaticConfigInt(cache_config_ram_cache_cutoff, "proxy.config.cache.ram_cache_cutoff");
  Dbg(dbg_ctl_cache_init, "cache_config_ram_cache_cutoff = %" PRId64 " = %" PRId64 "Mb", cache_config_ram_cache_cutoff,
      cache_config_ram_cache_cutoff / (1024 * 1024));

  RecEstablishStaticConfigInt32(cache_config_permit_pinning, "proxy.config.cache.permit.pinning");
  Dbg(dbg_ctl_cache_init, "proxy.config.cache.permit.pinning = %d", cache_config_permit_pinning);

  RecEstablishStaticConfigInt32(cache_config_dir_sync_frequency, "proxy.config.cache.dir.sync_frequency");
  Dbg(dbg_ctl_cache_init, "proxy.config.cache.dir.sync_frequency = %d", cache_config_dir_sync_frequency);

  RecEstablishStaticConfigInt32(cache_config_dir_sync_delay, "proxy.config.cache.dir.sync_delay");
  Dbg(dbg_ctl_cache_init, "proxy.config.cache.dir.sync_delay = %d", cache_config_dir_sync_delay);

  RecEstablishStaticConfigInt32(cache_config_dir_sync_max_write, "proxy.config.cache.dir.sync_max_write");
  Dbg(dbg_ctl_cache_init, "proxy.config.cache.dir.sync_max_write = %d", cache_config_dir_sync_max_write);

  RecEstablishStaticConfigInt32(cache_config_select_alternate, "proxy.config.cache.select_alternate");
  Dbg(dbg_ctl_cache_init, "proxy.config.cache.select_alternate = %d", cache_config_select_alternate);

  RecEstablishStaticConfigInt32(cache_config_max_doc_size, "proxy.config.cache.max_doc_size");
  Dbg(dbg_ctl_cache_init, "proxy.config.cache.max_doc_size = %d = %dMb", cache_config_max_doc_size,
      cache_config_max_doc_size / (1024 * 1024));

  RecEstablishStaticConfigInt32(cache_config_mutex_retry_delay, "proxy.config.cache.mutex_retry_delay");
  Dbg(dbg_ctl_cache_init, "proxy.config.cache.mutex_retry_delay = %dms", cache_config_mutex_retry_delay);

  RecEstablishStaticConfigInt32(cache_config_read_while_writer_max_retries, "proxy.config.cache.read_while_writer.max_retries");
  Dbg(dbg_ctl_cache_init, "proxy.config.cache.read_while_writer.max_retries = %d", cache_config_read_while_writer_max_retries);

  RecEstablishStaticConfigInt32(cache_read_while_writer_retry_delay, "proxy.config.cache.read_while_writer_retry.delay");
  Dbg(dbg_ctl_cache_init, "proxy.config.cache.read_while_writer_retry.delay = %dms", cache_read_while_writer_retry_delay);

  RecEstablishStaticConfigInt32(cache_config_hit_evacuate_percent, "proxy.config.cache.hit_evacuate_percent");
  Dbg(dbg_ctl_cache_init, "proxy.config.cache.hit_evacuate_percent = %d", cache_config_hit_evacuate_percent);

  RecEstablishStaticConfigInt32(cache_config_hit_evacuate_size_limit, "proxy.config.cache.hit_evacuate_size_limit");
  Dbg(dbg_ctl_cache_init, "proxy.config.cache.hit_evacuate_size_limit = %d", cache_config_hit_evacuate_size_limit);

  RecEstablishStaticConfigInt32(cache_config_force_sector_size, "proxy.config.cache.force_sector_size");

  ink_assert(RecRegisterConfigUpdateCb("proxy.config.cache.target_fragment_size", FragmentSizeUpdateCb, nullptr) != REC_ERR_FAIL);
  cache_config_target_fragment_size = RecGetRecordInt("proxy.config.cache.target_fragment_size").value_or(0);

  if (cache_config_target_fragment_size == 0) {
    cache_config_target_fragment_size = DEFAULT_TARGET_FRAGMENT_SIZE;
  } else if (cache_config_target_fragment_size - sizeof(Doc) > MAX_FRAG_SIZE) {
    Warning("The fragments size exceed the limitation, setting to MAX_FRAG_SIZE (%ld)", MAX_FRAG_SIZE + sizeof(Doc));
    cache_config_target_fragment_size = MAX_FRAG_SIZE + sizeof(Doc);
  }

  RecEstablishStaticConfigInt32(cache_config_max_disk_errors, "proxy.config.cache.max_disk_errors");
  Dbg(dbg_ctl_cache_init, "proxy.config.cache.max_disk_errors = %d", cache_config_max_disk_errors);

  RecEstablishStaticConfigInt32(cache_config_agg_write_backlog, "proxy.config.cache.agg_write_backlog");
  Dbg(dbg_ctl_cache_init, "proxy.config.cache.agg_write_backlog = %d", cache_config_agg_write_backlog);

  RecEstablishStaticConfigInt32(cache_config_enable_checksum, "proxy.config.cache.enable_checksum");
  Dbg(dbg_ctl_cache_init, "proxy.config.cache.enable_checksum = %d", cache_config_enable_checksum);

  RecEstablishStaticConfigInt32(cache_config_alt_rewrite_max_size, "proxy.config.cache.alt_rewrite_max_size");
  Dbg(dbg_ctl_cache_init, "proxy.config.cache.alt_rewrite_max_size = %d", cache_config_alt_rewrite_max_size);

  RecEstablishStaticConfigInt32(cache_config_read_while_writer, "proxy.config.cache.enable_read_while_writer");
  cache_config_read_while_writer = validate_rww(cache_config_read_while_writer);
  RecRegisterConfigUpdateCb("proxy.config.cache.enable_read_while_writer", update_cache_config, nullptr);
  Dbg(dbg_ctl_cache_init, "proxy.config.cache.enable_read_while_writer = %d", cache_config_read_while_writer);

  register_cache_stats(&cache_rsb, "proxy.process.cache");

  cacheProcessor.wait_for_cache = RecGetRecordInt("proxy.config.http.wait_for_cache").value_or(0);

  RecEstablishStaticConfigInt32(cache_config_persist_bad_disks, "proxy.config.cache.persist_bad_disks");
  Dbg(dbg_ctl_cache_init, "proxy.config.cache.persist_bad_disks = %d", cache_config_persist_bad_disks);
  if (cache_config_persist_bad_disks) {
    std::filesystem::path localstatedir{Layout::get()->localstatedir};
    std::filesystem::path bad_disks_path{localstatedir / ts::filename::BAD_DISKS};
    std::fstream          bad_disks_file{bad_disks_path.c_str(), bad_disks_file.in};
    if (bad_disks_file.good()) {
      for (std::string line; std::getline(bad_disks_file, line);) {
        if (bad_disks_file.fail()) {
          Error("Failed while trying to read known bad disks file: %s", bad_disks_path.c_str());
          break;
        }
        if (!line.empty()) {
          known_bad_disks.insert(std::move(line));
        }
      }
    }
    // not having a bad disks file is not an error.

    unsigned long known_bad_count = known_bad_disks.size();
    Warning("%lu previously known bad disks were recorded in %s.  They will not be added to the cache.", known_bad_count,
            bad_disks_path.c_str());
  }

  Result result = theCacheStore.read_config();
  if (result.failed()) {
    Fatal("Failed to read cache configuration %s: %s", ts::filename::STORAGE, result.message());
  }
}

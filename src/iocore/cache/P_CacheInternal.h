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

#include "iocore/eventsystem/Continuation.h"
#include "tscore/ink_platform.h"
#include "tscore/InkErrno.h"

#include "proxy/hdrs/HTTP.h"
#include "P_CacheHttp.h"
#include "P_CacheHosting.h"
#include "tsutil/Metrics.h"

#include "iocore/cache/CacheVC.h"
#include "iocore/cache/CacheEvacuateDocVC.h"

using ts::Metrics;

struct EvacuationBlock;

// Compilation Options

#define ALTERNATES 1
// #define CACHE_LOCK_FAIL_RATE         0.001
// #define CACHE_AGG_FAIL_RATE          0.005
#define MAX_CACHE_VCS_PER_THREAD 500

#define INTEGRAL_FRAGS 4

#ifdef DEBUG
#define DDbg(dbg_ctl, fmt, ...) Dbg(dbg_ctl, fmt, ##__VA_ARGS__)
#else
#define DDbg(dbg_ctl, fmt, ...)
#endif

#define AIO_SOFT_FAILURE -100000

#ifndef CACHE_LOCK_FAIL_RATE
#define CACHE_TRY_LOCK(_l, _m, _t) MUTEX_TRY_LOCK(_l, _m, _t)
#else
#define CACHE_TRY_LOCK(_l, _m, _t)                                                    \
  MUTEX_TRY_LOCK(_l, _m, _t);                                                         \
  if ((uint32_t)_t->generator.random() < (uint32_t)(UINT_MAX * CACHE_LOCK_FAIL_RATE)) \
  CACHE_MUTEX_RELEASE(_l)
#endif

#define VC_LOCK_RETRY_EVENT()                                                                                         \
  do {                                                                                                                \
    trigger = mutex->thread_holding->schedule_in_local(this, HRTIME_MSECONDS(cache_config_mutex_retry_delay), event); \
    return EVENT_CONT;                                                                                                \
  } while (0)

#define VC_SCHED_LOCK_RETRY()                                                                                  \
  do {                                                                                                         \
    trigger = mutex->thread_holding->schedule_in_local(this, HRTIME_MSECONDS(cache_config_mutex_retry_delay)); \
    return EVENT_CONT;                                                                                         \
  } while (0)

#define CONT_SCHED_LOCK_RETRY_RET(_c)                                                                  \
  do {                                                                                                 \
    _c->mutex->thread_holding->schedule_in_local(_c, HRTIME_MSECONDS(cache_config_mutex_retry_delay)); \
    return EVENT_CONT;                                                                                 \
  } while (0)

#define CONT_SCHED_LOCK_RETRY(_c) _c->mutex->thread_holding->schedule_in_local(_c, HRTIME_MSECONDS(cache_config_mutex_retry_delay))

#define VC_SCHED_WRITER_RETRY()                                           \
  do {                                                                    \
    ink_assert(!trigger);                                                 \
    writer_lock_retry++;                                                  \
    ink_hrtime _t = HRTIME_MSECONDS(cache_read_while_writer_retry_delay); \
    if (writer_lock_retry > 2)                                            \
      _t = HRTIME_MSECONDS(cache_read_while_writer_retry_delay) * 2;      \
    trigger = mutex->thread_holding->schedule_in_local(this, _t);         \
    return EVENT_CONT;                                                    \
  } while (0)

extern CacheStatsBlock cache_rsb;

// Configuration
extern int cache_config_dir_sync_frequency;
extern int cache_config_http_max_alts;
extern int cache_config_log_alternate_eviction;
extern int cache_config_permit_pinning;
extern int cache_config_select_alternate;
extern int cache_config_max_doc_size;
extern int cache_config_min_average_object_size;
extern int cache_config_agg_write_backlog;
extern int cache_config_enable_checksum;
extern int cache_config_alt_rewrite_max_size;
extern int cache_config_read_while_writer;
extern int cache_config_agg_write_backlog;
extern int cache_config_ram_cache_compress;
extern int cache_config_ram_cache_compress_percent;
extern int cache_config_ram_cache_use_seen_filter;
extern int cache_config_hit_evacuate_percent;
extern int cache_config_hit_evacuate_size_limit;
extern int cache_config_force_sector_size;
extern int cache_config_target_fragment_size;
extern int cache_config_mutex_retry_delay;
extern int cache_read_while_writer_retry_delay;
extern int cache_config_read_while_writer_max_retries;

#define PUSH_HANDLER(_x)                                          \
  do {                                                            \
    ink_assert(handler != (ContinuationHandler)(&CacheVC::dead)); \
    save_handler = handler;                                       \
    handler      = (ContinuationHandler)(_x);                     \
  } while (0)

#define POP_HANDLER                                               \
  do {                                                            \
    handler = save_handler;                                       \
    ink_assert(handler != (ContinuationHandler)(&CacheVC::dead)); \
  } while (0)

struct CacheRemoveCont : public Continuation {
  int event_handler(int event, void *data);

  CacheRemoveCont() : Continuation(nullptr) {}
};

// Global Data
extern ClassAllocator<CacheVC>            cacheVConnectionAllocator;
extern ClassAllocator<CacheEvacuateDocVC> cacheEvacuateDocVConnectionAllocator;
extern CacheSync                         *cacheDirSync;
// Function Prototypes
int                 cache_write(CacheVC *, CacheHTTPInfoVector *);
int                 get_alternate_index(CacheHTTPInfoVector *cache_vector, CacheKey key);
CacheEvacuateDocVC *new_DocEvacuator(int nbytes, Stripe *stripe);

// inline Functions

inline CacheVC *
new_CacheVC(Continuation *cont)
{
  EThread *t          = cont->mutex->thread_holding;
  CacheVC *c          = THREAD_ALLOC(cacheVConnectionAllocator, t);
  c->vector.data.data = &c->vector.data.fast_data[0];
  c->_action          = cont;
  c->mutex            = cont->mutex;
  c->start_time       = ink_get_hrtime();
  c->setThreadAffinity(t);
  ink_assert(c->trigger == nullptr);
  static DbgCtl dbg_ctl{"cache_new"};
  Dbg(dbg_ctl, "new %p", c);
  dir_clear(&c->dir);
  return c;
}

inline int
free_CacheVCCommon(CacheVC *cont)
{
  static DbgCtl dbg_ctl{"cache_free"};
  Dbg(dbg_ctl, "free %p", cont);
  ProxyMutex *mutex  = cont->mutex.get();
  Stripe     *stripe = cont->stripe;

  if (stripe) {
    Metrics::Gauge::decrement(cache_rsb.status[cont->op_type].active);
    Metrics::Gauge::decrement(stripe->cache_vol->vol_rsb.status[cont->op_type].active);
    if (cont->closed > 0) {
      Metrics::Counter::increment(cache_rsb.status[cont->op_type].success);
      Metrics::Counter::increment(stripe->cache_vol->vol_rsb.status[cont->op_type].success);
    } // else abort,cancel
  }
  ink_assert(mutex->thread_holding == this_ethread());
  if (cont->trigger) {
    cont->trigger->cancel();
  }
  ink_assert(!cont->is_io_in_progress());
  ink_assert(!cont->od);
  cont->io.action = nullptr;
  cont->io.mutex.clear();
  cont->io.aio_result       = 0;
  cont->io.aiocb.aio_nbytes = 0;
  cont->request.reset();
  cont->vector.clear();
  cont->vio.buffer.clear();
  cont->vio.mutex.clear();
  if (cont->vio.op == VIO::WRITE && cont->alternate_index == CACHE_ALT_INDEX_DEFAULT) {
    cont->alternate.destroy();
  } else {
    cont->alternate.clear();
  }
  cont->_action.cancelled = 0;
  cont->_action.mutex.clear();
  cont->mutex.clear();
  cont->buf.clear();
  cont->first_buf.clear();
  cont->blocks.clear();
  cont->writer_buf.clear();
  cont->alternate_index = CACHE_ALT_INDEX_DEFAULT;

  ats_free(cont->scan_stripe_map);

  memset((char *)&cont->vio, 0, cont->size_to_init);
#ifdef DEBUG
  SET_CONTINUATION_HANDLER(cont, &CacheVC::dead);
#endif
  return EVENT_DONE;
}

inline int
free_CacheVC(CacheVC *cont)
{
  free_CacheVCCommon(cont);
  THREAD_FREE(cont, cacheVConnectionAllocator, this_thread());
  return EVENT_DONE;
}

inline int
free_CacheEvacuateDocVC(CacheEvacuateDocVC *cont)
{
  free_CacheVCCommon(cont);
  THREAD_FREE(cont, cacheEvacuateDocVConnectionAllocator, this_thread());
  return EVENT_DONE;
}

inline int
CacheVC::calluser(int event)
{
  recursive++;
  ink_assert(!stripe || this_ethread() != stripe->mutex->thread_holding);
  vio.cont->handleEvent(event, (void *)&vio);
  recursive--;
  if (closed) {
    die();
    return EVENT_DONE;
  }
  return EVENT_CONT;
}

inline int
CacheVC::callcont(int event)
{
  recursive++;
  ink_assert(!stripe || this_ethread() != stripe->mutex->thread_holding);
  _action.continuation->handleEvent(event, this);
  recursive--;
  if (closed) {
    die();
  } else if (vio.vc_server) {
    handleEvent(EVENT_IMMEDIATE, nullptr);
  }
  return EVENT_DONE;
}

inline int
CacheVC::do_read_call(CacheKey *akey)
{
  doc_pos             = 0;
  read_key            = akey;
  io.aiocb.aio_nbytes = dir_approx_size(&dir);
  PUSH_HANDLER(&CacheVC::handleRead);
  return handleRead(EVENT_CALL, nullptr);
}

inline int
CacheVC::do_write_call()
{
  PUSH_HANDLER(&CacheVC::handleWrite);
  return handleWrite(EVENT_CALL, nullptr);
}

inline void
CacheVC::cancel_trigger()
{
  if (trigger) {
    trigger->cancel_action();
    trigger = nullptr;
  }
}

inline int
CacheVC::die()
{
  if (vio.op == VIO::WRITE) {
    if (f.update && total_len) {
      alternate.object_key_set(earliest_key);
    }
    if (!is_io_in_progress()) {
      SET_HANDLER(&CacheVC::openWriteClose);
      if (!recursive) {
        openWriteClose(EVENT_NONE, nullptr);
      }
    } // else catch it at the end of openWriteWriteDone
    return EVENT_CONT;
  } else {
    if (is_io_in_progress()) {
      save_handler = (ContinuationHandler)&CacheVC::openReadClose;
    } else {
      SET_HANDLER(&CacheVC::openReadClose);
      if (!recursive) {
        openReadClose(EVENT_NONE, nullptr);
      }
    }
    return EVENT_CONT;
  }
}

inline int
CacheVC::handleWriteLock(int /* event ATS_UNUSED */, Event *e)
{
  cancel_trigger();
  int ret = 0;
  {
    CACHE_TRY_LOCK(lock, stripe->mutex, mutex->thread_holding);
    if (!lock.is_locked()) {
      set_agg_write_in_progress();
      trigger = mutex->thread_holding->schedule_in_local(this, HRTIME_MSECONDS(cache_config_mutex_retry_delay));
      return EVENT_CONT;
    }
    ret = handleWrite(EVENT_CALL, e);
  }
  if (ret == EVENT_RETURN) {
    return handleEvent(AIO_EVENT_DONE, nullptr);
  }
  return EVENT_CONT;
}

inline int
CacheVC::do_write_lock()
{
  PUSH_HANDLER(&CacheVC::handleWriteLock);
  return handleWriteLock(EVENT_NONE, nullptr);
}

inline int
CacheVC::do_write_lock_call()
{
  PUSH_HANDLER(&CacheVC::handleWriteLock);
  return handleWriteLock(EVENT_CALL, nullptr);
}

inline bool
CacheVC::writer_done()
{
  OpenDirEntry *cod = od;
  if (!cod) {
    cod = stripe->open_read(&first_key);
  }
  CacheVC *w = (cod) ? cod->writers.head : nullptr;
  // If the write vc started after the reader, then its not the
  // original writer, since we never choose a writer that started
  // after the reader. The original writer was deallocated and then
  // reallocated for the same first_key
  for (; w && (w != write_vc || w->start_time > start_time); w = (CacheVC *)w->opendir_link.next) {
    ;
  }
  if (!w) {
    return true;
  }
  return false;
}

inline int
Stripe::close_write(CacheVC *cont)
{
  return open_dir.close_write(cont);
}

// Returns 0 on success or a positive error code on failure
inline int
Stripe::open_write(CacheVC *cont, int allow_if_writers, int max_writers)
{
  Stripe *stripe    = this;
  bool    agg_error = false;
  if (!cont->f.remove) {
    agg_error = (!cont->f.update && this->get_agg_todo_size() > cache_config_agg_write_backlog);
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

inline int
Stripe::close_write_lock(CacheVC *cont)
{
  EThread *t = cont->mutex->thread_holding;
  CACHE_TRY_LOCK(lock, mutex, t);
  if (!lock.is_locked()) {
    return -1;
  }
  return close_write(cont);
}

inline int
Stripe::open_write_lock(CacheVC *cont, int allow_if_writers, int max_writers)
{
  EThread *t = cont->mutex->thread_holding;
  CACHE_TRY_LOCK(lock, mutex, t);
  if (!lock.is_locked()) {
    return -1;
  }
  return open_write(cont, allow_if_writers, max_writers);
}

inline OpenDirEntry *
Stripe::open_read_lock(CryptoHash *key, EThread *t)
{
  CACHE_TRY_LOCK(lock, mutex, t);
  if (!lock.is_locked()) {
    return nullptr;
  }
  return open_dir.open_read(key);
}

inline int
Stripe::begin_read_lock(CacheVC *cont)
{
  // no need for evacuation as the entire document is already in memory
  if (cont->f.single_fragment) {
    return 0;
  }

  EThread *t = cont->mutex->thread_holding;
  CACHE_TRY_LOCK(lock, mutex, t);
  if (!lock.is_locked()) {
    return -1;
  }
  return begin_read(cont);
}

inline int
Stripe::close_read_lock(CacheVC *cont)
{
  EThread *t = cont->mutex->thread_holding;
  CACHE_TRY_LOCK(lock, mutex, t);
  if (!lock.is_locked()) {
    return -1;
  }
  return close_read(cont);
}

inline int
dir_delete_lock(CacheKey *key, Stripe *stripe, ProxyMutex *m, Dir *del)
{
  EThread *thread = m->thread_holding;
  CACHE_TRY_LOCK(lock, stripe->mutex, thread);
  if (!lock.is_locked()) {
    return -1;
  }
  return dir_delete(key, stripe, del);
}

inline int
dir_insert_lock(CacheKey *key, Stripe *stripe, Dir *to_part, ProxyMutex *m)
{
  EThread *thread = m->thread_holding;
  CACHE_TRY_LOCK(lock, stripe->mutex, thread);
  if (!lock.is_locked()) {
    return -1;
  }
  return dir_insert(key, stripe, to_part);
}

inline int
dir_overwrite_lock(CacheKey *key, Stripe *stripe, Dir *to_part, ProxyMutex *m, Dir *overwrite, bool must_overwrite = true)
{
  EThread *thread = m->thread_holding;
  CACHE_TRY_LOCK(lock, stripe->mutex, thread);
  if (!lock.is_locked()) {
    return -1;
  }
  return dir_overwrite(key, stripe, to_part, overwrite, must_overwrite);
}

void inline rand_CacheKey(CacheKey *next_key)
{
  EThread *ethread = this_ethread();
  next_key->b[0]   = ethread->generator.random();
  next_key->b[1]   = ethread->generator.random();
}

extern const uint8_t CacheKey_next_table[256];
void inline next_CacheKey(CacheKey *next, const CacheKey *key)
{
  next->u8[0] = CacheKey_next_table[key->u8[0]];
  for (int i = 1; i < 16; i++) {
    next->u8[i] = CacheKey_next_table[(next->u8[i - 1] + key->u8[i]) & 0xFF];
  }
}

extern const uint8_t CacheKey_prev_table[256];
void inline prev_CacheKey(CacheKey *prev, const CacheKey *key)
{
  for (int i = 15; i > 0; i--) {
    prev->u8[i] = 256 + CacheKey_prev_table[key->u8[i]] - key->u8[i - 1];
  }
  prev->u8[0] = CacheKey_prev_table[key->u8[0]];
}

inline unsigned int
next_rand(unsigned int *p)
{
  unsigned int seed = *p;
  seed              = 1103515145 * seed + 12345;
  *p                = seed;
  return seed;
}

extern ClassAllocator<CacheRemoveCont> cacheRemoveContAllocator;

inline CacheRemoveCont *
new_CacheRemoveCont()
{
  CacheRemoveCont *cache_rm = cacheRemoveContAllocator.alloc();

  cache_rm->mutex = new_ProxyMutex();
  SET_CONTINUATION_HANDLER(cache_rm, &CacheRemoveCont::event_handler);
  return cache_rm;
}

inline void
free_CacheRemoveCont(CacheRemoveCont *cache_rm)
{
  cache_rm->mutex = nullptr;
  cacheRemoveContAllocator.free(cache_rm);
}

inline int
CacheRemoveCont::event_handler(int event, void *data)
{
  (void)event;
  (void)data;
  free_CacheRemoveCont(this);
  return EVENT_DONE;
}

struct CacheHostRecord;
class Stripe;
class CacheHostTable;

struct Cache {
  int       cache_read_done       = 0;
  int       total_good_nvol       = 0;
  int       total_nvol            = 0;
  int       ready                 = CACHE_INITIALIZING;
  int64_t   cache_size            = 0; // in store block size
  int       total_initialized_vol = 0;
  CacheType scheme                = CACHE_NONE_TYPE;

  ReplaceablePtr<CacheHostTable> hosttable;

  int open(bool reconfigure, bool fix);
  int close();

  Action *lookup(Continuation *cont, const CacheKey *key, CacheFragType type, const char *hostname, int host_len);
  Action *open_read(Continuation *cont, const CacheKey *key, CacheFragType type, const char *hostname, int len);
  Action *open_write(Continuation *cont, const CacheKey *key, CacheFragType frag_type, int options = 0,
                     time_t pin_in_cache = (time_t)0, const char *hostname = nullptr, int host_len = 0);
  Action *remove(Continuation *cont, const CacheKey *key, CacheFragType type = CACHE_FRAG_TYPE_HTTP, const char *hostname = nullptr,
                 int host_len = 0);
  Action *scan(Continuation *cont, const char *hostname = nullptr, int host_len = 0, int KB_per_second = 2500);

  Action     *open_read(Continuation *cont, const CacheKey *key, CacheHTTPHdr *request, const HttpConfigAccessor *params,
                        CacheFragType type, const char *hostname, int host_len);
  Action     *open_write(Continuation *cont, const CacheKey *key, CacheHTTPInfo *old_info, time_t pin_in_cache = (time_t)0,
                         const CacheKey *key1 = nullptr, CacheFragType type = CACHE_FRAG_TYPE_HTTP, const char *hostname = nullptr,
                         int host_len = 0);
  static void generate_key(CryptoHash *hash, CacheURL *url);
  static void generate_key(HttpCacheKey *hash, CacheURL *url, bool ignore_query = false, cache_generation_t generation = -1);

  void vol_initialized(bool result);

  int open_done();

  Stripe *key_to_stripe(const CacheKey *key, const char *hostname, int host_len);

  Cache() {}
};

extern Cache *theCache;
extern Cache *caches[NUM_CACHE_FRAG_TYPES];

inline void
Cache::generate_key(CryptoHash *hash, CacheURL *url)
{
  url->hash_get(hash);
}

inline void
Cache::generate_key(HttpCacheKey *key, CacheURL *url, bool ignore_query, cache_generation_t generation)
{
  key->hostname = url->host_get(&key->hostlen);
  url->hash_get(&key->hash, ignore_query, generation);
}

inline unsigned int
cache_hash(const CryptoHash &hash)
{
  uint64_t     f     = hash.fold();
  unsigned int mhash = (unsigned int)(f >> 32);
  return mhash;
}

/** @file

  Record statistics support.

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

#include "P_RecCore.h"
#include "P_RecProcess.h"
#include <string_view>

//-------------------------------------------------------------------------
// raw_stat_get_total
//-------------------------------------------------------------------------

namespace
{
// Commonly used access to a raw stat, avoid typos.
inline RecRawStat *
thread_stat(EThread *et, RecRawStatBlock *rsb, int id)
{
  return (reinterpret_cast<RecRawStat *>(reinterpret_cast<char *>(et) + rsb->ethr_stat_offset)) + id;
}
} // namespace

static int
raw_stat_get_total(RecRawStatBlock *rsb, int id, RecRawStat *total)
{
  total->sum   = 0;
  total->count = 0;

  // get global values
  total->sum   = rsb->global[id]->sum;
  total->count = rsb->global[id]->count;

  // get thread local values
  for (EThread *et : eventProcessor.active_ethreads()) {
    RecRawStat *tlp = thread_stat(et, rsb, id);
    total->sum += tlp->sum;
    total->count += tlp->count;
  }

  for (EThread *et : eventProcessor.active_dthreads()) {
    RecRawStat *tlp = thread_stat(et, rsb, id);
    total->sum += tlp->sum;
    total->count += tlp->count;
  }

  if (total->sum < 0) { // Assure that we stay positive
    total->sum = 0;
  }

  return REC_ERR_OKAY;
}

//-------------------------------------------------------------------------
// raw_stat_sync_to_global
//-------------------------------------------------------------------------
static int
raw_stat_sync_to_global(RecRawStatBlock *rsb, int id)
{
  RecRawStat total;

  total.sum   = 0;
  total.count = 0;

  // sum the thread local values
  for (EThread *et : eventProcessor.active_ethreads()) {
    RecRawStat *tlp = thread_stat(et, rsb, id);
    total.sum += tlp->sum;
    total.count += tlp->count;
  }

  for (EThread *et : eventProcessor.active_dthreads()) {
    RecRawStat *tlp = thread_stat(et, rsb, id);
    total.sum += tlp->sum;
    total.count += tlp->count;
  }

  if (total.sum < 0) { // Assure that we stay positive
    total.sum = 0;
  }

  // lock so the setting of the globals and last values are atomic
  {
    ink_scoped_mutex_lock lock(rsb->mutex);

    // get the delta from the last sync
    RecRawStat delta;
    delta.sum   = total.sum - rsb->global[id]->last_sum;
    delta.count = total.count - rsb->global[id]->last_count;

    // increment the global values by the delta
    ink_atomic_increment(&(rsb->global[id]->sum), delta.sum);
    ink_atomic_increment(&(rsb->global[id]->count), delta.count);

    // set the new totals as the last values seen
    ink_atomic_swap(&(rsb->global[id]->last_sum), total.sum);
    ink_atomic_swap(&(rsb->global[id]->last_count), total.count);
  }

  return REC_ERR_OKAY;
}

//-------------------------------------------------------------------------
// raw_stat_clear
//-------------------------------------------------------------------------
static int
raw_stat_clear(RecRawStatBlock *rsb, int id)
{
  Debug("stats", "raw_stat_clear(): rsb pointer:%p id:%d", rsb, id);

  // the globals need to be reset too
  // lock so the setting of the globals and last values are atomic
  {
    ink_scoped_mutex_lock lock(rsb->mutex);
    ink_atomic_swap(&(rsb->global[id]->sum), (int64_t)0);
    ink_atomic_swap(&(rsb->global[id]->last_sum), (int64_t)0);
    ink_atomic_swap(&(rsb->global[id]->count), (int64_t)0);
    ink_atomic_swap(&(rsb->global[id]->last_count), (int64_t)0);
  }
  // reset the local stats
  for (EThread *et : eventProcessor.active_ethreads()) {
    RecRawStat *tlp = thread_stat(et, rsb, id);
    ink_atomic_swap(&(tlp->sum), (int64_t)0);
    ink_atomic_swap(&(tlp->count), (int64_t)0);
  }

  for (EThread *et : eventProcessor.active_dthreads()) {
    RecRawStat *tlp = thread_stat(et, rsb, id);
    ink_atomic_swap(&(tlp->sum), (int64_t)0);
    ink_atomic_swap(&(tlp->count), (int64_t)0);
  }

  return REC_ERR_OKAY;
}

//-------------------------------------------------------------------------
// raw_stat_clear_sum
//-------------------------------------------------------------------------
static int
raw_stat_clear_sum(RecRawStatBlock *rsb, int id)
{
  Debug("stats", "raw_stat_clear_sum(): rsb pointer:%p id:%d", rsb, id);

  // the globals need to be reset too
  // lock so the setting of the globals and last values are atomic
  {
    ink_scoped_mutex_lock lock(rsb->mutex);
    ink_atomic_swap(&(rsb->global[id]->sum), (int64_t)0);
    ink_atomic_swap(&(rsb->global[id]->last_sum), (int64_t)0);
  }

  // reset the local stats
  for (EThread *et : eventProcessor.active_ethreads()) {
    RecRawStat *tlp = thread_stat(et, rsb, id);
    ink_atomic_swap(&(tlp->sum), (int64_t)0);
  }

  for (EThread *et : eventProcessor.active_dthreads()) {
    RecRawStat *tlp = thread_stat(et, rsb, id);
    ink_atomic_swap(&(tlp->sum), (int64_t)0);
  }

  return REC_ERR_OKAY;
}

//-------------------------------------------------------------------------
// raw_stat_clear_count
//-------------------------------------------------------------------------
static int
raw_stat_clear_count(RecRawStatBlock *rsb, int id)
{
  Debug("stats", "raw_stat_clear_count(): rsb pointer:%p id:%d", rsb, id);

  // the globals need to be reset too
  // lock so the setting of the globals and last values are atomic
  {
    ink_scoped_mutex_lock lock(rsb->mutex);
    ink_atomic_swap(&(rsb->global[id]->count), (int64_t)0);
    ink_atomic_swap(&(rsb->global[id]->last_count), (int64_t)0);
  }

  // reset the local stats
  for (EThread *et : eventProcessor.active_ethreads()) {
    RecRawStat *tlp = thread_stat(et, rsb, id);
    ink_atomic_swap(&(tlp->count), (int64_t)0);
  }

  for (EThread *et : eventProcessor.active_dthreads()) {
    RecRawStat *tlp = thread_stat(et, rsb, id);
    ink_atomic_swap(&(tlp->count), (int64_t)0);
  }

  return REC_ERR_OKAY;
}

//-------------------------------------------------------------------------
// RecAllocateRawStatBlock
//-------------------------------------------------------------------------
RecRawStatBlock *
RecAllocateRawStatBlock(int num_stats)
{
  off_t ethr_stat_offset;
  RecRawStatBlock *rsb;

  // allocate thread-local raw-stat memory
  if ((ethr_stat_offset = eventProcessor.allocate(num_stats * sizeof(RecRawStat))) == -1) {
    return nullptr;
  }

  // create the raw-stat-block structure
  rsb = (RecRawStatBlock *)ats_malloc(sizeof(RecRawStatBlock));
  memset(rsb, 0, sizeof(RecRawStatBlock));

  rsb->global = (RecRawStat **)ats_malloc(num_stats * sizeof(RecRawStat *));
  memset(rsb->global, 0, num_stats * sizeof(RecRawStat *));

  rsb->num_stats        = 0;
  rsb->max_stats        = num_stats;
  rsb->ethr_stat_offset = ethr_stat_offset;

  ink_mutex_init(&(rsb->mutex));
  return rsb;
}

//-------------------------------------------------------------------------
// RecRegisterRawStat
//-------------------------------------------------------------------------
int
_RecRegisterRawStat(RecRawStatBlock *rsb, RecT rec_type, const char *name, RecDataT data_type, RecPersistT persist_type, int id,
                    RecRawStatSyncCb sync_cb)
{
  Debug("stats", "RecRawStatSyncCb(%s): rsb pointer:%p id:%d", name, rsb, id);

  // check to see if we're good to proceed
  ink_assert(id < rsb->max_stats);

  int err = REC_ERR_OKAY;

  RecRecord *r;
  RecData data_default;
  memset(&data_default, 0, sizeof(RecData));

  // register the record
  if ((r = RecRegisterStat(rec_type, name, data_type, data_default, persist_type)) == nullptr) {
    err = REC_ERR_FAIL;
    goto Ldone;
  }

  r->rsb_id = id; // This is the index within the RSB raw block for this stat, used for lookups by name.
  if (i_am_the_record_owner(r->rec_type)) {
    r->sync_required = r->sync_required | REC_PEER_SYNC_REQUIRED;
  } else {
    send_register_message(r);
  }

  // store a pointer to our record->stat_meta.data_raw in our rsb
  rsb->global[id]             = &(r->stat_meta.data_raw);
  rsb->global[id]->last_sum   = 0;
  rsb->global[id]->last_count = 0;

  // setup the periodic sync callback
  if (sync_cb) {
    RecRegisterRawStatSyncCb(name, sync_cb, rsb, id);
  }

Ldone:
  return err;
}

//-------------------------------------------------------------------------
// RecRawStatSync...
//-------------------------------------------------------------------------

// Note: On these RecRawStatSync callbacks, our 'data' is protected
// under its lock by the caller, so no need to worry!
int
RecRawStatSyncSum(const char *name, RecDataT data_type, RecData *data, RecRawStatBlock *rsb, int id)
{
  RecRawStat total;

  Debug("stats", "raw sync:sum for %s", name);
  raw_stat_sync_to_global(rsb, id);
  total.sum   = rsb->global[id]->sum;
  total.count = rsb->global[id]->count;
  RecDataSetFromInt64(data_type, data, total.sum);

  return REC_ERR_OKAY;
}

int
RecRawStatSyncCount(const char *name, RecDataT data_type, RecData *data, RecRawStatBlock *rsb, int id)
{
  RecRawStat total;

  Debug("stats", "raw sync:count for %s", name);
  raw_stat_sync_to_global(rsb, id);
  total.sum   = rsb->global[id]->sum;
  total.count = rsb->global[id]->count;
  RecDataSetFromInt64(data_type, data, total.count);

  return REC_ERR_OKAY;
}

int
RecRawStatSyncAvg(const char *name, RecDataT data_type, RecData *data, RecRawStatBlock *rsb, int id)
{
  RecRawStat total;
  RecFloat avg = 0.0f;

  Debug("stats", "raw sync:avg for %s", name);
  raw_stat_sync_to_global(rsb, id);
  total.sum   = rsb->global[id]->sum;
  total.count = rsb->global[id]->count;
  if (total.count != 0) {
    avg = (float)((double)total.sum / (double)total.count);
  }
  RecDataSetFromFloat(data_type, data, avg);
  return REC_ERR_OKAY;
}

int
RecRawStatSyncHrTimeAvg(const char *name, RecDataT data_type, RecData *data, RecRawStatBlock *rsb, int id)
{
  RecRawStat total;
  RecFloat r;

  Debug("stats", "raw sync:hr-timeavg for %s", name);
  raw_stat_sync_to_global(rsb, id);
  total.sum   = rsb->global[id]->sum;
  total.count = rsb->global[id]->count;

  if (total.count == 0) {
    r = 0.0f;
  } else {
    r = (float)((double)total.sum / (double)total.count);
    r = r / (float)(HRTIME_SECOND);
  }

  RecDataSetFromFloat(data_type, data, r);
  return REC_ERR_OKAY;
}

int
RecRawStatSyncIntMsecsToFloatSeconds(const char *name, RecDataT data_type, RecData *data, RecRawStatBlock *rsb, int id)
{
  RecRawStat total;
  RecFloat r;

  Debug("stats", "raw sync:seconds for %s", name);
  raw_stat_sync_to_global(rsb, id);
  total.sum   = rsb->global[id]->sum;
  total.count = rsb->global[id]->count;

  if (total.count == 0) {
    r = 0.0f;
  } else {
    r = (float)((double)total.sum / 1000);
  }

  RecDataSetFromFloat(data_type, data, r);
  return REC_ERR_OKAY;
}

//-------------------------------------------------------------------------
// RecSetRawStatXXX
//-------------------------------------------------------------------------
int
RecSetRawStatSum(RecRawStatBlock *rsb, int id, int64_t data)
{
  raw_stat_clear_sum(rsb, id);
  ink_atomic_swap(&(rsb->global[id]->sum), data);
  return REC_ERR_OKAY;
}

int
RecSetRawStatCount(RecRawStatBlock *rsb, int id, int64_t data)
{
  raw_stat_clear_count(rsb, id);
  ink_atomic_swap(&(rsb->global[id]->count), data);
  return REC_ERR_OKAY;
}

//-------------------------------------------------------------------------
// RecGetRawStatXXX
//-------------------------------------------------------------------------

int
RecGetRawStatSum(RecRawStatBlock *rsb, int id, int64_t *data)
{
  RecRawStat total;

  raw_stat_get_total(rsb, id, &total);
  *data = total.sum;
  return REC_ERR_OKAY;
}

int
RecGetRawStatCount(RecRawStatBlock *rsb, int id, int64_t *data)
{
  RecRawStat total;

  raw_stat_get_total(rsb, id, &total);
  *data = total.count;
  return REC_ERR_OKAY;
}

//-------------------------------------------------------------------------
// RecIncrGlobalRawStatXXX
//-------------------------------------------------------------------------
int
RecIncrGlobalRawStat(RecRawStatBlock *rsb, int id, int64_t incr)
{
  ink_atomic_increment(&(rsb->global[id]->sum), incr);
  ink_atomic_increment(&(rsb->global[id]->count), 1);
  return REC_ERR_OKAY;
}

int
RecIncrGlobalRawStatSum(RecRawStatBlock *rsb, int id, int64_t incr)
{
  ink_atomic_increment(&(rsb->global[id]->sum), incr);
  return REC_ERR_OKAY;
}

int
RecIncrGlobalRawStatCount(RecRawStatBlock *rsb, int id, int64_t incr)
{
  ink_atomic_increment(&(rsb->global[id]->count), incr);
  return REC_ERR_OKAY;
}

//-------------------------------------------------------------------------
// RecSetGlobalRawStatXXX
//-------------------------------------------------------------------------
int
RecSetGlobalRawStatSum(RecRawStatBlock *rsb, int id, int64_t data)
{
  ink_atomic_swap(&(rsb->global[id]->sum), data);
  return REC_ERR_OKAY;
}

int
RecSetGlobalRawStatCount(RecRawStatBlock *rsb, int id, int64_t data)
{
  ink_atomic_swap(&(rsb->global[id]->count), data);
  return REC_ERR_OKAY;
}

//-------------------------------------------------------------------------
// RecGetGlobalRawStatXXX
//-------------------------------------------------------------------------
int
RecGetGlobalRawStatSum(RecRawStatBlock *rsb, int id, int64_t *data)
{
  *data = rsb->global[id]->sum;
  return REC_ERR_OKAY;
}

int
RecGetGlobalRawStatCount(RecRawStatBlock *rsb, int id, int64_t *data)
{
  *data = rsb->global[id]->count;
  return REC_ERR_OKAY;
}

//-------------------------------------------------------------------------
// RegGetGlobalRawStatXXXPtr
//-------------------------------------------------------------------------
RecRawStat *
RecGetGlobalRawStatPtr(RecRawStatBlock *rsb, int id)
{
  return rsb->global[id];
}

int64_t *
RecGetGlobalRawStatSumPtr(RecRawStatBlock *rsb, int id)
{
  return &(rsb->global[id]->sum);
}

int64_t *
RecGetGlobalRawStatCountPtr(RecRawStatBlock *rsb, int id)
{
  return &(rsb->global[id]->count);
}

//-------------------------------------------------------------------------
// RecRegisterRawStatSyncCb
//-------------------------------------------------------------------------
int
RecRegisterRawStatSyncCb(const char *name, RecRawStatSyncCb sync_cb, RecRawStatBlock *rsb, int id)
{
  int err = REC_ERR_FAIL;

  ink_rwlock_rdlock(&g_records_rwlock);
  if (auto it = g_records_ht.find(name); it != g_records_ht.end()) {
    RecRecord *r = it->second;

    rec_mutex_acquire(&(r->lock));
    if (REC_TYPE_IS_STAT(r->rec_type)) {
      if (r->stat_meta.sync_cb) {
        // We shouldn't register sync callbacks twice...
        Fatal("attempted to register %s twice", name);
      }

      RecRawStat *raw;

      r->stat_meta.sync_rsb = rsb;
      r->stat_meta.sync_id  = id;
      r->stat_meta.sync_cb  = sync_cb;

      raw = RecGetGlobalRawStatPtr(r->stat_meta.sync_rsb, r->stat_meta.sync_id);

      raw->version = r->version;

      err = REC_ERR_OKAY;
    }
    rec_mutex_release(&(r->lock));
  }

  ink_rwlock_unlock(&g_records_rwlock);

  return err;
}

//-------------------------------------------------------------------------
// RecExecRawStatSyncCbs
//-------------------------------------------------------------------------
int
RecExecRawStatSyncCbs()
{
  RecRecord *r;
  int i, num_records;

  num_records = g_num_records;
  for (i = 0; i < num_records; i++) {
    r = &(g_records[i]);
    rec_mutex_acquire(&(r->lock));
    if (REC_TYPE_IS_STAT(r->rec_type)) {
      if (r->stat_meta.sync_cb) {
        if (r->version && r->version != r->stat_meta.sync_rsb->global[r->stat_meta.sync_id]->version) {
          raw_stat_clear(r->stat_meta.sync_rsb, r->stat_meta.sync_id);
          r->stat_meta.sync_rsb->global[r->stat_meta.sync_id]->version = r->version;
        } else {
          (*(r->stat_meta.sync_cb))(r->name, r->data_type, &(r->data), r->stat_meta.sync_rsb, r->stat_meta.sync_id);
        }
        r->sync_required = REC_SYNC_REQUIRED;
      }
    }
    rec_mutex_release(&(r->lock));
  }

  return REC_ERR_OKAY;
}

int
RecRawStatUpdateSum(RecRawStatBlock *rsb, int id)
{
  RecRawStat *raw = rsb->global[id];
  if (nullptr != raw) {
    RecRecord *r = reinterpret_cast<RecRecord *>(reinterpret_cast<char *>(raw) -
                                                 (reinterpret_cast<char *>(&reinterpret_cast<RecRecord *>(0)->stat_meta) -
                                                  reinterpret_cast<char *>(reinterpret_cast<RecRecord *>(0))));

    RecDataSetFromInt64(r->data_type, &r->data, rsb->global[id]->sum);
    r->sync_required = REC_SYNC_REQUIRED;
    return REC_ERR_OKAY;
  }
  return REC_ERR_FAIL;
}

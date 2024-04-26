/** @file

Record statistics support (EThread implementation).

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

#include "records/RecDefs.h"
#include "../../records/P_RecCore.h"
#include "../../records/P_RecProcess.h"
#include <string_view>

//-------------------------------------------------------------------------
// raw_stat_get_total
//-------------------------------------------------------------------------

struct RecRawStatBlockOpsImpl : RecRawStatBlockOps {
  int raw_stat_clear(RecRawStatBlock *rsb, int id) override;
  int raw_stat_clear_count(RecRawStatBlock *rsb, int id) override;
  int raw_stat_clear_sum(RecRawStatBlock *rsb, int id) override;
  int raw_stat_get_total(RecRawStatBlock *rsb, int id, RecRawStat *total) override;
  int raw_stat_sync_to_global(RecRawStatBlock *rsb, int id) override;

private:
  inline static DbgCtl _dbg_ctl{"stats"};
};

RecRawStatBlock *
RecAllocateRawStatBlockImpl(int num_stats)
{
  static RecRawStatBlockOpsImpl ops;

  off_t            ethr_stat_offset;
  RecRawStatBlock *rsb;

  // allocate thread-local raw-stat memory
  if ((ethr_stat_offset = eventProcessor.allocate(num_stats * sizeof(RecRawStat))) == -1) {
    return nullptr;
  }

  // create the raw-stat-block structure
  rsb = static_cast<RecRawStatBlock *>(ats_malloc(sizeof(RecRawStatBlock)));
  memset(rsb, 0, sizeof(RecRawStatBlock));

  rsb->global = static_cast<RecRawStat **>(ats_malloc(num_stats * sizeof(RecRawStat *)));
  memset(rsb->global, 0, num_stats * sizeof(RecRawStat *));

  rsb->num_stats        = 0;
  rsb->max_stats        = num_stats;
  rsb->ethr_stat_offset = ethr_stat_offset;

  ink_mutex_init(&(rsb->mutex));

  rsb->ops = &ops;
  return rsb;
}

void
SetupRecRawStatBlockAllocator()
{
  SetRecAllocateRawStatBlockAllocator(RecAllocateRawStatBlockImpl);
}

// Commonly used access to a raw stat, avoid typos.
static RecRawStat *
thread_stat(EThread *et, RecRawStatBlock *rsb, int id)
{
  return (reinterpret_cast<RecRawStat *>(reinterpret_cast<char *>(et) + rsb->ethr_stat_offset)) + id;
}

int
RecRawStatBlockOpsImpl::raw_stat_get_total(RecRawStatBlock *rsb, int id, RecRawStat *total)
{
  total->sum   = 0;
  total->count = 0;

  // get global values
  total->sum   = rsb->global[id]->sum;
  total->count = rsb->global[id]->count;

  // get thread local values
  for (EThread *et : eventProcessor.active_ethreads()) {
    RecRawStat *tlp  = thread_stat(et, rsb, id);
    total->sum      += tlp->sum;
    total->count    += tlp->count;
  }

  for (EThread *et : eventProcessor.active_dthreads()) {
    RecRawStat *tlp  = thread_stat(et, rsb, id);
    total->sum      += tlp->sum;
    total->count    += tlp->count;
  }

  if (total->sum < 0) { // Assure that we stay positive
    total->sum = 0;
  }

  return REC_ERR_OKAY;
}

//-------------------------------------------------------------------------
// raw_stat_sync_to_global
//-------------------------------------------------------------------------
int
RecRawStatBlockOpsImpl::raw_stat_sync_to_global(RecRawStatBlock *rsb, int id)
{
  RecRawStat total;

  total.sum   = 0;
  total.count = 0;

  // sum the thread local values
  for (EThread *et : eventProcessor.active_ethreads()) {
    RecRawStat *tlp  = thread_stat(et, rsb, id);
    total.sum       += tlp->sum;
    total.count     += tlp->count;
  }

  for (EThread *et : eventProcessor.active_dthreads()) {
    RecRawStat *tlp  = thread_stat(et, rsb, id);
    total.sum       += tlp->sum;
    total.count     += tlp->count;
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
int
RecRawStatBlockOpsImpl::raw_stat_clear(RecRawStatBlock *rsb, int id)
{
  Dbg(_dbg_ctl, "raw_stat_clear(): rsb pointer:%p id:%d", rsb, id);

  // the globals need to be reset too
  // lock so the setting of the globals and last values are atomic
  {
    ink_scoped_mutex_lock lock(rsb->mutex);
    ink_atomic_swap(&(rsb->global[id]->sum), static_cast<int64_t>(0));
    ink_atomic_swap(&(rsb->global[id]->last_sum), static_cast<int64_t>(0));
    ink_atomic_swap(&(rsb->global[id]->count), static_cast<int64_t>(0));
    ink_atomic_swap(&(rsb->global[id]->last_count), static_cast<int64_t>(0));
  }
  // reset the local stats
  for (EThread *et : eventProcessor.active_ethreads()) {
    RecRawStat *tlp = thread_stat(et, rsb, id);
    ink_atomic_swap(&(tlp->sum), static_cast<int64_t>(0));
    ink_atomic_swap(&(tlp->count), static_cast<int64_t>(0));
  }

  for (EThread *et : eventProcessor.active_dthreads()) {
    RecRawStat *tlp = thread_stat(et, rsb, id);
    ink_atomic_swap(&(tlp->sum), static_cast<int64_t>(0));
    ink_atomic_swap(&(tlp->count), static_cast<int64_t>(0));
  }

  return REC_ERR_OKAY;
}

//-------------------------------------------------------------------------
// raw_stat_clear_sum
//-------------------------------------------------------------------------
int
RecRawStatBlockOpsImpl::raw_stat_clear_sum(RecRawStatBlock *rsb, int id)
{
  Dbg(_dbg_ctl, "raw_stat_clear_sum(): rsb pointer:%p id:%d", rsb, id);

  // the globals need to be reset too
  // lock so the setting of the globals and last values are atomic
  {
    ink_scoped_mutex_lock lock(rsb->mutex);
    ink_atomic_swap(&(rsb->global[id]->sum), static_cast<int64_t>(0));
    ink_atomic_swap(&(rsb->global[id]->last_sum), static_cast<int64_t>(0));
  }

  // reset the local stats
  for (EThread *et : eventProcessor.active_ethreads()) {
    RecRawStat *tlp = thread_stat(et, rsb, id);
    ink_atomic_swap(&(tlp->sum), static_cast<int64_t>(0));
  }

  for (EThread *et : eventProcessor.active_dthreads()) {
    RecRawStat *tlp = thread_stat(et, rsb, id);
    ink_atomic_swap(&(tlp->sum), static_cast<int64_t>(0));
  }

  return REC_ERR_OKAY;
}

//-------------------------------------------------------------------------
// raw_stat_clear_count
//-------------------------------------------------------------------------
int
RecRawStatBlockOpsImpl::raw_stat_clear_count(RecRawStatBlock *rsb, int id)
{
  Dbg(_dbg_ctl, "raw_stat_clear_count(): rsb pointer:%p id:%d", rsb, id);

  // the globals need to be reset too
  // lock so the setting of the globals and last values are atomic
  {
    ink_scoped_mutex_lock lock(rsb->mutex);
    ink_atomic_swap(&(rsb->global[id]->count), static_cast<int64_t>(0));
    ink_atomic_swap(&(rsb->global[id]->last_count), static_cast<int64_t>(0));
  }

  // reset the local stats
  for (EThread *et : eventProcessor.active_ethreads()) {
    RecRawStat *tlp = thread_stat(et, rsb, id);
    ink_atomic_swap(&(tlp->count), static_cast<int64_t>(0));
  }

  for (EThread *et : eventProcessor.active_dthreads()) {
    RecRawStat *tlp = thread_stat(et, rsb, id);
    ink_atomic_swap(&(tlp->count), static_cast<int64_t>(0));
  }

  return REC_ERR_OKAY;
}

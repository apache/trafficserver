/** @file

  Record process definitions

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

#include "libts.h"

#include "I_Tasks.h"

#include "P_EventSystem.h"
#include "P_RecCore.h"
#include "P_RecProcess.h"
#include "P_RecMessage.h"
#include "P_RecUtils.h"
#include "P_RecFile.h"

#include "mgmtapi.h"
#include "ProcessManager.h"

// Marks whether the message handler has been initialized.
static bool message_initialized_p = false;
static bool g_started = false;
static EventNotify g_force_req_notify;
static int g_rec_raw_stat_sync_interval_ms = REC_RAW_STAT_SYNC_INTERVAL_MS;
static int g_rec_config_update_interval_ms = REC_CONFIG_UPDATE_INTERVAL_MS;
static int g_rec_remote_sync_interval_ms = REC_REMOTE_SYNC_INTERVAL_MS;
static Event *raw_stat_sync_cont_event;
static Event *config_update_cont_event;
static Event *sync_cont_event;

//-------------------------------------------------------------------------
// i_am_the_record_owner, only used for librecords_p.a
//-------------------------------------------------------------------------
bool
i_am_the_record_owner(RecT rec_type)
{
  if (g_mode_type == RECM_CLIENT) {
    switch (rec_type) {
    case RECT_PROCESS:
    case RECT_PLUGIN:
      return true;
    case RECT_CONFIG:
    case RECT_NODE:
    case RECT_CLUSTER:
    case RECT_LOCAL:
      return false;
    default:
      ink_assert(!"Unexpected RecT type");
      return false;
    }
  } else if (g_mode_type == RECM_STAND_ALONE) {
    switch (rec_type) {
    case RECT_CONFIG:
    case RECT_PROCESS:
    case RECT_NODE:
    case RECT_CLUSTER:
    case RECT_LOCAL:
    case RECT_PLUGIN:
      return true;
    default:
      ink_assert(!"Unexpected RecT type");
      return false;
    }
  }

  return false;
}

//-------------------------------------------------------------------------
// Simple setters for the intervals to decouple this from the proxy
//-------------------------------------------------------------------------
void
RecProcess_set_raw_stat_sync_interval_ms(int ms) {
  Debug("statsproc", "g_rec_raw_stat_sync_interval_ms -> %d", ms);
  g_rec_raw_stat_sync_interval_ms = ms;
  if (raw_stat_sync_cont_event) {
    Debug("statsproc", "Rescheduling raw-stat syncer");
    raw_stat_sync_cont_event->schedule_every(HRTIME_MSECONDS(g_rec_raw_stat_sync_interval_ms));
  }
}
void
RecProcess_set_config_update_interval_ms(int ms) {
  Debug("statsproc", "g_rec_config_update_interval_ms -> %d", ms);
  g_rec_config_update_interval_ms = ms;
  if (config_update_cont_event) {
    Debug("statsproc", "Rescheduling config syncer");
    config_update_cont_event->schedule_every(HRTIME_MSECONDS(g_rec_config_update_interval_ms));
  }
}
void
RecProcess_set_remote_sync_interval_ms(int ms) {
  Debug("statsproc", "g_rec_remote_sync_interval_ms -> %d", ms);
  g_rec_remote_sync_interval_ms = ms;
  if (sync_cont_event) {
    Debug("statsproc", "Rescheduling remote syncer");
    sync_cont_event->schedule_every(HRTIME_MSECONDS(g_rec_remote_sync_interval_ms));
  }
}

//-------------------------------------------------------------------------
// raw_stat_get_total
//-------------------------------------------------------------------------
static int
raw_stat_get_total(RecRawStatBlock *rsb, int id, RecRawStat *total)
{
  int i;
  RecRawStat *tlp;

  total->sum = 0;
  total->count = 0;

  // get global values
  total->sum = rsb->global[id]->sum;
  total->count = rsb->global[id]->count;

  // get thread local values
  for (i = 0; i < eventProcessor.n_ethreads; i++) {
    tlp = ((RecRawStat *) ((char *) (eventProcessor.all_ethreads[i]) + rsb->ethr_stat_offset)) + id;
    total->sum += tlp->sum;
    total->count += tlp->count;
  }

  for (i = 0; i < eventProcessor.n_dthreads; i++) {
    tlp = ((RecRawStat *) ((char *) (eventProcessor.all_dthreads[i]) + rsb->ethr_stat_offset)) + id;
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
  int i;
  RecRawStat *tlp;
  RecRawStat total;

  total.sum = 0;
  total.count = 0;

  // sum the thread local values
  for (i = 0; i < eventProcessor.n_ethreads; i++) {
    tlp = ((RecRawStat *) ((char *) (eventProcessor.all_ethreads[i]) + rsb->ethr_stat_offset)) + id;
    total.sum += tlp->sum;
    total.count += tlp->count;
  }

  for (i = 0; i < eventProcessor.n_dthreads; i++) {
    tlp = ((RecRawStat *) ((char *) (eventProcessor.all_dthreads[i]) + rsb->ethr_stat_offset)) + id;
    total.sum += tlp->sum;
    total.count += tlp->count;
  }

  if (total.sum < 0) { // Assure that we stay positive
    total.sum = 0;
  }

  // lock so the setting of the globals and last values are atomic
  ink_mutex_acquire(&(rsb->mutex));

  // get the delta from the last sync
  RecRawStat delta;
  delta.sum = total.sum - rsb->global[id]->last_sum;
  delta.count = total.count - rsb->global[id]->last_count;

  // This is too verbose now, so leaving it out / leif
  //Debug("stats", "raw_stat_sync_to_global(): rsb pointer:%p id:%d delta:%" PRId64 " total:%" PRId64 " last:%" PRId64 " global:%" PRId64 "\n",
  //rsb, id, delta.sum, total.sum, rsb->global[id]->last_sum, rsb->global[id]->sum);

  // increment the global values by the delta
  ink_atomic_increment(&(rsb->global[id]->sum), delta.sum);
  ink_atomic_increment(&(rsb->global[id]->count), delta.count);

  // set the new totals as the last values seen
  ink_atomic_swap(&(rsb->global[id]->last_sum), total.sum);
  ink_atomic_swap(&(rsb->global[id]->last_count), total.count);

  ink_mutex_release(&(rsb->mutex));

  return REC_ERR_OKAY;
}


//-------------------------------------------------------------------------
// raw_stat_clear
//-------------------------------------------------------------------------
static int
raw_stat_clear(RecRawStatBlock *rsb, int id)
{
  Debug("stats", "raw_stat_clear(): rsb pointer:%p id:%d\n", rsb, id);

  // the globals need to be reset too
  // lock so the setting of the globals and last values are atomic
  ink_mutex_acquire(&(rsb->mutex));
  ink_atomic_swap(&(rsb->global[id]->sum), (int64_t)0);
  ink_atomic_swap(&(rsb->global[id]->last_sum), (int64_t)0);
  ink_atomic_swap(&(rsb->global[id]->count), (int64_t)0);
  ink_atomic_swap(&(rsb->global[id]->last_count), (int64_t)0);
  ink_mutex_release(&(rsb->mutex));

  // reset the local stats
  RecRawStat *tlp;
  for (int i = 0; i < eventProcessor.n_ethreads; i++) {
    tlp = ((RecRawStat *) ((char *) (eventProcessor.all_ethreads[i]) + rsb->ethr_stat_offset)) + id;
    ink_atomic_swap(&(tlp->sum), (int64_t)0);
    ink_atomic_swap(&(tlp->count), (int64_t)0);
  }

  for (int i = 0; i < eventProcessor.n_dthreads; i++) {
    tlp = ((RecRawStat *) ((char *) (eventProcessor.all_dthreads[i]) + rsb->ethr_stat_offset)) + id;
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
  Debug("stats", "raw_stat_clear_sum(): rsb pointer:%p id:%d\n", rsb, id);

  // the globals need to be reset too
  // lock so the setting of the globals and last values are atomic
  ink_mutex_acquire(&(rsb->mutex));
  ink_atomic_swap(&(rsb->global[id]->sum), (int64_t)0);
  ink_atomic_swap(&(rsb->global[id]->last_sum), (int64_t)0);
  ink_mutex_release(&(rsb->mutex));

  // reset the local stats
  RecRawStat *tlp;
  for (int i = 0; i < eventProcessor.n_ethreads; i++) {
    tlp = ((RecRawStat *) ((char *) (eventProcessor.all_ethreads[i]) + rsb->ethr_stat_offset)) + id;
    ink_atomic_swap(&(tlp->sum), (int64_t)0);
  }

  for (int i = 0; i < eventProcessor.n_dthreads; i++) {
    tlp = ((RecRawStat *) ((char *) (eventProcessor.all_dthreads[i]) + rsb->ethr_stat_offset)) + id;
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
  Debug("stats", "raw_stat_clear_count(): rsb pointer:%p id:%d\n", rsb, id);

  // the globals need to be reset too
  // lock so the setting of the globals and last values are atomic
  ink_mutex_acquire(&(rsb->mutex));
  ink_atomic_swap(&(rsb->global[id]->count), (int64_t)0);
  ink_atomic_swap(&(rsb->global[id]->last_count), (int64_t)0);
  ink_mutex_release(&(rsb->mutex));

  // reset the local stats
  RecRawStat *tlp;
  for (int i = 0; i < eventProcessor.n_ethreads; i++) {
    tlp = ((RecRawStat *) ((char *) (eventProcessor.all_ethreads[i]) + rsb->ethr_stat_offset)) + id;
    ink_atomic_swap(&(tlp->count), (int64_t)0);
  }

  for (int i = 0; i < eventProcessor.n_dthreads; i++) {
    tlp = ((RecRawStat *) ((char *) (eventProcessor.all_dthreads[i]) + rsb->ethr_stat_offset)) + id;
    ink_atomic_swap(&(tlp->count), (int64_t)0);
  }

  return REC_ERR_OKAY;
}


//-------------------------------------------------------------------------
// recv_message_cb__process
//-------------------------------------------------------------------------
static int
recv_message_cb__process(RecMessage *msg, RecMessageT msg_type, void *cookie)
{
  int err;

  if ((err = recv_message_cb(msg, msg_type, cookie)) == REC_ERR_OKAY) {
    if (msg_type == RECG_PULL_ACK) {
      g_force_req_notify.lock();
      g_force_req_notify.signal();
      g_force_req_notify.unlock();
    }
  }
  return err;
}


//-------------------------------------------------------------------------
// raw_stat_sync_cont
//-------------------------------------------------------------------------
struct raw_stat_sync_cont: public Continuation
{
  raw_stat_sync_cont(ProxyMutex *m)
    : Continuation(m)
  {
    SET_HANDLER(&raw_stat_sync_cont::exec_callbacks);
  }

  int exec_callbacks(int /* event */, Event * /* e */)
  {
    RecExecRawStatSyncCbs();
    Debug("statsproc", "raw_stat_sync_cont() processed");

    return EVENT_CONT;
  }
};


//-------------------------------------------------------------------------
// config_update_cont
//-------------------------------------------------------------------------
struct config_update_cont: public Continuation
{
  config_update_cont(ProxyMutex *m)
    : Continuation(m)
  {
    SET_HANDLER(&config_update_cont::exec_callbacks);
  }

  int exec_callbacks(int /* event */, Event * /* e */)
  {
    RecExecConfigUpdateCbs(REC_PROCESS_UPDATE_REQUIRED);
    Debug("statsproc", "config_update_cont() processed");

    return EVENT_CONT;
  }
};


//-------------------------------------------------------------------------
// sync_cont
//-------------------------------------------------------------------------
struct sync_cont: public Continuation
{
  textBuffer *m_tb;

  sync_cont(ProxyMutex *m)
    : Continuation(m)
  {
    SET_HANDLER(&sync_cont::sync);
    m_tb = new textBuffer(65536);
  }

   ~sync_cont()
  {
    if (m_tb != NULL) {
      delete m_tb;
      m_tb = NULL;
    }
  }

  int sync(int /* event */, Event * /* e */)
  {
    send_push_message();
    RecSyncStatsFile();
    if (RecSyncConfigToTB(m_tb) == REC_ERR_OKAY) {
        RecWriteConfigFile(m_tb);
    }
    Debug("statsproc", "sync_cont() processed");

    return EVENT_CONT;
  }
};


//-------------------------------------------------------------------------
// RecProcessInit
//-------------------------------------------------------------------------
int
RecProcessInit(RecModeT mode_type, Diags *_diags)
{
  static bool initialized_p = false;

  if (initialized_p) {
    return REC_ERR_OKAY;
  }

  g_mode_type = mode_type;

  if (RecCoreInit(mode_type, _diags) == REC_ERR_FAIL) {
    return REC_ERR_FAIL;
  }

  /* -- defer RecMessageInit() until ProcessManager is initialized and
   *    started
   if (RecMessageInit(mode_type) == REC_ERR_FAIL) {
   return REC_ERR_FAIL;
   }

   if (RecMessageRegisterRecvCb(recv_message_cb__process, NULL)) {
   return REC_ERR_FAIL;
   }

   ink_cond_init(&g_force_req_cond);
   ink_mutex_init(&g_force_req_mutex, NULL);
   if (mode_type == RECM_CLIENT) {
   send_pull_message(RECG_PULL_REQ);
   ink_cond_wait(&g_force_req_cond, &g_force_req_mutex);
   ink_mutex_release(&g_force_req_mutex);
   }
   */

  initialized_p = true;

  return REC_ERR_OKAY;
}


void
RecMessageInit()
{
  ink_assert(g_mode_type != RECM_NULL);
  pmgmt->registerMgmtCallback(MGMT_EVENT_LIBRECORDS, RecMessageRecvThis, NULL);
  message_initialized_p = true;
}

//-------------------------------------------------------------------------
// RecProcessInitMessage
//-------------------------------------------------------------------------
int
RecProcessInitMessage(RecModeT mode_type)
{
  static bool initialized_p = false;

  if (initialized_p) {
    return REC_ERR_OKAY;
  }

  RecMessageInit();
  if (RecMessageRegisterRecvCb(recv_message_cb__process, NULL)) {
    return REC_ERR_FAIL;
  }

  if (mode_type == RECM_CLIENT) {
    send_pull_message(RECG_PULL_REQ);
    g_force_req_notify.lock();
    g_force_req_notify.wait();
    g_force_req_notify.unlock();
  }

  initialized_p = true;

  return REC_ERR_OKAY;
}


//-------------------------------------------------------------------------
// RecProcessStart
//-------------------------------------------------------------------------
int
RecProcessStart(void)
{
  if (g_started) {
    return REC_ERR_OKAY;
  }

  Debug("statsproc", "Starting sync continuations:");
  raw_stat_sync_cont *rssc = new raw_stat_sync_cont(new_ProxyMutex());
  Debug("statsproc", "\traw-stat syncer");
  raw_stat_sync_cont_event = eventProcessor.schedule_every(rssc, HRTIME_MSECONDS(g_rec_raw_stat_sync_interval_ms), ET_TASK);

  config_update_cont *cuc = new config_update_cont(new_ProxyMutex());
  Debug("statsproc", "\tconfig syncer");
  config_update_cont_event = eventProcessor.schedule_every(cuc, HRTIME_MSECONDS(g_rec_config_update_interval_ms), ET_TASK);

  sync_cont *sc = new sync_cont(new_ProxyMutex());
  Debug("statsproc", "\tremote syncer");
  sync_cont_event = eventProcessor.schedule_every(sc, HRTIME_MSECONDS(g_rec_remote_sync_interval_ms), ET_TASK);

  g_started = true;

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
    return NULL;
  }
  // create the raw-stat-block structure
  rsb = (RecRawStatBlock *)ats_malloc(sizeof(RecRawStatBlock));
  memset(rsb, 0, sizeof(RecRawStatBlock));
  rsb->ethr_stat_offset = ethr_stat_offset;
  rsb->global = (RecRawStat **)ats_malloc(num_stats * sizeof(RecRawStat *));
  memset(rsb->global, 0, num_stats * sizeof(RecRawStat *));
  rsb->num_stats = 0;
  rsb->max_stats = num_stats;
  ink_mutex_init(&(rsb->mutex),"net stat mutex");
  return rsb;
}


//-------------------------------------------------------------------------
// RecRegisterRawStat
//-------------------------------------------------------------------------
int
_RecRegisterRawStat(RecRawStatBlock *rsb, RecT rec_type, const char *name, RecDataT data_type, RecPersistT persist_type, int id,
                   RecRawStatSyncCb sync_cb)
{
  Debug("stats", "RecRawStatSyncCb(%s): rsb pointer:%p id:%d\n", name, rsb, id);

  // check to see if we're good to proceed
  ink_assert(id < rsb->max_stats);

  int err = REC_ERR_OKAY;

  RecRecord *r;
  RecData data_default;
  memset(&data_default, 0, sizeof(RecData));

  // register the record
  if ((r = RecRegisterStat(rec_type, name, data_type, data_default, persist_type)) == NULL) {
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
  rsb->global[id] = &(r->stat_meta.data_raw);
  rsb->global[id]->last_sum = 0;
  rsb->global[id]->last_count = 0;

  // setup the periodic sync callback
  RecRegisterRawStatSyncCb(name, sync_cb, rsb, id);

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
  total.sum = rsb->global[id]->sum;
  total.count = rsb->global[id]->count;
  RecDataSetFromInk64(data_type, data, total.sum);

  return REC_ERR_OKAY;
}

int
RecRawStatSyncCount(const char *name, RecDataT data_type, RecData *data, RecRawStatBlock *rsb, int id)
{
  RecRawStat total;

  Debug("stats", "raw sync:count for %s", name);
  raw_stat_sync_to_global(rsb, id);
  total.sum = rsb->global[id]->sum;
  total.count = rsb->global[id]->count;
  RecDataSetFromInk64(data_type, data, total.count);

  return REC_ERR_OKAY;
}

int
RecRawStatSyncAvg(const char *name, RecDataT data_type, RecData *data, RecRawStatBlock *rsb, int id)
{
  RecRawStat total;
  RecFloat avg = 0.0f;

  Debug("stats", "raw sync:avg for %s", name);
  raw_stat_sync_to_global(rsb, id);
  total.sum = rsb->global[id]->sum;
  total.count = rsb->global[id]->count;
  if (total.count != 0)
    avg = (float) ((double) total.sum / (double) total.count);
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
  total.sum = rsb->global[id]->sum;
  total.count = rsb->global[id]->count;
  if (total.count == 0) {
    r = 0.0f;
  } else {
    r = (float) ((double) total.sum / (double) total.count);
    r = r / (float) (HRTIME_SECOND);
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
  total.sum = rsb->global[id]->sum;
  total.count = rsb->global[id]->count;
  if (total.count == 0) {
    r = 0.0f;
  } else {
    r = (float) ((double) total.sum / 1000);
  }
  RecDataSetFromFloat(data_type, data, r);
  return REC_ERR_OKAY;
}

int
RecRawStatSyncMHrTimeAvg(const char *name, RecDataT data_type, RecData *data, RecRawStatBlock *rsb, int id)
{
  RecRawStat total;
  RecFloat r;

  Debug("stats", "raw sync:mhr-timeavg for %s", name);
  raw_stat_sync_to_global(rsb, id);
  total.sum = rsb->global[id]->sum;
  total.count = rsb->global[id]->count;
  if (total.count == 0) {
    r = 0.0f;
  } else {
    r = (float) ((double) total.sum / (double) total.count);
    r = r / (float) (HRTIME_MSECOND);
  }
  RecDataSetFromFloat(data_type, data, r);
  return REC_ERR_OKAY;
}


//-------------------------------------------------------------------------
// RecIncrRawStatXXX
//-------------------------------------------------------------------------
int
RecIncrRawStatBlock(RecRawStatBlock */* rsb ATS_UNUSED */, EThread */* ethread ATS_UNUSED */,
                    RecRawStat */* stat_array ATS_UNUSED */)
{
  return REC_ERR_FAIL;
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

int
RecSetRawStatBlock(RecRawStatBlock */* rsb ATS_UNUSED */, RecRawStat */* stat_array ATS_UNUSED */)
{
  return REC_ERR_FAIL;
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
  RecRecord *r;

  ink_rwlock_rdlock(&g_records_rwlock);
  if (ink_hash_table_lookup(g_records_ht, name, (void **) &r)) {
    rec_mutex_acquire(&(r->lock));
    if (REC_TYPE_IS_STAT(r->rec_type)) {
      if (!(r->stat_meta.sync_cb)) {
        r->stat_meta.sync_rsb = rsb;
        r->stat_meta.sync_id = id;
        r->stat_meta.sync_cb = sync_cb;
        r->stat_meta.sync_rsb->global[r->stat_meta.sync_id]->version = r->version;
        err = REC_ERR_OKAY;
      } else {
        ink_release_assert(false); // We shouldn't register CBs twice...
      }
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
          (*(r->stat_meta.sync_cb)) (r->name, r->data_type, &(r->data), r->stat_meta.sync_rsb, r->stat_meta.sync_id);
        }
        r->sync_required = REC_SYNC_REQUIRED;
      }
    }
    rec_mutex_release(&(r->lock));
  }

  return REC_ERR_OKAY;
}

void
RecSignalManager(int id, const char * msg, size_t msgsize)
{
  ink_assert(pmgmt);
  pmgmt->signalManager(id, msg, msgsize);
}

int
RecRegisterManagerCb(int _signal, RecManagerCb _fn, void *_data)
{
  return pmgmt->registerMgmtCallback(_signal, _fn, _data);
}

//-------------------------------------------------------------------------
// RecMessageSend
//-------------------------------------------------------------------------

int
RecMessageSend(RecMessage * msg)
{
  int msg_size;

  if (!message_initialized_p)
    return REC_ERR_OKAY;

  // Make a copy of the record, but truncate it to the size actually used
  if (g_mode_type == RECM_CLIENT || g_mode_type == RECM_SERVER) {
    msg->o_end = msg->o_write;
    msg_size = sizeof(RecMessageHdr) + (msg->o_write - msg->o_start);
    pmgmt->signalManager(MGMT_SIGNAL_LIBRECORDS, (char *) msg, msg_size);
  }

  return REC_ERR_OKAY;
}


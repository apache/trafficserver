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

#include "ts/ink_platform.h"
#include "ts/EventNotify.h"

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
static bool g_started             = false;
static EventNotify g_force_req_notify;
static int g_rec_raw_stat_sync_interval_ms = REC_RAW_STAT_SYNC_INTERVAL_MS;
static int g_rec_config_update_interval_ms = REC_CONFIG_UPDATE_INTERVAL_MS;
static int g_rec_remote_sync_interval_ms   = REC_REMOTE_SYNC_INTERVAL_MS;
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
RecProcess_set_raw_stat_sync_interval_ms(int ms)
{
  Debug("statsproc", "g_rec_raw_stat_sync_interval_ms -> %d", ms);
  g_rec_raw_stat_sync_interval_ms = ms;
  if (raw_stat_sync_cont_event) {
    Debug("statsproc", "Rescheduling raw-stat syncer");
    raw_stat_sync_cont_event->schedule_every(HRTIME_MSECONDS(g_rec_raw_stat_sync_interval_ms));
  }
}
void
RecProcess_set_config_update_interval_ms(int ms)
{
  Debug("statsproc", "g_rec_config_update_interval_ms -> %d", ms);
  g_rec_config_update_interval_ms = ms;
  if (config_update_cont_event) {
    Debug("statsproc", "Rescheduling config syncer");
    config_update_cont_event->schedule_every(HRTIME_MSECONDS(g_rec_config_update_interval_ms));
  }
}
void
RecProcess_set_remote_sync_interval_ms(int ms)
{
  Debug("statsproc", "g_rec_remote_sync_interval_ms -> %d", ms);
  g_rec_remote_sync_interval_ms = ms;
  if (sync_cont_event) {
    Debug("statsproc", "Rescheduling remote syncer");
    sync_cont_event->schedule_every(HRTIME_MSECONDS(g_rec_remote_sync_interval_ms));
  }
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
struct raw_stat_sync_cont : public Continuation {
  raw_stat_sync_cont(ProxyMutex *m) : Continuation(m) { SET_HANDLER(&raw_stat_sync_cont::exec_callbacks); }
  int
  exec_callbacks(int /* event */, Event * /* e */)
  {
    RecExecRawStatSyncCbs();
    Debug("statsproc", "raw_stat_sync_cont() processed");

    return EVENT_CONT;
  }
};

//-------------------------------------------------------------------------
// config_update_cont
//-------------------------------------------------------------------------
struct config_update_cont : public Continuation {
  config_update_cont(ProxyMutex *m) : Continuation(m) { SET_HANDLER(&config_update_cont::exec_callbacks); }
  int
  exec_callbacks(int /* event */, Event * /* e */)
  {
    RecExecConfigUpdateCbs(REC_PROCESS_UPDATE_REQUIRED);
    Debug("statsproc", "config_update_cont() processed");

    return EVENT_CONT;
  }
};

//-------------------------------------------------------------------------
// sync_cont
//-------------------------------------------------------------------------
struct sync_cont : public Continuation {
  textBuffer *m_tb;

  sync_cont(ProxyMutex *m) : Continuation(m)
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

  int
  sync(int /* event */, Event * /* e */)
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
  Debug("statsproc", "raw-stat syncer");
  raw_stat_sync_cont_event = eventProcessor.schedule_every(rssc, HRTIME_MSECONDS(g_rec_raw_stat_sync_interval_ms), ET_TASK);

  RecInt disable_modification = 0;
  RecGetRecordInt("proxy.config.disable_configuration_modification", &disable_modification);
  // Schedule continuation to call the configuration callbacks if we are allowed to modify configuration in RAM
  if (disable_modification == 1) {
    Debug("statsproc", "Disabled configuration modification");
  } else {
    config_update_cont *cuc = new config_update_cont(new_ProxyMutex());
    Debug("statsproc", "config syncer");
    config_update_cont_event = eventProcessor.schedule_every(cuc, HRTIME_MSECONDS(g_rec_config_update_interval_ms), ET_TASK);
  }

  sync_cont *sc = new sync_cont(new_ProxyMutex());
  Debug("statsproc", "remote syncer");
  sync_cont_event = eventProcessor.schedule_every(sc, HRTIME_MSECONDS(g_rec_remote_sync_interval_ms), ET_TASK);

  g_started = true;

  return REC_ERR_OKAY;
}

void
RecSignalManager(int id, const char *msg, size_t msgsize)
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
RecMessageSend(RecMessage *msg)
{
  int msg_size;

  if (!message_initialized_p)
    return REC_ERR_OKAY;

  // Make a copy of the record, but truncate it to the size actually used
  if (g_mode_type == RECM_CLIENT || g_mode_type == RECM_SERVER) {
    msg->o_end = msg->o_write;
    msg_size   = sizeof(RecMessageHdr) + (msg->o_write - msg->o_start);
    pmgmt->signalManager(MGMT_SIGNAL_LIBRECORDS, (char *)msg, msg_size);
  }

  return REC_ERR_OKAY;
}
